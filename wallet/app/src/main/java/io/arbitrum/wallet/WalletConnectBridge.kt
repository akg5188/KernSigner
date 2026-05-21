package io.arbitrum.wallet

import android.app.Application
import android.os.SystemClock
import com.reown.android.Core
import com.reown.android.CoreClient
import com.reown.android.relay.ConnectionType
import com.reown.walletkit.client.Wallet
import com.reown.walletkit.client.WalletKit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

private const val WALLETCONNECT_PROJECT_ID = "e0a4a3aa69ef7aece654921b704debce"

data class WalletConnectProposalUi(
    val peerName: String,
    val peerUrl: String,
    val peerDescription: String,
    val requiredChains: List<String>,
    val requiredMethods: List<String>,
    val optionalChains: List<String>,
    val optionalMethods: List<String>,
    val isScam: Boolean = false,
    val isVerified: Boolean = false,
    val securityLabel: String = "",
)

data class WalletConnectPendingRequest(
    val topic: String,
    val requestId: Long,
    val method: String,
    val chainId: String?,
    val params: String,
    val peerName: String,
    val peerUrl: String,
    val isScam: Boolean = false,
    val isVerified: Boolean = false,
    val securityLabel: String = "",
)

object WalletConnectBridge : WalletKit.WalletDelegate, CoreClient.CoreDelegate {
    private var initializing = false
    private var ready = false
    private var appContext: Application? = null
    private var pendingPairUri: String? = null
    private var initStartedAtMs: Long = 0L
    private var currentProposal: Wallet.Model.SessionProposal? = null
    private var currentRequest: Wallet.Model.SessionRequest? = null

    private val _status = MutableStateFlow("WalletConnect 未连接")
    val status: StateFlow<String> = _status.asStateFlow()

    private val _proposal = MutableStateFlow<WalletConnectProposalUi?>(null)
    val proposal: StateFlow<WalletConnectProposalUi?> = _proposal.asStateFlow()

    private val _request = MutableStateFlow<WalletConnectPendingRequest?>(null)
    val request: StateFlow<WalletConnectPendingRequest?> = _request.asStateFlow()

    fun ensureInitialized(application: Application) {
        appContext = application
        val now = SystemClock.elapsedRealtime()
        if (ready) return
        if (initializing && now - initStartedAtMs < 15_000L) return
        if (initializing && now - initStartedAtMs >= 15_000L) {
            _status.value = "WalletConnect 初始化超时，正在重试"
            initializing = false
            ready = false
        }
        initializing = true
        initStartedAtMs = now

        val appMetaData = Core.Model.AppMetaData(
            name = "Satochip Wallet",
            description = "Multi-chain QR relay wallet",
            url = "https://github.com/akg5188/satochip-signer",
            icons = listOf("https://raw.githubusercontent.com/WalletConnect/walletconnect-assets/master/Icon/Gradient/Icon.png"),
            redirect = "satochipwallet://wc",
            appLink = null,
            linkMode = false,
        )

        runCatching {
            CoreClient.initialize(
                application = application,
                projectId = WALLETCONNECT_PROJECT_ID,
                metaData = appMetaData,
                connectionType = ConnectionType.AUTOMATIC,
                telemetryEnabled = false,
                onError = { error ->
                    _status.value = "WalletConnect 初始化失败: ${error.throwable.message ?: "未知错误"}"
                    initializing = false
                    ready = false
                }
            )

            CoreClient.setDelegate(this)
            WalletKit.initialize(
                Wallet.Params.Init(core = CoreClient),
                onSuccess = {
                    runCatching {
                        // Important: delegate must be set after WalletKit.initialize,
                        // otherwise SignClient may throw "needs to be initialized first".
                        WalletKit.setWalletDelegate(this)
                    }.onFailure { delegateError ->
                        _status.value = "WalletConnect 委托注册失败: ${delegateError.message ?: "未知错误"}"
                        initializing = false
                        ready = false
                        return@initialize
                    }
                    initializing = false
                    ready = true
                    _status.value = "WalletConnect 已就绪"
                    drainPendingPairIfAny()
                },
                onError = { error ->
                    _status.value = "WalletConnect 启动失败: ${error.throwable.message ?: "未知错误"}"
                    initializing = false
                    ready = false
                }
            )
        }.onFailure { error ->
            initializing = false
            ready = false
            _status.value = "WalletConnect 初始化异常: ${error.message ?: "未知错误"}"
            throw error
        }
    }

    fun pair(uri: String) {
        val normalized = WalletConnectUriParser.extract(uri)
        if (normalized.isNullOrBlank()) {
            _status.value = "WalletConnect 配对失败: 仅允许有效的 WalletConnect v2 配对链接"
            return
        }

        if (!ready) {
            val app = appContext
            if (app != null && !initializing) {
                runCatching { ensureInitialized(app) }
            }
            pendingPairUri = normalized
            _status.value = if (initializing) {
                "WalletConnect 初始化中，完成后将自动配对"
            } else {
                "WalletConnect 正在重试初始化，完成后将自动配对"
            }
            return
        }

        performPair(normalized)
    }

    private fun performPair(uri: String) {
        runCatching {
            WalletKit.pair(
                Wallet.Params.Pair(uri),
                onSuccess = {
                    _status.value = "WalletConnect 配对链接已接收，等待 DApp 发起会话提案"
                },
                onError = { error ->
                    _status.value = "WalletConnect 配对失败: ${error.throwable.message ?: "未知错误"}"
                }
            )
        }.onFailure { error ->
            _status.value = "WalletConnect 配对异常: ${error.message ?: "未知错误"}"
            if (error.message?.contains("coreClient", ignoreCase = true) == true) {
                ready = false
                initializing = false
                pendingPairUri = uri
                appContext?.let { runCatching { ensureInitialized(it) } }
            }
        }
    }

    private fun drainPendingPairIfAny() {
        val uri = pendingPairUri ?: return
        pendingPairUri = null
        performPair(uri)
    }

    fun approveCurrentProposal(address: String, chainId: Long, onResult: (Result<Unit>) -> Unit = {}) {
        val proposal = currentProposal
        if (proposal == null) {
            onResult(Result.failure(IllegalStateException("当前没有待处理的 WalletConnect 会话提案")))
            return
        }
        runCatching {
            val namespaces = WalletKit.generateApprovedNamespaces(
                sessionProposal = proposal,
                supportedNamespaces = supportedNamespaces(address, chainId)
            )
            WalletKit.approveSession(
                Wallet.Params.SessionApprove(
                    proposerPublicKey = proposal.proposerPublicKey,
                    namespaces = namespaces,
                ),
                onSuccess = {
                    clearProposal()
                    _status.value = "WalletConnect 会话已连接"
                    onResult(Result.success(Unit))
                },
                onError = { error ->
                    val throwable = error.throwable
                    _status.value = "WalletConnect 批准失败: ${throwable.message ?: "未知错误"}"
                    onResult(Result.failure(throwable))
                }
            )
        }.onFailure { error ->
            _status.value = "WalletConnect 批准失败: ${error.message ?: "未知错误"}"
            onResult(Result.failure(error))
        }
    }

    fun rejectCurrentProposal(reason: String = "User rejected connection", onResult: (Result<Unit>) -> Unit = {}) {
        val proposal = currentProposal
        if (proposal == null) {
            onResult(Result.failure(IllegalStateException("当前没有待处理的 WalletConnect 会话提案")))
            return
        }
        WalletKit.rejectSession(
            Wallet.Params.SessionReject(
                proposerPublicKey = proposal.proposerPublicKey,
                reason = reason,
            ),
            onSuccess = {
                clearProposal()
                _status.value = "WalletConnect 会话已拒绝"
                onResult(Result.success(Unit))
            },
            onError = { error ->
                val throwable = error.throwable
                _status.value = "WalletConnect 拒绝失败: ${throwable.message ?: "未知错误"}"
                onResult(Result.failure(throwable))
            }
        )
    }

    fun respondCurrentRequestResult(result: String?, onResult: (Result<Unit>) -> Unit = {}) {
        val request = currentRequest
        if (request == null) {
            onResult(Result.failure(IllegalStateException("当前没有待处理的 WalletConnect 请求")))
            return
        }
        WalletKit.respondSessionRequest(
            Wallet.Params.SessionRequestResponse(
                sessionTopic = request.topic,
                jsonRpcResponse = Wallet.Model.JsonRpcResponse.JsonRpcResult(
                    id = request.request.id,
                    // WalletConnect SDK now expects a non-null JSON value string.
                    result = result ?: "null",
                )
            ),
            onSuccess = {
                _status.value = "WalletConnect 请求已完成"
                clearRequest()
                onResult(Result.success(Unit))
            },
            onError = { error ->
                val throwable = error.throwable
                _status.value = "WalletConnect 返回结果失败: ${throwable.message ?: "未知错误"}"
                onResult(Result.failure(throwable))
            }
        )
    }

    fun respondCurrentRequestError(
        message: String,
        code: Int = 5000,
        onResult: (Result<Unit>) -> Unit = {}
    ) {
        val request = currentRequest
        if (request == null) {
            onResult(Result.failure(IllegalStateException("当前没有待处理的 WalletConnect 请求")))
            return
        }
        WalletKit.respondSessionRequest(
            Wallet.Params.SessionRequestResponse(
                sessionTopic = request.topic,
                jsonRpcResponse = Wallet.Model.JsonRpcResponse.JsonRpcError(
                    id = request.request.id,
                    code = code,
                    message = message,
                )
            ),
            onSuccess = {
                _status.value = "WalletConnect 请求已拒绝"
                clearRequest()
                onResult(Result.success(Unit))
            },
            onError = { error ->
                val throwable = error.throwable
                _status.value = "WalletConnect 返回错误失败: ${throwable.message ?: "未知错误"}"
                onResult(Result.failure(throwable))
            }
        )
    }

    override fun onSessionProposal(sessionProposal: Wallet.Model.SessionProposal, verifyContext: Wallet.Model.VerifyContext) {
        currentProposal = sessionProposal
        val isScam = verifyContext.isScam == true
        val isVerified = verifyContext.validation == Wallet.Model.Validation.VALID
        _proposal.value = WalletConnectProposalUi(
            peerName = sessionProposal.name,
            peerUrl = sessionProposal.url,
            peerDescription = sessionProposal.description,
            requiredChains = flattenChains(sessionProposal.requiredNamespaces),
            requiredMethods = flattenMethods(sessionProposal.requiredNamespaces),
            optionalChains = flattenOptionalChains(sessionProposal.optionalNamespaces),
            optionalMethods = flattenOptionalMethods(sessionProposal.optionalNamespaces),
            isScam = isScam,
            isVerified = isVerified,
            securityLabel = when {
                isScam -> "风险: 可疑 DApp"
                isVerified -> "域名已验证"
                else -> "域名未验证"
            },
        )
        _status.value = when {
            isScam -> "收到可疑 DApp 会话提案，请谨慎确认"
            isVerified -> "收到 WalletConnect 会话提案"
            else -> "收到 WalletConnect 会话提案（域名未验证）"
        }
    }

    override fun onSessionRequest(sessionRequest: Wallet.Model.SessionRequest, verifyContext: Wallet.Model.VerifyContext) {
        currentRequest = sessionRequest
        val isScam = verifyContext.isScam == true
        val isVerified = verifyContext.validation == Wallet.Model.Validation.VALID
        _request.value = WalletConnectPendingRequest(
            topic = sessionRequest.topic,
            requestId = sessionRequest.request.id,
            method = sessionRequest.request.method,
            chainId = sessionRequest.chainId,
            params = sessionRequest.request.params,
            peerName = sessionRequest.peerMetaData?.name.orEmpty(),
            peerUrl = sessionRequest.peerMetaData?.url.orEmpty(),
            isScam = isScam,
            isVerified = isVerified,
            securityLabel = when {
                isScam -> "风险: 可疑请求"
                isVerified -> "来源已验证"
                else -> "来源未验证"
            },
        )
        _status.value = when {
            isScam -> "收到可疑 WalletConnect 请求: ${sessionRequest.request.method}"
            isVerified -> "收到 WalletConnect 请求: ${sessionRequest.request.method}"
            else -> "收到未验证来源的 WalletConnect 请求: ${sessionRequest.request.method}"
        }
    }

    override val onSessionAuthenticate: (Wallet.Model.SessionAuthenticate, Wallet.Model.VerifyContext) -> Unit =
        { _, verifyContext ->
            _status.value = when {
                verifyContext.isScam == true -> "收到可疑 WalletConnect Auth 请求，已忽略"
                else -> "收到 WalletConnect Auth 请求（当前版本暂不处理）"
            }
        }

    override fun onSessionDelete(sessionDelete: Wallet.Model.SessionDelete) {
        clearRequest()
        clearProposal()
        _status.value = when (sessionDelete) {
            is Wallet.Model.SessionDelete.Success -> "WalletConnect 会话已断开"
            is Wallet.Model.SessionDelete.Error -> "WalletConnect 断开异常: ${sessionDelete.error.message ?: "未知错误"}"
        }
    }

    override fun onSessionExtend(session: Wallet.Model.Session) {
        _status.value = "WalletConnect 会话已续期"
    }

    override fun onSessionSettleResponse(settleSessionResponse: Wallet.Model.SettledSessionResponse) {
        _status.value = when (settleSessionResponse) {
            is Wallet.Model.SettledSessionResponse.Result -> "WalletConnect 会话已建立"
            is Wallet.Model.SettledSessionResponse.Error -> "WalletConnect 会话建立失败: ${settleSessionResponse.errorMessage}"
        }
    }

    override fun onSessionUpdateResponse(sessionUpdateResponse: Wallet.Model.SessionUpdateResponse) {
        _status.value = when (sessionUpdateResponse) {
            is Wallet.Model.SessionUpdateResponse.Result -> "WalletConnect 会话已更新"
            is Wallet.Model.SessionUpdateResponse.Error -> "WalletConnect 会话更新失败: ${sessionUpdateResponse.errorMessage}"
        }
    }

    override fun onProposalExpired(proposal: Wallet.Model.ExpiredProposal) {
        clearProposal()
        _status.value = "WalletConnect 会话提案已过期，请重新扫码"
    }

    override fun onRequestExpired(request: Wallet.Model.ExpiredRequest) {
        clearRequest()
        _status.value = "WalletConnect 请求已过期，请重新发起"
    }

    override fun onConnectionStateChange(state: Wallet.Model.ConnectionState) {
        _status.value = if (state.isAvailable) {
            if (_status.value.contains("已")) _status.value else "WalletConnect 已连接到 relay"
        } else {
            val reason = when (val current = state.reason) {
                is Wallet.Model.ConnectionState.Reason.ConnectionClosed -> current.message
                is Wallet.Model.ConnectionState.Reason.ConnectionFailed -> current.throwable.message
                null -> null
            }
            "WalletConnect relay 未连接${reason?.let { ": $it" } ?: ""}"
        }
    }

    override fun onError(error: Wallet.Model.Error) {
        _status.value = "WalletConnect 错误: ${error.throwable.message ?: "未知错误"}"
    }

    @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
    override fun onPairingDelete(deletedPairing: Core.Model.DeletedPairing) = Unit

    @Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
    override fun onPairingExpired(expiredPairing: Core.Model.ExpiredPairing) = Unit

    override fun onPairingState(pairingState: Core.Model.PairingState) {
        if (pairingState.isPairingState) {
            _status.value = "WalletConnect 正在建立配对"
        }
    }

    private fun clearProposal() {
        currentProposal = null
        _proposal.value = null
    }

    private fun clearRequest() {
        currentRequest = null
        _request.value = null
    }

    fun clearPendingInteractiveState() {
        clearProposal()
        clearRequest()
    }

    fun disconnectAllSessions(onResult: (Result<Int>) -> Unit = {}) {
        clearPendingInteractiveState()
        val sessions = runCatching { WalletKit.getListOfActiveSessions() }
            .onFailure { error ->
                _status.value = "WalletConnect 读取会话失败: ${error.message ?: "未知错误"}"
                onResult(Result.failure(error))
            }
            .getOrNull()
            ?.map { it.topic }
            ?.distinct()
            .orEmpty()

        if (sessions.isEmpty()) {
            _status.value = "WalletConnect 已按高安全策略清空会话"
            onResult(Result.success(0))
            return
        }

        var remaining = sessions.size
        var disconnected = 0
        val failures = mutableListOf<Throwable>()
        _status.value = "WalletConnect 正在断开 ${sessions.size} 个活跃 DApp 会话"

        fun completeIfDone() {
            if (remaining != 0) return
            if (failures.isEmpty()) {
                _status.value = "WalletConnect 已按高安全策略断开所有 DApp 会话"
                onResult(Result.success(disconnected))
                return
            }

            val message = buildString {
                append("WalletConnect 断开部分会话失败")
                failures.firstOrNull()?.message?.takeIf { it.isNotBlank() }?.let {
                    append(": ").append(it)
                }
            }
            val aggregate = IllegalStateException(message).apply {
                failures.forEach(::addSuppressed)
            }
            _status.value = message
            onResult(Result.failure(aggregate))
        }

        sessions.forEach { topic ->
            runCatching {
                WalletKit.disconnectSession(
                    Wallet.Params.SessionDisconnect(sessionTopic = topic),
                    onSuccess = {
                        disconnected += 1
                        remaining -= 1
                        completeIfDone()
                    },
                    onError = { error ->
                        failures += error.throwable
                        remaining -= 1
                        completeIfDone()
                    }
                )
            }.onFailure { error ->
                failures += error
                remaining -= 1
                completeIfDone()
            }
        }
    }

    private fun flattenChains(namespaces: Map<String, Wallet.Model.Namespace.Proposal>): List<String> {
        return namespaces.values.flatMap { it.chains.orEmpty() }.distinct()
    }

    private fun flattenMethods(namespaces: Map<String, Wallet.Model.Namespace.Proposal>): List<String> {
        return namespaces.values.flatMap { it.methods }.distinct()
    }

    private fun flattenOptionalChains(namespaces: Map<String, Wallet.Model.Namespace.Proposal>?): List<String> {
        return namespaces?.let(::flattenChains).orEmpty()
    }

    private fun flattenOptionalMethods(namespaces: Map<String, Wallet.Model.Namespace.Proposal>?): List<String> {
        return namespaces?.let(::flattenMethods).orEmpty()
    }

    private fun supportedNamespaces(address: String, chainId: Long): Map<String, Wallet.Model.Namespace.Session> {
        WalletChains.require(chainId)
        val chains = listOf("eip155:$chainId")
        val accounts = chains.map { "$it:$address" }
        return mapOf(
            "eip155" to Wallet.Model.Namespace.Session(
                chains = chains,
                accounts = accounts,
                methods = listOf(
                    "eth_sendTransaction",
                    "eth_signTransaction",
                    "personal_sign",
                    "eth_accounts",
                    "eth_requestAccounts",
                    "eth_chainId",
                    "eth_signTypedData",
                    "eth_signTypedData_v3",
                    "eth_signTypedData_v4",
                ),
                events = listOf("accountsChanged", "chainChanged")
            )
        )
    }
}
