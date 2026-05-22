package io.arbitrum.wallet

import android.graphics.Bitmap

enum class WalletTab {
    HOME,
    ACTIVITY,
    DISCOVER,
}

enum class PendingResponseType {
    BROADCAST_TX,
    BROADCAST_BTC_TX,
    SHOW_SIGNATURE,
    RETURN_RAW_TRANSACTION,
    IMPORT_WATCH_ADDRESS,
    IMPORT_WEB3_ACCOUNT,
    RETURN_WEB3_SIGNATURE,
    RETURN_ELECTRUM_PSBT,
}

enum class PreparedQrKind {
    PI_REQUEST,
    WEB3_CONNECT,
    WEB3_SIGNATURE,
    ELECTRUM_RESULT,
}

const val DEFAULT_EVM_DERIVATION_PATH = "m/44'/60'/0'/0/0"

enum class WalletActivityKind {
    OUTGOING_TX,
    SIGNATURE,
    DAPP,
    SYSTEM,
    ONCHAIN,
}

data class AssetBalanceUi(
    val symbol: String,
    val name: String,
    val amount: String,
    val contractAddress: String? = null,
    val isNative: Boolean = false,
    val priceUsd: Double? = null,
    val usdAmount: Double? = null,
)

data class ChainPortfolioUi(
    val chainId: Long,
    val assets: List<AssetBalanceUi> = emptyList(),
    val lastUpdatedAt: Long = 0L,
    val status: String = "",
)

data class TransferContact(
    val id: String,
    val name: String,
    val address: String,
    val note: String = "",
    val chainId: Long? = null,
)

data class TrustedDappEntry(
    val host: String,
    val chainId: Long,
    val address: String,
    val trustedAt: Long = 0L,
)

data class WalletActivityItem(
    val id: String,
    val chainId: Long,
    val kind: WalletActivityKind,
    val title: String,
    val subtitle: String,
    val detail: String = "",
    val amountLabel: String = "",
    val statusLabel: String = "",
    val timestamp: Long,
    val txHash: String = "",
    val externalUrl: String = "",
)

data class BitcoinDerivedAddressPreview(
    val branch: Int,
    val branchLabel: String,
    val index: Int,
    val path: String,
    val address: String,
)

data class BitcoinWatchAccount(
    val id: String,
    val label: String,
    val note: String = "",
    val xpub: String,
    val prefix: String,
    val networkLabel: String,
    val scriptTypeLabel: String,
    val accountPathHint: String,
    val sourceLabel: String = "Imported from offline-signer get-xpub",
    val importedAt: Long,
    val accountFingerprintHex: String = "",
    val receivePreview: List<BitcoinDerivedAddressPreview> = emptyList(),
    val changePreview: List<BitcoinDerivedAddressPreview> = emptyList(),
    val derivationError: String = "",
    val balanceSats: Long = 0,
    val priceUsd: Double? = null,
    val utxoCount: Int = 0,
    val nextReceiveAddress: String = "",
    val nextChangeAddress: String = "",
    val lastReceiveUsedIndex: Int = -1,
    val lastChangeUsedIndex: Int = -1,
    val receiveUsedIndices: List<Int> = emptyList(),
    val changeUsedIndices: List<Int> = emptyList(),
    val lastSyncStatus: String = "",
    val lastSyncAt: Long = 0,
    val syncing: Boolean = false,
    val recentActivity: List<WalletActivityItem> = emptyList(),
)

data class WalletUiState(
    val activeTab: WalletTab = WalletTab.HOME,
    val newAddressInput: String = "",
    val addresses: List<String> = emptyList(),
    val addressNotes: Map<String, String> = emptyMap(),
    val selectedAddress: String = "",
    val evmDerivationPath: String = DEFAULT_EVM_DERIVATION_PATH,
    val web3BridgeAccounts: List<Web3BridgeAccount> = emptyList(),
    val bitcoinImportInput: String = "",
    val bitcoinWatchAccounts: List<BitcoinWatchAccount> = emptyList(),
    val bitcoinPrototypeStatus: String = defaultBitcoinPrototypeStatus(0),
    val selectedChainId: Long = WalletChains.DEFAULT.chainId,
    val chainPortfolios: Map<Long, ChainPortfolioUi> = emptyMap(),
    val loadingBalances: Boolean = false,
    val syncingActivity: Boolean = false,
    val error: String = "",
    val info: String = "",
    val transferTo: String = "",
    val transferAmount: String = "",
    val transferToken: String = WalletChains.DEFAULT.preferredTransferSymbol(),
    val contacts: List<TransferContact> = emptyList(),
    val contactNameInput: String = "",
    val contactAddressInput: String = "",
    val contactNoteInput: String = "",
    val requestInput: String = "",
    val requestTitle: String = "",
    val requestSummary: String = "",
    val transferInfo: String = "",
    val dappInfo: String = "",
    val relayHint: String = "",
    val preparedRequestChainId: Long? = null,
    val walletConnectStatus: String = "",
    val trustedDappEntries: List<TrustedDappEntry> = emptyList(),
    val walletConnectProposal: WalletConnectProposalUi? = null,
    val walletConnectPendingRequest: WalletConnectPendingRequest? = null,
    val preparingRequest: Boolean = false,
    val signQrPages: List<String> = emptyList(),
    val signQrPageIndex: Int = 0,
    val signQrBitmap: Bitmap? = null,
    val preparedQrKind: PreparedQrKind = PreparedQrKind.PI_REQUEST,
    val pendingResponseType: PendingResponseType? = null,
    val pendingWeb3Request: Web3EthSignRequest? = null,
    val preparedBitcoinAccountId: String? = null,
    val pendingBroadcastRawTransaction: String = "",
    val pendingBroadcastBitcoinTxHex: String = "",
    val txHash: String = "",
    val txHashChainId: Long? = null,
    val txHashExplorerUrl: String = "",
    val lastSignature: String = "",
    val lastSignatureAddress: String = "",
    val activityItems: List<WalletActivityItem> = emptyList(),
)
