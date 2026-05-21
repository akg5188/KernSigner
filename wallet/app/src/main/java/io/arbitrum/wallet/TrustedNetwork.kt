package io.arbitrum.wallet

import okhttp3.CertificatePinner
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.OkHttpClient
import okhttp3.Request

private data class TrustedHostPins(
    val host: String,
    val pins: List<String>,
)

object TrustedNetwork {
    // Pins observed on 2026-03-28. Each host includes the current leaf and issuing intermediate
    // so routine leaf rotation can continue while still rejecting unrelated certificates.
    private val pinnedHosts = listOf(
        TrustedHostPins(
            host = "arb1.arbitrum.io",
            pins = listOf(
                "sha256/ovSN23HL0cCapJd8lk+xPkNXPfZySzukjaBtzehbLkM=",
                "sha256/iFvwVyJSxnQdyaUvUERIf+8qk7gRze3612JMwoO3zdU=",
            ),
        ),
        TrustedHostPins(
            host = "arbitrum.blockscout.com",
            pins = listOf(
                "sha256/qmATtnqaELF4TeZc2TykrSYASAyPYlE5K7qVtPunpQQ=",
                "sha256/y7xVm0TVJNahMr2sZydE2jQH8SquXV9yLF9seROHHHU=",
            ),
        ),
        TrustedHostPins(
            host = "api.coingecko.com",
            pins = listOf(
                "sha256/1ObC8vlFrplRJ03DXfyaS0+4SzNEbDoCgfGyCFQ2zOM=",
                "sha256/kIdp6NNEd8wsugYyyIYFsi1ylMCED3hZbSR8ZFsa/A4=",
            ),
        ),
        TrustedHostPins(
            host = "coins.llama.fi",
            pins = listOf(
                "sha256/rkUS+QMRGwuh7j8EJnXgq1yqgIIHTvD3JlC6gRGN04E=",
                "sha256/kIdp6NNEd8wsugYyyIYFsi1ylMCED3hZbSR8ZFsa/A4=",
            ),
        ),
        TrustedHostPins(
            host = "blockstream.info",
            pins = listOf(
                "sha256/9AZIg3NfujJYTXeqbdna11kiWdkWCw/2/56Ocss5UJo=",
                "sha256/kZwN96eHtZftBWrOZUsd6cA4es80n3NzSk/XtYz2EqQ=",
            ),
        ),
        TrustedHostPins(
            host = "mempool.space",
            pins = listOf(
                "sha256/wV7micOM/PJtIxPpaZBTdQF0JnfIHXSGzrvsu7fzDdQ=",
                "sha256/KqkYYX5LYAYP7XGemqzbtPPIA8x7BS/BbOIcAXf3j2k=",
            ),
        ),
        TrustedHostPins(
            host = "mempool.emzy.de",
            pins = listOf(
                "sha256/lTPZ2uIRFziRjFobuhSqcgbmuWYaYTUf8c+0Sg2spS8=",
                "sha256/iFvwVyJSxnQdyaUvUERIf+8qk7gRze3612JMwoO3zdU=",
            ),
        ),
        TrustedHostPins(
            host = "btcscan.org",
            pins = listOf(
                "sha256/cD6KxsIe7o/87jpiUs0eGgF2rHqfxqVzJNV0ayn0Uok=",
                "sha256/kIdp6NNEd8wsugYyyIYFsi1ylMCED3hZbSR8ZFsa/A4=",
            ),
        ),
    )

    private val allPinnedHosts = pinnedHosts.associate { it.host to it.pins.toSet() }

    private val certificatePinner = CertificatePinner.Builder().apply {
        pinnedHosts.forEach { hostPins ->
            hostPins.pins.forEach { pin ->
                add(hostPins.host, pin)
            }
        }
    }.build()

    fun newPinnedClient(builder: OkHttpClient.Builder = OkHttpClient.Builder()): OkHttpClient.Builder {
        return builder.certificatePinner(certificatePinner)
    }

    fun requestBuilder(
        url: String,
        allowedHosts: Set<String>,
    ): Request.Builder {
        val normalized = validateTrustedHttpsUrl(url, allowedHosts)
        return Request.Builder().url(normalized)
    }

    private fun validateTrustedHttpsUrl(
        url: String,
        allowedHosts: Set<String>,
    ): String {
        val parsed = url.toHttpUrlOrNull() ?: error("无效的网络地址: $url")
        require(parsed.scheme == "https") { "仅允许 HTTPS 请求: ${parsed.host}" }
        require(parsed.host in allowedHosts) { "目标主机未列入白名单: ${parsed.host}" }
        require(allPinnedHosts.containsKey(parsed.host)) { "目标主机未配置证书 pinning: ${parsed.host}" }
        return parsed.toString()
    }
}
