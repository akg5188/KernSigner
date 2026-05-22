package io.arbitrum.wallet

import java.math.BigInteger
import java.net.URLDecoder
import java.util.Base64
import java.util.Locale

private const val ELECTRUM_BASE43_ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:"
private const val PSBT_MAGIC_HEX = "70736274ff"

data class ElectrumPsbtPayload(
    val base64: String,
    val byteCount: Int,
    val sourceLabel: String,
)

object ElectrumQrCodec {
    fun parsePsbt(raw: String): ElectrumPsbtPayload? {
        val source = raw.trim()
        if (source.isBlank()) return null

        for (candidate in electrumCandidates(source)) {
            decodePsbtBytes(candidate)?.let { bytes ->
                return ElectrumPsbtPayload(
                    base64 = Base64.getEncoder().withoutPadding().encodeToString(bytes),
                    byteCount = bytes.size,
                    sourceLabel = sourceLabel(candidate),
                )
            }
        }
        return null
    }

    internal fun decodeBase43(value: String): ByteArray? {
        val normalized = removeWhitespace(value).uppercase(Locale.US)
        if (normalized.isBlank()) return null
        if (normalized.any { ELECTRUM_BASE43_ALPHABET.indexOf(it) < 0 }) return null

        var number = BigInteger.ZERO
        val base = BigInteger.valueOf(43L)
        normalized.forEach { char ->
            number = number.multiply(base).add(BigInteger.valueOf(ELECTRUM_BASE43_ALPHABET.indexOf(char).toLong()))
        }

        val leadingZeros = normalized.takeWhile { it == ELECTRUM_BASE43_ALPHABET.first() }.length
        val payload = number.toByteArray().dropWhile { it == 0.toByte() }.toByteArray()
        return ByteArray(leadingZeros) + payload
    }

    internal fun removeWhitespace(value: String): String =
        value.filterNot { it.isWhitespace() }

    internal fun bytesToHex(bytes: ByteArray): String =
        bytes.joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }

    fun encodeBase43FromPsbtBase64(base64: String): String? {
        val bytes = decodeBase64(base64)?.takeIf(::isPsbt) ?: return null
        return encodeBase43(bytes)
    }

    fun encodeBase43FromTransactionHex(hex: String): String? {
        val clean = removeWhitespace(hex)
            .removePrefix("0x")
            .removePrefix("0X")
            .lowercase(Locale.US)
        val bytes = hexToBytesOrNull(clean) ?: return null
        return encodeBase43(bytes)
    }

    internal fun encodeBase43(bytes: ByteArray): String {
        if (bytes.isEmpty()) return ""

        val leadingZeros = bytes.takeWhile { it == 0.toByte() }.size
        var number = BigInteger(1, bytes)
        val base = BigInteger.valueOf(43L)
        val encoded = StringBuilder()
        while (number > BigInteger.ZERO) {
            val divRem = number.divideAndRemainder(base)
            encoded.append(ELECTRUM_BASE43_ALPHABET[divRem[1].toInt()])
            number = divRem[0]
        }
        repeat(leadingZeros) { encoded.append(ELECTRUM_BASE43_ALPHABET.first()) }
        return encoded.reverse().toString()
    }

    private fun decodePsbtBytes(value: String): ByteArray? {
        val compact = removeWhitespace(value).trim('"', '\'')
        if (compact.isBlank()) return null

        decodeBase64(compact)?.takeIf(::isPsbt)?.let { return it }

        val hexCandidate = compact
            .removePrefix("0x")
            .removePrefix("0X")
            .lowercase(Locale.US)
        if (hexCandidate.startsWith(PSBT_MAGIC_HEX) && hexCandidate.length % 2 == 0) {
            hexToBytesOrNull(hexCandidate)?.takeIf(::isPsbt)?.let { return it }
        }

        return decodeBase43(compact)?.takeIf(::isPsbt)
    }

    private fun electrumCandidates(raw: String): List<String> {
        val candidates = linkedSetOf<String>()
        fun addCandidate(value: String) {
            val trimmed = value.trim().trim('"', '\'')
            if (trimmed.isNotBlank()) {
                candidates += trimmed
                decodeUrlComponentPreservingPlus(trimmed)?.let { decoded ->
                    if (decoded.isNotBlank()) candidates += decoded
                }
            }
        }

        addCandidate(raw)

        val knownPrefixes = listOf(
            "electrum:",
            "psbt:",
            "bitcoin:",
        )
        knownPrefixes.firstOrNull { raw.startsWith(it, ignoreCase = true) }?.let { prefix ->
            addCandidate(raw.substring(prefix.length))
        }

        val queryStart = raw.indexOf('?')
        if (queryStart >= 0 && queryStart < raw.lastIndex) {
            parseQuery(raw.substring(queryStart + 1)).forEach { (key, value) ->
                if (
                    key.equals("psbt", ignoreCase = true) ||
                    key.equals("data", ignoreCase = true) ||
                    key.equals("tx", ignoreCase = true) ||
                    key.equals("transaction", ignoreCase = true)
                ) {
                    addCandidate(value)
                }
            }
        }

        return candidates.toList()
    }

    private fun parseQuery(queryRaw: String): Map<String, String> {
        val result = linkedMapOf<String, String>()
        queryRaw.split('&').forEach { piece ->
            val eq = piece.indexOf('=')
            if (eq > 0) {
                val key = decodeUrlComponentPreservingPlus(piece.substring(0, eq)) ?: piece.substring(0, eq)
                val value = decodeUrlComponentPreservingPlus(piece.substring(eq + 1)) ?: piece.substring(eq + 1)
                result[key] = value
            }
        }
        return result
    }

    private fun decodeUrlComponentPreservingPlus(value: String): String? {
        return runCatching {
            URLDecoder.decode(value.replace("+", "%2B"), "UTF-8")
        }.getOrNull()
    }

    private fun decodeBase64(value: String): ByteArray? {
        val compact = removeWhitespace(value).trim('"', '\'')
        if (compact.isBlank()) return null
        val padding = (4 - compact.length % 4) % 4
        val padded = compact + "=".repeat(padding)
        return runCatching { Base64.getDecoder().decode(padded) }.getOrNull()
            ?: runCatching { Base64.getUrlDecoder().decode(padded) }.getOrNull()
            ?: runCatching { Base64.getMimeDecoder().decode(compact) }.getOrNull()
    }

    private fun hexToBytesOrNull(raw: String): ByteArray? {
        if (raw.length % 2 != 0) return null
        return runCatching {
            ByteArray(raw.length / 2) { index ->
                raw.substring(index * 2, index * 2 + 2).toInt(16).toByte()
            }
        }.getOrNull()
    }

    private fun isPsbt(bytes: ByteArray): Boolean {
        return bytes.size >= 5 &&
            bytes[0] == 'p'.code.toByte() &&
            bytes[1] == 's'.code.toByte() &&
            bytes[2] == 'b'.code.toByte() &&
            bytes[3] == 't'.code.toByte() &&
            bytes[4] == 0xff.toByte()
    }

    private fun sourceLabel(candidate: String): String {
        val compact = removeWhitespace(candidate)
        return when {
            compact.startsWith("cHNidP", ignoreCase = true) -> "Electrum PSBT Base64"
            compact.removePrefix("0x").removePrefix("0X").startsWith(PSBT_MAGIC_HEX, ignoreCase = true) -> "Electrum PSBT Hex"
            else -> "Electrum Base43 PSBT"
        }
    }
}
