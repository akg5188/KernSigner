package io.arbitrum.wallet

import android.annotation.SuppressLint
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Message
import android.text.format.DateUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import com.journeyapps.barcodescanner.BarcodeEncoder
import java.math.BigDecimal
import java.math.BigInteger
import java.net.IDN
import java.net.URI
import java.text.NumberFormat
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
class MainActivity : BiometricGateActivity() {
    private enum class GalleryImportTarget {
        REQUEST,
        RESPONSE,
    }

    private val viewModel: MainViewModel by viewModels()
    private var galleryImportTarget = GalleryImportTarget.REQUEST

    private val requestQrLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@registerForActivityResult
        val text = result.data?.getStringExtra(ContinuousQrScanActivity.EXTRA_QR_RESULT) ?: return@registerForActivityResult
        viewModel.onRequestScanResult(text)
    }

    private val bitcoinImportQrLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@registerForActivityResult
        val text = result.data?.getStringExtra(QrScanActivity.EXTRA_QR_RESULT) ?: return@registerForActivityResult
        viewModel.importBitcoinWatchAccountFromPayload(text)
    }

    private val addressQrLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@registerForActivityResult
        val text = result.data?.getStringExtra(QrScanActivity.EXTRA_QR_RESULT) ?: return@registerForActivityResult
        viewModel.addAddress(text)
    }

    private val responseQrLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@registerForActivityResult
        val text = result.data?.getStringExtra(ContinuousQrScanActivity.EXTRA_QR_RESULT) ?: return@registerForActivityResult
        viewModel.onResponseScanResult(text)
    }

    private val galleryQrLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        lifecycleScope.launch {
            val payload = QrImageDecoder.decodeFromUri(this@MainActivity, uri)
            if (payload.isNullOrBlank()) {
                Toast.makeText(this@MainActivity, "相册图片未识别到二维码", Toast.LENGTH_SHORT).show()
                return@launch
            }
            when (galleryImportTarget) {
                GalleryImportTarget.REQUEST -> viewModel.onRequestScanResult(payload)
                GalleryImportTarget.RESPONSE -> viewModel.onResponseScanResult(payload)
            }
        }
    }

    private val videoQrLauncher = registerForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri == null) return@registerForActivityResult
        lifecycleScope.launch {
            Toast.makeText(this@MainActivity, "正在解析视频二维码，请稍候", Toast.LENGTH_SHORT).show()
            val payload = when (galleryImportTarget) {
                GalleryImportTarget.REQUEST -> decodeRequestPayloadFromVideo(uri)
                GalleryImportTarget.RESPONSE -> decodeResponsePayloadFromVideo(uri)
            }
            if (payload.isNullOrBlank()) {
                Toast.makeText(this@MainActivity, "视频未识别到二维码，请重试", Toast.LENGTH_SHORT).show()
                return@launch
            }
            when (galleryImportTarget) {
                GalleryImportTarget.REQUEST -> viewModel.onRequestScanResult(payload)
                GalleryImportTarget.RESPONSE -> viewModel.onResponseScanResult(payload)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val state by viewModel.uiState.collectAsStateWithLifecycle()
            MaterialTheme {
                WalletScreen(
                    state = state,
                    onSelectTab = viewModel::setActiveTab,
                    onSelectChain = viewModel::selectChain,
                    onNewAddressChange = viewModel::setNewAddressInput,
                    onEvmDerivationPathChange = viewModel::setEvmDerivationPath,
                    onAddAddress = viewModel::addAddressFromInput,
                    onScanAddress = ::startAddressScan,
                    onPrepareDerivedAddressImport = viewModel::prepareDerivedAddressImport,
                    onSelectAddress = viewModel::selectAddress,
                    onRemoveAddress = viewModel::removeAddress,
                    onSaveAddressNote = viewModel::saveAddressNote,
                    onRefreshBalances = viewModel::refreshSelectedActivity,
                    onBitcoinImportInputChange = viewModel::setBitcoinImportInput,
                    onScanBitcoinWatchAccount = ::startBitcoinImportScan,
                    onImportBitcoinWatchAccount = viewModel::importBitcoinWatchAccount,
                    onRemoveBitcoinWatchAccount = viewModel::removeBitcoinWatchAccount,
                    onSaveBitcoinWatchAccountNote = viewModel::saveBitcoinWatchAccountNote,
                    onSyncBitcoinWatchAccount = viewModel::syncBitcoinWatchAccount,
                    onPrepareBitcoinTransfer = viewModel::prepareBitcoinTransfer,
                    onPrepareTransferRequest = viewModel::prepareTransferRequest,
                    onRequestInputChange = viewModel::setRequestInput,
                    onImportRawRequest = viewModel::importRawRequest,
                    onApproveWalletConnectProposal = ::approveWalletConnectProposalWithBiometric,
                    onRejectWalletConnectProposal = viewModel::rejectWalletConnectProposal,
                    onApproveWalletConnectRequest = ::approveWalletConnectRequestWithBiometric,
                    onRejectWalletConnectRequest = viewModel::rejectCurrentWalletConnectRequest,
                    onRemoveTrustedDappEntry = viewModel::removeTrustedDappEntry,
                    onPrevRelayPage = viewModel::prevSignQrPage,
                    onNextRelayPage = viewModel::nextSignQrPage,
                    onAutoAdvanceRelayPage = viewModel::nextSignQrPage,
                    onScanRequest = ::startRequestScan,
                    onPickRequestFromGallery = ::startRequestGalleryImport,
                    onPickRequestFromVideo = ::startRequestVideoImport,
                    onScanResponse = ::startResponseScan,
                    onPickResponseFromGallery = ::startResponseGalleryImport,
                    onPickResponseFromVideo = ::startResponseVideoImport,
                    onOpenUrl = ::openExternalUrl,
                    onClearPreparedRequest = viewModel::clearPreparedRequest,
                    onClearError = viewModel::clearError,
                    onClearInfo = viewModel::clearInfo,
                    onClearTxHash = viewModel::clearTxHash,
                    onClearSignature = viewModel::clearSignature,
                    onConfirmPendingBroadcast = ::confirmPendingBroadcastWithBiometric,
                    onCancelPendingBroadcast = viewModel::cancelPendingBroadcast,
                )
            }
        }
    }

    private fun startRequestScan() {
        val intent = Intent(this, ContinuousQrScanActivity::class.java)
            .putExtra(ContinuousQrScanActivity.EXTRA_SCAN_MODE, ContinuousQrScanActivity.MODE_REQUEST)
        requestQrLauncher.launch(intent)
    }

    private fun startBitcoinImportScan() {
        val intent = Intent(this, QrScanActivity::class.java)
            .putExtra(QrScanActivity.EXTRA_STATUS_TEXT, "请扫描树莓派导出的 xpub / ypub / zpub 二维码")
        bitcoinImportQrLauncher.launch(intent)
    }

    private fun startAddressScan() {
        val intent = Intent(this, QrScanActivity::class.java)
            .putExtra(QrScanActivity.EXTRA_STATUS_TEXT, "请扫描 ARB / ETH 观察地址二维码")
        addressQrLauncher.launch(intent)
    }

    private fun startResponseScan() {
        val intent = Intent(this, ContinuousQrScanActivity::class.java)
            .putExtra(ContinuousQrScanActivity.EXTRA_SCAN_MODE, ContinuousQrScanActivity.MODE_RESPONSE)
        responseQrLauncher.launch(intent)
    }

    private fun startRequestGalleryImport() {
        galleryImportTarget = GalleryImportTarget.REQUEST
        galleryQrLauncher.launch("image/*")
    }

    private fun startResponseGalleryImport() {
        galleryImportTarget = GalleryImportTarget.RESPONSE
        galleryQrLauncher.launch("image/*")
    }

    private fun startRequestVideoImport() {
        galleryImportTarget = GalleryImportTarget.REQUEST
        videoQrLauncher.launch("video/*")
    }

    private fun startResponseVideoImport() {
        galleryImportTarget = GalleryImportTarget.RESPONSE
        videoQrLauncher.launch("video/*")
    }

    private suspend fun decodeRequestPayloadFromVideo(uri: Uri): String? {
        val resolver = RequestQrPayloadResolver()
        var payload: String? = null
        QrVideoDecoder.scanVideo(
            context = this,
            uri = uri,
            onProgress = null,
        ) { decodedText ->
            when (val resolution = resolver.accept(decodedText)) {
                is RequestQrResolution.Complete -> {
                    payload = resolution.payload
                    false
                }

                is RequestQrResolution.Progress -> true
            }
        }
        return payload
    }

    private suspend fun decodeResponsePayloadFromVideo(uri: Uri): String? {
        val resolver = ResponseQrPayloadResolver()
        var payload: String? = null
        QrVideoDecoder.scanVideo(
            context = this,
            uri = uri,
            onProgress = null,
        ) { decodedText ->
            when (val resolution = resolver.accept(decodedText)) {
                is ResponseQrResolution.Complete -> {
                    payload = resolution.payload
                    false
                }

                is ResponseQrResolution.Progress -> true
            }
        }
        return payload
    }

    private fun importRequestFromClipboard() {
        Toast.makeText(this, "高安全模式已禁用系统剪贴板导入，请改用扫码或手动粘贴到输入框。", Toast.LENGTH_LONG).show()
    }

    private fun openExternalUrl(url: String) {
        openExternalHttpsUrl(this, url) {
            Toast.makeText(this, "高安全模式仅允许打开受信任的区块浏览器 HTTPS 链接", Toast.LENGTH_SHORT).show()
        }
    }

    private fun approveWalletConnectProposalWithBiometric() {
        confirmSensitiveAction(
            title = "批准 DApp 连接",
            subtitle = "请再次验证后再建立 WalletConnect 会话",
        ) {
            viewModel.approveWalletConnectProposal()
        }
    }

    private fun approveWalletConnectRequestWithBiometric() {
        confirmSensitiveAction(
            title = "批准 DApp 请求",
            subtitle = "请再次验证后再生成离线签名请求",
        ) {
            viewModel.approveCurrentWalletConnectRequest()
        }
    }

    private fun confirmPendingBroadcastWithBiometric() {
        confirmSensitiveAction(
            title = "广播已签名交易",
            subtitle = "请再次验证后再向链上广播",
        ) {
            viewModel.confirmPendingBroadcast()
        }
    }
}

@Composable
private fun WalletScreen(
    state: WalletUiState,
    onSelectTab: (WalletTab) -> Unit,
    onSelectChain: (Long) -> Unit,
    onNewAddressChange: (String) -> Unit,
    onEvmDerivationPathChange: (String) -> Unit,
    onAddAddress: () -> Unit,
    onScanAddress: () -> Unit,
    onPrepareDerivedAddressImport: () -> Unit,
    onSelectAddress: (String) -> Unit,
    onRemoveAddress: (String) -> Unit,
    onSaveAddressNote: (String, String) -> Unit,
    onRefreshBalances: () -> Unit,
    onBitcoinImportInputChange: (String) -> Unit,
    onScanBitcoinWatchAccount: () -> Unit,
    onImportBitcoinWatchAccount: () -> Unit,
    onRemoveBitcoinWatchAccount: (String) -> Unit,
    onSaveBitcoinWatchAccountNote: (String, String) -> Unit,
    onSyncBitcoinWatchAccount: (String) -> Unit,
    onPrepareBitcoinTransfer: (String, String, String, String?) -> Unit,
    onPrepareTransferRequest: (String, String, String) -> Unit,
    onRequestInputChange: (String) -> Unit,
    onImportRawRequest: () -> Unit,
    onApproveWalletConnectProposal: () -> Unit,
    onRejectWalletConnectProposal: () -> Unit,
    onApproveWalletConnectRequest: () -> Unit,
    onRejectWalletConnectRequest: () -> Unit,
    onRemoveTrustedDappEntry: (TrustedDappEntry) -> Unit,
    onPrevRelayPage: () -> Unit,
    onNextRelayPage: () -> Unit,
    onAutoAdvanceRelayPage: () -> Unit,
    onScanRequest: () -> Unit,
    onPickRequestFromGallery: () -> Unit,
    onPickRequestFromVideo: () -> Unit,
    onScanResponse: () -> Unit,
    onPickResponseFromGallery: () -> Unit,
    onPickResponseFromVideo: () -> Unit,
    onOpenUrl: (String) -> Unit,
    onClearPreparedRequest: () -> Unit,
    onClearError: () -> Unit,
    onClearInfo: () -> Unit,
    onClearTxHash: () -> Unit,
    onClearSignature: () -> Unit,
    onConfirmPendingBroadcast: () -> Unit,
    onCancelPendingBroadcast: () -> Unit,
) {
    if (state.signQrPages.size > 1 && state.signQrBitmap != null) {
        LaunchedEffect(state.signQrPages) {
            while (true) {
                delay(1800)
                onAutoAdvanceRelayPage()
            }
        }
    }

    val chain = WalletChains.require(state.selectedChainId)
    val latestError by rememberUpdatedState(state.error)
    val latestInfo by rememberUpdatedState(state.info)
    var homeSelectedAssetId by rememberSaveable { mutableStateOf<String?>(null) }
    val pageScrollState = rememberScrollState()
    val hasPreparedRequest = state.signQrPages.isNotEmpty() && state.signQrBitmap != null
    val hasPendingBroadcast = state.pendingBroadcastBitcoinTxHex.isNotBlank() || state.pendingBroadcastRawTransaction.isNotBlank()

    BackHandler(enabled = state.activeTab != WalletTab.HOME || homeSelectedAssetId != null) {
        when {
            homeSelectedAssetId != null -> homeSelectedAssetId = null
            state.activeTab != WalletTab.HOME -> onSelectTab(WalletTab.HOME)
        }
    }

    LaunchedEffect(state.error) {
        if (state.error.isNotBlank()) {
            delay(5000)
            if (latestError.isNotBlank()) onClearError()
        }
    }

    LaunchedEffect(state.info) {
        if (state.info.isNotBlank()) {
            delay(5000)
            if (latestInfo.isNotBlank()) onClearInfo()
        }
    }

    LaunchedEffect(state.preparingRequest, hasPreparedRequest, hasPendingBroadcast) {
        if (state.preparingRequest || hasPreparedRequest || hasPendingBroadcast) {
            homeSelectedAssetId = null
            onSelectTab(
                if (hasPreparedRequest && state.preparedQrKind != PreparedQrKind.PI_REQUEST) {
                    WalletTab.DISCOVER
                } else {
                    WalletTab.HOME
                }
            )
            pageScrollState.scrollTo(0)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFFF6F7FB))
            .statusBarsPadding()
            .navigationBarsPadding()
            .verticalScroll(pageScrollState)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        WalletTabs(state.activeTab, onSelectTab)

        if (state.error.isNotBlank()) {
            MessageCard(
                title = "错误",
                message = state.error,
                background = Color(0xFFFFE4E6),
            )
        }
        if (state.info.isNotBlank()) {
            MessageCard(
                title = "提示",
                message = state.info,
                background = Color(0xFFE0F2FE),
            )
        }

        if (state.signQrBitmap != null) {
            PreparedRequestSection(
                state = state,
                onPrevRelayPage = onPrevRelayPage,
                onNextRelayPage = onNextRelayPage,
                onScanResponse = onScanResponse,
                onPickResponseFromGallery = onPickResponseFromGallery,
                onPickResponseFromVideo = onPickResponseFromVideo,
                onClearPreparedRequest = onClearPreparedRequest,
            )
        }

        if (state.pendingBroadcastBitcoinTxHex.isNotBlank() || state.pendingBroadcastRawTransaction.isNotBlank()) {
            PendingBroadcastSection(
                state = state,
                onConfirmPendingBroadcast = onConfirmPendingBroadcast,
                onCancelPendingBroadcast = onCancelPendingBroadcast,
            )
        }

        if (state.txHash.isNotBlank()) {
            val txChain = WalletChains.require(state.txHashChainId ?: state.selectedChainId)
            val txUrl = state.txHashExplorerUrl.ifBlank { txChain.txUrl(state.txHash) }
            val txExplorerLabel = if (state.txHashExplorerUrl.contains("blockstream.info", ignoreCase = true)) {
                "BTC 浏览器"
            } else {
                "${txChain.shortName} 浏览器"
            }
            Card(modifier = Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.White)) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("交易已广播", fontWeight = FontWeight.Bold, color = Color(0xFF166534))
                    Text(txExplorerLabel, fontSize = 12.sp, color = Color(0xFF475467))
                    SelectionContainer {
                        Text(txUrl, fontSize = 12.sp)
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { onOpenUrl(txUrl) }) { Text("查看详情") }
                        TextButton(onClick = onClearTxHash) { Text("关闭") }
                    }
                }
            }
        }

        if (state.lastSignature.isNotBlank()) {
            Card(modifier = Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.White)) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("签名结果", fontWeight = FontWeight.Bold)
                    if (state.lastSignatureAddress.isNotBlank()) {
                        Text("地址: ${state.lastSignatureAddress}", fontSize = 12.sp, color = Color(0xFF667085))
                    }
                    SelectionContainer {
                        Text(state.lastSignature, fontSize = 12.sp)
                    }
                    Text(
                        "高安全模式不再把签名结果写入系统剪贴板。如需回传 DApp，请优先使用 WalletConnect 自动返回。",
                        fontSize = 11.sp,
                        color = Color(0xFF667085),
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        TextButton(onClick = onClearSignature) { Text("关闭") }
                    }
                }
            }
        }

        when (state.activeTab) {
            WalletTab.HOME -> {
                AssetsHubSection(
                    state = state,
                    chain = chain,
                    selectedAssetId = homeSelectedAssetId,
                    onSelectedAssetChange = { homeSelectedAssetId = it },
                    onPrepareTransferRequest = onPrepareTransferRequest,
                    onSyncBitcoinWatchAccount = onSyncBitcoinWatchAccount,
                    onPrepareBitcoinTransfer = onPrepareBitcoinTransfer,
                    onRefreshBalances = onRefreshBalances,
                )
                if (homeSelectedAssetId == null) {
                    WatchWalletHubSection(
                        state = state,
                        chain = chain,
                        onNewAddressChange = onNewAddressChange,
                        onEvmDerivationPathChange = onEvmDerivationPathChange,
                        onAddAddress = onAddAddress,
                        onScanAddress = onScanAddress,
                        onPrepareDerivedAddressImport = onPrepareDerivedAddressImport,
                        onSelectAddress = onSelectAddress,
                        onRemoveAddress = onRemoveAddress,
                        onSaveAddressNote = onSaveAddressNote,
                        onImportInputChange = onBitcoinImportInputChange,
                        onScanImport = onScanBitcoinWatchAccount,
                        onImportAccount = onImportBitcoinWatchAccount,
                        onRemoveAccount = onRemoveBitcoinWatchAccount,
                        onSaveAccountNote = onSaveBitcoinWatchAccountNote,
                        onSyncAccount = onSyncBitcoinWatchAccount,
                    )
                    if (WalletChains.ALL.size > 1) {
                        ChainSelectorSection(
                            selectedChainId = state.selectedChainId,
                            onSelectChain = onSelectChain,
                        )
                    }
                }
            }

            WalletTab.ACTIVITY -> {
                ActivitySection(
                    state = state,
                    chain = chain,
                    onRefresh = onRefreshBalances,
                )
            }
            WalletTab.DISCOVER -> {
                WalletConnectSection(
                    state = state,
                    onApproveWalletConnectProposal = onApproveWalletConnectProposal,
                    onRejectWalletConnectProposal = onRejectWalletConnectProposal,
                    onApproveWalletConnectRequest = onApproveWalletConnectRequest,
                    onRejectWalletConnectRequest = onRejectWalletConnectRequest,
                    onRemoveTrustedDappEntry = onRemoveTrustedDappEntry,
                )
                DappToolsSection(
                    state = state,
                    onRequestInputChange = onRequestInputChange,
                    onImportRawRequest = onImportRawRequest,
                    onScanRequest = onScanRequest,
                    onPickRequestFromGallery = onPickRequestFromGallery,
                    onPickRequestFromVideo = onPickRequestFromVideo,
                )
            }
        }
    }
}

@Composable
private fun PendingBroadcastSection(
    state: WalletUiState,
    onConfirmPendingBroadcast: () -> Unit,
    onCancelPendingBroadcast: () -> Unit,
) {
    val isBitcoin = state.pendingBroadcastBitcoinTxHex.isNotBlank()
    val title = if (isBitcoin) {
        "待广播 BTC 交易"
    } else {
        "待广播 ${WalletChains.require(state.preparedRequestChainId ?: state.selectedChainId).shortName} 交易"
    }
    val summary = buildList {
        if (state.requestSummary.isNotBlank()) add(state.requestSummary)
        if (state.transferInfo.isNotBlank()) add(state.transferInfo)
        if (state.dappInfo.isNotBlank()) add(state.dappInfo)
    }.joinToString("\n\n")

    Card(modifier = Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.White)) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, fontWeight = FontWeight.Bold, color = Color(0xFFB54708))
            Text(
                "树莓派签名结果已经返回。请先人工核对收款地址、金额、手续费和 DApp 来源，再决定是否广播。",
                fontSize = 12.sp,
                color = Color(0xFF475467),
            )
            if (summary.isNotBlank()) {
                SelectionContainer {
                    Text(summary, fontSize = 12.sp, color = Color(0xFF111827))
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onConfirmPendingBroadcast) { Text("确认广播") }
                TextButton(onClick = onCancelPendingBroadcast) { Text("取消") }
            }
        }
    }
}

private fun openExternalHttpsUrl(
    context: Context,
    url: String,
    onError: ((String) -> Unit)? = null,
) {
    val allowedHosts = setOf(
        "arbiscan.io",
        "www.arbiscan.io",
        "blockstream.info",
    )
    val normalized = url.trim()
    val uri = runCatching { Uri.parse(normalized) }.getOrNull()
    val host = uri?.host?.trim()?.lowercase(Locale.US).orEmpty()
    if (uri == null ||
        !uri.scheme.equals("https", ignoreCase = true) ||
        uri.userInfo != null ||
        uri.fragment != null ||
        !uri.query.isNullOrBlank() ||
        (uri.port != -1 && uri.port != 443) ||
        !allowedHosts.contains(host)
    ) {
        onError?.invoke("blocked external non-https url · ${normalized.ifBlank { "<blank>" }}")
        return
    }
    val pathSegments = uri.pathSegments.orEmpty()
    val txId = pathSegments.getOrNull(1).orEmpty()
    val isTrustedTxPath =
        pathSegments.size == 2 &&
            pathSegments.firstOrNull() == "tx" &&
            txId.isNotBlank() &&
            txId.length in 16..128 &&
            txId.all { it.isLetterOrDigit() || it == 'x' }
    if (!isTrustedTxPath) {
        onError?.invoke("blocked external non-transaction url · ${normalized.ifBlank { "<blank>" }}")
        return
    }
    val intent = Intent(Intent.ACTION_VIEW, uri).apply {
        addCategory(Intent.CATEGORY_BROWSABLE)
    }
    runCatching {
        context.startActivity(intent)
    }.onFailure { error ->
        onError?.invoke("failed external open · ${error.message ?: error.javaClass.simpleName}")
    }
}

private fun buildBrowserLikeUserAgent(defaultUserAgent: String): String {
    val withoutWv = defaultUserAgent
        .replace("; wv", "")
        .replace(" wv)", ")")
        .replace(" Version/4.0", "")

    return if (Regex(" Chrome/\\d").containsMatchIn(withoutWv)) {
        withoutWv
    } else {
        "Mozilla/5.0 (Linux; Android 13; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Mobile Safari/537.36"
    }
}

@Composable
private fun WalletTabs(activeTab: WalletTab, onSelectTab: (WalletTab) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color.White, RoundedCornerShape(18.dp))
            .border(1.dp, Color(0xFFE5E7EB), RoundedCornerShape(18.dp))
            .padding(4.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        WalletTabItem("首页", activeTab == WalletTab.HOME, Modifier.weight(1f)) { onSelectTab(WalletTab.HOME) }
        WalletTabItem("活动", activeTab == WalletTab.ACTIVITY, Modifier.weight(1f)) { onSelectTab(WalletTab.ACTIVITY) }
        WalletTabItem("DApp", activeTab == WalletTab.DISCOVER, Modifier.weight(1f)) { onSelectTab(WalletTab.DISCOVER) }
    }
}

@Composable
private fun WalletTabItem(label: String, selected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    val background = if (selected) Color(0xFFF8FAFC) else Color.Transparent
    val contentColor = if (selected) Color(0xFF344054) else Color(0xFF667085)
    Box(
        modifier = modifier,
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(background, RoundedCornerShape(14.dp))
                .border(
                    width = if (selected) 1.dp else 0.dp,
                    color = if (selected) Color(0xFFE5E7EB) else Color.Transparent,
                    shape = RoundedCornerShape(14.dp),
                )
                .clickable(onClick = onClick)
                .padding(vertical = 11.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                label,
                color = contentColor,
                textAlign = TextAlign.Center,
                fontWeight = FontWeight.Medium,
            )
        }
    }
}

@Composable
private fun WalletSectionCard(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Color.White),
        shape = RoundedCornerShape(22.dp),
        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
            content = content,
        )
    }
}

@Composable
private fun SectionHeader(
    title: String,
    subtitle: String? = null,
    trailing: (@Composable () -> Unit)? = null,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(title, fontWeight = FontWeight.Bold, color = Color(0xFF101828), fontSize = 15.sp)
            if (!subtitle.isNullOrBlank()) {
                Text(subtitle, fontSize = 11.sp, color = Color(0xFF667085))
            }
        }
        if (trailing != null) {
            Box(contentAlignment = Alignment.CenterEnd) { trailing() }
        }
    }
}

@Composable
private fun StatusChip(
    label: String,
    background: Color = Color.White,
    contentColor: Color = Color(0xFF5B43B4),
) {
    Surface(
        color = background,
        shape = RoundedCornerShape(999.dp),
        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
    ) {
        Text(
            label,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
            fontSize = 11.sp,
            color = contentColor,
            fontWeight = FontWeight.Medium,
        )
    }
}

@Composable
private fun tpPrimaryButtonColors() = ButtonDefaults.buttonColors(
    containerColor = Color.White,
    contentColor = Color(0xFF5B43B4),
    disabledContainerColor = Color(0xFFFCFCFD),
    disabledContentColor = Color(0xFFA495D6),
)

private enum class HomeSectionMode {
    EVM,
    BTC,
}

@Composable
private fun SegmentedModeTabs(
    options: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color.White, RoundedCornerShape(999.dp))
            .border(1.dp, Color(0xFFE5E7EB), RoundedCornerShape(999.dp))
            .padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        options.forEachIndexed { index, label ->
            val selected = index == selectedIndex
            Box(
                modifier = Modifier
                    .weight(1f)
                    .background(
                        color = if (selected) Color(0xFFF8FAFC) else Color.Transparent,
                        shape = RoundedCornerShape(999.dp),
                    )
                    .border(
                        width = if (selected) 1.dp else 0.dp,
                        color = if (selected) Color(0xFFE5E7EB) else Color.Transparent,
                        shape = RoundedCornerShape(999.dp),
                    )
                    .clickable { onSelect(index) }
                    .padding(vertical = 8.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    label,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium,
                    color = if (selected) Color(0xFF344054) else Color(0xFF667085),
                )
            }
        }
    }
}

@Composable
private fun WatchWalletHubSection(
    state: WalletUiState,
    chain: WalletChain,
    onNewAddressChange: (String) -> Unit,
    onEvmDerivationPathChange: (String) -> Unit,
    onAddAddress: () -> Unit,
    onScanAddress: () -> Unit,
    onPrepareDerivedAddressImport: () -> Unit,
    onSelectAddress: (String) -> Unit,
    onRemoveAddress: (String) -> Unit,
    onSaveAddressNote: (String, String) -> Unit,
    onImportInputChange: (String) -> Unit,
    onScanImport: () -> Unit,
    onImportAccount: () -> Unit,
    onRemoveAccount: (String) -> Unit,
    onSaveAccountNote: (String, String) -> Unit,
    onSyncAccount: (String) -> Unit,
) {
    var mode by rememberSaveable { mutableStateOf(HomeSectionMode.EVM) }
    var showManualImport by rememberSaveable { mutableStateOf(false) }
    var showAddressManager by rememberSaveable { mutableStateOf(false) }
    var showBitcoinImportPanel by rememberSaveable { mutableStateOf(state.bitcoinWatchAccounts.isEmpty()) }
    var showBitcoinAccounts by rememberSaveable { mutableStateOf(state.bitcoinWatchAccounts.isNotEmpty()) }
    var expandedAccountId by rememberSaveable { mutableStateOf<String?>(null) }

    WalletSectionCard {
        SectionHeader(
            title = "观察钱包",
            trailing = {
                val label = if (mode == HomeSectionMode.EVM) {
                    if (state.addresses.isEmpty()) "未连接" else "${state.addresses.size} 个地址"
                } else {
                    if (state.bitcoinWatchAccounts.isEmpty()) "未导入" else "${state.bitcoinWatchAccounts.size} 个账户"
                }
                StatusChip(label)
            },
        )

        SegmentedModeTabs(
            options = listOf(chain.shortName, "BTC"),
            selectedIndex = if (mode == HomeSectionMode.EVM) 0 else 1,
            onSelect = { mode = if (it == 0) HomeSectionMode.EVM else HomeSectionMode.BTC },
        )

        if (mode == HomeSectionMode.EVM) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedTextField(
                    value = state.evmDerivationPath,
                    onValueChange = onEvmDerivationPathChange,
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text("派生路径", fontSize = 11.sp) },
                    placeholder = { Text(DEFAULT_EVM_DERIVATION_PATH, fontSize = 11.sp) },
                )
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    onClick = onPrepareDerivedAddressImport,
                    modifier = Modifier.weight(1f),
                ) {
                    Text("连接树莓派", fontSize = 11.sp)
                }
                OutlinedButton(onClick = { showAddressManager = !showAddressManager }, modifier = Modifier.weight(1f)) {
                    Text(if (showAddressManager) "收起地址" else "管理地址", fontSize = 11.sp)
                }
                OutlinedButton(onClick = { showManualImport = !showManualImport }, modifier = Modifier.weight(1f)) {
                    Text(if (showManualImport) "收起手动" else "手动添加", fontSize = 11.sp)
                }
            }

            if (showManualImport) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    OutlinedTextField(
                        value = state.newAddressInput,
                        onValueChange = onNewAddressChange,
                        placeholder = { Text("添加地址 (0x...)", fontSize = 11.sp) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                    )
                    OutlinedButton(onClick = onScanAddress) {
                        Text("扫码", fontSize = 11.sp)
                    }
                    OutlinedButton(onClick = onAddAddress) {
                        Text("添加", fontSize = 11.sp)
                    }
                }
            }

            if (showAddressManager && state.addresses.isNotEmpty()) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    state.addresses.forEach { address ->
                        AddressListItem(
                            address = address,
                            note = state.addressNotes[address.lowercase()].orEmpty(),
                            isSelected = address.equals(state.selectedAddress, ignoreCase = true),
                            onSelect = { onSelectAddress(address) },
                            onRemove = { onRemoveAddress(address) },
                            onSaveNote = onSaveAddressNote,
                        )
                    }
                }
            }
        } else {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    onClick = onScanImport,
                    modifier = Modifier.weight(1f),
                ) {
                    Text("扫码导入", fontSize = 11.sp)
                }
                OutlinedButton(onClick = { showBitcoinImportPanel = !showBitcoinImportPanel }, modifier = Modifier.weight(1f)) {
                    Text(if (showBitcoinImportPanel) "收起粘贴" else "粘贴导入", fontSize = 11.sp)
                }
                OutlinedButton(onClick = { showBitcoinAccounts = !showBitcoinAccounts }, modifier = Modifier.weight(1f)) {
                    Text(if (showBitcoinAccounts) "收起账户" else "查看账户", fontSize = 11.sp)
                }
            }

            if (showBitcoinImportPanel || state.bitcoinWatchAccounts.isEmpty()) {
                OutlinedTextField(
                    value = state.bitcoinImportInput,
                    onValueChange = onImportInputChange,
                    placeholder = { Text("粘贴 xpub / ypub / zpub", fontSize = 11.sp) },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 2,
                    maxLines = 3,
                )
                OutlinedButton(
                    onClick = onImportAccount,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("导入账户", fontSize = 11.sp)
                }
            }

            if (showBitcoinAccounts && state.bitcoinWatchAccounts.isNotEmpty()) {
                Column(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    state.bitcoinWatchAccounts.forEach { account ->
                        BitcoinWatchAccountCard(
                            account = account,
                            expanded = expandedAccountId == account.id,
                            onToggleExpanded = {
                                expandedAccountId = if (expandedAccountId == account.id) null else account.id
                            },
                            onRemoveAccount = onRemoveAccount,
                            onSaveNote = onSaveAccountNote,
                            onSyncAccount = onSyncAccount,
                            showBalanceSummary = false,
                            showSyncSummary = false,
                            showTransferAction = false,
                            onPrepareTransfer = { _, _, _, _ -> },
                        )
                    }
                }
            }
        }
    }
}

private data class CombinedAssetEntry(
    val id: String,
    val symbol: String,
    val title: String,
    val subtitle: String,
    val amountLabel: String,
    val usdLabel: String?,
    val isBitcoin: Boolean,
    val token: TokenInfo? = null,
    val asset: AssetBalanceUi? = null,
    val btcAccount: BitcoinWatchAccount? = null,
)

private enum class AssetActionMode {
    RECEIVE,
    SEND,
}

private enum class TransferInputMode {
    ASSET,
    USD,
}

private data class TransactionDetailField(
    val label: String,
    val value: String,
    val copyable: Boolean = false,
)

private data class TransactionDetailUi(
    val title: String,
    val amountLabel: String,
    val statusLabel: String,
    val fields: List<TransactionDetailField>,
)

private fun formatAbsoluteTimestamp(timestamp: Long): String {
    return SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date(timestamp))
}

private fun formatGwei(value: BigInteger?): String? {
    if (value == null) return null
    val decimal = BigDecimal(value).divide(BigDecimal.TEN.pow(9), 9, java.math.RoundingMode.DOWN).stripTrailingZeros()
    return "${decimal.toPlainString()} Gwei"
}

private fun isBitcoinActivity(item: WalletActivityItem): Boolean {
    return item.amountLabel.contains("BTC", ignoreCase = true) ||
        item.title.contains("BTC", ignoreCase = true) ||
        item.externalUrl.contains("blockstream.info")
}

private fun buildFallbackTransactionDetail(item: WalletActivityItem, chain: WalletChain): TransactionDetailUi {
    val fields = buildList {
        add(TransactionDetailField("网络", WalletChains.require(item.chainId.takeIf { it != 0L } ?: chain.chainId).shortName))
        if (item.subtitle.isNotBlank()) add(TransactionDetailField("对方", item.subtitle, copyable = true))
        if (item.detail.isNotBlank()) add(TransactionDetailField("说明", item.detail))
        if (item.txHash.isNotBlank()) add(TransactionDetailField("交易哈希", item.txHash, copyable = true))
        add(TransactionDetailField("时间", formatAbsoluteTimestamp(item.timestamp)))
    }
    return TransactionDetailUi(
        title = item.title,
        amountLabel = item.amountLabel,
        statusLabel = item.statusLabel.ifBlank { "链上记录" },
        fields = fields,
    )
}

private suspend fun resolveTransactionDetail(
    item: WalletActivityItem,
    chain: WalletChain,
): TransactionDetailUi {
    if (item.txHash.isBlank()) return buildFallbackTransactionDetail(item, chain)
    return if (isBitcoinActivity(item)) {
        val detail = BitcoinTransferService.fetchTransactionDetail(item.externalUrl, item.txHash) ?: return buildFallbackTransactionDetail(item, chain)
        TransactionDetailUi(
            title = item.title,
            amountLabel = item.amountLabel,
            statusLabel = detail.statusLabel,
            fields = buildList {
                add(TransactionDetailField("付款方", detail.fromSummary, copyable = true))
                add(TransactionDetailField("收款方", detail.toSummary, copyable = true))
                add(TransactionDetailField("矿工费", formatBitcoinSats(detail.feeSats)))
                detail.blockHeight?.let { add(TransactionDetailField("区块高度", it.toString(), copyable = true)) }
                detail.confirmations?.let { add(TransactionDetailField("确认数", it.toString())) }
                add(TransactionDetailField("时间", formatAbsoluteTimestamp(detail.timestamp)))
                add(TransactionDetailField("输入 / 输出", "${detail.inputCount} / ${detail.outputCount}"))
                add(TransactionDetailField("大小 / 权重", "${detail.size} bytes / ${detail.weight} wu"))
                add(TransactionDetailField("交易哈希", detail.txid, copyable = true))
            },
        )
    } else {
        val detail = EvmRpc.getTransactionDetail(chain, item.txHash) ?: return buildFallbackTransactionDetail(item, chain)
        TransactionDetailUi(
            title = item.title,
            amountLabel = item.amountLabel,
            statusLabel = detail.statusLabel,
            fields = buildList {
                add(TransactionDetailField("付款方", detail.fromAddress.ifBlank { "-" }, copyable = detail.fromAddress.isNotBlank()))
                add(TransactionDetailField("收款方", detail.toAddress.ifBlank { "-" }, copyable = detail.toAddress.isNotBlank()))
                detail.feeWei?.let { add(TransactionDetailField("网络费", "${BigDecimal(it).divide(BigDecimal.TEN.pow(18), 18, java.math.RoundingMode.DOWN).stripTrailingZeros().toPlainString()} ${chain.nativeSymbol}")) }
                detail.gasUsed?.let { gasUsed ->
                    val gasLimit = detail.gasLimit?.toString() ?: "-"
                    add(TransactionDetailField("Gas Used / Limit", "${gasUsed} / $gasLimit"))
                }
                formatGwei(detail.baseFeePerGas)?.let { add(TransactionDetailField("Base Fee", it)) }
                formatGwei(detail.priorityFeePerGas)?.let { add(TransactionDetailField("Priority Fee", it)) }
                formatGwei(detail.maxFeePerGas)?.let { add(TransactionDetailField("Max Fee", it)) }
                detail.nonce?.let { add(TransactionDetailField("Nonce", it.toString())) }
                detail.blockNumber?.let { add(TransactionDetailField("区块高度", it.toString(), copyable = true)) }
                detail.confirmations?.let { add(TransactionDetailField("确认数", it.toString())) }
                add(TransactionDetailField("时间", formatAbsoluteTimestamp(detail.timestamp)))
                if (detail.methodLabel.isNotBlank()) add(TransactionDetailField("方法", detail.methodLabel))
                detail.transferSummary.forEachIndexed { index, line ->
                    add(TransactionDetailField("转账 ${index + 1}", line))
                }
                if (detail.dataInput.isNotBlank()) add(TransactionDetailField("数据输入", detail.dataInput, copyable = true))
                add(TransactionDetailField("交易哈希", detail.txHash, copyable = true))
            },
        )
    }
}

@Composable
private fun AssetsHubSection(
    state: WalletUiState,
    chain: WalletChain,
    selectedAssetId: String?,
    onSelectedAssetChange: (String?) -> Unit,
    onPrepareTransferRequest: (String, String, String) -> Unit,
    onSyncBitcoinWatchAccount: (String) -> Unit,
    onPrepareBitcoinTransfer: (String, String, String, String?) -> Unit,
    onRefreshBalances: () -> Unit,
) {
    val context = LocalContext.current
    val evmAssets = (state.chainPortfolios[state.selectedChainId]?.assets ?: emptyList()).map { asset ->
        CombinedAssetEntry(
            id = "evm:${asset.symbol}",
            symbol = asset.symbol,
            title = asset.symbol,
            subtitle = asset.name,
            amountLabel = asset.amount,
            usdLabel = formatUsdAmount(asset.usdAmount),
            isBitcoin = false,
            token = chain.tokens.firstOrNull { it.symbol.equals(asset.symbol, ignoreCase = true) },
            asset = asset,
        )
    }
    val btcAssets = state.bitcoinWatchAccounts.map { account ->
        CombinedAssetEntry(
            id = "btc:${account.id}",
            symbol = "BTC",
            title = account.note.ifBlank { account.label },
            subtitle = buildString {
                append(account.scriptTypeLabel)
                if (account.accountFingerprintHex.isNotBlank()) {
                    append(" · 指纹 ")
                    append(account.accountFingerprintHex)
                }
            },
            amountLabel = formatBitcoinSats(account.balanceSats),
            usdLabel = formatUsdAmount(bitcoinBalanceUsd(account.balanceSats, account.priceUsd)),
            isBitcoin = true,
            btcAccount = account,
        )
    }
    val assets = btcAssets + evmAssets
    var actionMode by rememberSaveable(selectedAssetId) { mutableStateOf<AssetActionMode?>(null) }
    var transferInputMode by rememberSaveable(selectedAssetId) { mutableStateOf(TransferInputMode.ASSET) }
    var receiveInputMode by rememberSaveable(selectedAssetId) { mutableStateOf(TransferInputMode.ASSET) }
    var selectedHistoryItem by remember(selectedAssetId) { mutableStateOf<WalletActivityItem?>(null) }
    var sendTo by rememberSaveable(selectedAssetId) { mutableStateOf("") }
    var sendAmount by rememberSaveable(selectedAssetId) { mutableStateOf("") }
    var receiveAmount by rememberSaveable(selectedAssetId) { mutableStateOf("") }
    var feeRate by rememberSaveable(selectedAssetId) { mutableStateOf("") }
    val transferScanLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode != Activity.RESULT_OK) return@rememberLauncherForActivityResult
        val text = result.data?.getStringExtra(QrScanActivity.EXTRA_QR_RESULT) ?: return@rememberLauncherForActivityResult
        val target = extractTransferTargetFromQr(text)
        if (target.isNotBlank()) {
            sendTo = target
        }
    }

    LaunchedEffect(assets.map { it.id }) {
        if (selectedAssetId != null && assets.none { it.id == selectedAssetId }) {
            onSelectedAssetChange(null)
        }
    }

    val selected = assets.firstOrNull { it.id == selectedAssetId }
    BackHandler(enabled = selected != null) {
        if (selectedHistoryItem != null) {
            selectedHistoryItem = null
        } else if (actionMode != null) {
            actionMode = null
        } else {
            onSelectedAssetChange(null)
        }
    }
    LaunchedEffect(
        selectedAssetId,
        selected?.btcAccount?.id,
        selected?.btcAccount?.lastSyncAt,
        selected?.btcAccount?.lastSyncStatus,
        selected?.btcAccount?.syncing,
    ) {
        val btcAccount = selected?.btcAccount
        val shouldAutoSync = btcAccount != null &&
            !btcAccount.syncing &&
            btcAccount.lastSyncAt == 0L &&
            btcAccount.lastSyncStatus.isBlank()
        if (shouldAutoSync) {
            btcAccount?.id?.let(onSyncBitcoinWatchAccount)
        }
    }
    val recentItems = remember(state.activityItems, selectedAssetId, state.bitcoinWatchAccounts) {
        val chosen = selected
        when {
            chosen == null -> emptyList()
            chosen.isBitcoin -> chosen.btcAccount?.recentActivity.orEmpty()
            else -> state.activityItems.filter { item ->
                item.chainId == chain.chainId && (
                    item.title.contains(chosen.symbol, ignoreCase = true) ||
                        item.amountLabel.contains(chosen.symbol, ignoreCase = true) ||
                        item.subtitle.contains(chosen.symbol, ignoreCase = true)
                    )
            }
        }
    }
    LaunchedEffect(selectedAssetId, selected?.symbol, selected?.isBitcoin, recentItems.size) {
        if (selected != null && !selected.isBitcoin && recentItems.isEmpty()) {
            onRefreshBalances()
        }
    }

    WalletSectionCard {
        SectionHeader(title = "资产")
        if (assets.isEmpty()) {
            Text("暂无资产", fontSize = 12.sp, color = Color(0xFF667085))
            return@WalletSectionCard
        }

        if (selected == null) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                assets.forEach { entry ->
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                onSelectedAssetChange(entry.id)
                                actionMode = null
                            },
                        color = Color.White,
                        shape = RoundedCornerShape(14.dp),
                        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 12.dp, vertical = 10.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                Text(entry.title, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                                Text(entry.subtitle, fontSize = 11.sp, color = Color(0xFF667085))
                            }
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(10.dp),
                            ) {
                                Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                    Text(entry.amountLabel, fontSize = 13.sp, fontWeight = FontWeight.Medium, color = Color(0xFF111827))
                                    Text(entry.usdLabel ?: "--", fontSize = 11.sp, color = Color(0xFF667085))
                                }
                                Text(">", fontSize = 14.sp, color = Color(0xFF98A2B3))
                            }
                        }
                    }
                }
            }
            return@WalletSectionCard
        }

        selected.let { entry ->
            val priceUsd = if (entry.isBitcoin) entry.btcAccount?.priceUsd else entry.asset?.priceUsd
            val receiveAddress = if (entry.isBitcoin) entry.btcAccount?.nextReceiveAddress.orEmpty() else state.selectedAddress
            val resolvedReceiveAssetAmount = convertTransferInputToAssetAmount(receiveAmount, priceUsd, receiveInputMode)
            val resolvedSendAssetAmount = convertTransferInputToAssetAmount(sendAmount, priceUsd, transferInputMode)
            val receivePreviewText = when {
                receiveAmount.isBlank() -> null
                receiveInputMode == TransferInputMode.ASSET -> formatUsdAmount(calculateAssetUsd(receiveAmount, priceUsd))
                    ?: "美元换算需先同步价格"
                else -> resolvedReceiveAssetAmount?.let { "$it ${entry.symbol}" } ?: "${entry.symbol} 换算需先同步价格"
            }
            val sendPreviewText = when {
                sendAmount.isBlank() -> null
                transferInputMode == TransferInputMode.ASSET -> formatUsdAmount(calculateAssetUsd(sendAmount, priceUsd))
                    ?: "美元换算需先同步价格"
                else -> resolvedSendAssetAmount?.let { "$it ${entry.symbol}" } ?: "${entry.symbol} 换算需先同步价格"
            }
            val receiveQrPayload = buildReceiveQrPayload(
                entry = entry,
                chain = chain,
                address = receiveAddress,
                amountAsset = if (receiveInputMode == TransferInputMode.ASSET) receiveAmount.trim().takeIf { it.isNotBlank() } else resolvedReceiveAssetAmount,
            )
            val receiveQrBitmap = remember(receiveQrPayload) { generateInlineQrBitmap(receiveQrPayload) }

            when (actionMode) {
                null -> {
                    Surface(
                        color = Color.White,
                        shape = RoundedCornerShape(16.dp),
                        border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                    ) {
                        Column(
                            modifier = Modifier.padding(14.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            TextButton(
                                onClick = {
                                    onSelectedAssetChange(null)
                                    actionMode = null
                                },
                            ) {
                                Text("返回资产", fontSize = 11.sp, color = Color(0xFF5B43B4))
                            }
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(
                                    modifier = Modifier.weight(1f),
                                    verticalArrangement = Arrangement.spacedBy(2.dp),
                                ) {
                                    Text(entry.title, fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                                    Text(entry.subtitle, fontSize = 11.sp, color = Color(0xFF667085))
                                }
                                Column(
                                    horizontalAlignment = Alignment.End,
                                    verticalArrangement = Arrangement.spacedBy(2.dp),
                                ) {
                                    Text(entry.amountLabel, fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                                    Text(entry.usdLabel ?: "--", fontSize = 12.sp, color = Color(0xFF667085))
                                }
                            }
                            if (entry.isBitcoin && entry.btcAccount != null) {
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    Text(
                                        entry.btcAccount.lastSyncStatus.ifBlank { "主网优先走 Electrum 快速同步，余额和最近交易会尽快一起刷新" },
                                        modifier = Modifier.weight(1f),
                                        fontSize = 11.sp,
                                        color = Color(0xFF667085),
                                    )
                                    OutlinedButton(onClick = { onSyncBitcoinWatchAccount(entry.btcAccount.id) }) {
                                        Text("同步", fontSize = 11.sp)
                                    }
                                }
                            }
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                OutlinedButton(
                                    onClick = { actionMode = AssetActionMode.RECEIVE },
                                    modifier = Modifier.weight(1f),
                                ) {
                                    Text("收款", fontSize = 11.sp)
                                }
                                OutlinedButton(
                                    onClick = { actionMode = AssetActionMode.SEND },
                                    modifier = Modifier.weight(1f),
                                ) {
                                    Text("转账", fontSize = 11.sp)
                                }
                            }
                        }
                    }

                    if (selectedHistoryItem == null) {
                        Text("全部交易记录", fontSize = 12.sp, fontWeight = FontWeight.Medium, color = Color(0xFF111827))
                        AssetHistoryList(
                            items = recentItems,
                            chain = chain,
                            onOpenDetail = { selectedHistoryItem = it },
                        )
                    } else {
                        TransactionDetailCard(
                            item = selectedHistoryItem!!,
                            chain = chain,
                            onBack = { selectedHistoryItem = null },
                        )
                    }
                }

                AssetActionMode.RECEIVE -> {
                    Surface(
                        color = Color.White,
                        shape = RoundedCornerShape(16.dp),
                        border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                    ) {
                        Column(
                            modifier = Modifier.padding(14.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            TextButton(onClick = { actionMode = null }) {
                                Text("返回 ${entry.title}", fontSize = 11.sp, color = Color(0xFF5B43B4))
                            }
                            Text("收款 ${entry.title}", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                            if (receiveAddress.isBlank()) {
                                Text(
                                    if (entry.isBitcoin) "先同步这个 BTC 账户，才能拿到收款地址。" else "先连接观察地址，才能查看收款地址。",
                                    fontSize = 11.sp,
                                    color = Color(0xFF667085),
                                )
                                if (entry.isBitcoin) {
                                    OutlinedButton(
                                        onClick = { entry.btcAccount?.id?.let(onSyncBitcoinWatchAccount) },
                                        modifier = Modifier.fillMaxWidth(),
                                    ) {
                                        Text("同步地址", fontSize = 11.sp)
                                    }
                                }
                            } else {
                                SegmentedModeTabs(
                                    options = listOf("按币", "按美元"),
                                    selectedIndex = if (receiveInputMode == TransferInputMode.ASSET) 0 else 1,
                                    onSelect = { receiveInputMode = if (it == 0) TransferInputMode.ASSET else TransferInputMode.USD },
                                )
                                OutlinedTextField(
                                    value = receiveAmount,
                                    onValueChange = { receiveAmount = it },
                                    label = { Text(if (receiveInputMode == TransferInputMode.ASSET) "收款数量" else "收款美元", fontSize = 11.sp) },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                )
                                if (receivePreviewText != null) {
                                    Text(receivePreviewText, fontSize = 11.sp, color = Color(0xFF667085))
                                }
                                receiveQrBitmap?.let { bitmap ->
                                    Surface(
                                        color = Color.White,
                                        shape = RoundedCornerShape(14.dp),
                                        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
                                    ) {
                                        Column(
                                            modifier = Modifier
                                                .fillMaxWidth()
                                                .padding(vertical = 12.dp),
                                            horizontalAlignment = Alignment.CenterHorizontally,
                                            verticalArrangement = Arrangement.spacedBy(8.dp),
                                        ) {
                                            Image(
                                                bitmap = bitmap.asImageBitmap(),
                                                contentDescription = "${entry.symbol} 收款二维码",
                                                modifier = Modifier.size(184.dp),
                                            )
                                            Text("扫码收款", fontSize = 11.sp, color = Color(0xFF667085))
                                        }
                                    }
                                }
                                SelectionContainer {
                                    Text(receiveAddress, fontSize = 12.sp, color = Color(0xFF0F172A))
                                }
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                ) {
                                    OutlinedButton(
                                        onClick = {
                                            copyPlainText(
                                                context = context,
                                                label = "receive-address",
                                                value = receiveAddress,
                                                successMessage = "地址已复制",
                                            )
                                        },
                                        modifier = Modifier.weight(1f),
                                    ) {
                                        Text("复制地址", fontSize = 11.sp)
                                    }
                                    OutlinedButton(
                                        onClick = {
                                            copyPlainText(
                                                context = context,
                                                label = "receive-qr-payload",
                                                value = receiveQrPayload,
                                                successMessage = "收款二维码内容已复制",
                                            )
                                        },
                                        modifier = Modifier.weight(1f),
                                    ) {
                                        Text("复制二维码内容", fontSize = 11.sp)
                                    }
                                }
                            }
                        }
                    }
                }

                AssetActionMode.SEND -> {
                    Surface(
                        color = Color.White,
                        shape = RoundedCornerShape(16.dp),
                        border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                    ) {
                        Column(
                            modifier = Modifier.padding(14.dp),
                            verticalArrangement = Arrangement.spacedBy(10.dp),
                        ) {
                            TextButton(onClick = { actionMode = null }) {
                                Text("返回 ${entry.title}", fontSize = 11.sp, color = Color(0xFF5B43B4))
                            }
                            Text("转账 ${entry.title}", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                            SegmentedModeTabs(
                                options = listOf("按币", "按美元"),
                                selectedIndex = if (transferInputMode == TransferInputMode.ASSET) 0 else 1,
                                onSelect = { transferInputMode = if (it == 0) TransferInputMode.ASSET else TransferInputMode.USD },
                            )
                            OutlinedTextField(
                                value = sendTo,
                                onValueChange = { sendTo = it },
                                label = { Text("收款地址", fontSize = 11.sp) },
                                modifier = Modifier.fillMaxWidth(),
                                minLines = 2,
                            )
                            OutlinedButton(
                                onClick = {
                                    val intent = Intent(context, QrScanActivity::class.java)
                                        .putExtra(QrScanActivity.EXTRA_STATUS_TEXT, "请扫描收款地址二维码")
                                    transferScanLauncher.launch(intent)
                                },
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Text("扫码填入地址", fontSize = 11.sp)
                            }
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                OutlinedTextField(
                                    value = sendAmount,
                                    onValueChange = { sendAmount = it },
                                    label = { Text(if (transferInputMode == TransferInputMode.ASSET) "数量" else "美元", fontSize = 11.sp) },
                                    modifier = Modifier.weight(1f),
                                    singleLine = true,
                                )
                                if (entry.isBitcoin) {
                                    OutlinedTextField(
                                        value = feeRate,
                                        onValueChange = { feeRate = it },
                                        label = { Text("sat/vB", fontSize = 11.sp) },
                                        modifier = Modifier.weight(1f),
                                        singleLine = true,
                                    )
                                }
                            }
                            if (sendPreviewText != null) {
                                Text(sendPreviewText, fontSize = 11.sp, color = Color(0xFF667085))
                            }
                            OutlinedButton(
                                onClick = {
                                    val assetAmount = if (transferInputMode == TransferInputMode.ASSET) sendAmount.trim() else resolvedSendAssetAmount.orEmpty()
                                    if (entry.isBitcoin) {
                                        entry.btcAccount?.id?.let { accountId ->
                                            onPrepareBitcoinTransfer(accountId, sendTo, assetAmount, feeRate.takeIf { it.isNotBlank() })
                                        }
                                    } else {
                                        onPrepareTransferRequest(sendTo, assetAmount, entry.symbol)
                                    }
                                },
                                modifier = Modifier.fillMaxWidth(),
                                enabled = sendTo.isNotBlank() && if (transferInputMode == TransferInputMode.ASSET) {
                                    sendAmount.isNotBlank()
                                } else {
                                    resolvedSendAssetAmount != null
                                } && !state.preparingRequest,
                            ) {
                                Text(if (state.preparingRequest) "正在生成二维码..." else "发送 ${entry.symbol}", fontSize = 11.sp)
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun AssetHistoryList(
    items: List<WalletActivityItem>,
    chain: WalletChain,
    onOpenDetail: (WalletActivityItem) -> Unit,
) {
    if (items.isEmpty()) {
        Text("还没有记录", fontSize = 11.sp, color = Color(0xFF667085))
        return
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items.forEach { item ->
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onOpenDetail(item) },
                color = Color.White,
                shape = RoundedCornerShape(14.dp),
                border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
            ) {
                Column(
                    modifier = Modifier.padding(12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(2.dp),
                        ) {
                            Text(item.title, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
                            if (item.subtitle.isNotBlank()) {
                                Text(item.subtitle, fontSize = 11.sp, color = Color(0xFF667085))
                            }
                        }
                        if (item.amountLabel.isNotBlank()) {
                            Text(item.amountLabel, fontSize = 12.sp, fontWeight = FontWeight.Medium, color = Color(0xFF111827))
                        }
                    }
                    if (item.detail.isNotBlank()) {
                        Text(item.detail, fontSize = 11.sp, color = Color(0xFF98A2B3))
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            "${WalletChains.require(item.chainId.takeIf { it != 0L } ?: chain.chainId).shortName} · ${DateUtils.getRelativeTimeSpanString(item.timestamp)}",
                            fontSize = 10.sp,
                            color = Color(0xFF667085),
                        )
                        Text("详情", fontSize = 11.sp, color = Color(0xFF5B43B4))
                    }
                }
            }
        }
    }
}

@Composable
private fun TransactionDetailCard(
    item: WalletActivityItem,
    chain: WalletChain,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var detail by remember(item.id) { mutableStateOf(buildFallbackTransactionDetail(item, chain)) }
    var loading by remember(item.id) { mutableStateOf(item.txHash.isNotBlank()) }
    var loadError by remember(item.id) { mutableStateOf<String?>(null) }

    LaunchedEffect(item.id) {
        loading = item.txHash.isNotBlank()
        loadError = null
        detail = buildFallbackTransactionDetail(item, chain)
        if (item.txHash.isBlank()) {
            loading = false
            return@LaunchedEffect
        }
        runCatching { resolveTransactionDetail(item, chain) }
            .onSuccess { detail = it }
            .onFailure { loadError = it.message ?: "加载详情失败" }
        loading = false
    }

    Surface(
        color = Color.White,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            TextButton(onClick = onBack) {
                Text("返回交易记录", fontSize = 11.sp, color = Color(0xFF5B43B4))
            }
            Text("交易详情", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
            Text(detail.statusLabel, fontSize = 12.sp, color = Color(0xFF667085))
            if (detail.amountLabel.isNotBlank()) {
                Text(detail.amountLabel, fontSize = 22.sp, fontWeight = FontWeight.SemiBold, color = Color(0xFF111827))
            }
            if (loading) {
                Text("正在加载链上详情...", fontSize = 11.sp, color = Color(0xFF667085))
            }
            loadError?.let {
                Text(it, fontSize = 11.sp, color = Color(0xFFB42318))
            }
            detail.fields.forEach { field ->
                Surface(
                    color = Color.White,
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
                ) {
                    Column(
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(field.label, fontSize = 11.sp, color = Color(0xFF667085))
                        SelectionContainer {
                            Text(field.value, fontSize = 12.sp, color = Color(0xFF111827))
                        }
                        if (field.copyable) {
                            TextButton(
                                onClick = {
                                    copyPlainText(
                                        context = context,
                                        label = field.label,
                                        value = field.value,
                                        successMessage = "${field.label}已复制",
                                    )
                                },
                            ) {
                                Text("复制${field.label}", fontSize = 11.sp, color = Color(0xFF5B43B4))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun BitcoinTransferCompactCard(
    account: BitcoinWatchAccount,
    onSyncAccount: (String) -> Unit,
    onPrepareTransfer: (String, String, String, String?) -> Unit,
) {
    var showComposer by rememberSaveable(account.id) { mutableStateOf(false) }
    var transferTo by rememberSaveable(account.id) { mutableStateOf("") }
    var transferAmount by rememberSaveable(account.id) { mutableStateOf("") }
    var feeRate by rememberSaveable(account.id) { mutableStateOf("") }

    Surface(
        color = Color.White,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(account.label, fontWeight = FontWeight.SemiBold, fontSize = 13.sp, color = Color(0xFF111827))
                    Text(
                        formatBitcoinSats(account.balanceSats),
                        fontSize = 17.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = Color(0xFF111827),
                    )
                    Text(
                        formatUsdAmount(bitcoinBalanceUsd(account.balanceSats, account.priceUsd)) ?: "--",
                        fontSize = 11.sp,
                        color = Color(0xFF667085),
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    OutlinedButton(onClick = { onSyncAccount(account.id) }) {
                        Text(if (account.syncing) "同步中" else "同步", fontSize = 10.sp)
                    }
                    Button(onClick = { showComposer = !showComposer }) {
                        Text(if (showComposer) "收起" else "发送", fontSize = 10.sp)
                    }
                }
            }

            if (showComposer) {
                OutlinedTextField(
                    value = transferTo,
                    onValueChange = { transferTo = it },
                    placeholder = { Text("收款 BTC 地址", fontSize = 11.sp) },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 2,
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedTextField(
                        value = transferAmount,
                        onValueChange = { transferAmount = it },
                        placeholder = { Text("数量", fontSize = 11.sp) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                    )
                    OutlinedTextField(
                        value = feeRate,
                        onValueChange = { feeRate = it },
                        placeholder = { Text("sat/vB", fontSize = 11.sp) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                    )
                }
                val btcUsdPreview = calculateUsdPreview(transferAmount, account.priceUsd)
                if (btcUsdPreview != null) {
                    Text(btcUsdPreview, fontSize = 11.sp, color = Color(0xFF667085))
                }
                Button(
                    onClick = { onPrepareTransfer(account.id, transferTo, transferAmount, feeRate.takeIf { it.isNotBlank() }) },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("准备签名", fontSize = 11.sp)
                }
            }
        }
    }
}

@Composable
private fun WalletOverviewSection(
    state: WalletUiState,
    chain: WalletChain,
    onRefreshBalances: () -> Unit,
    onNewAddressChange: (String) -> Unit,
    onEvmDerivationPathChange: (String) -> Unit,
    onAddAddress: () -> Unit,
    onPrepareDerivedAddressImport: () -> Unit,
    onSelectAddress: (String) -> Unit,
    onRemoveAddress: (String) -> Unit,
) {
    var showManualImport by rememberSaveable { mutableStateOf(false) }
    var showAddressManager by rememberSaveable { mutableStateOf(false) }

    WalletSectionCard {
        SectionHeader(
            title = chain.displayName,
            trailing = {
                TextButton(onClick = onRefreshBalances) {
                    Text(if (state.loadingBalances) "同步中" else "刷新", fontSize = 12.sp)
                }
            },
        )

        Surface(
            color = Color(0xFF0F172A),
            shape = RoundedCornerShape(18.dp),
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text(
                    if (state.selectedAddress.isBlank()) "未连接观察地址" else shortAddressLabel(state.selectedAddress, head = 12, tail = 10),
                    fontSize = 24.sp,
                    lineHeight = 28.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = Color.White,
                )
                Text(
                    state.evmDerivationPath,
                    fontSize = 12.sp,
                    color = Color(0xFFCBD5E1),
                )
                OutlinedTextField(
                    value = state.evmDerivationPath,
                    onValueChange = onEvmDerivationPathChange,
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    placeholder = { Text(DEFAULT_EVM_DERIVATION_PATH, fontSize = 12.sp) },
                )
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                onClick = onPrepareDerivedAddressImport,
                modifier = Modifier.weight(1f),
            ) {
                Text("从树莓派导入", fontSize = 12.sp)
            }
            OutlinedButton(
                onClick = { showAddressManager = !showAddressManager },
                modifier = Modifier.weight(1f),
            ) {
                Text(if (showAddressManager) "收起地址" else "管理地址", fontSize = 12.sp)
            }
        }

        TextButton(onClick = { showManualImport = !showManualImport }) {
            Text(if (showManualImport) "收起手动添加" else "手动添加地址", fontSize = 12.sp)
        }

        if (showManualImport) {
            Row(
                modifier = Modifier
                    .fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = state.newAddressInput,
                    onValueChange = onNewAddressChange,
                    placeholder = { Text("添加地址 (0x...)", fontSize = 12.sp) },
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = 56.dp),
                    singleLine = true,
                )
                Button(onClick = onAddAddress, modifier = Modifier.heightIn(min = 48.dp)) {
                    Text("添加", fontSize = 12.sp)
                }
            }
        }

        if (showAddressManager && state.addresses.isNotEmpty()) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 4.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                state.addresses.forEach { address ->
                    AddressListItem(
                        address = address,
                        note = state.addressNotes[address.lowercase()].orEmpty(),
                        isSelected = address.equals(state.selectedAddress, ignoreCase = true),
                        onSelect = { onSelectAddress(address) },
                        onRemove = { onRemoveAddress(address) },
                        onSaveNote = { _, _ -> },
                    )
                }
            }
        }
    }
}

@Composable
private fun BitcoinPrototypeSection(
    state: WalletUiState,
    onImportInputChange: (String) -> Unit,
    onScanImport: () -> Unit,
    onImportAccount: () -> Unit,
    onRemoveAccount: (String) -> Unit,
    onSyncAccount: (String) -> Unit,
    onPrepareTransfer: (String, String, String, String?) -> Unit,
) {
    var showImportPanel by rememberSaveable { mutableStateOf(state.bitcoinWatchAccounts.isEmpty()) }
    var showAccounts by rememberSaveable { mutableStateOf(false) }
    var expandedAccountId by rememberSaveable { mutableStateOf<String?>(null) }
    val totalSats = state.bitcoinWatchAccounts.sumOf { it.balanceSats }
    val totalUsd = state.bitcoinWatchAccounts.mapNotNull { bitcoinBalanceUsd(it.balanceSats, it.priceUsd) }.takeIf { it.isNotEmpty() }?.sum()

    WalletSectionCard {
        SectionHeader(
            title = "Bitcoin",
            trailing = {
                if (state.bitcoinWatchAccounts.isNotEmpty()) {
                    StatusChip("${state.bitcoinWatchAccounts.size} 个账户")
                }
            },
        )

        Surface(
            color = Color(0xFF111827),
            shape = RoundedCornerShape(18.dp),
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text(
                    if (state.bitcoinWatchAccounts.isEmpty()) "0 BTC" else formatBitcoinSats(totalSats),
                    fontSize = 26.sp,
                    lineHeight = 30.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = Color.White,
                )
                Text(
                    formatUsdAmount(totalUsd) ?: "--",
                    fontSize = 14.sp,
                    color = Color(0xFFD1D5DB),
                )
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Button(
                onClick = { showImportPanel = !showImportPanel },
                modifier = Modifier.weight(1f),
            ) {
                Text(if (showImportPanel) "收起导入" else "导入账户", fontSize = 12.sp)
            }
            OutlinedButton(
                onClick = { showAccounts = !showAccounts },
                modifier = Modifier.weight(1f),
            ) {
                Text(if (showAccounts) "收起账户" else "查看账户", fontSize = 12.sp)
            }
        }

        if (showImportPanel || state.bitcoinWatchAccounts.isEmpty()) {
            OutlinedTextField(
                value = state.bitcoinImportInput,
                onValueChange = onImportInputChange,
                placeholder = {
                    Text("粘贴 xpub / ypub / zpub，或直接扫码导入", fontSize = 12.sp)
                },
                modifier = Modifier.fillMaxWidth(),
                minLines = 2,
                maxLines = 4,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(onClick = onScanImport, modifier = Modifier.weight(1f)) { Text("扫码导入", fontSize = 12.sp) }
                Button(onClick = onImportAccount, modifier = Modifier.weight(1f)) { Text("导入 BTC 账户", fontSize = 12.sp) }
            }
        }

        if (state.bitcoinWatchAccounts.isNotEmpty() && showAccounts) {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                state.bitcoinWatchAccounts.forEach { account ->
                    BitcoinWatchAccountCard(
                        account = account,
                        expanded = expandedAccountId == account.id,
                        onToggleExpanded = {
                            expandedAccountId = if (expandedAccountId == account.id) null else account.id
                        },
                        onRemoveAccount = onRemoveAccount,
                        onSaveNote = { _, _ -> },
                        onSyncAccount = onSyncAccount,
                        onPrepareTransfer = onPrepareTransfer,
                    )
                }
            }
        }
    }
}

@Composable
private fun BitcoinWatchAccountCard(
    account: BitcoinWatchAccount,
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    onRemoveAccount: (String) -> Unit,
    onSaveNote: (String, String) -> Unit,
    onSyncAccount: (String) -> Unit,
    showBalanceSummary: Boolean = true,
    showSyncSummary: Boolean = true,
    showTransferAction: Boolean = true,
    onPrepareTransfer: (String, String, String, String?) -> Unit,
) {
    val context = LocalContext.current
    var showTransferComposer by rememberSaveable(account.id) { mutableStateOf(false) }
    var showNoteEditor by rememberSaveable(account.id) { mutableStateOf(false) }
    var noteDraft by rememberSaveable(account.id, account.note) { mutableStateOf(account.note) }
    var transferTo by rememberSaveable(account.id) { mutableStateOf("") }
    var transferAmount by rememberSaveable(account.id) { mutableStateOf("") }
    var feeRate by rememberSaveable(account.id) { mutableStateOf("") }

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = Color.White,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
    ) {
        Column(
            modifier = Modifier.padding(13.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Text(
                        account.note.ifBlank { account.label.ifBlank { "BTC" } },
                        fontWeight = FontWeight.SemiBold,
                        color = Color(0xFF101828),
                        fontSize = 13.sp,
                    )
                    Text(
                        buildString {
                            append(account.scriptTypeLabel)
                            if (account.accountFingerprintHex.isNotBlank()) {
                                append(" · 指纹 ")
                                append(account.accountFingerprintHex)
                            }
                        },
                        fontSize = 11.sp,
                        color = Color(0xFF667085),
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { showNoteEditor = !showNoteEditor }) {
                        Text(if (showNoteEditor) "收起备注" else "备注", fontSize = 11.sp)
                    }
                    TextButton(onClick = onToggleExpanded) {
                        Text(if (expanded) "收起" else "查看", fontSize = 11.sp)
                    }
                }
            }

            if (showNoteEditor) {
                OutlinedTextField(
                    value = noteDraft,
                    onValueChange = { noteDraft = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text("BTC 钱包备注", fontSize = 11.sp) },
                    placeholder = { Text("给这个 BTC 钱包取个名字", fontSize = 11.sp) },
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                ) {
                    TextButton(
                        onClick = {
                            noteDraft = account.note
                            showNoteEditor = false
                        },
                    ) {
                        Text("取消", fontSize = 12.sp)
                    }
                    TextButton(
                        onClick = {
                            onSaveNote(account.id, noteDraft)
                            showNoteEditor = false
                        },
                    ) {
                        Text("保存", fontSize = 12.sp)
                    }
                }
            }

            if (showBalanceSummary) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        if (account.balanceSats > 0) formatBitcoinSats(account.balanceSats) else "0 BTC",
                        fontSize = 20.sp,
                        lineHeight = 24.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = Color(0xFF111827),
                    )
                    Text(
                        formatUsdAmount(bitcoinBalanceUsd(account.balanceSats, account.priceUsd)) ?: "--",
                        fontSize = 12.sp,
                        color = Color(0xFF667085),
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    onClick = { onSyncAccount(account.id) },
                    modifier = Modifier.weight(1f),
                ) {
                    Text(if (account.syncing) "同步中..." else "同步余额", fontSize = 12.sp)
                }
                if (showTransferAction) {
                    Button(
                        onClick = { showTransferComposer = !showTransferComposer },
                        modifier = Modifier.weight(1f),
                        colors = tpPrimaryButtonColors(),
                    ) {
                        Text(if (showTransferComposer) "收起转账" else "发送 BTC", fontSize = 12.sp)
                    }
                }
            }

            if (showSyncSummary && (account.lastSyncStatus.isNotBlank() || account.nextReceiveAddress.isNotBlank())) {
                Surface(
                    color = Color(0xFFF8FAFC),
                    shape = RoundedCornerShape(14.dp),
                    border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                ) {
                    Column(
                        modifier = Modifier.padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        if (account.lastSyncStatus.isNotBlank()) {
                            Text(
                                account.lastSyncStatus,
                                fontSize = 12.sp,
                                color = Color(0xFF475467),
                            )
                        }
                        if (account.nextReceiveAddress.isNotBlank()) {
                            Text(
                                shortAddressLabel(account.nextReceiveAddress, head = 14, tail = 10),
                                fontSize = 12.sp,
                                color = Color(0xFF0F172A),
                            )
                        }
                    }
                }
            }

            if (showTransferAction && showTransferComposer) {
                OutlinedTextField(
                    value = transferTo,
                    onValueChange = { transferTo = it },
                    placeholder = { Text("收款 BTC 地址", fontSize = 12.sp) },
                    modifier = Modifier.fillMaxWidth(),
                    minLines = 2,
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedTextField(
                        value = transferAmount,
                        onValueChange = { transferAmount = it },
                        placeholder = { Text("数量 (BTC)", fontSize = 12.sp) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                    )
                    OutlinedTextField(
                        value = feeRate,
                        onValueChange = { feeRate = it },
                        placeholder = { Text("手续费 sat/vB", fontSize = 12.sp) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedButton(
                        onClick = { showTransferComposer = false },
                        modifier = Modifier.weight(1f),
                    ) {
                        Text("取消", fontSize = 12.sp)
                    }
                    Button(
                        onClick = {
                            onPrepareTransfer(account.id, transferTo, transferAmount, feeRate.takeIf { it.isNotBlank() })
                        },
                        modifier = Modifier.weight(1f),
                    ) {
                        Text("准备签名", fontSize = 12.sp)
                    }
                }
            }

            if (expanded) {
                Surface(
                    color = Color(0xFFF8FAFC),
                    shape = RoundedCornerShape(14.dp),
                    border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                ) {
                    Column(
                        modifier = Modifier.padding(12.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(account.accountPathHint, fontSize = 12.sp, color = Color(0xFF344054))
                        if (account.accountFingerprintHex.isNotBlank()) {
                            Text("指纹 ${account.accountFingerprintHex}", fontSize = 11.sp, color = Color(0xFF667085))
                        }
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        shortAddressLabel(account.xpub, head = 18, tail = 14),
                        modifier = Modifier.weight(1f),
                        fontSize = 11.sp,
                        lineHeight = 15.sp,
                        color = Color(0xFF0F172A),
                    )
                    TextButton(
                        onClick = {
                            copyPlainText(
                                context = context,
                                label = "btc-xpub",
                                value = account.xpub,
                                successMessage = "扩展公钥已复制",
                            )
                        }
                    ) {
                        Text("复制 xpub", fontSize = 11.sp)
                    }
                }
                if (account.derivationError.isNotBlank()) {
                    Text(account.derivationError, fontSize = 11.sp, color = Color(0xFFB42318))
                } else {
                    BitcoinAddressPreviewGroup(
                        title = "收款地址预览",
                        addresses = account.receivePreview,
                    )
                    BitcoinAddressPreviewGroup(
                        title = "找零地址预览",
                        addresses = account.changePreview,
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
            ) {
                TextButton(onClick = { onRemoveAccount(account.id) }) {
                    Text("删除", fontSize = 12.sp)
                }
            }
        }
    }
}

@Composable
private fun BitcoinAddressPreviewGroup(
    title: String,
    addresses: List<BitcoinDerivedAddressPreview>,
) {
    if (addresses.isEmpty()) return
    val context = LocalContext.current

    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(
            title,
            fontSize = 12.sp,
            fontWeight = FontWeight.Medium,
            color = Color(0xFF101828),
        )
        addresses.forEach { preview ->
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = Color.White,
                shape = RoundedCornerShape(12.dp),
                border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
            ) {
                Column(
                    modifier = Modifier.padding(10.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            "${preview.branchLabel} #${preview.index}",
                            fontSize = 11.sp,
                            color = Color(0xFF475467),
                        )
                        TextButton(
                            onClick = {
                                copyPlainText(
                                    context = context,
                                    label = "btc-address",
                                    value = preview.address,
                                    successMessage = "BTC 地址已复制",
                                )
                            }
                        ) {
                            Text("复制", fontSize = 11.sp)
                        }
                    }
                    SelectionContainer {
                        Text(
                            preview.address,
                            fontSize = 12.sp,
                            color = Color(0xFF0F172A),
                        )
                    }
                    Text(
                        preview.path,
                        fontSize = 10.sp,
                        color = Color(0xFF667085),
                    )
                }
            }
        }
    }
}

private fun shortAddressLabel(address: String, head: Int = 8, tail: Int = 6): String {
    if (address.length <= head + tail + 3) return address
    return "${address.take(head)}...${address.takeLast(tail)}"
}

private fun copyPlainText(
    context: Context,
    label: String,
    value: String,
    successMessage: String,
) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText(label, value))
    Toast.makeText(context, successMessage, Toast.LENGTH_SHORT).show()
}

private val usdFormatter: NumberFormat = NumberFormat.getCurrencyInstance(Locale.US).apply {
    maximumFractionDigits = 2
}

private fun formatUsdAmount(value: Double?): String? = value?.let { usdFormatter.format(it) }

private fun calculateUsdPreview(amount: String, priceUsd: Double?): String? {
    val decimal = amount.toBigDecimalOrNull() ?: return null
    return priceUsd?.let { decimal.multiply(BigDecimal.valueOf(it)).toDouble() }
        ?.let { usdFormatter.format(it) }
}

private fun calculateAssetUsd(amount: String, priceUsd: Double?): Double? {
    val decimal = amount.toBigDecimalOrNull() ?: return null
    return priceUsd?.let { decimal.multiply(BigDecimal.valueOf(it)).toDouble() }
}

private fun convertTransferInputToAssetAmount(
    input: String,
    priceUsd: Double?,
    mode: TransferInputMode,
): String? {
    val trimmed = input.trim()
    if (trimmed.isBlank()) return null
    if (mode == TransferInputMode.ASSET) return trimmed
    val usd = trimmed.toBigDecimalOrNull() ?: return null
    val price = priceUsd?.toBigDecimal() ?: return null
    if (price <= BigDecimal.ZERO) return null
    return usd.divide(price, 8, java.math.RoundingMode.DOWN)
        .stripTrailingZeros()
        .toPlainString()
}

private fun extractTransferTargetFromQr(payload: String): String {
    val trimmed = payload.trim()
    if (trimmed.isBlank()) return ""
    return when {
        trimmed.startsWith("bitcoin:", ignoreCase = true) -> trimmed.substringAfter(':').substringBefore('?').trim()
        trimmed.startsWith("ethereum:", ignoreCase = true) -> {
            trimmed.substringAfter(':')
                .substringBefore('?')
                .substringBefore('@')
                .trim()
        }
        else -> trimmed.substringBefore('?').trim()
    }
}

private fun buildReceiveQrPayload(
    entry: CombinedAssetEntry,
    chain: WalletChain,
    address: String,
    amountAsset: String?,
): String {
    val normalizedAddress = address.trim()
    if (normalizedAddress.isBlank()) return ""
    val normalizedAmount = amountAsset?.trim()?.takeIf { it.isNotBlank() }
    return if (entry.isBitcoin) {
        buildString {
            append("bitcoin:")
            append(normalizedAddress)
            if (normalizedAmount != null) {
                append("?amount=")
                append(normalizedAmount)
            }
        }
    } else {
        if (normalizedAmount != null) {
            "$normalizedAddress\n${entry.symbol}: $normalizedAmount\nNetwork: ${chain.shortName}"
        } else {
            normalizedAddress
        }
    }
}

private fun generateInlineQrBitmap(payload: String): Bitmap? {
    val normalized = payload.trim()
    if (normalized.isBlank()) return null
    return runCatching {
        val matrix = QRCodeWriter().encode(
            normalized,
            BarcodeFormat.QR_CODE,
            512,
            512,
            mapOf(
                EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.M,
                EncodeHintType.MARGIN to 1,
            ),
        )
        BarcodeEncoder().createBitmap(matrix)
    }.getOrNull()
}

private fun bitcoinBalanceUsd(sats: Long, priceUsd: Double?): Double? {
    val btc = BigDecimal.valueOf(sats).divide(BigDecimal.valueOf(100_000_000L), 8, java.math.RoundingMode.DOWN)
    return priceUsd?.let { btc.multiply(BigDecimal.valueOf(it)).toDouble() }
}

private fun findTokenPrice(state: WalletUiState, chain: WalletChain, symbol: String): Double? {
    val assets = state.chainPortfolios[chain.chainId]?.assets ?: return null
    return assets.firstOrNull { it.symbol.equals(symbol, ignoreCase = true) }?.priceUsd
}

private fun formatPriceTime(timestamp: Long): String {
    return SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(timestamp))
}

@Composable
private fun ChainSelectorSection(
    selectedChainId: Long,
    onSelectChain: (Long) -> Unit,
) {
    val selectedChain = WalletChains.require(selectedChainId)
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Color.White),
        shape = RoundedCornerShape(22.dp),
        border = BorderStroke(1.dp, Color(0xFFE5E7EB)),
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text("网络", fontWeight = FontWeight.Bold)
                    Text("切换后同步该链资产和活动", fontSize = 12.sp, color = Color(0xFF667085))
                }
                Surface(
                    color = Color(0xFFF8FAFC),
                    shape = RoundedCornerShape(999.dp),
                    border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                ) {
                    Text(
                        selectedChain.shortName,
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                        fontSize = 12.sp,
                        color = Color(0xFF344054),
                        fontWeight = FontWeight.Medium,
                    )
                }
            }
            Row(
                modifier = Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                WalletChains.ALL.forEach { chain ->
                    val selected = chain.chainId == selectedChainId
                    Box(
                        modifier = Modifier
                            .background(
                                color = if (selected) Color(chain.accentColor) else Color(0xFFF8FAFC),
                                shape = RoundedCornerShape(18.dp),
                            )
                            .border(
                                width = 1.dp,
                                color = if (selected) Color.Transparent else Color(0xFFE2E8F0),
                                shape = RoundedCornerShape(18.dp),
                            )
                            .clickable { onSelectChain(chain.chainId) },
                    ) {
                        Column(modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
                            Text(
                                chain.shortName,
                                color = if (selected) Color.White else Color(0xFF111827),
                                fontWeight = FontWeight.SemiBold,
                            )
                            Text(
                                chain.nativeSymbol,
                                color = if (selected) Color(0xFFE2E8F0) else Color(0xFF64748B),
                                fontSize = 11.sp,
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun AddressListItem(
    address: String,
    note: String,
    isSelected: Boolean,
    onSelect: () -> Unit,
    onRemove: () -> Unit,
    onSaveNote: (String, String) -> Unit,
) {
    var showNoteEditor by rememberSaveable(address) { mutableStateOf(false) }
    var noteDraft by rememberSaveable(address, note) { mutableStateOf(note) }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFFF8FAFC), RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            modifier = Modifier.fillMaxWidth(),
            contentAlignment = Alignment.CenterStart,
        ) {
            SelectionContainer {
                Text(
                    text = address,
                    fontSize = 11.sp,
                    lineHeight = 15.sp,
                    color = Color(0xFF0F172A),
                    softWrap = true,
                    overflow = TextOverflow.Visible,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
        Text(
            text = if (note.isBlank()) "未备注，可点“备注”给这个地址取个名字" else "备注：$note",
            fontSize = 11.sp,
            lineHeight = 15.sp,
            color = if (note.isBlank()) Color(0xFF98A2B3) else Color(0xFF475467),
        )
        if (showNoteEditor) {
            OutlinedTextField(
                value = noteDraft,
                onValueChange = { noteDraft = it },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("地址备注", fontSize = 11.sp) },
                placeholder = { Text("给这个地址取个名字", fontSize = 11.sp) },
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
            ) {
                TextButton(
                    onClick = {
                        noteDraft = note
                        showNoteEditor = false
                    },
                ) {
                    Text("取消", fontSize = 12.sp)
                }
                TextButton(
                    onClick = {
                        onSaveNote(address, noteDraft)
                        showNoteEditor = false
                    },
                ) {
                    Text("保存", fontSize = 12.sp)
                }
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (isSelected) {
                Text(
                    "当前",
                    fontSize = 11.sp,
                    color = Color(0xFF475467),
                    modifier = Modifier.padding(end = 4.dp),
                )
            }
            TextButton(onClick = { showNoteEditor = !showNoteEditor }) {
                Text(if (showNoteEditor) "收起备注" else "备注", fontSize = 12.sp)
            }
            TextButton(
                onClick = onSelect,
                enabled = !isSelected,
            ) {
                Text("切换", fontSize = 12.sp)
            }
            TextButton(onClick = onRemove) {
                Text("删除", fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun PortfolioSection(
    portfolio: ChainPortfolioUi?,
    isLoading: Boolean,
) {
    WalletSectionCard {
        val priceLabel = portfolio?.lastUpdatedAt?.let { "价格 ${formatPriceTime(it)}" }
        SectionHeader(
            title = "资产",
            trailing = priceLabel?.let {
                {
                    Text(it, fontSize = 11.sp, color = Color(0xFF667085))
                }
            },
        )
            if (isLoading && portfolio == null) {
                Text("正在同步余额...", color = Color(0xFF64748B))
            } else if (portfolio == null) {
                Text("暂无资产", color = Color(0xFF64748B))
            } else {
                portfolio.assets
                    .sortedBy { assetAmountLooksZero(it.amount) }
                    .forEach { asset ->
                        Surface(
                            modifier = Modifier.fillMaxWidth(),
                            color = if (asset.isNative) Color(0xFFF8FAFC) else Color.White,
                            shape = RoundedCornerShape(16.dp),
                            border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                        ) {
                        val usdLabel = formatUsdAmount(asset.usdAmount)
                        val unitPriceLabel = asset.priceUsd?.let { formatUsdAmount(it) }
                        val usdCaption = when {
                            usdLabel != null -> "≈ $usdLabel"
                            unitPriceLabel != null -> "≈ $unitPriceLabel / ${asset.symbol}"
                            else -> "≈ --"
                        }
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 14.dp, vertical = 12.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                                        Text(asset.symbol, fontWeight = FontWeight.SemiBold, color = Color(0xFF101828))
                                        if (asset.isNative) {
                                            StatusChip(
                                                label = "主币",
                                                background = Color(0xFFEFF8FF),
                                                contentColor = Color(0xFF175CD3),
                                            )
                                        }
                                    }
                                    Text(asset.name, fontSize = 11.sp, color = Color(0xFF667085))
                                }
                                Column(
                                    horizontalAlignment = Alignment.End,
                                    verticalArrangement = Arrangement.spacedBy(2.dp),
                                ) {
                                    Text(
                                        asset.amount,
                                        fontWeight = FontWeight.Medium,
                                        color = if (assetAmountLooksZero(asset.amount)) Color(0xFF98A2B3) else Color(0xFF101828),
                                    )
                                    Text(
                                        usdCaption,
                                        fontSize = 11.sp,
                                        color = Color(0xFF475467),
                                    )
                                }
                            }
                        }
                    }
            }
        }
    }

    @Composable
    private fun TransferSection(
        state: WalletUiState,
        chain: WalletChain,
        onTransferToChange: (String) -> Unit,
        onTransferAmountChange: (String) -> Unit,
        onTransferTokenChange: (String) -> Unit,
        onTransferAmountAll: () -> Unit,
        onPrepareTransfer: () -> Unit,
    ) {
        WalletSectionCard {
            SectionHeader(
                title = "转账",
            )
            OutlinedTextField(
                value = state.transferTo,
                onValueChange = onTransferToChange,
                label = { Text("收款地址") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = state.transferAmount,
                    onValueChange = onTransferAmountChange,
                    label = { Text("数量") },
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                )
                TextButton(onClick = onTransferAmountAll) { Text("全部", fontSize = 12.sp) }
            }
            val transferUsdLabel = calculateUsdPreview(
                state.transferAmount,
                findTokenPrice(state, chain, state.transferToken),
            )
            if (transferUsdLabel != null) {
                Text(
                    transferUsdLabel,
                    fontSize = 12.sp,
                    color = Color(0xFF475467),
                    modifier = Modifier.padding(top = 4.dp, bottom = 4.dp),
                )
            }
            Row(
                modifier = Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                chain.tokens.forEach { token ->
                    val selected = token.symbol.equals(state.transferToken, ignoreCase = true)
                    Box(
                        modifier = Modifier
                            .background(
                                color = if (selected) Color(chain.accentColor) else Color(0xFFF8FAFC),
                                shape = RoundedCornerShape(999.dp),
                            )
                            .border(
                                width = 1.dp,
                                color = if (selected) Color.Transparent else Color(0xFFE2E8F0),
                                shape = RoundedCornerShape(999.dp),
                            )
                            .clickable { onTransferTokenChange(token.symbol) },
                    ) {
                        Text(
                            token.symbol,
                            modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                            color = if (selected) Color.White else Color(0xFF0F172A),
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium,
                        )
                    }
                }
            }
            Button(onClick = onPrepareTransfer, modifier = Modifier.fillMaxWidth()) {
                Text("生成转账请求")
            }
        }
}

@Composable
private fun DappToolsSection(
    state: WalletUiState,
    onRequestInputChange: (String) -> Unit,
    onImportRawRequest: () -> Unit,
    onScanRequest: () -> Unit,
    onPickRequestFromGallery: () -> Unit,
    onPickRequestFromVideo: () -> Unit,
) {
    var showManualTools by rememberSaveable { mutableStateOf(false) }

    WalletSectionCard {
        SectionHeader(title = "WalletConnect / DApp")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
            Button(onClick = onScanRequest, modifier = Modifier.weight(1f)) {
                Text("扫描二维码", fontSize = 13.sp)
            }
            OutlinedButton(onClick = onPickRequestFromGallery, modifier = Modifier.weight(1f)) {
                Text("相册导入", fontSize = 12.sp)
            }
        }
        OutlinedButton(onClick = onPickRequestFromVideo, modifier = Modifier.fillMaxWidth()) {
            Text("导入视频", fontSize = 12.sp)
        }
        TextButton(onClick = { showManualTools = !showManualTools }) {
            Text(if (showManualTools) "收起文本导入" else "文本导入")
        }
        if (showManualTools) {
            OutlinedTextField(
                value = state.requestInput,
                onValueChange = onRequestInputChange,
                label = { Text("粘贴 WalletConnect / 原始请求") },
                modifier = Modifier.fillMaxWidth(),
                minLines = 2,
            )
            OutlinedButton(onClick = onImportRawRequest, modifier = Modifier.fillMaxWidth()) {
                Text("解析文本", fontSize = 12.sp)
            }
        }
    }
}

@Composable
private fun ActivitySection(
    state: WalletUiState,
    chain: WalletChain,
    onRefresh: () -> Unit,
) {
    var selectedItem by remember { mutableStateOf<WalletActivityItem?>(null) }
    val filteredItems = state.activityItems.filter {
        it.chainId == chain.chainId || it.kind == WalletActivityKind.DAPP || it.kind == WalletActivityKind.SYSTEM
    }
    BackHandler(enabled = selectedItem != null) { selectedItem = null }
    WalletSectionCard {
        SectionHeader(
            title = "活动",
            subtitle = if (state.syncingActivity) "正在同步最近代币活动..." else "本地操作记录和最近链上代币转账",
            trailing = { TextButton(onClick = onRefresh) { Text("刷新") } },
        )
            if (selectedItem != null) {
                TransactionDetailCard(item = selectedItem!!, chain = chain, onBack = { selectedItem = null })
            } else if (filteredItems.isEmpty()) {
                Text("还没有活动记录。完成转账、签名或连接 DApp 后会显示在这里。", color = Color(0xFF64748B))
            } else {
                filteredItems.forEach { item ->
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { selectedItem = item },
                        color = Color(0xFFF8FAFC),
                        shape = RoundedCornerShape(18.dp),
                        border = BorderStroke(1.dp, Color(0xFFE2E8F0)),
                    ) {
                        Column(modifier = Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(item.title, fontWeight = FontWeight.SemiBold)
                                    Text(item.subtitle, fontSize = 12.sp, color = Color(0xFF64748B))
                                }
                                if (item.amountLabel.isNotBlank()) {
                                    Text(item.amountLabel, fontWeight = FontWeight.Medium)
                                }
                            }
                            if (item.detail.isNotBlank()) {
                                Text(item.detail, fontSize = 12.sp, color = Color(0xFF475467))
                            }
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Text(
                                    "${WalletChains.require(item.chainId).shortName} · ${DateUtils.getRelativeTimeSpanString(item.timestamp)}",
                                    fontSize = 11.sp,
                                    color = Color(0xFF64748B),
                                )
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    if (item.statusLabel.isNotBlank()) {
                                        Text(item.statusLabel, fontSize = 11.sp, color = Color(0xFF0F766E))
                                    }
                                    Text("详情", fontSize = 11.sp, color = Color(0xFF5B43B4))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

@Composable
private fun WalletConnectSection(
    state: WalletUiState,
    onApproveWalletConnectProposal: () -> Unit,
    onRejectWalletConnectProposal: () -> Unit,
    onApproveWalletConnectRequest: () -> Unit,
    onRejectWalletConnectRequest: () -> Unit,
    onRemoveTrustedDappEntry: (TrustedDappEntry) -> Unit,
) {
    val proposalHost = state.walletConnectProposal?.let { walletConnectPeerHost(it.peerUrl) }.orEmpty()
    val requestHost = state.walletConnectPendingRequest?.let { walletConnectPeerHost(it.peerUrl) }.orEmpty()
    val proposalTrusted = hasTrustedDappEntryForCurrentScope(state, proposalHost)
    val requestTrusted = hasTrustedDappEntryForCurrentScope(state, requestHost)
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = Color.White),
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("WalletConnect", fontWeight = FontWeight.Bold)
            Text(
                state.walletConnectStatus.ifBlank { "尚未建立 WalletConnect 会话" },
                fontSize = 12.sp,
                color = Color(0xFF475467),
            )
            if (state.trustedDappEntries.isNotEmpty()) {
                SectionLabel("本机可信范围")
                state.trustedDappEntries.forEach { entry ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(2.dp),
                        ) {
                            Text(entry.host, fontSize = 12.sp, color = Color(0xFF0F172A))
                            Text(trustedDappEntrySummary(entry), fontSize = 11.sp, color = Color(0xFF475467))
                        }
                        TextButton(onClick = { onRemoveTrustedDappEntry(entry) }) { Text("移除") }
                    }
                }
            }
            state.walletConnectProposal?.let { proposal ->
                SectionLabel("待批准会话")
                Text("DApp: ${proposal.peerName.ifBlank { "-" }}", fontSize = 12.sp)
                if (proposal.securityLabel.isNotBlank()) {
                    Text("安全状态: ${proposal.securityLabel}", fontSize = 12.sp, color = if (proposal.isScam) Color(0xFFB42318) else Color(0xFF475467))
                }
                if (proposal.peerUrl.isNotBlank()) {
                    Text("URL: ${proposal.peerUrl}", fontSize = 12.sp)
                }
                Text(
                    "主机: ${proposalHost.ifBlank { "非法或缺少 HTTPS 主机" }}",
                    fontSize = 12.sp,
                    color = if (proposalHost.isBlank()) Color(0xFFB42318) else Color(0xFF475467),
                )
                if (proposalHost.isNotBlank()) {
                    Text(
                        if (proposalTrusted) {
                            "状态: 已绑定到当前地址/链的可信范围"
                        } else {
                            "状态: 尚未绑定到当前地址/链，批准后会写入当前作用域"
                        },
                        fontSize = 12.sp,
                        color = Color(0xFF475467),
                    )
                }
                if (proposal.peerDescription.isNotBlank()) {
                    Text("说明: ${proposal.peerDescription}", fontSize = 12.sp, color = Color(0xFF475467))
                }
                if (proposal.requiredChains.isNotEmpty()) {
                    Text("链: ${proposal.requiredChains.joinToString()}", fontSize = 12.sp)
                }
                if (proposal.requiredMethods.isNotEmpty()) {
                    Text("方法: ${proposal.requiredMethods.joinToString()}", fontSize = 12.sp)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onApproveWalletConnectProposal,
                        enabled = proposal.isVerified && !proposal.isScam && proposalHost.isNotBlank(),
                    ) { Text(if (proposalTrusted) "批准连接" else "批准并绑定当前地址/链") }
                    TextButton(onClick = onRejectWalletConnectProposal) { Text("拒绝") }
                }
            }
            state.walletConnectPendingRequest?.let { request ->
                SectionLabel("当前请求")
                Text("DApp: ${request.peerName.ifBlank { "-" }}", fontSize = 12.sp)
                if (request.securityLabel.isNotBlank()) {
                    Text("安全状态: ${request.securityLabel}", fontSize = 12.sp, color = if (request.isScam) Color(0xFFB42318) else Color(0xFF475467))
                }
                Text("方法: ${request.method}", fontSize = 12.sp)
                request.chainId?.takeIf { it.isNotBlank() }?.let {
                    Text("链: $it", fontSize = 12.sp)
                }
                if (request.peerUrl.isNotBlank()) {
                    Text("URL: ${request.peerUrl}", fontSize = 12.sp, color = Color(0xFF475467))
                }
                Text(
                    "主机: ${requestHost.ifBlank { "非法或缺少 HTTPS 主机" }}",
                    fontSize = 12.sp,
                    color = if (requestHost.isBlank()) Color(0xFFB42318) else Color(0xFF475467),
                )
                if (requestHost.isNotBlank()) {
                    Text(
                        if (requestTrusted) {
                            "状态: 已命中当前地址/链的可信范围"
                        } else {
                            "状态: 不在当前地址/链的可信范围"
                        },
                        fontSize = 12.sp,
                        color = if (requestTrusted) Color(0xFF475467) else Color(0xFFB42318),
                    )
                }
                if (request.params.isNotBlank()) {
                    val paramsPreview = request.params.replace('\n', ' ').let { if (it.length > 280) it.take(280) + "..." else it }
                    SelectionContainer {
                        Text("参数: $paramsPreview", fontSize = 12.sp, color = Color(0xFF475467))
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onApproveWalletConnectRequest,
                        enabled = request.isVerified && !request.isScam && requestHost.isNotBlank() && requestTrusted,
                    ) { Text("继续处理") }
                    TextButton(onClick = onRejectWalletConnectRequest) { Text("拒绝请求") }
                }
            }
        }
    }
}

private fun walletConnectPeerHost(peerUrl: String): String {
    val uri = runCatching { URI(peerUrl.trim()) }.getOrNull() ?: return ""
    if (!uri.scheme.equals("https", ignoreCase = true)) return ""
    if (uri.userInfo != null) return ""
    if (uri.fragment != null) return ""
    if (uri.port != -1 && uri.port != 443) return ""
    return normalizeTrustedDappHostUi(uri.host)
}

private fun hasTrustedDappEntryForCurrentScope(state: WalletUiState, host: String): Boolean {
    val normalizedHost = host.trim().lowercase()
    val selectedAddress = state.selectedAddress
    if (normalizedHost.isBlank() || selectedAddress.isBlank()) return false
    val now = System.currentTimeMillis()
    return state.trustedDappEntries.any { entry ->
        entry.host == normalizedHost &&
            entry.chainId == state.selectedChainId &&
            entry.address.equals(selectedAddress, ignoreCase = true) &&
            !isTrustedDappEntryExpiredUi(entry, now)
    }
}

private fun trustedDappEntrySummary(entry: TrustedDappEntry): String {
    val chainLabel = WalletChains.byId(entry.chainId)?.shortName ?: "chain-${entry.chainId}"
    val addressLabel = if (entry.address.length <= 14) {
        entry.address
    } else {
        "${entry.address.take(8)}...${entry.address.takeLast(4)}"
    }
    val expiresAt = entry.trustedAt + 24L * 60L * 60L * 1000L
    val expiryLabel = if (entry.trustedAt > 0L) {
        DateUtils.getRelativeTimeSpanString(
            expiresAt,
            System.currentTimeMillis(),
            DateUtils.MINUTE_IN_MILLIS,
            DateUtils.FORMAT_ABBREV_RELATIVE,
        ).toString()
    } else {
        "即将过期"
    }
    return "$chainLabel · $addressLabel · $expiryLabel 到期"
}

private fun isTrustedDappEntryExpiredUi(entry: TrustedDappEntry, now: Long = System.currentTimeMillis()): Boolean {
    val trustedAt = entry.trustedAt.takeIf { it > 0L } ?: return true
    return now - trustedAt >= 24L * 60L * 60L * 1000L
}

private fun normalizeTrustedDappHostUi(host: String?): String {
    val rawHost = host?.trim()?.lowercase().orEmpty()
    if (rawHost.isBlank()) return ""
    if (rawHost.startsWith(".") || rawHost.endsWith(".") || rawHost.contains("..")) return ""
    if (rawHost == "localhost" || !rawHost.contains('.')) return ""
    if (rawHost.contains(':')) return ""
    if (rawHost.any { ch -> !(ch in 'a'..'z' || ch in '0'..'9' || ch == '-' || ch == '.') }) return ""
    if (rawHost.split('.').any { label ->
            label.isBlank() || label.startsWith('-') || label.endsWith('-') || label.startsWith("xn--")
        }
    ) {
        return ""
    }
    val asciiHost = runCatching { IDN.toASCII(rawHost, IDN.USE_STD3_ASCII_RULES).lowercase() }.getOrNull() ?: return ""
    if (asciiHost != rawHost || isIpv4LiteralUi(asciiHost)) return ""
    return asciiHost
}

private fun isIpv4LiteralUi(host: String): Boolean {
    val parts = host.split('.')
    if (parts.size != 4 || parts.any { it.isBlank() || it.any { ch -> ch !in '0'..'9' } }) return false
    return parts.all { part -> part.toIntOrNull()?.let { it in 0..255 } == true }
}

@Composable
private fun PreparedRequestSection(
    state: WalletUiState,
    onPrevRelayPage: () -> Unit,
    onNextRelayPage: () -> Unit,
    onScanResponse: () -> Unit,
    onPickResponseFromGallery: () -> Unit,
    onPickResponseFromVideo: () -> Unit,
    onClearPreparedRequest: () -> Unit,
) {
    val qrPagerLabel = when (state.preparedQrKind) {
        PreparedQrKind.PI_REQUEST -> "动态中转"
        PreparedQrKind.WEB3_CONNECT -> "动态连接码"
        PreparedQrKind.WEB3_SIGNATURE -> "动态签名码"
        PreparedQrKind.ELECTRUM_RESULT -> "Electrum 结果码"
    }
    Card(modifier = Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = Color.White)) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(state.requestTitle.ifBlank { "树莓派签名请求" }, fontWeight = FontWeight.Bold)
            if (state.requestSummary.isNotBlank()) {
                SelectionContainer {
                    Text(state.requestSummary, modifier = Modifier.fillMaxWidth(), fontSize = 12.sp)
                }
            }
            if (state.transferInfo.isNotBlank()) {
                SectionLabel("转账信息")
                SelectionContainer {
                    Text(state.transferInfo, modifier = Modifier.fillMaxWidth(), fontSize = 11.sp, color = Color(0xFF475467))
                }
            }
            if (state.dappInfo.isNotBlank()) {
                SectionLabel("DApp 信息")
                SelectionContainer {
                    Text(state.dappInfo, modifier = Modifier.fillMaxWidth(), fontSize = 11.sp, color = Color(0xFF475467))
                }
            }
            if (state.relayHint.isNotBlank()) {
                Text(state.relayHint, fontSize = 11.sp, color = Color(0xFF667085))
            }
            if (state.signQrPages.size > 1) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    TextButton(onClick = onPrevRelayPage) { Text("上一张") }
                    Text("${state.signQrPageIndex + 1}/${state.signQrPages.size}")
                    TextButton(onClick = onNextRelayPage) { Text("下一张") }
                }
            }
            RelayQrFrame(
                bitmap = state.signQrBitmap!!,
                pageIndex = state.signQrPageIndex,
                pageCount = state.signQrPages.size,
                qrSize = if (state.signQrPages.size > 1) 318.dp else 326.dp,
                labelPrefix = qrPagerLabel,
            )
            when (state.preparedQrKind) {
                PreparedQrKind.PI_REQUEST -> {
                    if (state.pendingResponseType == null) {
                        Text(
                            "树莓派签名后，请直接让原钱包扫描树莓派屏幕上的结果二维码，不需要再扫回手机。",
                            fontSize = 11.sp,
                            color = Color(0xFF667085),
                        )
                    } else {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                            OutlinedButton(onClick = onScanResponse, modifier = Modifier.weight(1f)) {
                                Text("扫码结果", fontSize = 12.sp)
                            }
                            OutlinedButton(onClick = onPickResponseFromGallery, modifier = Modifier.weight(1f)) {
                                Text("相册导入", fontSize = 12.sp)
                            }
                        }
                        OutlinedButton(onClick = onPickResponseFromVideo, modifier = Modifier.fillMaxWidth()) {
                            Text("导入视频", fontSize = 12.sp)
                        }
                    }
                }

                PreparedQrKind.WEB3_CONNECT -> {
                    Spacer(modifier = Modifier.height(0.dp))
                }

                PreparedQrKind.WEB3_SIGNATURE -> {
                    Spacer(modifier = Modifier.height(0.dp))
                }

                PreparedQrKind.ELECTRUM_RESULT -> {
                    Spacer(modifier = Modifier.height(0.dp))
                }
            }
            TextButton(onClick = onClearPreparedRequest) {
                Text(if (state.preparedQrKind == PreparedQrKind.PI_REQUEST) "取消" else "关闭")
            }
        }
    }
}

@Composable
private fun MessageCard(title: String, message: String, background: Color) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = background),
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, background.copy(alpha = 0.55f)),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text(title, fontWeight = FontWeight.Bold, color = Color(0xFF101828), fontSize = 13.sp)
            Text(message, fontSize = 11.sp, color = Color(0xFF344054))
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(text, modifier = Modifier.fillMaxWidth(), fontWeight = FontWeight.Medium)
}

private fun assetAmountLooksZero(value: String): Boolean {
    val normalized = value.trim().lowercase()
    if (normalized.isBlank()) return true
    if (normalized == "0") return true
    return normalized.toDoubleOrNull()?.let { kotlin.math.abs(it) < 0.0000000001 } ?: false
}

@Composable
private fun RelayQrFrame(
    bitmap: Bitmap,
    pageIndex: Int,
    pageCount: Int,
    qrSize: Dp,
    labelPrefix: String = "动态中转",
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = Color(0xFF0F172A)),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (pageCount > 1) {
                Surface(
                    color = Color(0xFF1D4ED8),
                    shape = RectangleShape,
                ) {
                    Text(
                        text = "$labelPrefix ${pageIndex + 1}/$pageCount · 1 秒轮播",
                        modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp),
                        color = Color.White,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Medium,
                        textAlign = TextAlign.Center,
                    )
                }
            }
            Box(
                modifier = Modifier
                    .background(Color.White)
                    .border(2.dp, Color(0xFFE5E7EB))
                    .padding(12.dp),
            ) {
                Image(
                    bitmap = bitmap.asImageBitmap(),
                    contentDescription = "relay qr",
                    modifier = Modifier.size(qrSize),
                )
            }
        }
    }
}
