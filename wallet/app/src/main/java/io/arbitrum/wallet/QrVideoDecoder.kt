package io.arbitrum.wallet

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

data class VideoQrScanProgress(
    val currentFrame: Int,
    val totalFrames: Int,
    val positionMs: Long,
)

data class VideoQrScanResult(
    val foundAny: Boolean,
    val uniqueQrCount: Int,
    val scannedFrames: Int,
)

@VisibleForTesting
internal object VideoFrameSampler {
    const val MAX_FRAMES = 480
    private const val SHORT_STEP_US = 150_000L
    private const val MEDIUM_STEP_US = 200_000L
    private const val LONG_STEP_US = 250_000L

    fun buildTimestamps(durationUs: Long): List<Long> {
        if (durationUs <= 0L) return listOf(0L)

        val preferredStep = when {
            durationUs <= 15_000_000L -> SHORT_STEP_US
            durationUs <= 90_000_000L -> MEDIUM_STEP_US
            else -> LONG_STEP_US
        }
        val cappedStep = maxOf(preferredStep, ceilDiv(durationUs, MAX_FRAMES.toLong()))
        val timestamps = mutableListOf<Long>()
        var positionUs = 0L
        while (positionUs < durationUs) {
            timestamps += positionUs
            positionUs += cappedStep
        }
        if (timestamps.lastOrNull() != durationUs) {
            timestamps += durationUs
        }
        return timestamps
    }

    private fun ceilDiv(value: Long, divisor: Long): Long {
        return if (value <= 0L) 0L else ((value - 1L) / divisor) + 1L
    }
}

object QrVideoDecoder {
    private const val MAX_FRAME_EDGE = 1440

    suspend fun scanVideo(
        context: Context,
        uri: Uri,
        onProgress: ((VideoQrScanProgress) -> Unit)? = null,
        onDecodedText: (String) -> Boolean,
    ): VideoQrScanResult = withContext(Dispatchers.IO) {
        val retriever = MediaMetadataRetriever()
        val seen = linkedSetOf<String>()
        var scannedFrames = 0

        try {
            retriever.setDataSource(context, uri)
            val timestamps = VideoFrameSampler.buildTimestamps(readDurationUs(retriever))
            for ((index, timestampUs) in timestamps.withIndex()) {
                scannedFrames = index + 1
                if (onProgress != null) {
                    withContext(Dispatchers.Main.immediate) {
                        onProgress(
                            VideoQrScanProgress(
                                currentFrame = scannedFrames,
                                totalFrames = timestamps.size,
                                positionMs = timestampUs / 1_000L,
                            )
                        )
                    }
                }

                val frame = extractFrame(retriever, timestampUs) ?: continue
                val decoded = try {
                    QrImageDecoder.decodeBitmap(frame)
                } finally {
                    if (!frame.isRecycled) {
                        frame.recycle()
                    }
                }
                val text = decoded?.trim().orEmpty()
                if (text.isBlank() || !seen.add(text)) continue

                val keepGoing = withContext(Dispatchers.Main.immediate) {
                    onDecodedText(text)
                }
                if (!keepGoing) {
                    return@withContext VideoQrScanResult(
                        foundAny = true,
                        uniqueQrCount = seen.size,
                        scannedFrames = scannedFrames,
                    )
                }
            }
        } finally {
            runCatching { retriever.release() }
        }

        VideoQrScanResult(
            foundAny = seen.isNotEmpty(),
            uniqueQrCount = seen.size,
            scannedFrames = scannedFrames,
        )
    }

    private fun readDurationUs(retriever: MediaMetadataRetriever): Long {
        val durationMs = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)
            ?.toLongOrNull()
            ?: return 0L
        return durationMs * 1_000L
    }

    private fun extractFrame(retriever: MediaMetadataRetriever, timestampUs: Long): Bitmap? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            preferredScaledSize(retriever)?.let { (width, height) ->
                runCatching {
                    retriever.getScaledFrameAtTime(
                        timestampUs,
                        MediaMetadataRetriever.OPTION_CLOSEST,
                        width,
                        height,
                    )
                }.getOrNull()?.let { return it }
            }
        }
        return runCatching {
            retriever.getFrameAtTime(timestampUs, MediaMetadataRetriever.OPTION_CLOSEST)
        }.getOrNull()
    }

    private fun preferredScaledSize(retriever: MediaMetadataRetriever): Pair<Int, Int>? {
        val width = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)
            ?.toIntOrNull()
            ?: return null
        val height = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)
            ?.toIntOrNull()
            ?: return null
        if (width <= 0 || height <= 0) return null

        val longestEdge = maxOf(width, height)
        if (longestEdge <= MAX_FRAME_EDGE) {
            return width to height
        }
        val scale = MAX_FRAME_EDGE.toDouble() / longestEdge.toDouble()
        return (width * scale).roundToInt().coerceAtLeast(1) to
            (height * scale).roundToInt().coerceAtLeast(1)
    }
}
