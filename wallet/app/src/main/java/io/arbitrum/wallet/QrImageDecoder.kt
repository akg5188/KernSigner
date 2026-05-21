package io.arbitrum.wallet

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.InvertedLuminanceSource
import com.google.zxing.MultiFormatReader
import com.google.zxing.RGBLuminanceSource
import com.google.zxing.common.GlobalHistogramBinarizer
import com.google.zxing.common.HybridBinarizer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.tasks.await
import kotlinx.coroutines.withContext

object QrImageDecoder {
    suspend fun decodeFromUri(context: Context, uri: Uri): String? {
        decodeWithMlKit(context, uri)?.let { return it }
        val bitmap = withContext(Dispatchers.IO) { loadBitmap(context, uri) } ?: return null
        return decodeBitmap(bitmap)
    }

    suspend fun decodeBitmap(bitmap: Bitmap): String? {
        decodeBitmapWithMlKit(bitmap)?.let { return it }
        return decodeBitmapWithZxingVariants(bitmap)
    }

    private suspend fun decodeWithMlKit(context: Context, uri: Uri): String? = withContext(Dispatchers.Default) {
        val scanner = BarcodeScanning.getClient(
            BarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .build()
        )
        try {
            val image = InputImage.fromFilePath(context, uri)
            scanner.process(image).await()
                .firstNotNullOfOrNull { it.rawValue?.trim()?.takeIf(String::isNotBlank) }
        } catch (_: Exception) {
            null
        } finally {
            runCatching { scanner.close() }
        }
    }

    private fun loadBitmap(context: Context, uri: Uri): Bitmap? {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }

        val maxEdge = maxOf(bounds.outWidth, bounds.outHeight)
        var sample = 1
        while (maxEdge / sample > 2200) sample *= 2

        val opts = BitmapFactory.Options().apply { inSampleSize = sample }
        return context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
    }

    private suspend fun decodeBitmapWithMlKit(bitmap: Bitmap): String? = withContext(Dispatchers.Default) {
        val scanner = BarcodeScanning.getClient(
            BarcodeScannerOptions.Builder()
                .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                .build()
        )
        try {
            val candidates = candidateBitmaps(bitmap)
            try {
                for (candidate in candidates) {
                    val image = InputImage.fromBitmap(candidate, 0)
                    val text = scanner.process(image).await()
                        .firstNotNullOfOrNull { it.rawValue?.trim()?.takeIf(String::isNotBlank) }
                    if (!text.isNullOrBlank()) return@withContext text
                }
            } finally {
                recycleCandidateBitmaps(bitmap, candidates)
            }
            null
        } catch (_: Exception) {
            null
        } finally {
            runCatching { scanner.close() }
        }
    }

    private fun decodeBitmapWithZxingVariants(bitmap: Bitmap): String? {
        val candidates = candidateBitmaps(bitmap)
        try {
            for (candidate in candidates) {
                decodeBitmapWithZxing(candidate)?.let { return it }
            }
            return null
        } finally {
            recycleCandidateBitmaps(bitmap, candidates)
        }
    }

    private fun recycleCandidateBitmaps(source: Bitmap, candidates: List<Bitmap>) {
        candidates.forEach { candidate ->
            if (candidate !== source && !candidate.isRecycled) {
                candidate.recycle()
            }
        }
    }

    private fun decodeBitmapWithZxing(bitmap: Bitmap): String? {
        if (bitmap.width <= 0 || bitmap.height <= 0) return null
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val source = RGBLuminanceSource(bitmap.width, bitmap.height, pixels)
        val hints = mapOf<DecodeHintType, Any>(
            DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
            DecodeHintType.TRY_HARDER to true,
            DecodeHintType.CHARACTER_SET to "UTF-8",
        )
        val candidates = listOf(
            BinaryBitmap(HybridBinarizer(source)),
            BinaryBitmap(GlobalHistogramBinarizer(source)),
            BinaryBitmap(HybridBinarizer(InvertedLuminanceSource(source))),
            BinaryBitmap(GlobalHistogramBinarizer(InvertedLuminanceSource(source))),
        )
        val reader = MultiFormatReader()
        for (candidate in candidates) {
            val text = runCatching { reader.decode(candidate, hints).text?.trim() }.getOrNull()
            if (!text.isNullOrBlank()) return text
            reader.reset()
        }
        return null
    }

    private fun candidateBitmaps(source: Bitmap): List<Bitmap> {
        val list = mutableListOf<Bitmap>()
        list += source

        fun addCrop(left: Int, top: Int, width: Int, height: Int) {
            if (width <= 0 || height <= 0) return
            if (left < 0 || top < 0) return
            if (left + width > source.width || top + height > source.height) return
            list += Bitmap.createBitmap(source, left, top, width, height)
        }

        val w = source.width
        val h = source.height

        // Full-screen screenshots often have tiny QR in the middle area.
        addCrop(w / 4, h / 4, w / 2, h / 2)
        addCrop(w / 5, h / 5, w * 3 / 5, h * 3 / 5)

        // Top / bottom halves for chat and browser UIs.
        addCrop(0, 0, w, h / 2)
        addCrop(0, h / 2, w, h / 2)

        // Quadrants.
        addCrop(0, 0, w / 2, h / 2)
        addCrop(w / 2, 0, w / 2, h / 2)
        addCrop(0, h / 2, w / 2, h / 2)
        addCrop(w / 2, h / 2, w / 2, h / 2)

        return list
    }
}
