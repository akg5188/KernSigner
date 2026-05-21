package io.arbitrum.wallet

import java.net.URLDecoder
import java.nio.charset.StandardCharsets

object WalletConnectUriParser {
    private const val MAX_WALLETCONNECT_URI_LENGTH = 2048
    private val wcRegex = Regex("""wc:[^\p{Cntrl}\s"'<>]+""", RegexOption.IGNORE_CASE)
    private val wcV2Regex = Regex("""^wc:[0-9a-f-]{32,}@2\?[^\p{Cntrl}\s"'<>]+$""", RegexOption.IGNORE_CASE)

    fun extract(input: String): String? {
        if (input.isBlank()) return null
        val candidates = mutableListOf(input.trim())
        var decoded = input.trim()
        repeat(3) {
            val next = runCatching {
                URLDecoder.decode(decoded, StandardCharsets.UTF_8.name())
            }.getOrNull() ?: return@repeat
            if (next == decoded) return@repeat
            decoded = next
            candidates += decoded
        }
        for (candidate in candidates) {
            val direct = candidate.trim()
            normalizeWalletConnectUri(direct)?.let { return it }
            wcRegex.find(direct)?.value?.let { embedded ->
                normalizeWalletConnectUri(embedded)?.let { return it }
            }
        }
        return null
    }

    private fun normalizeWalletConnectUri(candidate: String): String? {
        val direct = candidate.trim()
        if (!direct.startsWith("wc:", ignoreCase = true)) return null
        if (direct.length !in 32..MAX_WALLETCONNECT_URI_LENGTH) return null
        if (direct.any { it.isWhitespace() || it.isISOControl() }) return null
        if (!wcV2Regex.matches(direct)) return null
        val lower = direct.lowercase()
        if (!lower.contains("relay-protocol=") || !lower.contains("symkey=")) return null
        return direct
    }
}
