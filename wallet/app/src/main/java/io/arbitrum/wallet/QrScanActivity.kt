package io.arbitrum.wallet

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.google.zxing.BarcodeFormat
import com.journeyapps.barcodescanner.DecoratedBarcodeView
import com.journeyapps.barcodescanner.DefaultDecoderFactory
import com.journeyapps.barcodescanner.camera.CameraSettings
import com.journeyapps.barcodescanner.camera.CenterCropStrategy
import kotlinx.coroutines.launch

class QrScanActivity : BiometricGateActivity() {
    companion object {
        const val EXTRA_QR_RESULT = "qr_result"
        const val EXTRA_STATUS_TEXT = "status_text"
    }

    private lateinit var barcodeView: DecoratedBarcodeView
    private lateinit var titleView: TextView
    private lateinit var statusView: TextView
    private lateinit var torchButton: Button
    private var hasReturned = false
    private var lastText = ""
    private var lastReadAt = 0L
    private var torchEnabled = false
    private val hasFlash by lazy {
        packageManager.hasSystemFeature(PackageManager.FEATURE_CAMERA_FLASH)
    }

    private val galleryLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        lifecycleScope.launch {
            val decoded = QrImageDecoder.decodeFromUri(this@QrScanActivity, uri)
            if (decoded.isNullOrBlank()) {
                updateStatus("相册图片未识别到二维码，请重试")
                Toast.makeText(this@QrScanActivity, "相册图片未识别到二维码", Toast.LENGTH_SHORT).show()
                return@launch
            }
            returnResult(decoded)
        }
    }

    private val videoLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        lifecycleScope.launch {
            updateStatus("正在解析视频二维码，请稍候")
            val scanResult = QrVideoDecoder.scanVideo(
                context = this@QrScanActivity,
                uri = uri,
                onProgress = { progress ->
                    updateStatus("正在解析视频 ${progress.currentFrame}/${progress.totalFrames}")
                },
            ) { decodedText ->
                returnResult(decodedText)
                false
            }
            if (!hasReturned) {
                updateStatus("视频未识别到二维码，请重试")
                val message = if (scanResult.scannedFrames > 0) {
                    "视频未识别到二维码"
                } else {
                    "视频解析失败，请重试"
                }
                Toast.makeText(this@QrScanActivity, message, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private val cameraPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startScan() else {
                setResult(Activity.RESULT_CANCELED)
                finish()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_qr_scan)
        barcodeView = findViewById(R.id.barcode_scanner)
        titleView = findViewById(R.id.tv_scan_title)
        statusView = findViewById(R.id.tv_scan_status)
        torchButton = findViewById(R.id.btn_toggle_torch)
        configureScanner()
        barcodeView.setTorchListener(object : DecoratedBarcodeView.TorchListener {
            override fun onTorchOn() {
                torchEnabled = true
                updateTorchButton()
            }

            override fun onTorchOff() {
                torchEnabled = false
                updateTorchButton()
            }
        })
        titleView.text = getString(R.string.scan_title_response)
        updateStatus(intent.getStringExtra(EXTRA_STATUS_TEXT) ?: getString(R.string.scan_status_response))
        findViewById<Button>(R.id.btn_pick_qr_from_gallery).setOnClickListener {
            galleryLauncher.launch("image/*")
        }
        findViewById<Button>(R.id.btn_pick_qr_from_video).setOnClickListener {
            videoLauncher.launch("video/*")
        }
        torchButton.setOnClickListener { toggleTorch() }
        torchButton.visibility = if (hasFlash) View.VISIBLE else View.GONE
        updateTorchButton()
        findViewById<Button>(R.id.btn_cancel_scan).setOnClickListener {
            setResult(Activity.RESULT_CANCELED)
            finish()
        }
    }

    override fun onResume() {
        super.onResume()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startScan()
        } else {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    override fun onPause() {
        barcodeView.pause()
        super.onPause()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> {
                if (hasFlash) {
                    setTorchEnabled(true)
                    true
                } else {
                    super.onKeyDown(keyCode, event)
                }
            }

            KeyEvent.KEYCODE_VOLUME_DOWN -> {
                if (hasFlash) {
                    setTorchEnabled(false)
                    true
                } else {
                    super.onKeyDown(keyCode, event)
                }
            }

            else -> super.onKeyDown(keyCode, event)
        }
    }

    private fun startScan() {
        barcodeView.decodeContinuous { result ->
            if (hasReturned) return@decodeContinuous
            val text = result.text?.trim().orEmpty()
            if (text.isBlank()) return@decodeContinuous
            val now = SystemClock.elapsedRealtime()
            if (text == lastText && now - lastReadAt < 500L) return@decodeContinuous
            lastText = text
            lastReadAt = now
            returnResult(text)
        }
        barcodeView.resume()
    }

    private fun updateStatus(text: String) {
        statusView.text = text
        barcodeView.setStatusText("")
    }

    private fun configureScanner() {
        barcodeView.decoderFactory = DefaultDecoderFactory(listOf(BarcodeFormat.QR_CODE))
        barcodeView.setCameraSettings(
            CameraSettings().apply {
                setAutoFocusEnabled(true)
                setContinuousFocusEnabled(true)
                setMeteringEnabled(true)
                setExposureEnabled(true)
                setBarcodeSceneModeEnabled(true)
                setAutoTorchEnabled(hasFlash)
            }
        )
        barcodeView.barcodeView.setPreviewScalingStrategy(CenterCropStrategy())
        barcodeView.barcodeView.setMarginFraction(0.08)
    }

    private fun toggleTorch() {
        setTorchEnabled(!torchEnabled)
    }

    private fun setTorchEnabled(enabled: Boolean) {
        if (!hasFlash || torchEnabled == enabled) return
        if (enabled) {
            barcodeView.setTorchOn()
        } else {
            barcodeView.setTorchOff()
        }
    }

    private fun updateTorchButton() {
        if (!::torchButton.isInitialized) return
        torchButton.text = getString(
            if (torchEnabled) R.string.scan_torch_off
            else R.string.scan_torch_on
        )
        torchButton.isEnabled = hasFlash
        torchButton.alpha = if (hasFlash) 1f else 0.45f
    }

    private fun returnResult(payload: String) {
        if (hasReturned) return
        hasReturned = true
        setResult(Activity.RESULT_OK, Intent().putExtra(EXTRA_QR_RESULT, payload))
        finish()
    }
}
