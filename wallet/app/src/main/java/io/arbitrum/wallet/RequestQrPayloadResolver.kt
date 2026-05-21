package io.arbitrum.wallet

import com.sparrowwallet.hummingbird.ResultType
import com.sparrowwallet.hummingbird.URDecoder

sealed class RequestQrResolution {
    data class Progress(val status: String) : RequestQrResolution()
    data class Complete(val payload: String) : RequestQrResolution()
}

class RequestQrPayloadResolver {
    private val assembler = MultiFragmentAssembler()
    private val urDecoder = URDecoder()

    fun accept(rawText: String): RequestQrResolution {
        val text = rawText.trim()
        if (text.isBlank()) return RequestQrResolution.Progress("二维码内容为空，请继续扫描")

        WalletConnectUriParser.extract(text)?.let { wcUri ->
            return RequestQrResolution.Complete(wcUri)
        }

        if (text.startsWith("ur:", ignoreCase = true)) {
            return handleUrRequestPart(text)
        }

        return runCatching { TpQrCodec.parseInput(text) }
            .fold(
                onSuccess = { parsed ->
                    when (parsed) {
                        is ParseResult.SignRequest -> RequestQrResolution.Complete(text)
                        is ParseResult.Fragment -> {
                            when (val assembly = assembler.accept(parsed.fragment)) {
                                is AssemblyResult.Progress -> {
                                    RequestQrResolution.Progress("已接收分片 ${assembly.received}/${assembly.total}，请继续扫描")
                                }

                                is AssemblyResult.Complete -> RequestQrResolution.Complete(assembly.payload)
                                is AssemblyResult.Error -> RequestQrResolution.Progress("${assembly.reason}，请继续扫描")
                            }
                        }
                    }
                },
                onFailure = { error ->
                    val reason = error.message ?: "二维码解析失败"
                    RequestQrResolution.Progress("$reason，请继续扫描")
                }
            )
    }

    private fun handleUrRequestPart(text: String): RequestQrResolution {
        val accepted = runCatching { urDecoder.receivePart(text) }.getOrDefault(false)
        if (!accepted) {
            return RequestQrResolution.Progress("Web3 请求分片无效，请继续扫描")
        }

        return when (val result = urDecoder.result) {
            null -> {
                val percent = (urDecoder.estimatedPercentComplete * 100).toInt().coerceIn(0, 99)
                RequestQrResolution.Progress("已接收 Web3 请求分片 ${percent}%，请继续扫描")
            }

            else -> when (result.type) {
                ResultType.SUCCESS -> RequestQrResolution.Complete(result.ur.toString())
                ResultType.FAILURE -> RequestQrResolution.Progress(result.error.ifBlank { "Web3 请求解析失败，请重试" })
                else -> RequestQrResolution.Progress("Web3 请求解析状态未知，请重试")
            }
        }
    }
}
