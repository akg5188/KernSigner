package io.arbitrum.wallet

data class TokenInfo(
    val symbol: String,
    val name: String,
    val decimals: Int,
    val address: String? = null,
)

data class WalletChain(
    val chainId: Long,
    val chainIdHex: String,
    val slug: String,
    val displayName: String,
    val shortName: String,
    val nativeSymbol: String,
    val rpcUrl: String,
    val explorerUrl: String,
    val accentColor: Long,
    val historyLookbackBlocks: Long,
    val tokens: List<TokenInfo>,
    val coingeckoId: String? = null,
) {
    fun txUrl(txHash: String): String = "$explorerUrl/tx/${txHash.ensureHexPrefix()}"

    fun findTokenByAddress(address: String?): TokenInfo? {
        if (address.isNullOrBlank()) return null
        return tokens.firstOrNull { it.address.equals(address, ignoreCase = true) }
    }

    fun supportsSymbol(symbol: String): Boolean {
        return tokens.any { it.symbol.equals(symbol, ignoreCase = true) }
    }

    fun preferredTransferSymbol(): String {
        return tokens.firstOrNull { it.address != null }?.symbol ?: nativeSymbol
    }
}

object WalletChains {
    val ARBITRUM = WalletChain(
        chainId = 42161L,
        chainIdHex = "0xa4b1",
        slug = "arbitrum",
        displayName = "Arbitrum One",
        shortName = "Arbitrum",
        nativeSymbol = "ETH",
        rpcUrl = "https://arb1.arbitrum.io/rpc",
        explorerUrl = "https://arbiscan.io",
        accentColor = 0xFF3E63FF,
        historyLookbackBlocks = 250_000,
        tokens = listOf(
            TokenInfo("ETH", "Ether", 18),
            TokenInfo("USDC", "USD Coin", 6, "0xaf88d065e77c8cC2239327C5EDb3A432268e5831"),
            TokenInfo("USDT", "Tether USD", 6, "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9"),
        ),
        coingeckoId = "arbitrum",
    )

    val ALL = listOf(ARBITRUM)
    val DEFAULT = ARBITRUM

    fun byId(chainId: Long?): WalletChain? = ALL.firstOrNull { it.chainId == chainId }

    fun require(chainId: Long): WalletChain = byId(chainId) ?: DEFAULT

    fun supportedWalletConnectChains(): List<String> = ALL.map { "eip155:${it.chainId}" }
}

object ArbitrumConfig {
    const val CHAIN_ID = 42161L
    const val CHAIN_ID_HEX = "0xa4b1"
    const val RPC_URL = "https://arb1.arbitrum.io/rpc"
    const val EXPLORER = "https://arbiscan.io"
    val TOKENS = linkedMapOf<String, TokenInfo>().apply {
        WalletChains.ARBITRUM.tokens.forEach { token ->
            put(token.symbol, token)
        }
    }

    fun findTokenByAddress(address: String?): TokenInfo? = WalletChains.ARBITRUM.findTokenByAddress(address)
}

private fun String.ensureHexPrefix(): String = if (startsWith("0x", ignoreCase = true)) this else "0x$this"
