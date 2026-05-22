package io.arbitrum.wallet

import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.util.zip.CRC32
import java.util.zip.Deflater
import java.util.zip.DeflaterOutputStream
import java.util.Base64
import kotlin.math.ceil
import kotlin.math.min

/**
 * 将离线签名请求编码为 tpr1 格式，供树莓派扫描。
 * 格式: tpr1:index/total.crc.chunk
 */
object RelayQrCodec {
    private const val PREFIX = "tpr1:"
    private const val TARGET_CHUNK_CHARS = 120
    private const val MIN_FRAGMENT_COUNT = 7
    private const val SINGLE_PAGE_THRESHOLD = 96

    data class RelayQrBundle(val payloads: List<String>, val isFragmented: Boolean)

    fun buildRelayPayloads(requestPayload: String, targetChunkChars: Int = TARGET_CHUNK_CHARS): RelayQrBundle {
        val payload = requestPayload.trim()
        require(payload.isNotBlank()) { "请求为空" }

        val compressed = deflateUtf8(payload)
        val encoded = Base64.getUrlEncoder().withoutPadding().encodeToString(compressed)
        val crc = crc32Decimal(encoded)
        val targetChars = targetChunkChars.coerceIn(40, TARGET_CHUNK_CHARS)
        val chunkChars = when {
            encoded.length <= SINGLE_PAGE_THRESHOLD -> encoded.length
            else -> {
                val perPage = ceil(encoded.length.toDouble() / MIN_FRAGMENT_COUNT).toInt()
                min(targetChars, perPage).coerceAtLeast(1)
            }
        }
        val chunks = encoded.chunked(chunkChars)
        val pages = chunks.mapIndexed { index, chunk ->
            "$PREFIX${index + 1}/${chunks.size}.$crc.$chunk"
        }
        return RelayQrBundle(payloads = pages, isFragmented = pages.size > 1)
    }

    private fun deflateUtf8(value: String): ByteArray {
        val out = ByteArrayOutputStream()
        val deflater = Deflater(Deflater.BEST_COMPRESSION)
        DeflaterOutputStream(out, deflater).use { it.write(value.toByteArray(StandardCharsets.UTF_8)) }
        return out.toByteArray()
    }

    fun crc32Decimal(value: String): String {
        val crc = CRC32()
        crc.update(value.toByteArray(StandardCharsets.UTF_8))
        return (crc.value and 0xFFFFFFFFL).toString()
    }
}
