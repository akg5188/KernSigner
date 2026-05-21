package io.arbitrum.wallet

import android.app.Application
import android.graphics.Bitmap
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import com.journeyapps.barcodescanner.BarcodeEncoder
import java.math.BigDecimal
import java.math.BigInteger
import java.math.RoundingMode
import java.net.IDN
import java.net.URI
import java.util.UUID
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

private val typedDataJson = Json { ignoreUnknownKeys = true; isLenient = true }

class MainViewModel(application: Application) : AndroidViewModel(application) {
    companion object {
        private const val AUTO_APPROVE_WALLETCONNECT = false
        private const val ADDRESS_DISCOVERY_MESSAGE_PREFIX = "tp-watch-address-discovery:"
        private const val REQUIRE_VERIFIED_WALLETCONNECT = true
        private const val TRUSTED_DAPP_ENTRY_TTL_MS = 24L * 60L * 60L * 1000L
        private const val MAX_TRUSTED_DAPP_ENTRIES = 64
        private const val BITCOIN_SNAPSHOT_CACHE_TTL_MS = 2L * 60L * 1000L
        private val STRICT_WALLETCONNECT_METHODS = setOf(
            "eth_sendtransaction",
            "eth_signtransaction",
            "personal_sign",
            "eth_accounts",
            "eth_requestaccounts",
            "eth_chainid",
            "eth_signtypeddata",
            "eth_signtypeddata_v3",
            "eth_signtypeddata_v4",
        )
    }

    private data class CachedBitcoinSnapshot(
        val snapshot: BitcoinAccountSnapshot,
        val syncedAt: Long,
    )

    private val prefs = WalletStorage.openSecurePreferences(application)
    private val _uiState = MutableStateFlow(WalletUiState())
    val uiState: StateFlow<WalletUiState> = _uiState.asStateFlow()

    private val localActivityItems = mutableListOf<WalletActivityItem>()
    private val syncedActivityByChain = linkedMapOf<Long, List<WalletActivityItem>>()
    private val bitcoinSnapshotCache = linkedMapOf<String, CachedBitcoinSnapshot>()
    private val sessionSecurityObserver = object : DefaultLifecycleObserver {
        override fun onStop(owner: LifecycleOwner) {
            clearSensitiveSessionState()
        }
    }

    init {
        ProcessLifecycleOwner.get().lifecycle.addObserver(sessionSecurityObserver)
        restorePersistedState()
        pruneExpiredTrustedDappEntries()
        runCatching {
            WalletConnectBridge.ensureInitialized(application)
        }.onFailure { error ->
            _uiState.update {
                it.copy(walletConnectStatus = "WalletConnect 初始化异常: ${error.message ?: "未知错误"}")
            }
        }
        bindWalletConnectState()
    }

    override fun onCleared() {
        ProcessLifecycleOwner.get().lifecycle.removeObserver(sessionSecurityObserver)
        super.onCleared()
    }

    fun setActiveTab(tab: WalletTab) = _uiState.update { it.copy(activeTab = tab) }
    fun setNewAddressInput(value: String) = _uiState.update { it.copy(newAddressInput = value) }
    fun setEvmDerivationPath(value: String) {
        _uiState.update { it.copy(evmDerivationPath = value) }
        WalletStorage.writeEvmDerivationPath(prefs, value)
    }
    fun setBitcoinImportInput(value: String) = _uiState.update { it.copy(bitcoinImportInput = value) }
    fun importBitcoinWatchAccountFromPayload(value: String) {
        _uiState.update { it.copy(bitcoinImportInput = value) }
        importBitcoinWatchAccountInternal(value)
    }
    fun setTransferTo(value: String) = _uiState.update { it.copy(transferTo = value) }
    fun setTransferAmount(value: String) = _uiState.update { it.copy(transferAmount = value) }
    fun setTransferToken(value: String) = _uiState.update { it.copy(transferToken = value) }

    fun transferAllTokens() {
        val state = _uiState.value
        val amount = state.chainPortfolios[state.selectedChainId]?.assets
            ?.firstOrNull { it.symbol.equals(state.transferToken, ignoreCase = true) }
            ?.amount ?: return
        _uiState.update { it.copy(transferAmount = amount) }
    }
    fun setRequestInput(value: String) = _uiState.update { it.copy(requestInput = value) }
    fun setContactNameInput(value: String) = _uiState.update { it.copy(contactNameInput = value) }
    fun setContactAddressInput(value: String) = _uiState.update { it.copy(contactAddressInput = value) }
    fun setContactNoteInput(value: String) = _uiState.update { it.copy(contactNoteInput = value) }
    fun clearError() = _uiState.update { it.copy(error = "") }
    fun clearInfo() = _uiState.update { it.copy(info = "") }
    fun clearTxHash() = _uiState.update { it.copy(txHash = "", txHashChainId = null, txHashExplorerUrl = "") }
    fun clearSignature() = _uiState.update { it.copy(lastSignature = "", lastSignatureAddress = "") }

    fun selectChain(chainId: Long) {
        val chain = WalletChains.byId(chainId) ?: return
        if (_uiState.value.selectedChainId != chain.chainId) {
            clearSensitiveSessionState()
        }
        selectChainInternal(chain, persist = true, triggerReload = true, message = "")
    }

    fun addAddressFromInput() {
        addAddress(_uiState.value.newAddressInput)
    }

    fun addAddress(addr: String) {
        val normalized = normalizeAddress(addr)
            ?: return setError("地址格式错误")
        if (_uiState.value.selectedAddress != normalized) {
            clearSensitiveSessionState()
        }

        _uiState.update {
            if (it.addresses.contains(normalized)) {
                it.copy(
                    selectedAddress = normalized,
                    newAddressInput = "",
                    error = "",
                    info = "已切换到该观察地址",
                )
            } else {
                it.copy(
                    addresses = it.addresses + normalized,
                    selectedAddress = normalized,
                    newAddressInput = "",
                    error = "",
                    info = "已添加观察地址",
                )
            }
        }
        persistAddresses()
        loadBalances()
    }

    fun saveAddressNote(address: String, note: String) {
        val normalized = normalizeAddress(address)
            ?: return setError("未找到观察地址")
        if (_uiState.value.addresses.none { it.equals(normalized, ignoreCase = true) }) {
            return setError("未找到观察地址")
        }
        val trimmed = note.trim()
        val key = canonicalAddressKey(normalized)
        _uiState.update { state ->
            val updatedNotes = state.addressNotes.toMutableMap().apply {
                if (trimmed.isBlank()) remove(key) else put(key, trimmed)
            }
            state.copy(
                addressNotes = updatedNotes,
                info = if (trimmed.isBlank()) "地址备注已清除" else "地址备注已保存",
                error = "",
            )
        }
        persistAddresses()
    }

    fun prepareDerivedAddressImport() {
        val state = _uiState.value
        val path = normalizeDerivationPath(state.evmDerivationPath)
            ?: return setError("派生路径格式错误，请使用 m/44'/60'/0'/0/0 这种格式")
        val chain = WalletChains.require(state.selectedChainId)
        val payload = TpRequestBuilder.buildPersonalSignRequest(
            address = null,
            message = "$ADDRESS_DISCOVERY_MESSAGE_PREFIX${System.currentTimeMillis()}",
            chain = chain,
            requestId = "discover-address-${System.currentTimeMillis()}",
            derivationPath = path,
        )
        prepareRelayRequest(
            payload = payload,
            explicitResponseType = PendingResponseType.IMPORT_WATCH_ADDRESS,
            explicitTitle = "${chain.shortName} 派生地址导入",
        )
        _uiState.update {
            it.copy(
                evmDerivationPath = path,
                info = "已生成派生地址导入二维码，请让树莓派扫描后再把结果扫回手机。",
                error = "",
            )
        }
        WalletStorage.writeEvmDerivationPath(prefs, path)
    }

    fun prepareWeb3AccountImport() {
        val state = _uiState.value
        val selectedAddress = normalizeAddress(state.selectedAddress)
            ?: return setError("请先在首页添加并选择一个 EVM 观察地址，再绑定 Web3 桥接")
        val path = normalizeDerivationPath(state.evmDerivationPath)
            ?: return setError("派生路径格式错误，请使用 m/44'/60'/0'/0/0 这种格式")
        val chain = WalletChains.require(state.selectedChainId)
        val payload = TpRequestBuilder.buildExportWeb3AccountRequest(
            addressPath = path,
            expectedAddress = selectedAddress,
            chain = chain,
            requestId = "web3-account-${System.currentTimeMillis()}",
        )
        viewModelScope.launch {
            runCatching {
                val bundle = RelayQrCodec.buildRelayPayloads(payload)
                showPreparedQrPages(
                    title = "绑定当前观察地址",
                    summary = buildString {
                        appendLine("观察地址: $selectedAddress")
                        appendLine("路径: $path")
                        appendLine("链: ${chain.displayName}")
                    }.trim(),
                    transferInfo = "",
                    dappInfo = "",
                    relayHint = if (bundle.payloads.size > 1) {
                        "已生成 ${bundle.payloads.size} 张二维码，将自动轮播给树莓派扫描。"
                    } else {
                        "已生成 1 张二维码，让树莓派直接扫描。"
                    },
                    pages = bundle.payloads,
                    preparedQrKind = PreparedQrKind.PI_REQUEST,
                    pendingResponseType = PendingResponseType.IMPORT_WEB3_ACCOUNT,
                    focusTab = WalletTab.DISCOVER,
                    infoMessage = "请让树莓派扫描当前二维码，并选择和首页观察地址对应的来源。",
                )
            }.onFailure { error ->
                setError("绑定 Web3 观察地址二维码生成失败: ${error.message}")
            }
        }
    }

    fun showSelectedWeb3ConnectQr() {
        val account = currentWeb3BridgeAccount()
            ?: return setError("当前首页观察地址还没有绑定 Web3 桥接，请先点「绑定当前观察地址」")
        viewModelScope.launch {
            runCatching {
                val pages = Web3UrCodec.buildConnectQrPages(account)
                showPreparedQrPages(
                    title = "连接硬件钱包",
                    summary = buildString {
                        appendLine("地址: ${account.address}")
                        appendLine("路径: ${account.addressPath}")
                        appendLine("来源: ${account.sourceLabel}")
                    }.trim(),
                    transferInfo = "",
                    dappInfo = "",
                    relayHint = if (pages.size > 1) {
                        "这是动态连接二维码，请让钱包持续扫描直到导入完成。"
                    } else {
                        "这是单张连接二维码，请直接让钱包扫描。"
                    },
                    pages = pages,
                    preparedQrKind = PreparedQrKind.WEB3_CONNECT,
                    pendingResponseType = null,
                    focusTab = WalletTab.DISCOVER,
                    infoMessage = "已生成连接二维码。",
                )
            }.onFailure { error ->
                setError("生成 Web3 连接二维码失败: ${error.message}")
            }
        }
    }

    fun selectAddress(address: String) {
        if (_uiState.value.selectedAddress != address) {
            clearSensitiveSessionState()
        }
        _uiState.update { it.copy(selectedAddress = address, error = "", info = "") }
        persistAddresses()
        loadBalances()
    }

    fun importBitcoinWatchAccount() = importBitcoinWatchAccountInternal(_uiState.value.bitcoinImportInput)

    private fun importBitcoinWatchAccountInternal(rawInput: String) {
        val parsed = parseBitcoinWatchAccountImport(rawInput)
            ?: return setError("请输入有效的 xpub / ypub / zpub / tpub / upub / vpub，或直接粘贴 offline-signer get-xpub 输出")

        val now = System.currentTimeMillis()
        var importedAccountId: String? = null
        _uiState.update { state ->
            val existing = state.bitcoinWatchAccounts.firstOrNull { it.xpub == parsed.xpub }
            if (existing != null) {
                state.copy(
                    bitcoinImportInput = "",
                    bitcoinPrototypeStatus = defaultBitcoinPrototypeStatus(state.bitcoinWatchAccounts.size),
                    info = "BTC 观察账户已存在：${existing.label}",
                    error = "",
                )
            } else {
                val account = enrichBitcoinWatchAccount(
                    BitcoinWatchAccount(
                        id = UUID.randomUUID().toString(),
                        label = parsed.defaultLabel,
                        note = "",
                        xpub = parsed.xpub,
                        prefix = parsed.prefix,
                        networkLabel = parsed.networkLabel,
                        scriptTypeLabel = parsed.scriptTypeLabel,
                        accountPathHint = parsed.accountPathHint,
                        importedAt = now,
                    )
                )
                if (account.derivationError.isNotBlank()) {
                    return@update state.copy(
                        error = account.derivationError,
                        info = "",
                    )
                }
                importedAccountId = account.id
                val accounts = listOf(account) + state.bitcoinWatchAccounts
                state.copy(
                    bitcoinImportInput = "",
                    bitcoinWatchAccounts = accounts,
                    bitcoinPrototypeStatus = defaultBitcoinPrototypeStatus(accounts.size),
                    info = "已导入 BTC 观察账户：${account.label}",
                    error = "",
                )
            }
        }
        persistBitcoinWatchAccounts()
        importedAccountId?.let(::syncBitcoinWatchAccount)
    }

    fun saveBitcoinWatchAccountNote(accountId: String, note: String) {
        val account = _uiState.value.bitcoinWatchAccounts.firstOrNull { it.id == accountId }
            ?: return setError("未找到 BTC 观察账户")
        val trimmed = note.trim()
        updateBitcoinWatchAccount(
            accountId = account.id,
            transform = { current -> current.copy(note = trimmed) },
        )
        _uiState.update {
            it.copy(
                info = if (trimmed.isBlank()) "BTC 钱包备注已清除" else "BTC 钱包备注已保存",
                error = "",
            )
        }
    }

    fun removeBitcoinWatchAccount(accountId: String) {
        bitcoinSnapshotCache.remove(accountId)
        _uiState.update { state ->
            val accounts = state.bitcoinWatchAccounts.filterNot { it.id == accountId }
            state.copy(
                bitcoinWatchAccounts = accounts,
                bitcoinPrototypeStatus = defaultBitcoinPrototypeStatus(accounts.size),
                info = "BTC 观察账户原型已删除",
                error = "",
            )
        }
        persistBitcoinWatchAccounts()
    }

    fun syncBitcoinWatchAccount(accountId: String) {
        val current = _uiState.value.bitcoinWatchAccounts.firstOrNull { it.id == accountId }
            ?: return setError("未找到 BTC 观察账户")
        _uiState.update { state ->
            state.copy(
                bitcoinWatchAccounts = state.bitcoinWatchAccounts.map { account ->
                    if (account.id == accountId) account.copy(syncing = true, lastSyncStatus = "正在同步链上状态...") else account
                },
                error = "",
            )
        }
        viewModelScope.launch {
            runCatching {
                BitcoinTransferService.syncAccount(
                    current,
                    includeActivity = true,
                    progress = { status ->
                        updateBitcoinWatchAccount(
                            accountId = accountId,
                            transform = { account ->
                                account.copy(syncing = true, lastSyncStatus = status)
                            },
                            persist = false,
                        )
                    },
                )
            }
                .onSuccess { snapshot ->
                    val syncedAt = System.currentTimeMillis()
                    cacheBitcoinSnapshot(accountId, snapshot, syncedAt)
                    updateBitcoinWatchAccount(
                        accountId = accountId,
                        transform = { account ->
                            account.copy(
                            balanceSats = snapshot.balanceSats,
                            priceUsd = snapshot.priceUsd ?: account.priceUsd,
                            utxoCount = snapshot.utxoCount,
                            nextReceiveAddress = snapshot.nextReceiveAddress,
                            nextChangeAddress = snapshot.nextChangeAddress,
                            lastReceiveUsedIndex = snapshot.lastReceiveUsedIndex,
                            lastChangeUsedIndex = snapshot.lastChangeUsedIndex,
                            receiveUsedIndices = snapshot.receiveUsedIndices,
                            changeUsedIndices = snapshot.changeUsedIndices,
                            lastSyncStatus = if (!snapshot.activityComplete && snapshot.ownedAddresses.isNotEmpty()) {
                                "${snapshot.status} 正在后台刷新最近交易..."
                            } else {
                                snapshot.status
                            },
                            lastSyncAt = syncedAt,
                            syncing = false,
                            recentActivity = when {
                                snapshot.activityComplete -> snapshot.recentActivity
                                snapshot.recentActivity.isNotEmpty() -> snapshot.recentActivity
                                else -> account.recentActivity
                            },
                        )
                        },
                    )
                    _uiState.update {
                        it.copy(
                            info = "BTC 账户已同步：${current.label}",
                            error = "",
                        )
                    }
                    if (!snapshot.activityComplete && snapshot.ownedAddresses.isNotEmpty()) {
                        launch {
                            runCatching {
                                BitcoinTransferService.fetchRecentActivity(
                                    prefix = current.prefix,
                                    ownedAddresses = snapshot.ownedAddresses.toSet(),
                                )
                            }.onSuccess { recentActivity ->
                                updateBitcoinWatchAccount(
                                    accountId = accountId,
                                    transform = { account ->
                                        account.copy(
                                            recentActivity = recentActivity,
                                            lastSyncStatus = if (recentActivity.isEmpty()) {
                                                "${snapshot.status} 最近交易已刷新。"
                                            } else {
                                                "${snapshot.status} 最近交易已刷新 ${recentActivity.size} 条。"
                                            },
                                        )
                                    },
                                )
                            }.onFailure {
                                updateBitcoinWatchAccount(
                                    accountId = accountId,
                                    transform = { account ->
                                        account.copy(
                                            lastSyncStatus = "${snapshot.status} 最近交易暂未刷新。",
                                        )
                                    },
                                )
                            }
                        }
                    }
                }
                .onFailure { error ->
                    updateBitcoinWatchAccount(
                        accountId = accountId,
                        transform = { account ->
                            account.copy(
                            syncing = false,
                            lastSyncStatus = "同步失败：${error.message ?: "未知错误"}",
                        )
                        },
                    )
                    setError("BTC 账户同步失败: ${error.message}")
                }
        }
    }

    fun prepareBitcoinTransfer(
        accountId: String,
        destinationAddress: String,
        amountText: String,
        feeRateText: String?,
    ) {
        val account = _uiState.value.bitcoinWatchAccounts.firstOrNull { it.id == accountId }
            ?: return setError("未找到 BTC 观察账户")
        _uiState.update {
            it.copy(
                preparingRequest = true,
                requestTitle = "",
                requestSummary = "",
                transferInfo = "",
                dappInfo = "",
                relayHint = "",
                preparedRequestChainId = null,
                preparedBitcoinAccountId = null,
                signQrPages = emptyList(),
                signQrPageIndex = 0,
                signQrBitmap = null,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingResponseType = null,
                pendingWeb3Request = null,
                pendingBroadcastRawTransaction = "",
                pendingBroadcastBitcoinTxHex = "",
                requestInput = "",
                lastSignature = "",
                lastSignatureAddress = "",
                error = "",
                info = "正在准备 BTC 转账请求...",
            )
        }
        viewModelScope.launch {
            val cachedSnapshot = currentBitcoinSnapshot(account)
            runCatching {
                BitcoinTransferService.prepareTransfer(
                    account = account,
                    destinationAddress = destinationAddress,
                    amountText = amountText,
                    feeRateText = feeRateText,
                    snapshot = cachedSnapshot,
                )
            }.onSuccess { prepared ->
                val syncedAt = System.currentTimeMillis()
                cacheBitcoinSnapshot(accountId, prepared.snapshot, syncedAt)
                val bundle = RelayQrCodec.buildRelayPayloads(prepared.requestPayload)
                val qr = generateQrBitmap(bundle.payloads.first())
                updateBitcoinWatchAccount(
                    accountId = accountId,
                    transform = { account ->
                        account.copy(
                        balanceSats = prepared.snapshot.balanceSats,
                        priceUsd = prepared.snapshot.priceUsd ?: account.priceUsd,
                        utxoCount = prepared.snapshot.utxoCount,
                        nextReceiveAddress = prepared.snapshot.nextReceiveAddress,
                        nextChangeAddress = prepared.snapshot.nextChangeAddress,
                        lastReceiveUsedIndex = prepared.snapshot.lastReceiveUsedIndex,
                        lastChangeUsedIndex = prepared.snapshot.lastChangeUsedIndex,
                        receiveUsedIndices = prepared.snapshot.receiveUsedIndices,
                        changeUsedIndices = prepared.snapshot.changeUsedIndices,
                        lastSyncStatus = prepared.snapshot.status,
                        lastSyncAt = syncedAt,
                        syncing = false,
                    )
                    },
                )
                _uiState.update {
                    it.copy(
                        preparingRequest = false,
                        requestTitle = "BTC 转账待树莓派签名",
                        requestSummary = buildString {
                            appendLine("账户: ${account.label}")
                            appendLine("收款地址: ${prepared.destinationAddress}")
                            appendLine("发送金额: ${formatBitcoinSats(prepared.amountSats)}")
                            appendLine("矿工费: ${formatBitcoinSats(prepared.feeSats)}")
                            appendLine("输入数: ${prepared.inputCount}")
                            if (prepared.changeSats > 0 && prepared.changeAddress != null) {
                                appendLine("找零: ${formatBitcoinSats(prepared.changeSats)}")
                                appendLine("找零地址: ${prepared.changeAddress}")
                            }
                        }.trim(),
                        transferInfo = buildString {
                            appendLine("BTC 账户: ${account.label}")
                            appendLine("to: ${prepared.destinationAddress}")
                            appendLine("amount: ${formatBitcoinSats(prepared.amountSats)}")
                            appendLine("fee: ${formatBitcoinSats(prepared.feeSats)}")
                            appendLine("inputs: ${prepared.inputCount}")
                        }.trim(),
                        dappInfo = "BTC 观察账户 -> 树莓派 PSBT 冷签 -> 手机广播",
                        relayHint = if (bundle.payloads.size > 1) {
                            "已生成 ${bundle.payloads.size} 张 BTC PSBT 二维码，将自动轮播给树莓派扫描。签名完成后，再把树莓派回显的 tx 二维码扫回手机广播。"
                        } else {
                            "已生成 1 张 BTC PSBT 二维码。让树莓派扫描签名后，再把它回显的 tx 二维码扫回手机广播。"
                        },
                        signQrPages = bundle.payloads,
                        signQrPageIndex = 0,
                        signQrBitmap = qr,
                        preparedQrKind = PreparedQrKind.PI_REQUEST,
                        pendingResponseType = PendingResponseType.BROADCAST_BTC_TX,
                        pendingWeb3Request = null,
                        preparedBitcoinAccountId = accountId,
                        preparedRequestChainId = null,
                        pendingBroadcastRawTransaction = "",
                        pendingBroadcastBitcoinTxHex = "",
                        requestInput = "",
                        txHash = "",
                        txHashChainId = null,
                        txHashExplorerUrl = "",
                        lastSignature = "",
                        lastSignatureAddress = "",
                        error = "",
                        info = "BTC 转账请求已准备好，请让树莓派扫描当前二维码。",
                        activeTab = WalletTab.HOME,
                    )
                }
            }.onFailure { error ->
                setError("BTC 转账准备失败: ${error.message}")
            }
        }
    }

    fun removeAddress(address: String) {
        val shouldClearSensitiveState = _uiState.value.selectedAddress.equals(address, ignoreCase = true)
        val normalized = normalizeAddress(address)
        _uiState.update { state ->
            val updated = state.addresses.filterNot { it.equals(address, ignoreCase = true) }
            val selected = when {
                updated.isEmpty() -> ""
                state.selectedAddress.equals(address, ignoreCase = true) -> updated.first()
                else -> state.selectedAddress
            }
            state.copy(
                addresses = updated,
                addressNotes = state.addressNotes.toMutableMap().apply {
                    normalized?.let { remove(canonicalAddressKey(it)) }
                },
                selectedAddress = selected,
                chainPortfolios = if (selected.isBlank()) emptyMap() else state.chainPortfolios,
                info = "已删除观察地址",
                error = "",
            )
        }
        persistAddresses()
        if (shouldClearSensitiveState) {
            clearSensitiveSessionState()
        }
        if (_uiState.value.selectedAddress.isNotBlank()) {
            loadBalances()
        }
    }

    fun loadBalances(
        chainId: Long = _uiState.value.selectedChainId,
        address: String = _uiState.value.selectedAddress,
        silent: Boolean = false,
    ) {
        if (address.isBlank()) return
        val chain = WalletChains.require(chainId)
        viewModelScope.launch {
            if (!silent) {
                _uiState.update { it.copy(loadingBalances = true, error = "") }
            }
            try {
                val balances = mutableListOf<AssetBalanceUi>()
                val nativeToken = chain.tokens.first()
                val nativeBalance = EvmRpc.getBalance(chain, address)
                val nativeAmount = formatUnits(nativeBalance, nativeToken.decimals)
                val nativePrice = EvmRpc.getUsdPrice(chain, null)
                balances += AssetBalanceUi(
                    symbol = nativeToken.symbol,
                    name = nativeToken.name,
                    amount = nativeAmount,
                    isNative = true,
                    priceUsd = nativePrice,
                    usdAmount = calculateUsd(nativeAmount, nativePrice),
                )
                chain.tokens.filter { it.address != null }.forEach { token ->
                    val amount = EvmRpc.getTokenBalance(chain, token.address!!, address)
                    val formatted = formatUnits(amount, token.decimals)
                    val price = EvmRpc.getUsdPrice(chain, token.address)
                balances += AssetBalanceUi(
                    symbol = token.symbol,
                    name = token.name,
                    amount = formatted,
                    contractAddress = token.address,
                    priceUsd = price,
                    usdAmount = calculateUsd(formatted, price),
                )
            }
                val finalBalances = enrichMissingPrices(balances)
                _uiState.update { state ->
                    state.copy(
                        loadingBalances = false,
                        chainPortfolios = state.chainPortfolios + (
                            chain.chainId to ChainPortfolioUi(
                                chainId = chain.chainId,
                                assets = finalBalances,
                                lastUpdatedAt = System.currentTimeMillis(),
                                status = "已同步 ${chain.shortName}",
                            )
                        ),
                        error = "",
                    )
                }
                syncRecentActivity(chain, address)
            } catch (e: Exception) {
                _uiState.update {
                    it.copy(
                        loadingBalances = false,
                        error = "加载 ${chain.shortName} 资产失败: ${e.message}",
                    )
                }
            }
        }
    }

    fun refreshSelectedActivity() {
        val state = _uiState.value
        if (state.selectedAddress.isBlank()) return
        loadBalances(state.selectedChainId, state.selectedAddress)
    }

    fun prepareTransfer() {
        prepareTransferRequest(
            toInput = _uiState.value.transferTo,
            amountInput = _uiState.value.transferAmount,
            tokenSymbol = _uiState.value.transferToken,
        )
    }

    fun prepareTransferRequest(
        toInput: String,
        amountInput: String,
        tokenSymbol: String,
    ) {
        val state = _uiState.value
        val chain = WalletChains.require(state.selectedChainId)
        val from = state.selectedAddress
        val to = normalizeAddress(toInput)
        val amount = amountInput.trim()
        val token = chain.tokens.firstOrNull { it.symbol.equals(tokenSymbol, ignoreCase = true) }
            ?: return setError("当前链不支持 $tokenSymbol")

        if (from.isBlank()) return setError("请先添加观察地址")
        if (to == null) return setError("接收地址格式错误")
        if (amount.isBlank()) return setError("请输入数量")

        _uiState.update {
            it.copy(
                transferTo = toInput,
                transferAmount = amountInput,
                transferToken = token.symbol,
            )
        }

        viewModelScope.launch {
            try {
                val request = if (token.address == null) {
                    buildNativeTransferRequest(chain, from, to, amount, state.evmDerivationPath)
                } else {
                    buildTokenTransferRequest(chain, from, to, amount, token, state.evmDerivationPath)
                }
                prepareRelayRequest(
                    payload = request,
                    explicitResponseType = PendingResponseType.BROADCAST_TX,
                    explicitTitle = "${chain.shortName} ${token.symbol} 转账",
                )
                _uiState.update {
                    it.copy(
                        info = "${chain.shortName} ${token.symbol} 转账请求已生成",
                        error = "",
                    )
                }
            } catch (e: Exception) {
                setError("构建交易失败: ${e.message}")
            }
        }
    }

    fun prepareTransferEth() {
        val chain = WalletChains.require(_uiState.value.selectedChainId)
        _uiState.update { it.copy(transferToken = chain.nativeSymbol) }
        prepareTransfer()
    }

    fun prepareTransferToken() {
        prepareTransfer()
    }

    fun addContact() {
        val state = _uiState.value
        val name = state.contactNameInput.trim()
        val address = normalizeAddress(state.contactAddressInput)
            ?: return setError("联系人地址格式错误")
        if (name.isBlank()) return setError("请输入联系人名称")

        val existing = state.contacts.firstOrNull { it.address.equals(address, ignoreCase = true) }
        if (existing != null) {
            _uiState.update {
                it.copy(
                    transferTo = existing.address,
                    contactNameInput = "",
                    contactAddressInput = "",
                    contactNoteInput = "",
                    info = "联系人已存在，已填入转账地址",
                    error = "",
                )
            }
            return
        }

        val contact = TransferContact(
            id = UUID.randomUUID().toString(),
            name = name,
            address = address,
            note = state.contactNoteInput.trim(),
            chainId = state.selectedChainId,
        )
        _uiState.update {
            it.copy(
                contacts = listOf(contact) + it.contacts,
                transferTo = address,
                contactNameInput = "",
                contactAddressInput = "",
                contactNoteInput = "",
                info = "联系人已保存",
                error = "",
            )
        }
        persistContacts()
    }

    fun useContact(contactId: String) {
        val contact = _uiState.value.contacts.firstOrNull { it.id == contactId } ?: return
        _uiState.update { it.copy(transferTo = contact.address, error = "", info = "已填入联系人地址") }
        contact.chainId?.let { chainId ->
            WalletChains.byId(chainId)?.let { selectChainInternal(it, persist = true, triggerReload = true, message = "") }
        }
    }

    fun removeContact(contactId: String) {
        _uiState.update {
            it.copy(
                contacts = it.contacts.filterNot { contact -> contact.id == contactId },
                info = "联系人已删除",
                error = "",
            )
        }
        persistContacts()
    }

    fun approveWalletConnectProposal() {
        val proposal = _uiState.value.walletConnectProposal
            ?: return setError("当前没有待处理的 WalletConnect 会话提案")
        if (!isTrustedWalletConnectProposal(proposal)) {
            val trustedHost = trustedWalletConnectHost(proposal.peerUrl)
            val proposalPolicyError = walletConnectProposalPolicyError(proposal, _uiState.value.selectedChainId)
            val reason = when {
                proposal.isScam -> "已拦截可疑 DApp，会话不可批准"
                trustedHost.isBlank() -> "高安全模式仅允许连接 HTTPS 且主机名合法的 DApp"
                REQUIRE_VERIFIED_WALLETCONNECT && !proposal.isVerified -> "高安全模式仅允许连接域名已验证的 DApp"
                proposalPolicyError != null -> proposalPolicyError
                else -> "高安全模式仅允许连接域名已验证的 DApp"
            }
            setError(reason)
            return
        }
        val address = _uiState.value.selectedAddress
        if (address.isBlank()) return setError("请先选择观察地址，再批准 WalletConnect 会话")
        val chainId = _uiState.value.selectedChainId
        val trustedHost = trustedWalletConnectHost(proposal.peerUrl)
        WalletConnectBridge.approveCurrentProposal(address, chainId) { result ->
            result.onSuccess {
                val now = System.currentTimeMillis()
                val wasAlreadyTrusted = currentTrustedDappEntry(trustedHost) != null
                _uiState.update { state ->
                    val activeEntries = pruneTrustedDappEntries(state.trustedDappEntries, now)
                    val trustedEntry = TrustedDappEntry(
                        host = trustedHost,
                        chainId = chainId,
                        address = address,
                        trustedAt = now,
                    )
                    state.copy(
                        trustedDappEntries = (listOf(trustedEntry) + activeEntries.filterNot {
                            it.host == trustedHost && it.chainId == chainId && it.address.equals(address, ignoreCase = true)
                        }).take(MAX_TRUSTED_DAPP_ENTRIES),
                        info = if (wasAlreadyTrusted) {
                            "WalletConnect 会话已批准：$trustedHost（当前地址/链可信范围已续期 24 小时）"
                        } else {
                            "WalletConnect 会话已批准，并已将 $trustedHost 绑定到当前地址/链的可信范围（24 小时有效）"
                        },
                        error = "",
                        activeTab = WalletTab.DISCOVER,
                    )
                }
                persistTrustedDappEntries()
                recordLocalActivity(
                    WalletActivityItem(
                        id = "dapp-proposal-${System.currentTimeMillis()}",
                        chainId = chainId,
                        kind = WalletActivityKind.DAPP,
                        title = "WalletConnect 已连接",
                        subtitle = proposal.peerName.ifBlank { trustedHost },
                        detail = proposal.requiredChains.joinToString().ifBlank { WalletChains.require(chainId).shortName },
                        statusLabel = if (wasAlreadyTrusted) "已批准并续期 24 小时" else "已批准并绑定当前地址/链（24 小时）",
                        timestamp = System.currentTimeMillis(),
                    )
                )
            }
            result.onFailure { setError("WalletConnect 批准失败: ${it.message}") }
        }
    }

    fun rejectWalletConnectProposal() {
        WalletConnectBridge.rejectCurrentProposal { result ->
            result.onFailure { setError("WalletConnect 拒绝失败: ${it.message}") }
        }
    }

    fun approveCurrentWalletConnectRequest() {
        val request = _uiState.value.walletConnectPendingRequest
            ?: return setError("当前没有待处理的 WalletConnect 请求")
        if (!isTrustedWalletConnectRequest(request)) {
            val trustedHost = trustedWalletConnectHost(request.peerUrl)
            val reason = when {
                request.isScam -> "已拦截可疑 DApp 请求"
                trustedHost.isBlank() -> "高安全模式仅允许处理 HTTPS 且主机名合法的 DApp 请求"
                REQUIRE_VERIFIED_WALLETCONNECT && !request.isVerified -> "高安全模式仅允许处理来源已验证的 DApp 请求"
                else -> "该 DApp 在当前地址/链下不在可信范围，已阻止当前请求"
            }
            setError(reason)
            return
        }
        viewModelScope.launch {
            handleWalletConnectRequest(request)
        }
    }

    fun removeTrustedDappEntry(entry: TrustedDappEntry) {
        val normalizedHost = entry.host.trim().lowercase()
        val normalizedAddress = normalizeAddress(entry.address) ?: entry.address.trim()
        if (normalizedHost.isBlank() || normalizedAddress.isBlank()) return
        if (_uiState.value.trustedDappEntries.none {
                it.host == normalizedHost &&
                    it.chainId == entry.chainId &&
                    it.address.equals(normalizedAddress, ignoreCase = true)
            }
        ) {
            return setError("未找到该可信 DApp 范围")
        }
        clearSensitiveSessionState()
        _uiState.update { state ->
            state.copy(
                trustedDappEntries = state.trustedDappEntries.filterNot {
                    it.host == normalizedHost &&
                        it.chainId == entry.chainId &&
                        it.address.equals(normalizedAddress, ignoreCase = true)
                },
                info = "已移除可信 DApp 范围：$normalizedHost · ${WalletChains.byId(entry.chainId)?.shortName ?: entry.chainId} · ${normalizedAddress.take(8)}...",
                error = "",
                activeTab = WalletTab.DISCOVER,
            )
        }
        persistTrustedDappEntries()
        recordLocalActivity(
            WalletActivityItem(
                id = "dapp-scope-remove-$normalizedHost-${entry.chainId}-${System.currentTimeMillis()}",
                chainId = entry.chainId,
                kind = WalletActivityKind.SYSTEM,
                title = "可信 DApp 范围已移除",
                subtitle = normalizedHost,
                detail = normalizedAddress,
                statusLabel = "当前地址/链绑定已删除",
                timestamp = System.currentTimeMillis(),
            )
        )
    }

    fun rejectCurrentWalletConnectRequest() {
        val request = _uiState.value.walletConnectPendingRequest
            ?: return setError("当前没有待处理的 WalletConnect 请求")
        WalletConnectBridge.respondCurrentRequestError("用户拒绝了 DApp 请求", 4001) { result ->
            result.onFailure { setError("WalletConnect 拒绝请求失败: ${it.message}") }
        }
        recordLocalActivity(
            WalletActivityItem(
                id = "wc-reject-${request.requestId}",
                chainId = parseWalletConnectChainId(request.chainId) ?: _uiState.value.selectedChainId,
                kind = WalletActivityKind.DAPP,
                title = "DApp 请求已拒绝",
                subtitle = request.peerName.ifBlank { request.peerUrl.ifBlank { "未知 DApp" } },
                detail = request.method,
                statusLabel = "已拒绝",
                timestamp = System.currentTimeMillis(),
            )
        )
        _uiState.update {
            it.copy(
                walletConnectPendingRequest = null,
                info = "已拒绝 DApp 请求: ${request.method}",
                error = "",
                activeTab = WalletTab.DISCOVER,
            )
        }
    }

    fun importRawRequest() {
        val payload = _uiState.value.requestInput.trim()
        if (payload.isBlank()) return setError("请输入或扫码 DApp 请求")
        handleIncomingPayload(payload, WalletTab.DISCOVER)
    }

    fun onRequestScanResult(payload: String) {
        _uiState.update { it.copy(requestInput = payload, activeTab = WalletTab.DISCOVER, error = "") }
        handleIncomingPayload(payload, WalletTab.DISCOVER)
    }

    fun onResponseScanResult(payload: String) {
        val parsed = TpResponseParser.parse(payload)
        if (
            parsed.isError ||
            (
                parsed.rawTransaction == null &&
                    parsed.signature == null &&
                    parsed.bitcoinTxHex == null &&
                    parsed.web3Account == null
                )
        ) {
            return setError("无法解析树莓派结果")
        }

        viewModelScope.launch {
            val currentState = _uiState.value
            val currentResponseType = currentState.pendingResponseType
            val pendingWeb3Request = currentState.pendingWeb3Request
            when {
                parsed.web3Account != null -> {
                    val importedAccount = parsed.web3Account.copy(
                        importedAt = parsed.web3Account.importedAt.takeIf { it > 0L } ?: System.currentTimeMillis(),
                    )
                    val selectedAddress = normalizeAddress(currentState.selectedAddress)
                    val importedAddress = normalizeAddress(importedAccount.address)
                    if (
                        currentResponseType == PendingResponseType.IMPORT_WEB3_ACCOUNT &&
                        selectedAddress != null &&
                        importedAddress != null &&
                        !selectedAddress.equals(importedAddress, ignoreCase = true)
                    ) {
                        return@launch setError(
                            "树莓派返回地址 ${shortAddress(importedAddress)} 与首页当前观察地址 ${shortAddress(selectedAddress)} 不一致，请检查派生路径或账户来源"
                        )
                    }
                    upsertWeb3BridgeAccount(importedAccount)
                    val normalizedAddress = importedAddress
                    if (normalizedAddress != null) {
                        _uiState.update { state ->
                            val updatedAddresses = if (state.addresses.any { it.equals(normalizedAddress, ignoreCase = true) }) {
                                state.addresses
                            } else {
                                state.addresses + normalizedAddress
                            }
                            state.copy(
                                addresses = updatedAddresses,
                                selectedAddress = if (state.selectedAddress.isBlank()) normalizedAddress else state.selectedAddress,
                            )
                        }
                        persistAddresses()
                    }
                    recordLocalActivity(
                        WalletActivityItem(
                            id = "web3-account-${importedAccount.address.lowercase()}-${System.currentTimeMillis()}",
                            chainId = WalletChains.byId(1L)?.chainId ?: _uiState.value.selectedChainId,
                            kind = WalletActivityKind.SYSTEM,
                            title = "已保存 Web3 兼容账户",
                            subtitle = shortAddress(importedAccount.address),
                            detail = "${importedAccount.sourceLabel} · ${importedAccount.addressPath}",
                            statusLabel = "兼容连接可用",
                            timestamp = System.currentTimeMillis(),
                        )
                    )
                    _uiState.update {
                        it.copy(
                            signQrBitmap = null,
                            signQrPages = emptyList(),
                            signQrPageIndex = 0,
                            preparedQrKind = PreparedQrKind.PI_REQUEST,
                            pendingResponseType = null,
                            pendingWeb3Request = null,
                            preparedBitcoinAccountId = null,
                            preparedRequestChainId = null,
                            requestTitle = "",
                            requestSummary = "",
                            transferInfo = "",
                            dappInfo = "",
                            relayHint = "",
                            pendingBroadcastRawTransaction = "",
                            pendingBroadcastBitcoinTxHex = "",
                            error = "",
                            info = "已保存兼容桥接账户：${shortAddress(importedAccount.address)}。优先请用树莓派首页“连接钱包 -> Web3钱包”直连 OKX / Bitget。",
                            requestInput = "",
                        )
                    }
                }

                currentResponseType == PendingResponseType.RETURN_WEB3_SIGNATURE && pendingWeb3Request != null -> {
                    runCatching {
                        val signatureBytes = Web3UrCodec.extractSignatureBytes(pendingWeb3Request, parsed)
                        val pages = Web3UrCodec.buildEthSignatureQrPages(
                            requestId = pendingWeb3Request.requestId,
                            requestIdDataItem = pendingWeb3Request.requestIdDataItem,
                            signatureBytes = signatureBytes,
                            origin = pendingWeb3Request.origin,
                        )
                        showPreparedQrPages(
                            title = "让 OKX / Bitget 扫描签名结果",
                            summary = Web3UrCodec.buildRequestSummary(pendingWeb3Request),
                            transferInfo = "",
                            dappInfo = "树莓派签名结果已经转成 Keystone 兼容二维码，请回到钱包继续扫描。",
                            relayHint = if (pages.size > 1) {
                                "这是动态签名结果二维码，请让钱包持续扫描直到回传完成。"
                            } else {
                                "这是单张签名结果二维码，请直接让钱包扫描。"
                            },
                            pages = pages,
                            preparedQrKind = PreparedQrKind.WEB3_SIGNATURE,
                            pendingResponseType = null,
                            pendingWeb3Request = null,
                            focusTab = WalletTab.DISCOVER,
                            preparedRequestChainId = WalletChains.byId(pendingWeb3Request.chainId)?.chainId,
                            infoMessage = "已生成回传给钱包的签名二维码。",
                        )
                    }.onFailure { error ->
                        setError("生成 Web3 签名回传二维码失败: ${error.message}")
                        return@launch
                    }
                    recordLocalActivity(
                        WalletActivityItem(
                            id = "web3-signature-${pendingWeb3Request.requestId ?: System.currentTimeMillis()}",
                            chainId = WalletChains.byId(pendingWeb3Request.chainId)?.chainId ?: _uiState.value.selectedChainId,
                            kind = WalletActivityKind.SIGNATURE,
                            title = "Web3 签名结果已生成",
                            subtitle = pendingWeb3Request.origin ?: "OKX / Bitget / Keystone",
                            detail = "${pendingWeb3Request.dataType.name} · ${pendingWeb3Request.derivationPath}",
                            statusLabel = "待钱包扫码",
                            timestamp = System.currentTimeMillis(),
                        )
                    )
                }

                parsed.bitcoinTxHex != null -> {
                    if (_uiState.value.preparedBitcoinAccountId == null) {
                        return@launch setError("当前没有待广播的 BTC 请求")
                    }
                    _uiState.update {
                        it.copy(
                            signQrBitmap = null,
                            signQrPages = emptyList(),
                            signQrPageIndex = 0,
                            preparedQrKind = PreparedQrKind.PI_REQUEST,
                            pendingResponseType = null,
                            pendingWeb3Request = null,
                            pendingBroadcastBitcoinTxHex = parsed.bitcoinTxHex,
                            error = "",
                            info = "已收到 BTC 签名交易，请人工核对后再广播。",
                            requestInput = "",
                        )
                    }
                }

                parsed.rawTransaction != null -> {
                    _uiState.update {
                        it.copy(
                            signQrBitmap = null,
                            signQrPages = emptyList(),
                            signQrPageIndex = 0,
                            preparedQrKind = PreparedQrKind.PI_REQUEST,
                            pendingResponseType = null,
                            pendingWeb3Request = null,
                            pendingBroadcastRawTransaction = parsed.rawTransaction,
                            preparedBitcoinAccountId = null,
                            error = "",
                            info = "已收到签名交易，请人工核对后再广播。",
                            requestInput = "",
                        )
                    }
                }

                parsed.signature != null -> {
                    val chainId = _uiState.value.preparedRequestChainId ?: _uiState.value.selectedChainId
                    val pendingRequest = _uiState.value.walletConnectPendingRequest
                    if (pendingRequest != null) {
                        WalletConnectBridge.respondCurrentRequestResult(parsed.signature) { result ->
                            result.onFailure { setError("WalletConnect 返回签名失败: ${it.message}") }
                        }
                    }
                    recordLocalActivity(
                        WalletActivityItem(
                            id = "sig-${System.currentTimeMillis()}",
                            chainId = chainId,
                            kind = WalletActivityKind.SIGNATURE,
                            title = "签名结果已返回",
                            subtitle = parsed.address.orEmpty().ifBlank { _uiState.value.selectedAddress },
                            detail = _uiState.value.requestSummary,
                            statusLabel = if (pendingRequest != null) "已回传 DApp" else "待复制",
                            timestamp = System.currentTimeMillis(),
                        )
                    )
                    _uiState.update {
                        it.copy(
                            lastSignature = if (currentResponseType == PendingResponseType.IMPORT_WATCH_ADDRESS) "" else parsed.signature,
                            lastSignatureAddress = if (currentResponseType == PendingResponseType.IMPORT_WATCH_ADDRESS) "" else parsed.address.orEmpty(),
                            signQrBitmap = null,
                            signQrPages = emptyList(),
                            signQrPageIndex = 0,
                            preparedQrKind = PreparedQrKind.PI_REQUEST,
                            pendingResponseType = null,
                            pendingWeb3Request = null,
                            preparedBitcoinAccountId = null,
                            preparedRequestChainId = null,
                            error = "",
                            info = if (currentResponseType == PendingResponseType.IMPORT_WATCH_ADDRESS) {
                                "树莓派地址已返回"
                            } else if (pendingRequest != null) {
                                "签名结果已返回给 WalletConnect"
                            } else {
                                "签名结果已返回"
                            },
                        )
                    }
                    if (currentResponseType == PendingResponseType.IMPORT_WATCH_ADDRESS) {
                        val imported = normalizeAddress(parsed.address)
                            ?: return@launch setError("树莓派未返回有效地址")
                        addAddress(imported)
                        _uiState.update {
                            it.copy(
                                info = "已从树莓派导入观察地址: ${shortAddress(imported)}",
                                error = "",
                            )
                        }
                    }
                }
            }
        }
    }

    fun nextSignQrPage() {
        val pages = _uiState.value.signQrPages
        if (pages.size <= 1) return
        updateQrPage((_uiState.value.signQrPageIndex + 1) % pages.size)
    }

    fun prevSignQrPage() {
        val pages = _uiState.value.signQrPages
        if (pages.size <= 1) return
        updateQrPage(if (_uiState.value.signQrPageIndex <= 0) pages.lastIndex else _uiState.value.signQrPageIndex - 1)
    }

    fun clearPreparedRequest() {
        _uiState.update {
            it.copy(
                preparingRequest = false,
                requestTitle = "",
                requestSummary = "",
                transferInfo = "",
                dappInfo = "",
                relayHint = "",
                preparedRequestChainId = null,
                preparedBitcoinAccountId = null,
                signQrPages = emptyList(),
                signQrPageIndex = 0,
                signQrBitmap = null,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingResponseType = null,
                pendingWeb3Request = null,
                pendingBroadcastRawTransaction = "",
                pendingBroadcastBitcoinTxHex = "",
                requestInput = "",
                lastSignature = "",
                lastSignatureAddress = "",
                error = "",
                info = "",
            )
        }
    }

    fun confirmPendingBroadcast() {
        val state = _uiState.value
        when {
            state.pendingBroadcastBitcoinTxHex.isNotBlank() -> confirmPendingBitcoinBroadcast(state)
            state.pendingBroadcastRawTransaction.isNotBlank() -> confirmPendingEvmBroadcast(state)
        }
    }

    fun cancelPendingBroadcast() {
        if (_uiState.value.walletConnectPendingRequest != null) {
            WalletConnectBridge.respondCurrentRequestError("用户取消了广播", 4001) { result ->
                result.onFailure { setError("WalletConnect 返回取消结果失败: ${it.message}") }
            }
        }
        _uiState.update {
            it.copy(
                preparingRequest = false,
                signQrPages = emptyList(),
                signQrPageIndex = 0,
                signQrBitmap = null,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingBroadcastRawTransaction = "",
                pendingBroadcastBitcoinTxHex = "",
                preparedRequestChainId = null,
                preparedBitcoinAccountId = null,
                requestTitle = "",
                requestSummary = "",
                transferInfo = "",
                dappInfo = "",
                relayHint = "",
                pendingResponseType = null,
                pendingWeb3Request = null,
                lastSignature = "",
                lastSignatureAddress = "",
                info = "已取消广播",
                error = "",
                requestInput = "",
            )
        }
    }

    private fun bindWalletConnectState() {
        viewModelScope.launch {
            WalletConnectBridge.status.collectLatest { status ->
                _uiState.update { it.copy(walletConnectStatus = status) }
            }
        }
        viewModelScope.launch {
            WalletConnectBridge.proposal.collectLatest { proposal ->
                _uiState.update { it.copy(walletConnectProposal = proposal) }
                if (proposal == null) return@collectLatest
                val trustedHost = trustedWalletConnectHost(proposal.peerUrl)
                when {
                    proposal.isScam -> {
                        WalletConnectBridge.rejectCurrentProposal("Rejected suspicious DApp proposal") { result ->
                            result.onFailure { setError("自动拒绝可疑 WalletConnect 会话失败: ${it.message}") }
                        }
                        recordLocalActivity(
                            WalletActivityItem(
                                id = "wc-scam-proposal-${System.currentTimeMillis()}",
                                chainId = _uiState.value.selectedChainId,
                                kind = WalletActivityKind.DAPP,
                                title = "已拒绝可疑 DApp 会话",
                                subtitle = proposal.peerName.ifBlank { proposal.peerUrl.ifBlank { "未知 DApp" } },
                                detail = proposal.requiredMethods.joinToString(),
                                statusLabel = "自动拒绝",
                                timestamp = System.currentTimeMillis(),
                            )
                        )
                        _uiState.update {
                            it.copy(
                                walletConnectProposal = null,
                                info = "已自动拒绝可疑 WalletConnect 会话提案。",
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }

                    REQUIRE_VERIFIED_WALLETCONNECT && !proposal.isVerified -> {
                        WalletConnectBridge.rejectCurrentProposal("Rejected unverified DApp proposal in high-security mode") { result ->
                            result.onFailure { setError("自动拒绝未验证 WalletConnect 会话失败: ${it.message}") }
                        }
                        recordLocalActivity(
                            WalletActivityItem(
                                id = "wc-unverified-proposal-${System.currentTimeMillis()}",
                                chainId = _uiState.value.selectedChainId,
                                kind = WalletActivityKind.DAPP,
                                title = "已拒绝未验证 DApp 会话",
                                subtitle = proposal.peerName.ifBlank { proposal.peerUrl.ifBlank { "未知 DApp" } },
                                detail = proposal.requiredMethods.joinToString(),
                                statusLabel = "高安全模式拦截",
                                timestamp = System.currentTimeMillis(),
                            )
                        )
                        _uiState.update {
                            it.copy(
                                walletConnectProposal = null,
                                info = "高安全模式仅允许连接域名已验证的 DApp，已自动拒绝当前会话提案。",
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }

                    trustedHost.isBlank() -> {
                        WalletConnectBridge.rejectCurrentProposal("Rejected WalletConnect proposal with non-HTTPS or invalid host") { result ->
                            result.onFailure { setError("自动拒绝非法 WalletConnect 会话失败: ${it.message}") }
                        }
                        recordLocalActivity(
                            WalletActivityItem(
                                id = "wc-invalid-host-proposal-${System.currentTimeMillis()}",
                                chainId = _uiState.value.selectedChainId,
                                kind = WalletActivityKind.DAPP,
                                title = "已拒绝非法 DApp 会话",
                                subtitle = proposal.peerName.ifBlank { proposal.peerUrl.ifBlank { "未知 DApp" } },
                                detail = proposal.peerUrl.ifBlank { "缺少 HTTPS 主机" },
                                statusLabel = "主机校验拦截",
                                timestamp = System.currentTimeMillis(),
                            )
                        )
                        _uiState.update {
                            it.copy(
                                walletConnectProposal = null,
                                info = "高安全模式仅允许 HTTPS 且主机名合法的 DApp，会话已自动拒绝。",
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }

                    walletConnectProposalPolicyError(proposal, _uiState.value.selectedChainId) != null -> {
                        val policyError = walletConnectProposalPolicyError(proposal, _uiState.value.selectedChainId)
                            ?: "高安全模式已拒绝超范围 DApp 会话"
                        WalletConnectBridge.rejectCurrentProposal("Rejected WalletConnect proposal outside strict security policy") { result ->
                            result.onFailure { setError("自动拒绝超范围 WalletConnect 会话失败: ${it.message}") }
                        }
                        recordLocalActivity(
                            WalletActivityItem(
                                id = "wc-policy-reject-proposal-${System.currentTimeMillis()}",
                                chainId = _uiState.value.selectedChainId,
                                kind = WalletActivityKind.DAPP,
                                title = "已拒绝超范围 DApp 会话",
                                subtitle = proposal.peerName.ifBlank { trustedHost.ifBlank { proposal.peerUrl.ifBlank { "未知 DApp" } } },
                                detail = proposal.requiredMethods.joinToString().ifBlank { proposal.requiredChains.joinToString() },
                                statusLabel = "最小权限策略拦截",
                                timestamp = System.currentTimeMillis(),
                            )
                        )
                        _uiState.update {
                            it.copy(
                                walletConnectProposal = null,
                                info = policyError,
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }

                    AUTO_APPROVE_WALLETCONNECT -> {
                    val currentAddress = _uiState.value.selectedAddress
                    if (currentAddress.isBlank()) {
                        setError("收到 WalletConnect 会话提案，但当前未选择观察地址，无法自动批准")
                    } else {
                        WalletConnectBridge.approveCurrentProposal(currentAddress, _uiState.value.selectedChainId) { result ->
                            result.onSuccess {
                                val approvedAddress = _uiState.value.selectedAddress
                                if (approvedAddress.isNotBlank()) {
                                    _uiState.update { state ->
                                        val now = System.currentTimeMillis()
                                        val activeEntries = pruneTrustedDappEntries(state.trustedDappEntries, now)
                                        val trustedEntry = TrustedDappEntry(
                                            host = trustedHost,
                                            chainId = state.selectedChainId,
                                            address = approvedAddress,
                                            trustedAt = now,
                                        )
                                        state.copy(
                                            trustedDappEntries = (listOf(trustedEntry) + activeEntries.filterNot {
                                                it.host == trustedHost &&
                                                    it.chainId == state.selectedChainId &&
                                                    it.address.equals(approvedAddress, ignoreCase = true)
                                            }).take(MAX_TRUSTED_DAPP_ENTRIES),
                                        )
                                    }
                                    persistTrustedDappEntries()
                                }
                                recordLocalActivity(
                                    WalletActivityItem(
                                        id = "dapp-proposal-${System.currentTimeMillis()}",
                                        chainId = _uiState.value.selectedChainId,
                                        kind = WalletActivityKind.DAPP,
                                        title = "WalletConnect 已连接",
                                        subtitle = proposal.peerName.ifBlank { trustedHost.ifBlank { proposal.peerUrl.ifBlank { "未知 DApp" } } },
                                        detail = proposal.requiredChains.joinToString().ifBlank { trustedHost },
                                        statusLabel = "会话已批准并绑定当前地址/链（24 小时）",
                                        timestamp = System.currentTimeMillis(),
                                    )
                                )
                                _uiState.update {
                                    it.copy(
                                        info = "WalletConnect 会话已自动批准，并已绑定到当前地址/链的可信范围（24 小时有效）。",
                                        error = "",
                                    )
                                }
                            }
                            result.onFailure {
                                setError("WalletConnect 自动批准失败: ${it.message}")
                            }
                        }
                    }
                    }

                    else -> {
                        val hostHint = if (currentTrustedDappEntry(trustedHost) != null) {
                            "该 DApp 已在当前地址/链的可信范围内。"
                        } else {
                            "该 DApp 尚未绑定到当前地址/链，批准后才会写入当前作用域的可信范围。"
                        }
                        _uiState.update {
                            it.copy(
                                info = "收到新的 WalletConnect 连接请求，请先审核 DApp 来源、链和方法后再批准。$hostHint",
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }
                }
            }
        }
        viewModelScope.launch {
            WalletConnectBridge.request.collectLatest { request ->
                _uiState.update { it.copy(walletConnectPendingRequest = request) }
                if (request != null) {
                    val trustedHost = trustedWalletConnectHost(request.peerUrl)
                    when {
                        request.isScam -> {
                            WalletConnectBridge.respondCurrentRequestError("已拒绝可疑 DApp 请求", 4001) { result ->
                                result.onFailure { setError("自动拒绝可疑 WalletConnect 请求失败: ${it.message}") }
                            }
                            recordLocalActivity(
                                WalletActivityItem(
                                    id = "wc-scam-request-${request.requestId}",
                                    chainId = parseWalletConnectChainId(request.chainId) ?: _uiState.value.selectedChainId,
                                    kind = WalletActivityKind.DAPP,
                                    title = "已拒绝可疑 DApp 请求",
                                    subtitle = request.peerName.ifBlank { request.peerUrl.ifBlank { "未知 DApp" } },
                                    detail = request.method,
                                    statusLabel = "自动拒绝",
                                    timestamp = System.currentTimeMillis(),
                                )
                            )
                            _uiState.update {
                                it.copy(
                                    walletConnectPendingRequest = null,
                                    info = "已自动拒绝可疑 WalletConnect 请求。",
                                    error = "",
                                    activeTab = WalletTab.DISCOVER,
                                )
                            }
                        }

                        REQUIRE_VERIFIED_WALLETCONNECT && !request.isVerified -> {
                            WalletConnectBridge.respondCurrentRequestError("高安全模式仅允许处理来源已验证的 DApp 请求", 4001) { result ->
                                result.onFailure { setError("自动拒绝未验证 WalletConnect 请求失败: ${it.message}") }
                            }
                            recordLocalActivity(
                                WalletActivityItem(
                                    id = "wc-unverified-request-${request.requestId}",
                                    chainId = parseWalletConnectChainId(request.chainId) ?: _uiState.value.selectedChainId,
                                    kind = WalletActivityKind.DAPP,
                                    title = "已拒绝未验证 DApp 请求",
                                    subtitle = request.peerName.ifBlank { request.peerUrl.ifBlank { "未知 DApp" } },
                                    detail = request.method,
                                    statusLabel = "高安全模式拦截",
                                    timestamp = System.currentTimeMillis(),
                                )
                            )
                            _uiState.update {
                                it.copy(
                                    walletConnectPendingRequest = null,
                                    info = "高安全模式仅允许处理来源已验证的 DApp 请求，已自动拒绝当前请求。",
                                    error = "",
                                    activeTab = WalletTab.DISCOVER,
                                )
                            }
                        }

                        trustedHost.isBlank() -> {
                            WalletConnectBridge.respondCurrentRequestError("高安全模式仅允许处理 HTTPS 且主机名合法的 DApp 请求", 4001) { result ->
                                result.onFailure { setError("自动拒绝非法 WalletConnect 请求失败: ${it.message}") }
                            }
                            recordLocalActivity(
                                WalletActivityItem(
                                    id = "wc-invalid-host-request-${request.requestId}",
                                    chainId = parseWalletConnectChainId(request.chainId) ?: _uiState.value.selectedChainId,
                                    kind = WalletActivityKind.DAPP,
                                    title = "已拒绝非法 DApp 请求",
                                    subtitle = request.peerName.ifBlank { request.peerUrl.ifBlank { "未知 DApp" } },
                                    detail = request.method,
                                    statusLabel = "主机校验拦截",
                                    timestamp = System.currentTimeMillis(),
                                )
                            )
                            _uiState.update {
                                it.copy(
                                    walletConnectPendingRequest = null,
                                    info = "高安全模式仅允许处理 HTTPS 且主机名合法的 DApp 请求，已自动拒绝当前请求。",
                                    error = "",
                                    activeTab = WalletTab.DISCOVER,
                                )
                            }
                        }

                        currentTrustedDappEntry(trustedHost) == null -> {
                            WalletConnectBridge.respondCurrentRequestError("该 DApp 在当前地址/链下不在可信范围", 4001) { result ->
                                result.onFailure { setError("自动拒绝未信任 WalletConnect 请求失败: ${it.message}") }
                            }
                            recordLocalActivity(
                                WalletActivityItem(
                                    id = "wc-untrusted-host-request-${request.requestId}",
                                    chainId = parseWalletConnectChainId(request.chainId) ?: _uiState.value.selectedChainId,
                                    kind = WalletActivityKind.DAPP,
                                    title = "已拒绝未信任 DApp 请求",
                                    subtitle = request.peerName.ifBlank { trustedHost },
                                    detail = request.method,
                                    statusLabel = "当前地址/链信任拦截",
                                    timestamp = System.currentTimeMillis(),
                                )
                            )
                            _uiState.update {
                                it.copy(
                                    walletConnectPendingRequest = null,
                                    info = "该 DApp 在当前地址/链下不在可信范围，已自动拒绝当前请求。",
                                    error = "",
                                    activeTab = WalletTab.DISCOVER,
                                )
                            }
                        }

                        shouldAutoHandleWalletConnectRequest(request) -> {
                            handleWalletConnectRequest(request)
                        }
                        else -> {
                            _uiState.update {
                                it.copy(
                                    info = "收到新的 DApp 请求，请先审核来源、方法和参数后再继续。当前主机: $trustedHost",
                                    error = "",
                                    activeTab = WalletTab.DISCOVER,
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    private fun restorePersistedState() {
        val addresses = WalletStorage.readAddresses(prefs, ::normalizeAddress)
        val addressNotes = WalletStorage.readAddressNotes(prefs, ::normalizeAddress)
        val selected = WalletStorage.readSelectedAddress(prefs, ::normalizeAddress)
        val effectiveSelected = when {
            selected.isNotBlank() && addresses.contains(selected) -> selected
            addresses.isNotEmpty() -> addresses.first()
            else -> ""
        }
        val selectedChain = WalletChains.byId(WalletStorage.readSelectedChainId(prefs))?.chainId
            ?: WalletChains.DEFAULT.chainId
        val contacts = WalletStorage.readContacts(prefs, ::normalizeAddress)
        val trustedDappEntries = pruneTrustedDappEntries(
            WalletStorage.readTrustedDappEntries(
                prefs = prefs,
                defaultChainId = selectedChain,
                defaultAddress = effectiveSelected,
                normalizer = ::normalizeAddress,
            )
        )
        val evmDerivationPath = WalletStorage.readEvmDerivationPath(prefs)
        val web3BridgeAccounts = WalletStorage.readWeb3BridgeAccounts(prefs)
        val bitcoinWatchAccounts = WalletStorage.readBitcoinWatchAccounts(prefs).map(::enrichBitcoinWatchAccount)
        localActivityItems.clear()
        localActivityItems += WalletStorage.readActivity(prefs)
        _uiState.update {
            it.copy(
                addresses = addresses,
                addressNotes = addressNotes,
                selectedAddress = effectiveSelected,
                evmDerivationPath = evmDerivationPath,
                web3BridgeAccounts = web3BridgeAccounts,
                bitcoinWatchAccounts = bitcoinWatchAccounts,
                bitcoinPrototypeStatus = defaultBitcoinPrototypeStatus(bitcoinWatchAccounts.size),
                selectedChainId = selectedChain,
                transferToken = WalletChains.require(selectedChain).preferredTransferSymbol(),
                contacts = contacts,
                trustedDappEntries = trustedDappEntries,
            )
        }
        publishActivity()
        if (effectiveSelected.isNotBlank()) {
            loadBalances(selectedChain, effectiveSelected)
        }
    }

    private fun persistAddresses() {
        val state = _uiState.value
        WalletStorage.writeAddresses(prefs, state.addresses, state.selectedAddress)
        val allowedKeys = state.addresses.map(::canonicalAddressKey).toSet()
        WalletStorage.writeAddressNotes(
            prefs,
            state.addressNotes.filterKeys { it in allowedKeys },
        )
    }

    private fun persistContacts() {
        WalletStorage.writeContacts(prefs, _uiState.value.contacts)
    }

    private fun persistTrustedDappEntries() {
        WalletStorage.writeTrustedDappEntries(prefs, _uiState.value.trustedDappEntries)
    }

    private fun persistBitcoinWatchAccounts() {
        WalletStorage.writeBitcoinWatchAccounts(prefs, _uiState.value.bitcoinWatchAccounts)
    }

    private fun persistWeb3BridgeAccounts() {
        WalletStorage.writeWeb3BridgeAccounts(prefs, _uiState.value.web3BridgeAccounts)
    }

    private fun cacheBitcoinSnapshot(accountId: String, snapshot: BitcoinAccountSnapshot, syncedAt: Long) {
        bitcoinSnapshotCache[accountId] = CachedBitcoinSnapshot(
            snapshot = snapshot,
            syncedAt = syncedAt,
        )
    }

    private fun currentBitcoinSnapshot(account: BitcoinWatchAccount): BitcoinAccountSnapshot? {
        val cached = bitcoinSnapshotCache[account.id] ?: return null
        val snapshotAgeMs = System.currentTimeMillis() - cached.syncedAt
        return cached.snapshot.takeIf { cached.syncedAt > 0L && snapshotAgeMs in 0..BITCOIN_SNAPSHOT_CACHE_TTL_MS }
    }

    private fun updateBitcoinWatchAccount(
        accountId: String,
        transform: (BitcoinWatchAccount) -> BitcoinWatchAccount,
        persist: Boolean = true,
    ) {
        _uiState.update { state ->
            state.copy(
                bitcoinWatchAccounts = state.bitcoinWatchAccounts.map { account ->
                    if (account.id == accountId) transform(account) else account
                }
            )
        }
        if (persist) {
            persistBitcoinWatchAccounts()
        }
    }

    private fun canonicalAddressKey(address: String): String {
        return normalizeAddress(address)?.lowercase() ?: address.trim().lowercase()
    }

    private fun persistActivity() {
        WalletStorage.writeActivity(prefs, localActivityItems)
    }

    private fun syncRecentActivity(chain: WalletChain, address: String) {
        viewModelScope.launch {
            _uiState.update { it.copy(syncingActivity = true) }
            runCatching {
                EvmRpc.getRecentAddressActivity(chain, address)
            }.onSuccess { items ->
                syncedActivityByChain[chain.chainId] = items
                publishActivity()
            }.onFailure {
                syncedActivityByChain.remove(chain.chainId)
                publishActivity()
            }
            _uiState.update { it.copy(syncingActivity = false) }
        }
    }

    private suspend fun buildNativeTransferRequest(
        chain: WalletChain,
        from: String,
        to: String,
        amount: String,
        derivationPath: String,
    ): String {
        val (maxPriority, maxFee) = EvmRpc.getBlockGasParams(chain)
        val nonce = EvmRpc.getNonce(chain, from)
        val valueWei = amountToWei(amount, chain.tokens.first().decimals)
        val gasLimit = EvmRpc.estimateGas(
            chain,
            mapOf(
                "from" to from,
                "to" to to,
                "value" to "0x${valueWei.toString(16)}",
                "data" to "0x",
            )
        )
        return TpRequestBuilder.buildSignTransactionRequest(
            fromAddress = from,
            txData = TxData(
                from = from,
                to = to,
                value = valueWei,
                data = "0x",
                gasLimit = gasLimit,
                nonce = nonce,
                maxFeePerGas = maxFee,
                maxPriorityFeePerGas = maxPriority,
                type = 2,
            ),
            chain = chain,
            derivationPath = derivationPath,
        )
    }

    private suspend fun buildTokenTransferRequest(
        chain: WalletChain,
        from: String,
        to: String,
        amount: String,
        token: TokenInfo,
        derivationPath: String,
    ): String {
        val tokenAddress = token.address ?: error("该资产不是 ERC20")
        val (maxPriority, maxFee) = EvmRpc.getBlockGasParams(chain)
        val nonce = EvmRpc.getNonce(chain, from)
        val amountWei = amountToWei(amount, token.decimals)
        val data = "a9059cbb" +
            "0".repeat(24) + to.removePrefix("0x").lowercase() +
            amountWei.toString(16).padStart(64, '0')
        val gasLimit = EvmRpc.estimateGas(
            chain,
            mapOf(
                "from" to from,
                "to" to tokenAddress,
                "data" to "0x$data",
            )
        )
        return TpRequestBuilder.buildSignTransactionRequest(
            fromAddress = from,
            txData = TxData(
                from = from,
                to = tokenAddress,
                value = BigInteger.ZERO,
                data = "0x$data",
                gasLimit = gasLimit,
                nonce = nonce,
                maxFeePerGas = maxFee,
                maxPriorityFeePerGas = maxPriority,
                type = 2,
            ),
            chain = chain,
            derivationPath = derivationPath,
        )
    }

    private fun selectChainInternal(
        chain: WalletChain,
        persist: Boolean,
        triggerReload: Boolean,
        message: String,
    ) {
        _uiState.update {
            it.copy(
                selectedChainId = chain.chainId,
                transferToken = if (chain.supportsSymbol(it.transferToken)) it.transferToken else chain.preferredTransferSymbol(),
                info = if (message.isBlank()) it.info else message,
                error = "",
            )
        }
        if (persist) {
            WalletStorage.writeSelectedChainId(prefs, chain.chainId)
        }
        if (triggerReload && _uiState.value.selectedAddress.isNotBlank()) {
            loadBalances(chain.chainId, _uiState.value.selectedAddress)
        }
    }

    private fun prepareRelayRequest(
        payload: String,
        explicitResponseType: PendingResponseType?,
        explicitTitle: String?,
        focusTab: WalletTab = WalletTab.HOME,
    ) {
        viewModelScope.launch {
            prepareRelayRequestNow(payload, explicitResponseType, explicitTitle, focusTab)
        }
    }

    private suspend fun prepareRelayRequestNow(
        payload: String,
        explicitResponseType: PendingResponseType?,
        explicitTitle: String?,
        focusTab: WalletTab = WalletTab.HOME,
    ): Boolean {
        return try {
            val request = TpQrCodec.parseSignRequest(payload)
            val chain = WalletChains.byId(request.chainId)
                ?: throw IllegalArgumentException("当前不支持链 ${request.chainId}")
            val bundle = RelayQrCodec.buildRelayPayloads(payload)
            val qr = generateQrBitmap(bundle.payloads.first())
            _uiState.update { state ->
                state.copy(
                    selectedChainId = chain.chainId,
                    transferToken = if (chain.supportsSymbol(state.transferToken)) state.transferToken else chain.preferredTransferSymbol(),
                    requestTitle = explicitTitle ?: "${chain.shortName} ${requestTitle(request)}",
                    requestSummary = buildRequestSummary(request),
                    transferInfo = buildTransferInfo(request),
                    dappInfo = buildDappInfo(request),
                    relayHint = if (bundle.payloads.size > 1) {
                        "已生成 ${bundle.payloads.size} 张静态二维码，将自动轮播给离线签名器扫描。"
                    } else {
                        "已生成 1 张静态二维码，可直接给树莓派扫描。"
                    },
                    preparedRequestChainId = chain.chainId,
                    preparedBitcoinAccountId = null,
                    signQrPages = bundle.payloads,
                    signQrPageIndex = 0,
                    signQrBitmap = qr,
                    preparedQrKind = PreparedQrKind.PI_REQUEST,
                    pendingResponseType = explicitResponseType ?: inferResponseType(request),
                    pendingWeb3Request = null,
                    error = "",
                    info = "请求已准备好，请让树莓派扫描本页二维码。",
                    activeTab = focusTab,
                    txHash = if ((explicitResponseType ?: inferResponseType(request)) == PendingResponseType.BROADCAST_TX) "" else state.txHash,
                    txHashChainId = if ((explicitResponseType ?: inferResponseType(request)) == PendingResponseType.BROADCAST_TX) null else state.txHashChainId,
                    txHashExplorerUrl = if ((explicitResponseType ?: inferResponseType(request)) == PendingResponseType.BROADCAST_TX) "" else state.txHashExplorerUrl,
                    lastSignature = if ((explicitResponseType ?: inferResponseType(request)) == PendingResponseType.SHOW_SIGNATURE) "" else state.lastSignature,
                    lastSignatureAddress = if ((explicitResponseType ?: inferResponseType(request)) == PendingResponseType.SHOW_SIGNATURE) "" else state.lastSignatureAddress,
                )
            }
            true
        } catch (e: Exception) {
            setError("解析请求失败: ${e.message}")
            false
        }
    }

    private fun handleIncomingPayload(payload: String, focusTab: WalletTab) {
        val trimmedPayload = payload.trim()
        if (trimmedPayload.startsWith("ur:", ignoreCase = true)) {
            viewModelScope.launch {
                prepareWeb3RelayRequestNow(trimmedPayload, focusTab)
            }
            return
        }
        val walletConnectUri = WalletConnectUriParser.extract(payload)
        if (trimmedPayload.startsWith("wc:", ignoreCase = true) && walletConnectUri == null) {
            setError("仅允许有效的 WalletConnect v2 配对链接，且长度不能过长")
            return
        }
        val normalizedPayload = walletConnectUri ?: trimmedPayload
        if (walletConnectUri != null) {
            try {
                WalletConnectBridge.pair(walletConnectUri)
                recordLocalActivity(
                    WalletActivityItem(
                        id = "wc-pair-${System.currentTimeMillis()}",
                        chainId = _uiState.value.selectedChainId,
                        kind = WalletActivityKind.DAPP,
                        title = "收到 WalletConnect 配对链接",
                        subtitle = "等待 DApp 发起会话提案",
                        statusLabel = "连接中",
                        timestamp = System.currentTimeMillis(),
                    )
                )
                _uiState.update {
                    it.copy(
                        info = "已接收 WalletConnect 配对链接。收到提案后请在钱包内手动批准。",
                        error = "",
                        activeTab = focusTab,
                    )
                }
            } catch (e: Exception) {
                setError("WalletConnect 配对失败: ${e.message}")
            }
            return
        }
        viewModelScope.launch {
            prepareExternalRelayRequestNow(normalizedPayload, focusTab)
        }
    }

    private suspend fun handleWalletConnectRequest(request: WalletConnectPendingRequest) {
        val address = _uiState.value.selectedAddress
        if (address.isBlank()) {
            WalletConnectBridge.respondCurrentRequestError("请先在钱包里选择观察地址", 4001)
            setError("请先选择观察地址，再处理 WalletConnect 请求")
            return
        }
        try {
            when (
                val prepared = WalletConnectRequestCodec.prepare(
                    request = request,
                    selectedAddress = address,
                    activeChainId = _uiState.value.selectedChainId,
                    derivationPath = _uiState.value.evmDerivationPath,
                )
            ) {
                is WalletConnectPreparedRequest.ImmediateResult -> {
                    prepared.switchToChainId?.let { targetChainId ->
                        WalletChains.byId(targetChainId)?.let { chain ->
                            selectChainInternal(
                                chain = chain,
                                persist = true,
                                triggerReload = true,
                                message = "已切换到 ${chain.shortName}",
                            )
                        }
                    }
                    WalletConnectBridge.respondCurrentRequestResult(prepared.result) { result ->
                        result.onFailure { setError("WalletConnect 返回结果失败: ${it.message}") }
                    }
                    recordLocalActivity(
                        WalletActivityItem(
                            id = "wc-immediate-${request.requestId}",
                            chainId = prepared.switchToChainId ?: _uiState.value.selectedChainId,
                            kind = WalletActivityKind.DAPP,
                            title = "DApp 请求已处理",
                            subtitle = request.method,
                            detail = request.peerName.ifBlank { request.peerUrl },
                            statusLabel = "已即时返回",
                            timestamp = System.currentTimeMillis(),
                        )
                    )
                    _uiState.update {
                        it.copy(
                            info = "WalletConnect 请求已直接返回: ${request.method}",
                            error = "",
                        )
                    }
                }

                is WalletConnectPreparedRequest.RelayToPi -> {
                    val ok = prepareRelayRequestNow(
                        payload = prepared.payload,
                        explicitResponseType = prepared.responseType,
                        explicitTitle = prepared.title,
                        focusTab = WalletTab.DISCOVER,
                    )
                    recordLocalActivity(
                        WalletActivityItem(
                            id = "wc-relay-${request.requestId}",
                            chainId = prepared.chainId,
                            kind = WalletActivityKind.DAPP,
                            title = "DApp 发起 ${request.method}",
                            subtitle = request.peerName.ifBlank { request.peerUrl.ifBlank { "未知 DApp" } },
                            detail = WalletChains.require(prepared.chainId).displayName,
                            statusLabel = if (ok) "待树莓派签名" else "生成二维码失败",
                            timestamp = System.currentTimeMillis(),
                        )
                    )
                    if (!ok) {
                        _uiState.update {
                            it.copy(
                                info = "已收到 WalletConnect 请求 ${request.method}，但未成功生成树莓派二维码。",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    } else {
                        _uiState.update {
                            it.copy(
                                info = "已收到 WalletConnect 请求 ${request.method}，请直接扫描页面上的签名二维码。",
                                error = "",
                                activeTab = WalletTab.DISCOVER,
                            )
                        }
                    }
                }
            }
        } catch (e: Exception) {
            WalletConnectBridge.respondCurrentRequestError(e.message ?: "请求处理失败", 5000)
            setError("WalletConnect 请求处理失败: ${e.message}")
        }
    }

    private fun updateQrPage(index: Int) {
        val pages = _uiState.value.signQrPages
        if (pages.isEmpty() || index !in pages.indices) return
        runCatching { generateQrBitmapSync(pages[index]) }
            .onSuccess { bitmap ->
                _uiState.update { it.copy(signQrPageIndex = index, signQrBitmap = bitmap) }
            }
            .onFailure { error ->
                _uiState.update {
                    it.copy(error = "中转二维码第 ${index + 1} 张生成失败: ${error.message}")
                }
            }
    }

    private suspend fun generateQrBitmap(payload: String): Bitmap = withContext(Dispatchers.Default) {
        generateQrBitmapSync(payload)
    }

    private fun generateQrBitmapSync(payload: String): Bitmap {
        val matrix = QRCodeWriter().encode(
            payload,
            BarcodeFormat.QR_CODE,
            900,
            900,
            mapOf(
                EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.L,
                EncodeHintType.MARGIN to 1,
                EncodeHintType.CHARACTER_SET to "UTF-8",
            )
        )
        return BarcodeEncoder().createBitmap(matrix)
    }

    private fun inferResponseType(request: TpSignRequest): PendingResponseType {
        return if (request is TpSignTransactionRequest) PendingResponseType.BROADCAST_TX else PendingResponseType.SHOW_SIGNATURE
    }

    private fun requestTitle(request: TpSignRequest): String {
        return when (request) {
            is TpSignTransactionRequest -> "待签交易"
            is TpSignPersonalMessageRequest -> "待签消息"
            is TpSignTypedDataRequest -> "待签 TypedData"
        }
    }

    private fun buildRequestSummary(request: TpSignRequest): String {
        return when (request) {
            is TpSignTransactionRequest -> buildTransactionSummary(request)
            is TpSignPersonalMessageRequest -> buildPersonalSignSummary(request)
            is TpSignTypedDataRequest -> buildTypedDataSummary(request)
        }
    }

    private fun buildTransferInfo(request: TpSignRequest): String {
        if (request !is TpSignTransactionRequest) return ""
        val chain = WalletChains.require(request.chainId)
        val tx = request.txData
        val to = tx.string("to") ?: "-"
        val from = request.address ?: tx.string("from") ?: "-"
        val valueWei = parseQuantity(tx.string("value") ?: "0x0")
        val dataHex = tx.string("data") ?: tx.string("input") ?: "0x"
        val cleanData = cleanHex(dataHex)
        val token = chain.findTokenByAddress(to)

        if (cleanData.startsWith("a9059cbb") && cleanData.length >= 136 && token != null) {
            val recipient = "0x" + cleanData.substring(32, 72)
            val amount = BigInteger(cleanData.substring(72, 136), 16)
            return buildString {
                appendLine("from: $from")
                appendLine("token: ${token.symbol}")
                appendLine("to: $recipient")
                appendLine("amount: ${formatUnits(amount, token.decimals)}")
                appendLine("chainId: ${request.chainId} (${chain.displayName})")
            }.trim()
        }

        return buildString {
            appendLine("from: $from")
            appendLine("to: $to")
            appendLine("value: ${formatUnits(valueWei, chain.tokens.first().decimals)} ${chain.nativeSymbol}")
            appendLine("contractCall: ${if (cleanData.isNotBlank()) "yes" else "no"}")
            if (cleanData.length >= 8) appendLine("methodId: 0x${cleanData.take(8)}")
            appendLine("chainId: ${request.chainId} (${chain.displayName})")
        }.trim()
    }

    private fun buildDappInfo(request: TpSignRequest): String {
        return buildString {
            appendLine("network: ${WalletChains.require(request.chainId).displayName}")
            appendLine("dappName: ${request.dappName ?: "-"}")
            appendLine("dappUrl: ${request.dappUrl ?: "-"}")
            appendLine("source/origin: ${request.dappSource ?: "-"}")
        }.trim()
    }

    private fun buildTransactionSummary(request: TpSignTransactionRequest): String {
        return buildString {
            appendLine("action: ${request.action}")
            appendLine("network: ${WalletChains.require(request.chainId).displayName}")
            appendLine("address: ${request.address ?: request.txData.string("from") ?: "-"}")
            appendLine("nonce: ${request.txData.string("nonce") ?: "-"}")
            appendLine("gasLimit: ${request.txData.string("gasLimit") ?: request.txData.string("gas") ?: "-"}")
            appendLine("type: ${request.txData.string("type") ?: "-"}")
            appendLine("requestId: ${request.requestId ?: "-"}")
        }.trim()
    }

    private fun buildPersonalSignSummary(request: TpSignPersonalMessageRequest): String {
        return buildString {
            appendLine("action: ${request.action}")
            appendLine("network: ${WalletChains.require(request.chainId).displayName}")
            appendLine("address: ${request.address ?: "-"}")
            appendLine("message: ${preview(request.message)}")
            appendLine("bytes: ${request.message.toByteArray().size}")
            appendLine("requestId: ${request.requestId ?: "-"}")
        }.trim()
    }

    private fun buildTypedDataSummary(request: TpSignTypedDataRequest): String {
        return buildString {
            appendLine("action: ${request.action}")
            appendLine("network: ${WalletChains.require(request.chainId).displayName}")
            appendLine("address: ${request.address ?: "-"}")
            appendLine("primaryType: ${request.primaryType ?: "-"}")
            appendLine("chainId: ${request.chainId} (${chainName(request.chainId)})")
            appendLine("bytes: ${request.typedDataJson.toByteArray().size}")
            appendLine("requestId: ${request.requestId ?: "-"}")
        }.trim()
    }

    private fun clearSensitiveSessionState() {
        WalletConnectBridge.clearPendingInteractiveState()
        disconnectWalletConnectSessionsForSecurity()
        _uiState.update {
            it.copy(
                preparingRequest = false,
                signQrPages = emptyList(),
                signQrPageIndex = 0,
                signQrBitmap = null,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingResponseType = null,
                pendingWeb3Request = null,
                pendingBroadcastRawTransaction = "",
                pendingBroadcastBitcoinTxHex = "",
                preparedRequestChainId = null,
                requestTitle = "",
                requestSummary = "",
                transferInfo = "",
                dappInfo = "",
                relayHint = "",
                requestInput = "",
                walletConnectProposal = null,
                walletConnectPendingRequest = null,
                lastSignature = "",
                lastSignatureAddress = "",
                info = "",
            )
        }
    }

    private fun disconnectWalletConnectSessionsForSecurity() {
        WalletConnectBridge.disconnectAllSessions { result ->
            result.onFailure { error ->
                setError("高安全模式断开 DApp 会话失败: ${error.message ?: "未知错误"}")
            }
        }
    }

    private fun shouldAutoHandleWalletConnectRequest(request: WalletConnectPendingRequest): Boolean {
        return when (request.method.lowercase()) {
            "eth_accounts", "eth_requestaccounts", "eth_chainid" -> true
            else -> false
        }
    }

    private fun isTrustedWalletConnectProposal(proposal: WalletConnectProposalUi): Boolean {
        return !proposal.isScam &&
            trustedWalletConnectHost(proposal.peerUrl).isNotBlank() &&
            walletConnectProposalPolicyError(proposal, _uiState.value.selectedChainId) == null &&
            (!REQUIRE_VERIFIED_WALLETCONNECT || proposal.isVerified)
    }

    private fun isTrustedWalletConnectRequest(request: WalletConnectPendingRequest): Boolean {
        val trustedHost = trustedWalletConnectHost(request.peerUrl)
        return !request.isScam &&
            trustedHost.isNotBlank() &&
            currentTrustedDappEntry(trustedHost) != null &&
            (!REQUIRE_VERIFIED_WALLETCONNECT || request.isVerified)
    }

    private fun trustedWalletConnectHost(peerUrl: String): String {
        val uri = runCatching { URI(peerUrl.trim()) }.getOrNull() ?: return ""
        if (!uri.scheme.equals("https", ignoreCase = true)) return ""
        if (uri.userInfo != null) return ""
        if (uri.fragment != null) return ""
        if (uri.port != -1 && uri.port != 443) return ""
        return normalizeTrustedDappHost(uri.host)
    }

    private fun currentTrustedDappEntry(host: String, state: WalletUiState = _uiState.value): TrustedDappEntry? {
        val normalizedHost = host.trim().lowercase()
        val selectedAddress = state.selectedAddress
        if (normalizedHost.isBlank() || selectedAddress.isBlank()) return null
        val now = System.currentTimeMillis()
        return state.trustedDappEntries.firstOrNull {
            it.host == normalizedHost &&
                it.chainId == state.selectedChainId &&
                it.address.equals(selectedAddress, ignoreCase = true) &&
                !isTrustedDappEntryExpired(it, now)
        }
    }

    private fun walletConnectProposalPolicyError(proposal: WalletConnectProposalUi, selectedChainId: Long): String? {
        val unsupportedRequiredMethods = proposal.requiredMethods
            .map { it.trim() }
            .filter { it.isNotBlank() }
            .distinctBy { it.lowercase() }
            .filterNot { STRICT_WALLETCONNECT_METHODS.contains(it.lowercase()) }
        if (unsupportedRequiredMethods.isNotEmpty()) {
            return "高安全模式仅允许最小签名方法集，已拒绝必需的超范围方法: ${unsupportedRequiredMethods.joinToString()}"
        }

        val requiredChains = proposal.requiredChains
            .map { it.trim() }
            .filter { it.isNotBlank() }
        val invalidRequiredChainLabels = requiredChains.filter { parseWalletConnectChainId(it) == null }
        if (invalidRequiredChainLabels.isNotEmpty()) {
            return "高安全模式仅允许标准 EVM 链标识，已拒绝必需的异常链请求: ${invalidRequiredChainLabels.joinToString()}"
        }

        val unsupportedRequiredChains = requiredChains
            .mapNotNull(::parseWalletConnectChainId)
            .distinct()
            .filter { it != selectedChainId }
            .map { chainId ->
                WalletChains.byId(chainId)?.displayName ?: "chain-$chainId"
            }
        if (unsupportedRequiredChains.isNotEmpty()) {
            return "高安全模式仅允许连接当前手动选中的链，已拒绝必需的额外链请求: ${unsupportedRequiredChains.joinToString()}"
        }
        return null
    }

    private fun pruneExpiredTrustedDappEntries() {
        val pruned = pruneTrustedDappEntries(_uiState.value.trustedDappEntries)
        if (pruned == _uiState.value.trustedDappEntries) return
        _uiState.update { it.copy(trustedDappEntries = pruned) }
        persistTrustedDappEntries()
    }

    private fun pruneTrustedDappEntries(
        entries: List<TrustedDappEntry>,
        now: Long = System.currentTimeMillis(),
    ): List<TrustedDappEntry> {
        return entries
            .asSequence()
            .mapNotNull { entry ->
                val normalizedHost = normalizeTrustedDappHost(entry.host)
                val normalizedAddress = normalizeAddress(entry.address)
                if (normalizedHost.isBlank() || normalizedAddress.isNullOrBlank()) {
                    null
                } else {
                    entry.copy(host = normalizedHost, address = normalizedAddress)
                }
            }
            .filterNot { isTrustedDappEntryExpired(it, now) }
            .distinctBy { "${it.host}|${it.chainId}|${it.address.lowercase()}" }
            .sortedByDescending { it.trustedAt }
            .take(MAX_TRUSTED_DAPP_ENTRIES)
            .toList()
    }

    private fun isTrustedDappEntryExpired(entry: TrustedDappEntry, now: Long = System.currentTimeMillis()): Boolean {
        val trustedAt = entry.trustedAt.takeIf { it > 0L } ?: return true
        return now - trustedAt >= TRUSTED_DAPP_ENTRY_TTL_MS
    }

    private fun normalizeTrustedDappHost(host: String?): String {
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
        if (asciiHost != rawHost) return ""
        if (isIpv4Literal(asciiHost)) return ""
        return asciiHost
    }

    private fun isIpv4Literal(host: String): Boolean {
        val parts = host.split('.')
        if (parts.size != 4 || parts.any { it.isBlank() || it.any { ch -> ch !in '0'..'9' } }) return false
        return parts.all { part -> part.toIntOrNull()?.let { it in 0..255 } == true }
    }

    private fun parseWalletConnectChainId(chainId: String?): Long? {
        val value = chainId.orEmpty().trim()
        return when {
            value.startsWith("eip155:", ignoreCase = true) -> value.substringAfterLast(':').toLongOrNull()
            value.startsWith("0x", ignoreCase = true) -> value.removePrefix("0x").removePrefix("0X").toLongOrNull(16)
            else -> value.toLongOrNull()
        }
    }

    private fun recordLocalActivity(item: WalletActivityItem) {
        localActivityItems.removeAll { it.id == item.id }
        localActivityItems += item
        localActivityItems.sortByDescending { it.timestamp }
        persistActivity()
        publishActivity()
    }

    private fun publishActivity() {
        val merged = (localActivityItems + syncedActivityByChain.values.flatten())
            .sortedByDescending { it.timestamp }
            .distinctBy {
                if (it.txHash.isNotBlank()) "${it.kind}-${it.chainId}-${it.txHash}-${it.amountLabel}"
                else it.id
            }
        _uiState.update { it.copy(activityItems = merged) }
    }

    private fun normalizeAddress(raw: String?): String? {
        val value = raw.orEmpty().trim()
        if (value.isBlank()) return null
        val embedded = Regex("0x[a-fA-F0-9]{40}").find(value)?.value
        val candidate = embedded ?: value
            .removePrefix("ethereum:")
            .removePrefix("Ethereum:")
            .removePrefix("ETHEREUM:")
            .substringBefore('?')
            .substringBefore('#')
            .substringBefore('@')
            .trim()
        if (candidate.isBlank()) return null
        val address = if (candidate.startsWith("0x")) candidate else "0x$candidate"
        if (address.length != 42) return null
        if (!address.removePrefix("0x").all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return null
        return address
    }

    private fun normalizeDerivationPath(raw: String?): String? {
        val value = raw.orEmpty().trim()
        if (value.isBlank()) return DEFAULT_EVM_DERIVATION_PATH
        if (!value.startsWith("m")) return null
        if (value == "m") return value
        val segments = value.split("/")
        if (segments.first() != "m") return null
        val valid = segments.drop(1).all { segment ->
            val cleaned = segment.removeSuffix("'").removeSuffix("h").removeSuffix("H")
            cleaned.isNotBlank() && cleaned.all(Char::isDigit)
        }
        return value.takeIf { valid }
    }

    private fun shortAddress(address: String): String {
        return if (address.length <= 14) address else "${address.take(8)}...${address.takeLast(6)}"
    }

    private fun setError(message: String) {
        _uiState.update { it.copy(error = message, info = "", preparingRequest = false) }
    }

    private fun confirmPendingBitcoinBroadcast(state: WalletUiState) {
        val txHex = state.pendingBroadcastBitcoinTxHex
        val accountId = state.preparedBitcoinAccountId ?: return setError("当前没有待广播的 BTC 请求")
        val account = state.bitcoinWatchAccounts.firstOrNull { it.id == accountId }
            ?: return setError("未找到对应的 BTC 账户")
        viewModelScope.launch {
            try {
                val txid = BitcoinTransferService.broadcastTransaction(account.prefix, txHex)
                _uiState.update {
                    it.copy(
                        signQrPages = emptyList(),
                        signQrPageIndex = 0,
                        signQrBitmap = null,
                        pendingResponseType = null,
                        pendingBroadcastRawTransaction = "",
                        pendingBroadcastBitcoinTxHex = "",
                        preparedRequestChainId = null,
                        preparedBitcoinAccountId = null,
                        requestTitle = "",
                        requestSummary = "",
                        transferInfo = "",
                        dappInfo = "",
                        relayHint = "",
                        requestInput = "",
                        txHash = txid,
                        txHashChainId = null,
                        txHashExplorerUrl = "${bitcoinEsploraBaseUrl(account.prefix).removeSuffix("/api")}/tx/$txid",
                        lastSignature = "",
                        lastSignatureAddress = "",
                        preparedQrKind = PreparedQrKind.PI_REQUEST,
                        pendingWeb3Request = null,
                        error = "",
                        info = "BTC 交易已广播：$txid",
                    )
                }
                recordLocalActivity(
                    WalletActivityItem(
                        id = "btc-$txid",
                        chainId = WalletChains.DEFAULT.chainId,
                        kind = WalletActivityKind.OUTGOING_TX,
                        title = "BTC 交易已广播",
                        subtitle = account.label,
                        detail = state.requestSummary,
                        amountLabel = extractAmountLabel(state.requestSummary).ifBlank { "BTC" },
                        statusLabel = "已广播",
                        timestamp = System.currentTimeMillis(),
                        txHash = txid,
                    )
                )
                syncBitcoinWatchAccount(accountId)
            } catch (e: Exception) {
                setError("BTC 广播失败: ${e.message}")
            }
        }
    }

    private fun confirmPendingEvmBroadcast(state: WalletUiState) {
        val rawTx = state.pendingBroadcastRawTransaction
        val chain = WalletChains.require(state.preparedRequestChainId ?: state.selectedChainId)
        viewModelScope.launch {
            try {
                val hash = EvmRpc.sendRawTransaction(chain, rawTx)
                val pendingRequest = state.walletConnectPendingRequest
                if (pendingRequest != null) {
                    WalletConnectBridge.respondCurrentRequestResult("0x$hash") { result ->
                        result.onFailure { setError("WalletConnect 返回交易哈希失败: ${it.message}") }
                    }
                }
                val explorerUrl = chain.txUrl(hash)
                recordLocalActivity(
                    WalletActivityItem(
                        id = "tx-$hash",
                        chainId = chain.chainId,
                        kind = WalletActivityKind.OUTGOING_TX,
                        title = "${chain.shortName} 交易已广播",
                        subtitle = state.transferInfo.lineSequence().firstOrNull().orEmpty(),
                        detail = state.requestSummary,
                        amountLabel = extractAmountLabel(state.transferInfo),
                        statusLabel = if (pendingRequest != null) "已返回 DApp" else "已广播",
                        timestamp = System.currentTimeMillis(),
                        txHash = "0x$hash",
                        externalUrl = explorerUrl,
                    )
                )
                _uiState.update {
                    it.copy(
                        pendingBroadcastRawTransaction = "",
                        txHash = hash,
                        txHashChainId = chain.chainId,
                        txHashExplorerUrl = explorerUrl,
                        preparedRequestChainId = null,
                        requestTitle = "",
                        requestSummary = "",
                        transferInfo = "",
                        dappInfo = "",
                        relayHint = "",
                        preparedQrKind = PreparedQrKind.PI_REQUEST,
                        pendingWeb3Request = null,
                        error = "",
                        info = if (pendingRequest != null) "交易已广播，并已返回给 WalletConnect" else "交易已广播",
                    )
                }
                loadBalances(chain.chainId, _uiState.value.selectedAddress, silent = true)
            } catch (e: Exception) {
                setError("广播失败: ${e.message}")
            }
        }
    }

    private fun preview(value: String, max: Int = 100): String {
        val singleLine = value.replace('\n', ' ')
        return if (singleLine.length <= max) singleLine else singleLine.take(max) + "..."
    }

    private fun parseQuantity(raw: String): BigInteger {
        val value = raw.trim()
        if (value.isBlank()) return BigInteger.ZERO
        return if (value.startsWith("0x") || value.startsWith("0X")) {
            BigInteger(value.removePrefix("0x").removePrefix("0X").ifBlank { "0" }, 16)
        } else {
            value.toBigIntegerOrNull() ?: BigInteger.ZERO
        }
    }

    private fun cleanHex(value: String): String = value.removePrefix("0x").removePrefix("0X").lowercase()

    private fun formatUnits(value: BigInteger, decimals: Int): String {
        if (value == BigInteger.ZERO) return "0"
        val divisor = BigDecimal.TEN.pow(decimals)
        return BigDecimal(value)
            .divide(divisor, decimals.coerceAtMost(8), RoundingMode.DOWN)
            .stripTrailingZeros()
            .toPlainString()
    }

    private fun calculateUsd(amount: String, priceUsd: Double?): Double? {
        val decimal = amount.toBigDecimalOrNull() ?: return null
        return priceUsd?.let { decimal.multiply(BigDecimal.valueOf(it)).toDouble() }
    }

    private suspend fun enrichMissingPrices(balances: List<AssetBalanceUi>): List<AssetBalanceUi> {
        val needed = balances.filter { it.priceUsd == null }
        if (needed.isEmpty()) return balances
        val symbols = needed.map { it.symbol.uppercase() }.distinct()
        val priceMap = mutableMapOf<String, Double>()
        symbols.forEach { symbol ->
            val price = EvmRpc.fetchCoingeckoPriceForSymbol(symbol)
            if (price != null) priceMap[symbol] = price
        }
        return balances.map { asset ->
            val symbolKey = asset.symbol.uppercase()
            val price = asset.priceUsd ?: priceMap[symbolKey]
            if (price == null) asset else asset.copy(priceUsd = price, usdAmount = calculateUsd(asset.amount, price))
        }
    }

    private fun amountToWei(amount: String, decimals: Int): BigInteger {
        val cleaned = amount.trim()
        val parts = cleaned.split('.')
        val intPart = parts.firstOrNull().orEmpty().ifBlank { "0" }.toBigIntegerOrNull() ?: BigInteger.ZERO
        val fraction = (parts.getOrNull(1) ?: "").padEnd(decimals, '0').take(decimals)
        val frac = if (fraction.isBlank()) BigInteger.ZERO else fraction.toBigIntegerOrNull() ?: BigInteger.ZERO
        return intPart * BigInteger.TEN.pow(decimals) + frac
    }

    private fun chainName(chainId: Long): String {
        return WalletChains.byId(chainId)?.displayName ?: "Chain $chainId"
    }

    private fun shortAddress(address: String, head: Int = 6, tail: Int = 4): String {
        val normalized = normalizeAddress(address) ?: return address
        return "${normalized.take(head)}...${normalized.takeLast(tail)}"
    }

    private fun extractAmountLabel(transferInfo: String): String {
        return transferInfo.lineSequence()
            .firstOrNull { it.startsWith("amount:", ignoreCase = true) || it.startsWith("value:", ignoreCase = true) }
            ?.substringAfter(':')
            ?.trim()
            .orEmpty()
    }

    private suspend fun showPreparedQrPages(
        title: String,
        summary: String,
        transferInfo: String,
        dappInfo: String,
        relayHint: String,
        pages: List<String>,
        preparedQrKind: PreparedQrKind,
        pendingResponseType: PendingResponseType?,
        focusTab: WalletTab,
        infoMessage: String,
        preparedRequestChainId: Long? = null,
        pendingWeb3Request: Web3EthSignRequest? = null,
    ) {
        val filteredPages = pages.filter { it.isNotBlank() }
        require(filteredPages.isNotEmpty()) { "二维码内容为空" }
        val qr = generateQrBitmap(filteredPages.first())
        _uiState.update {
            it.copy(
                preparingRequest = false,
                requestTitle = title,
                requestSummary = summary,
                transferInfo = transferInfo,
                dappInfo = dappInfo,
                relayHint = relayHint,
                preparedRequestChainId = preparedRequestChainId,
                preparedBitcoinAccountId = null,
                signQrPages = filteredPages,
                signQrPageIndex = 0,
                signQrBitmap = qr,
                preparedQrKind = preparedQrKind,
                pendingResponseType = pendingResponseType,
                pendingWeb3Request = pendingWeb3Request,
                pendingBroadcastRawTransaction = "",
                pendingBroadcastBitcoinTxHex = "",
                requestInput = "",
                lastSignature = "",
                lastSignatureAddress = "",
                txHash = "",
                txHashChainId = null,
                txHashExplorerUrl = "",
                error = "",
                info = infoMessage,
                activeTab = focusTab,
            )
        }
    }

    private suspend fun prepareWeb3RelayRequestNow(payload: String, focusTab: WalletTab): Boolean {
        return try {
            val request = Web3UrCodec.parseEthSignRequestUr(payload)
            val tpPayload = Web3UrCodec.buildTpRequest(request, currentWeb3BridgeAccount(request))
            val relayWallet = Web3RelayWallet.detect(request.origin)
            val bundle = Web3RelayCodec.buildNativeRelayPayloads(request, tpPayload)
            showPreparedQrPages(
                title = when (request.dataType) {
                    Web3RequestDataType.TRANSACTION,
                    Web3RequestDataType.TYPED_TRANSACTION -> "${relayWallet.displayName} 交易待树莓派签名"
                    Web3RequestDataType.PERSONAL_MESSAGE -> "${relayWallet.displayName} 消息待树莓派签名"
                    Web3RequestDataType.TYPED_DATA -> "${relayWallet.displayName} TypedData 待树莓派签名"
                },
                summary = Web3UrCodec.buildRequestSummary(request),
                transferInfo = "",
                dappInfo = "",
                relayHint = if (bundle.payloads.size > 1) {
                    "已生成 ${bundle.payloads.size} 张中转二维码，将自动轮播给树莓派扫描。"
                } else {
                    "已生成 1 张中转二维码，让树莓派直接扫描。"
                },
                pages = bundle.payloads,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingResponseType = null,
                pendingWeb3Request = null,
                focusTab = focusTab,
                preparedRequestChainId = WalletChains.byId(request.chainId)?.chainId,
                infoMessage = "钱包请求已转成低密度二维码，请让树莓派扫描。",
            )
            true
        } catch (e: Exception) {
            setError("钱包二维码中转失败: ${e.message}")
            false
        }
    }

    private suspend fun prepareExternalRelayRequestNow(payload: String, focusTab: WalletTab): Boolean {
        return try {
            val request = TpQrCodec.parseSignRequest(payload)
            val chain = WalletChains.byId(request.chainId)
                ?: throw IllegalArgumentException("当前不支持链 ${request.chainId}")
            val bundle = RelayQrCodec.buildRelayPayloads(payload)
            showPreparedQrPages(
                title = "${chain.shortName} ${requestTitle(request)}",
                summary = buildRequestSummary(request),
                transferInfo = buildTransferInfo(request),
                dappInfo = "",
                relayHint = if (bundle.payloads.size > 1) {
                    "已生成 ${bundle.payloads.size} 张中转二维码，将自动轮播给树莓派扫描。"
                } else {
                    "已生成 1 张中转二维码，让树莓派直接扫描。"
                },
                pages = bundle.payloads,
                preparedQrKind = PreparedQrKind.PI_REQUEST,
                pendingResponseType = null,
                focusTab = focusTab,
                infoMessage = "外部钱包请求已准备好，请让树莓派扫描当前二维码。",
                preparedRequestChainId = chain.chainId,
            )
            true
        } catch (e: Exception) {
            setError("解析请求失败: ${e.message}")
            false
        }
    }

    private fun currentWeb3BridgeAccount(
        request: Web3EthSignRequest? = null,
        state: WalletUiState = _uiState.value,
    ): Web3BridgeAccount? {
        val accounts = state.web3BridgeAccounts.sortedByDescending { it.importedAt }
        if (accounts.isEmpty()) return null
        val selectedAddress = normalizeAddress(state.selectedAddress)
        val selectedAccount = selectedAddress?.let { selected ->
            accounts.firstOrNull { it.address.equals(selected, ignoreCase = true) }
        }
        if (request == null) return selectedAccount

        val requestAddress = normalizeAddress(request.address)
        fun matchesRequest(account: Web3BridgeAccount): Boolean {
            return account.addressPath.equals(request.derivationPath, ignoreCase = true) &&
                (requestAddress == null || account.address.equals(requestAddress, ignoreCase = true))
        }

        if (selectedAccount != null) {
            return selectedAccount.takeIf(::matchesRequest)
        }
        if (selectedAddress != null) return null

        return accounts.firstOrNull { account ->
            matchesRequest(account)
        } ?: accounts.firstOrNull { account ->
            requestAddress != null && account.address.equals(requestAddress, ignoreCase = true)
        } ?: accounts.firstOrNull { account ->
            account.addressPath.equals(request.derivationPath, ignoreCase = true)
        }
    }

    private fun upsertWeb3BridgeAccount(account: Web3BridgeAccount) {
        _uiState.update { state ->
            val deduped = state.web3BridgeAccounts.filterNot {
                it.address.equals(account.address, ignoreCase = true) &&
                    it.addressPath.equals(account.addressPath, ignoreCase = true)
            }
            state.copy(web3BridgeAccounts = listOf(account) + deduped)
        }
        persistWeb3BridgeAccounts()
    }
}

private fun kotlinx.serialization.json.JsonObject.string(key: String): String? =
    this[key]?.jsonPrimitive?.contentOrNull
