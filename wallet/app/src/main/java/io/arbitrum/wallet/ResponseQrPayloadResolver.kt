package io.arbitrum.wallet

private val pMofNRegex = Regex("""^p(\d+)of(\d+)\s+(.+)$""", RegexOption.IGNORE_CASE)

sealed class ResponseQrResolution {
    data class Progress(val status: String) : ResponseQrResolution()
    data class Complete(val payload: String) : ResponseQrResolution()
}

class ResponseQrPayloadResolver {
    private var expectedTotal: Int? = null
    private val parts = linkedMapOf<Int, String>()

    fun accept(rawText: String): ResponseQrResolution {
        val text = rawText.trim()
        if (text.isBlank()) return ResponseQrResolution.Progress("二维码内容为空，请继续扫描")

        val match = pMofNRegex.matchEntire(text)
        if (match == null) {
            reset()
            return ResponseQrResolution.Complete(text)
        }

        val index = match.groupValues[1].toIntOrNull()
        val total = match.groupValues[2].toIntOrNull()
        val chunk = match.groupValues[3]
        if (index == null || total == null || total <= 0 || index !in 1..total || chunk.isBlank()) {
            reset()
            return ResponseQrResolution.Progress("结果分片格式无效，请继续扫描")
        }

        val expected = expectedTotal
        if (expected == null) {
            expectedTotal = total
        } else if (expected != total) {
            reset()
            expectedTotal = total
        }

        parts[index] = chunk
        if (parts.size < total) {
            return ResponseQrResolution.Progress("已接收结果分片 ${parts.size}/${total}，请继续扫描")
        }

        if ((1..total).any { !parts.containsKey(it) }) {
            return ResponseQrResolution.Progress("结果分片缺失，请继续扫描")
        }

        val payload = buildString {
            for (partIndex in 1..total) {
                append(parts[partIndex])
            }
        }
        reset()
        return ResponseQrResolution.Complete(payload)
    }

    fun reset() {
        expectedTotal = null
        parts.clear()
    }
}
