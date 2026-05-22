package io.arbitrum.wallet

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.bitcoinj.core.Transaction
import java.net.URLDecoder
import java.util.Locale

private val json = Json { ignoreUnknownKeys = true }

data class ParsedResponse(
    val rawTransaction: String?,
    val bitcoinTxHex: String?,
    val signature: String?,
    val address: String?,
    val web3Account: Web3BridgeAccount? = null,
    val isError: Boolean = false,
)

object TpResponseParser {
    fun parse(responsePayload: String): ParsedResponse {
        val raw = responsePayload.trim()
        if (raw.isBlank()) return ParsedResponse(null, null, null, null, isError = true)

        extractBitcoinTransactionHex(raw)?.let { txHex ->
            return ParsedResponse(rawTransaction = null, bitcoinTxHex = txHex, signature = null, address = null)
        }

        val dash = raw.indexOf('-')
        if (dash <= 0) return ParsedResponse(null, null, null, null, isError = true)

        val queryPart = raw.substring(dash + 1).removePrefix("?")
        val dataRaw = parseQuery(queryPart)["data"] ?: return ParsedResponse(null, null, null, null, isError = true)
        val dataObj = try {
            json.parseToJsonElement(dataRaw).jsonObject
        } catch (e: Throwable) {
            return ParsedResponse(null, null, null, null, isError = true)
        }

        val rawTx = dataObj["rawTransaction"]?.jsonPrimitive?.content
        val sig = dataObj["signature"]?.jsonPrimitive?.content
        val addr = dataObj["address"]?.jsonPrimitive?.content
        val web3Account = try {
            dataObj["web3Account"]?.jsonObject?.let(::parseWeb3Account)
        } catch (e: Throwable) {
            return ParsedResponse(null, null, null, null, isError = true)
        }

        return ParsedResponse(
            rawTransaction = rawTx,
            bitcoinTxHex = null,
            signature = sig,
            address = addr,
            web3Account = web3Account,
        )
    }

    private fun parseQuery(queryRaw: String): Map<String, String> {
        val result = mutableMapOf<String, String>()
        val marker = queryRaw.indexOf("data=")
        if (marker >= 0) {
            queryRaw.substring(0, marker).split('&').forEach { piece ->
                val eq = piece.indexOf('=')
                if (eq > 0) result[piece.substring(0, eq)] = URLDecoder.decode(piece.substring(eq + 1), "UTF-8")
            }
            result["data"] = URLDecoder.decode(queryRaw.substring(marker + 5), "UTF-8")
            return result
        }
        queryRaw.split('&').forEach { piece ->
            val eq = piece.indexOf('=')
            if (eq > 0) result[piece.substring(0, eq)] = URLDecoder.decode(piece.substring(eq + 1), "UTF-8")
        }
        return result
    }

    private fun extractBitcoinTransactionHex(raw: String): String? {
        for (candidate in bitcoinTransactionCandidates(raw)) {
            val decoded = decodeBitcoinTransactionHex(candidate)
            if (decoded != null) return decoded
        }
        return null
    }

    private fun bitcoinTransactionCandidates(raw: String): List<String> {
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
            "btctx:",
            "bitcoin-tx:",
            "bitcoin_tx:",
            "signedtx:",
            "signed-tx:",
            "tx:",
            "electrum:",
            "bitcoin:",
        )
        knownPrefixes.firstOrNull { raw.startsWith(it, ignoreCase = true) }?.let { prefix ->
            addCandidate(raw.substring(prefix.length))
        }

        val queryStart = raw.indexOf('?')
        if (queryStart >= 0 && queryStart < raw.lastIndex) {
            parseTransactionQuery(raw.substring(queryStart + 1)).forEach { (key, value) ->
                if (
                    key.equals("tx", ignoreCase = true) ||
                    key.equals("rawtx", ignoreCase = true) ||
                    key.equals("rawTransaction", ignoreCase = true) ||
                    key.equals("transaction", ignoreCase = true) ||
                    key.equals("signedtx", ignoreCase = true) ||
                    key.equals("signedTransaction", ignoreCase = true) ||
                    key.equals("data", ignoreCase = true)
                ) {
                    addCandidate(value)
                }
            }
        }

        return candidates.toList()
    }

    private fun decodeBitcoinTransactionHex(value: String): String? {
        val compact = ElectrumQrCodec.removeWhitespace(value)
        if (compact.isBlank()) return null

        val hexCandidate = compact
            .removePrefix("0x")
            .removePrefix("0X")
            .lowercase(Locale.US)
        if (isLikelyBitcoinTxHex(hexCandidate)) return hexCandidate

        val base43Bytes = ElectrumQrCodec.decodeBase43(compact) ?: return null
        val base43Hex = ElectrumQrCodec.bytesToHex(base43Bytes)
        return if (isLikelyBitcoinTxHex(base43Hex)) base43Hex else null
    }

    private fun isLikelyBitcoinTxHex(value: String): Boolean {
        if (value.length < 120 || value.length % 2 != 0) return false
        if (!value.all { it.isDigit() || it in 'a'..'f' }) return false
        if (value.startsWith("70736274ff")) return false
        val bytes = hexToBytesOrNull(value) ?: return false
        return runCatching {
            val tx = Transaction(bitcoinNetworkParamsForPrefix("xpub"), bytes)
            tx.inputs.isNotEmpty() && tx.outputs.isNotEmpty()
        }.getOrDefault(false)
    }

    private fun parseTransactionQuery(queryRaw: String): Map<String, String> {
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

    private fun hexToBytesOrNull(raw: String): ByteArray? {
        if (raw.length % 2 != 0) return null
        return runCatching {
            ByteArray(raw.length / 2) { index ->
                raw.substring(index * 2, index * 2 + 2).toInt(16).toByte()
            }
        }.getOrNull()
    }

    private fun parseWeb3Account(obj: kotlinx.serialization.json.JsonObject): Web3BridgeAccount {
        fun required(key: String): String =
            obj[key]?.jsonPrimitive?.content?.trim().orEmpty()
                .ifBlank { throw IllegalArgumentException("web3Account 缺少 $key") }

        return Web3BridgeAccount(
            address = required("address"),
            addressPath = required("addressPath"),
            accountPath = required("accountPath"),
            masterFingerprint = required("masterFingerprint"),
            compressedPubKeyHex = required("compressedPubKeyHex"),
            chainCodeHex = required("chainCodeHex"),
            xpub = required("xpub"),
            sourceLabel = required("sourceLabel"),
            importedAt = obj["importedAt"]?.jsonPrimitive?.content?.toLongOrNull() ?: System.currentTimeMillis(),
            label = obj["label"]?.jsonPrimitive?.content?.trim().orEmpty().ifBlank { "Web3 账户" },
            childrenPath = obj["childrenPath"]?.jsonPrimitive?.content?.trim().orEmpty().ifBlank { "0/*" },
        )
    }
}
