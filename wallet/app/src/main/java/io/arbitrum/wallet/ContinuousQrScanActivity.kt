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
import com.google.zxing.DecodeHintType
import com.journeyapps.barcodescanner.BarcodeCallback
import com.journeyapps.barcodescanner.DecoratedBarcodeView
import com.journeyapps.barcodescanner.DefaultDecoderFactory
import com.journeyapps.barcodescanner.camera.CameraSettings
import com.journeyapps.barcodescanner.camera.CenterCropStrategy
import kotlinx.coroutines.launch

class ContinuousQrScanActivity : BiometricGateActivity() {
    companion object {
        const val EXTRA_QR_RESULT = "qr_result"
        const val EXTRA_SCAN_MODE = "scan_mode"
        const val MODE_REQUEST = "request"
        const val MODE_RESPONSE = "response"
    }

    private lateinit var barcodeView: DecoratedBarcodeView
    private lateinit var torchButton: Button
    private lateinit var titleView: TextView
    private lateinit var statusView: TextView
    private val requestResolver = RequestQrPayloadResolver()
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
            val decoded = QrImageDecoder.decodeFromUri(this@ContinuousQrScanActivity, uri)
            if (decoded.isNullOrBlank()) {
                updateStatus("相册图片未识别到二维码，请重试")
                Toast.makeText(this@ContinuousQrScanActivity, "相册图片未识别到二维码", Toast.LENGTH_SHORT).show()
                return@launch
            }
            if (scanMode == MODE_RESPONSE) {
                returnPayload(decoded)
            } else {
                handleRequestScanText(decoded)
            }
        }
    }

    private val videoLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        lifecycleScope.launch {
            updateStatus("正在解析视频二维码，请稍候")
            val scanResult = QrVideoDecoder.scanVideo(
                context = this@ContinuousQrScanActivity,
                uri = uri,
                onProgress = { progress ->
                    updateStatus("正在解析视频 ${progress.currentFrame}/${progress.totalFrames}")
                },
            ) { decodedText ->
                if (scanMode == MODE_RESPONSE) {
                    returnPayload(decodedText)
                    false
                } else {
                    handleRequestScanText(decodedText)
                    !hasReturned
                }
            }
            if (!hasReturned) {
                updateStatus("视频未识别到二维码，请重试")
                val message = if (scanResult.scannedFrames > 0) {
                    "视频未识别到可用二维码"
                } else {
                    "视频解析失败，请重试"
                }
                Toast.makeText(this@ContinuousQrScanActivity, message, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private val scanMode: String
        get() = intent.getStringExtra(EXTRA_SCAN_MODE) ?: MODE_REQUEST

    private val cameraPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startContinuousScan() else {
                setResult(Activity.RESULT_CANCELED)
                finish()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_qr_scan)

        barcodeView = findViewById(R.id.barcode_scanner)
        torchButton = findViewById(R.id.btn_toggle_torch)
        titleView = findViewById(R.id.tv_scan_title)
        statusView = findViewById(R.id.tv_scan_status)
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
        torchButton.setOnClickListener { toggleTorch() }
        torchButton.visibility = if (hasFlash) View.VISIBLE else View.GONE
        updateTorchButton()
        findViewById<Button>(R.id.btn_pick_qr_from_gallery).setOnClickListener {
            galleryLauncher.launch("image/*")
        }
        findViewById<Button>(R.id.btn_pick_qr_from_video).setOnClickListener {
            videoLauncher.launch("video/*")
        }
        findViewById<Button>(R.id.btn_cancel_scan).setOnClickListener {
            setResult(Activity.RESULT_CANCELED)
            finish()
        }
    }

    override fun onResume() {
        super.onResume()
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startContinuousScan()
        } else {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    override fun onPause() {
        barcodeView.pause()
        super.onPause()
    }

    override fun onDestroy() {
        runCatching { barcodeView.pauseAndWait() }
        super.onDestroy()
    }

    private fun startContinuousScan() {
        barcodeView.decodeContinuous(scanCallback)
        barcodeView.resume()
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

    private val scanCallback = BarcodeCallback { scanResult ->
        if (hasReturned) return@BarcodeCallback
        val text = scanResult.text?.trim().orEmpty()
        if (text.isBlank()) return@BarcodeCallback

        val now = SystemClock.elapsedRealtime()
        if (text == lastText && now - lastReadAt < 600L) {
            return@BarcodeCallback
        }
        lastText = text
        lastReadAt = now

        if (scanMode == MODE_RESPONSE) {
            returnPayload(text)
        } else {
            handleRequestScanText(text)
        }
    }

    private fun handleRequestScanText(text: String) {
        when (val resolution = requestResolver.accept(text)) {
            is RequestQrResolution.Complete -> returnPayload(resolution.payload)
            is RequestQrResolution.Progress -> updateStatus(resolution.status)
        }
    }

    private fun returnPayload(payload: String) {
        if (hasReturned) return
        hasReturned = true
        setResult(Activity.RESULT_OK, Intent().putExtra(EXTRA_QR_RESULT, payload))
        finish()
    }

    private fun configureScanner() {
        val hints = mapOf(
            DecodeHintType.TRY_HARDER to true,
            DecodeHintType.CHARACTER_SET to Charsets.UTF_8.name()
        )
        barcodeView.decoderFactory = DefaultDecoderFactory(
            listOf(BarcodeFormat.QR_CODE),
            hints,
            Charsets.UTF_8.name(),
            0
        )
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
        titleView.text = if (scanMode == MODE_RESPONSE) {
            getString(R.string.scan_title_response)
        } else {
            getString(R.string.scan_title_request)
        }
        updateStatus(
            if (scanMode == MODE_RESPONSE) getString(R.string.scan_status_response)
            else getString(R.string.scan_status_request)
        )
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
    }

    private fun updateStatus(text: String) {
        statusView.text = text
        barcodeView.setStatusText("")
    }
}
