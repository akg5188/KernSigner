package io.arbitrum.wallet

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.math.BigDecimal
import java.math.BigInteger
import java.math.RoundingMode
import java.net.URLEncoder
import java.time.Instant
import java.util.concurrent.TimeUnit

data class TokenTransferActivity(
    val chainId: Long,
    val token: TokenInfo,
    val txHash: String,
    val blockNumber: Long,
    val timestamp: Long,
    val from: String,
    val to: String,
    val amount: BigInteger,
    val incoming: Boolean,
)

data class EvmTransactionDetail(
    val txHash: String,
    val statusLabel: String,
    val fromAddress: String,
    val toAddress: String,
    val feeWei: BigInteger?,
    val blockNumber: Long?,
    val confirmations: Long?,
    val timestamp: Long,
    val methodLabel: String,
    val gasUsed: BigInteger?,
    val gasLimit: BigInteger?,
    val baseFeePerGas: BigInteger?,
    val priorityFeePerGas: BigInteger?,
    val maxFeePerGas: BigInteger?,
    val nonce: Long?,
    val dataInput: String,
    val transferSummary: List<String> = emptyList(),
)

object EvmRpc {
    private val allowedHosts = setOf(
        "arb1.arbitrum.io",
        "arbitrum.blockscout.com",
        "coins.llama.fi",
        "api.coingecko.com",
    )
    private val client = TrustedNetwork.newPinnedClient(
        OkHttpClient.Builder()
            .connectTimeout(30, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
    ).build()

private const val JSON_MEDIA = "application/json"
private const val TRANSFER_TOPIC =
    "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"
private const val LOG_SCAN_WINDOW = 25_000L
private const val BLOCKSCOUT_ARBITRUM_API = "https://arbitrum.blockscout.com/api/v2"
private val fallbackGasPrice = BigInteger("1000000000")

    suspend fun getBalance(chain: WalletChain, address: String): BigInteger = withContext(Dispatchers.IO) {
        val resp = post(chain, buildRequest("eth_getBalance", JSONArray().put(address).put("latest")))
        parseHexResult(resp)
    }

    suspend fun getTokenBalance(chain: WalletChain, tokenAddress: String, walletAddress: String): BigInteger = withContext(Dispatchers.IO) {
        val data = "0x70a08231" + "0".repeat(24) + walletAddress.removePrefix("0x").lowercase()
        val params = JSONArray().put(JSONObject().put("to", tokenAddress).put("data", data)).put("latest")
        val resp = post(chain, buildRequest("eth_call", params))
        parseHexResult(resp)
    }

    suspend fun getNonce(chain: WalletChain, address: String): BigInteger = withContext(Dispatchers.IO) {
        val resp = post(chain, buildRequest("eth_getTransactionCount", JSONArray().put(address).put("pending")))
        parseHexResult(resp)
    }

    suspend fun sendRawTransaction(chain: WalletChain, rawTxHex: String): String = withContext(Dispatchers.IO) {
        val raw = rawTxHex.ensureHexPrefix()
        val resp = post(chain, buildRequest("eth_sendRawTransaction", JSONArray().put(raw)))
        val obj = JSONObject(resp)
        throwIfError(obj)
        obj.optString("result", "").removePrefix("0x")
    }

    suspend fun estimateGas(chain: WalletChain, tx: Map<String, String>): BigInteger = withContext(Dispatchers.IO) {
        val params = JSONObject().apply {
            tx["from"]?.let { put("from", it) }
            tx["to"]?.let { put("to", it) }
            tx["value"]?.let { put("value", it) }
            tx["data"]?.let { put("data", it) }
        }
        val resp = post(chain, buildRequest("eth_estimateGas", JSONArray().put(params)))
        parseHexResult(resp, "0x5208")
    }

    suspend fun getGasPrice(chain: WalletChain): BigInteger = withContext(Dispatchers.IO) {
        val resp = post(chain, buildRequest("eth_gasPrice", JSONArray()))
        parseHexResult(resp)
    }

    suspend fun getBlockGasParams(chain: WalletChain): Pair<BigInteger, BigInteger> = withContext(Dispatchers.IO) {
        val resp = post(chain, buildRequest("eth_getBlockByNumber", JSONArray().put("latest").put(false)))
        val obj = JSONObject(resp)
        throwIfError(obj)
        val block = obj.optJSONObject("result")
        val baseFee = block?.optString("baseFeePerGas", "0x0")
            ?.takeIf { it.isNotBlank() }
            ?.let { parseHex(it) }
            ?: fallbackGasPrice
        val maxPriority = baseFee.divide(BigInteger.TEN).coerceAtLeast(fallbackGasPrice)
        val maxFee = baseFee.multiply(BigInteger.valueOf(2)).coerceAtLeast(fallbackGasPrice.multiply(BigInteger.valueOf(2)))
        maxPriority to maxFee
    }

    suspend fun getRecentTokenTransfers(
        chain: WalletChain,
        walletAddress: String,
        limit: Int = 10,
    ): List<TokenTransferActivity> = withContext(Dispatchers.IO) {
        val tokens = chain.tokens.filter { it.address != null }
        if (tokens.isEmpty()) return@withContext emptyList()

        val latestBlock = getBlockNumber(chain)
        val earliestBlock = (latestBlock - chain.historyLookbackBlocks).coerceAtLeast(0)
        val encodedAddress = "0x" + walletAddress.removePrefix("0x").lowercase().padStart(64, '0')
        val transfers = linkedMapOf<String, TokenTransferActivity>()
        var rangeEnd = latestBlock

        while (rangeEnd >= earliestBlock && transfers.size < limit * 6) {
            val rangeStart = (rangeEnd - LOG_SCAN_WINDOW + 1).coerceAtLeast(earliestBlock)
            tokens.forEach { token ->
                val contract = token.address ?: return@forEach
                fetchTokenLogs(chain, contract, rangeStart, rangeEnd, JSONArray().put(TRANSFER_TOPIC).put(encodedAddress))
                    .forEach { log ->
                        val txHash = log.optString("transactionHash")
                        val blockNumber = parseHex(log.optString("blockNumber", "0x0")).toLong()
                        val amount = parseHex(log.optString("data", "0x0"))
                        val to = topicToAddress(log.optJSONArray("topics")?.optString(2))
                        transfers["$txHash-out-$contract"] = TokenTransferActivity(
                            chainId = chain.chainId,
                            token = token,
                            txHash = txHash,
                            blockNumber = blockNumber,
                            timestamp = 0L,
                            from = walletAddress,
                            to = to,
                            amount = amount,
                            incoming = false,
                        )
                    }
                fetchTokenLogs(chain, contract, rangeStart, rangeEnd, JSONArray().put(TRANSFER_TOPIC).put(JSONObject.NULL).put(encodedAddress))
                    .forEach { log ->
                        val txHash = log.optString("transactionHash")
                        val blockNumber = parseHex(log.optString("blockNumber", "0x0")).toLong()
                        val amount = parseHex(log.optString("data", "0x0"))
                        val from = topicToAddress(log.optJSONArray("topics")?.optString(1))
                        transfers["$txHash-in-$contract"] = TokenTransferActivity(
                            chainId = chain.chainId,
                            token = token,
                            txHash = txHash,
                            blockNumber = blockNumber,
                            timestamp = 0L,
                            from = from,
                            to = walletAddress,
                            amount = amount,
                            incoming = true,
                        )
                    }
            }
            if (rangeStart == earliestBlock) break
            rangeEnd = rangeStart - 1
        }

        val blockTimestamps = transfers.values
            .map { it.blockNumber }
            .distinct()
            .associateWith { getBlockTimestamp(chain, it) }

        transfers.values
            .map { it.copy(timestamp = blockTimestamps[it.blockNumber] ?: System.currentTimeMillis()) }
            .sortedByDescending { it.timestamp }
            .take(limit)
    }

    suspend fun getRecentAddressActivity(
        chain: WalletChain,
        walletAddress: String,
        limit: Int = 40,
    ): List<WalletActivityItem> = withContext(Dispatchers.IO) {
        if (chain.chainId != WalletChains.ARBITRUM.chainId) {
            return@withContext getRecentTokenTransfers(chain, walletAddress, limit).map { transfer ->
                val counterparty = if (transfer.incoming) transfer.from else transfer.to
                WalletActivityItem(
                    id = "onchain-${transfer.txHash}-${transfer.token.symbol}-${transfer.incoming}",
                    chainId = chain.chainId,
                    kind = WalletActivityKind.ONCHAIN,
                    title = if (transfer.incoming) "收到 ${transfer.token.symbol}" else "转出 ${transfer.token.symbol}",
                    subtitle = counterparty.ifBlank { "链上账户" },
                    detail = "${shortAddressForHistory(transfer.from)} -> ${shortAddressForHistory(transfer.to)}",
                    amountLabel = "${if (transfer.incoming) "+" else "-"}${formatDisplayUnits(transfer.amount, transfer.token.decimals)} ${transfer.token.symbol}",
                    statusLabel = "链上记录",
                    timestamp = transfer.timestamp,
                    txHash = transfer.txHash,
                    externalUrl = chain.txUrl(transfer.txHash),
                )
            }
        }

        runCatching {
            fetchBlockscoutAddressActivity(chain, walletAddress, limit)
        }.getOrElse {
            getRecentTokenTransfers(chain, walletAddress, limit).map { transfer ->
                val counterparty = if (transfer.incoming) transfer.from else transfer.to
                WalletActivityItem(
                    id = "onchain-${transfer.txHash}-${transfer.token.symbol}-${transfer.incoming}",
                    chainId = chain.chainId,
                    kind = WalletActivityKind.ONCHAIN,
                    title = if (transfer.incoming) "收到 ${transfer.token.symbol}" else "转出 ${transfer.token.symbol}",
                    subtitle = counterparty.ifBlank { "链上账户" },
                    detail = "${shortAddressForHistory(transfer.from)} -> ${shortAddressForHistory(transfer.to)}",
                    amountLabel = "${if (transfer.incoming) "+" else "-"}${formatDisplayUnits(transfer.amount, transfer.token.decimals)} ${transfer.token.symbol}",
                    statusLabel = "链上记录",
                    timestamp = transfer.timestamp,
                    txHash = transfer.txHash,
                    externalUrl = chain.txUrl(transfer.txHash),
                )
            }
        }
    }

    suspend fun getTransactionDetail(
        chain: WalletChain,
        txHash: String,
    ): EvmTransactionDetail? = withContext(Dispatchers.IO) {
        if (txHash.isBlank()) return@withContext null
        if (chain.chainId != WalletChains.ARBITRUM.chainId) return@withContext null
        fetchBlockscoutTransactionDetail(txHash.ensureHexPrefix(), chain)
    }

    private fun fetchBlockscoutAddressActivity(
        chain: WalletChain,
        walletAddress: String,
        limit: Int,
    ): List<WalletActivityItem> {
        val nativeItems = fetchBlockscoutPagedItems("/addresses/${walletAddress}/transactions")
            .mapNotNull { item ->
                val txHash = item.optString("hash").trim()
                val value = item.optString("value", "0").trim().ifBlank { "0" }
                val valueWei = value.toBigIntegerOrNull() ?: BigInteger.ZERO
                if (txHash.isBlank() || valueWei <= BigInteger.ZERO) return@mapNotNull null
                val from = item.optJSONObject("from")?.optString("hash").orEmpty()
                val to = item.optJSONObject("to")?.optString("hash").orEmpty()
                val incoming = to.equals(walletAddress, ignoreCase = true)
                WalletActivityItem(
                    id = "blockscout-native-$txHash-$incoming",
                    chainId = chain.chainId,
                    kind = WalletActivityKind.ONCHAIN,
                    title = if (incoming) "收到 ${chain.nativeSymbol}" else "转出 ${chain.nativeSymbol}",
                    subtitle = (if (incoming) from else to).ifBlank { "链上账户" },
                    detail = "${shortAddressForHistory(from)} -> ${shortAddressForHistory(to)}",
                    amountLabel = "${if (incoming) "+" else "-"}${formatDisplayUnits(valueWei, 18)} ${chain.nativeSymbol}",
                    statusLabel = "链上记录",
                    timestamp = parseBlockscoutTimestamp(item.optString("timestamp")),
                    txHash = txHash,
                    externalUrl = chain.txUrl(txHash),
                )
            }

        val tokenItems = fetchBlockscoutPagedItems("/addresses/${walletAddress}/token-transfers")
            .mapNotNull { item ->
                val txHash = item.optString("transaction_hash").trim()
                if (txHash.isBlank()) return@mapNotNull null
                val token = item.optJSONObject("token")
                val symbol = token?.optString("symbol").orEmpty().trim().ifBlank { return@mapNotNull null }
                if (!chain.supportsSymbol(symbol)) return@mapNotNull null
                val decimals = token?.optString("decimals")?.toIntOrNull()
                    ?: chain.tokens.firstOrNull { it.symbol.equals(symbol, ignoreCase = true) }?.decimals
                    ?: 18
                val total = item.optString("total", "0").trim().ifBlank { "0" }
                val amount = total.toBigIntegerOrNull() ?: BigInteger.ZERO
                val from = item.optJSONObject("from")?.optString("hash").orEmpty()
                val to = item.optJSONObject("to")?.optString("hash").orEmpty()
                val incoming = to.equals(walletAddress, ignoreCase = true)
                WalletActivityItem(
                    id = "blockscout-token-$txHash-$symbol-$incoming",
                    chainId = chain.chainId,
                    kind = WalletActivityKind.ONCHAIN,
                    title = if (incoming) "收到 $symbol" else "转出 $symbol",
                    subtitle = (if (incoming) from else to).ifBlank { "链上账户" },
                    detail = "${shortAddressForHistory(from)} -> ${shortAddressForHistory(to)}",
                    amountLabel = "${if (incoming) "+" else "-"}${formatDisplayUnits(amount, decimals)} $symbol",
                    statusLabel = "链上记录",
                    timestamp = parseBlockscoutTimestamp(item.optString("timestamp")),
                    txHash = txHash,
                    externalUrl = chain.txUrl(txHash),
                )
            }

        return (nativeItems + tokenItems)
            .sortedByDescending { it.timestamp }
            .distinctBy { "${it.txHash}-${it.amountLabel}" }
            .take(limit)
    }

    private fun fetchBlockscoutTransactionDetail(
        txHash: String,
        chain: WalletChain,
    ): EvmTransactionDetail {
        val json = getJson("$BLOCKSCOUT_ARBITRUM_API/transactions/$txHash")
        val transferSummary = buildList {
            val transfers = json.optJSONArray("token_transfers") ?: JSONArray()
            for (index in 0 until transfers.length()) {
                val item = transfers.optJSONObject(index) ?: continue
                val token = item.optJSONObject("token")
                val symbol = token?.optString("symbol").orEmpty().trim()
                if (!chain.supportsSymbol(symbol)) continue
                val decimals = token?.optString("decimals")?.toIntOrNull()
                    ?: chain.tokens.firstOrNull { it.symbol.equals(symbol, ignoreCase = true) }?.decimals
                    ?: 18
                val total = item.optString("total", "0").trim().ifBlank { "0" }
                val amount = total.toBigIntegerOrNull() ?: BigInteger.ZERO
                val from = item.optJSONObject("from")?.optString("hash").orEmpty()
                val to = item.optJSONObject("to")?.optString("hash").orEmpty()
                add("${formatDisplayUnits(amount, decimals)} $symbol · ${shortAddressForHistory(from)} -> ${shortAddressForHistory(to)}")
            }
        }.take(4)
        return EvmTransactionDetail(
            txHash = json.optString("hash").ifBlank { txHash },
            statusLabel = when {
                json.optString("result").equals("success", ignoreCase = true) -> "链上确认"
                json.optString("status").equals("ok", ignoreCase = true) -> "链上确认"
                else -> "待确认"
            },
            fromAddress = json.optJSONObject("from")?.optString("hash").orEmpty(),
            toAddress = json.optJSONObject("to")?.optString("hash").orEmpty(),
            feeWei = json.optString("fee").trim().toBigIntegerOrNull(),
            blockNumber = json.optLong("block_number").takeIf { it > 0 },
            confirmations = json.optLong("confirmations").takeIf { it > 0 },
            timestamp = parseBlockscoutTimestamp(json.optString("timestamp")),
            methodLabel = json.optString("method").ifBlank {
                json.optJSONObject("decoded_input")?.optString("method_call").orEmpty()
            }.ifBlank { "链上交易" },
            gasUsed = json.optString("gas_used").trim().toBigIntegerOrNull(),
            gasLimit = json.optString("gas_limit").trim().toBigIntegerOrNull(),
            baseFeePerGas = json.optString("base_fee_per_gas").trim().toBigIntegerOrNull(),
            priorityFeePerGas = json.optString("priority_fee").trim().toBigIntegerOrNull(),
            maxFeePerGas = json.optString("max_fee_per_gas").trim().toBigIntegerOrNull(),
            nonce = json.optLong("nonce").takeIf { it >= 0 },
            dataInput = json.optString("raw_input").ifBlank { "0x" },
            transferSummary = transferSummary,
        )
    }

    private fun fetchBlockscoutPagedItems(path: String, pageLimit: Int = 5): List<JSONObject> {
        val items = mutableListOf<JSONObject>()
        var nextParams: JSONObject? = null
        for (page in 0 until pageLimit) {
            val url = buildBlockscoutUrl(path, nextParams)
            val response = getJson(url)
            val pageItems = response.optJSONArray("items") ?: JSONArray()
            for (index in 0 until pageItems.length()) {
                pageItems.optJSONObject(index)?.let(items::add)
            }
            nextParams = response.optJSONObject("next_page_params")
            if (nextParams == null) break
        }
        return items
    }

    private fun buildBlockscoutUrl(path: String, nextParams: JSONObject?): String {
        if (nextParams == null) return "$BLOCKSCOUT_ARBITRUM_API$path"
        val query = buildList {
            val iterator = nextParams.keys()
            while (iterator.hasNext()) {
                val key = iterator.next()
                val value = nextParams.opt(key)?.toString().orEmpty()
                if (value.isNotBlank()) {
                    add("${URLEncoder.encode(key, "UTF-8")}=${URLEncoder.encode(value, "UTF-8")}")
                }
            }
        }.joinToString("&")
        return if (query.isBlank()) "$BLOCKSCOUT_ARBITRUM_API$path" else "$BLOCKSCOUT_ARBITRUM_API$path?$query"
    }

    private fun getJson(url: String): JSONObject {
        val request = TrustedNetwork.requestBuilder(url, allowedHosts)
            .header("User-Agent", "Mozilla/5.0")
            .header("Accept", "application/json")
            .get()
            .build()
        client.newCall(request).execute().use { response ->
            require(response.isSuccessful) { "Blockscout 请求失败: ${response.code}" }
            val body = response.body?.string().orEmpty()
            return JSONObject(body)
        }
    }

    private fun parseBlockscoutTimestamp(value: String): Long {
        return runCatching { Instant.parse(value).toEpochMilli() }.getOrElse { System.currentTimeMillis() }
    }

    private fun formatDisplayUnits(value: BigInteger, decimals: Int): String {
        if (decimals <= 0) return value.toString()
        val divisor = BigDecimal.TEN.pow(decimals)
        val decimalValue = BigDecimal(value).divide(divisor, decimals.coerceAtMost(18), RoundingMode.DOWN)
        val stripped = decimalValue.stripTrailingZeros()
        return stripped.toPlainString()
    }

    private fun shortAddressForHistory(address: String): String {
        if (address.isBlank()) return "-"
        return if (address.length <= 14) address else "${address.take(8)}...${address.takeLast(6)}"
    }

    private fun fetchTokenLogs(
        chain: WalletChain,
        tokenAddress: String,
        fromBlock: Long,
        toBlock: Long,
        topics: JSONArray,
    ): List<JSONObject> {
        val filter = JSONObject()
            .put("address", tokenAddress)
            .put("fromBlock", "0x${fromBlock.toString(16)}")
            .put("toBlock", "0x${toBlock.toString(16)}")
            .put("topics", topics)
        val resp = post(chain, buildRequest("eth_getLogs", JSONArray().put(filter)))
        val obj = JSONObject(resp)
        throwIfError(obj)
        val array = obj.optJSONArray("result") ?: return emptyList()
        return buildList {
            for (index in 0 until array.length()) {
                array.optJSONObject(index)?.let(::add)
            }
        }
    }

    private fun getBlockNumber(chain: WalletChain): Long {
        val resp = post(chain, buildRequest("eth_blockNumber", JSONArray()))
        return parseHexResult(resp).toLong()
    }

    private fun getBlockTimestamp(chain: WalletChain, blockNumber: Long): Long {
        val params = JSONArray().put("0x${blockNumber.toString(16)}").put(false)
        val resp = post(chain, buildRequest("eth_getBlockByNumber", params))
        val obj = JSONObject(resp)
        throwIfError(obj)
        val ts = obj.optJSONObject("result")?.optString("timestamp", "0x0").orEmpty()
        val seconds = parseHex(ts).toLong()
        return if (seconds > 0) seconds * 1000 else System.currentTimeMillis()
    }

    private fun buildRequest(method: String, params: JSONArray): String {
        return JSONObject()
            .put("jsonrpc", "2.0")
            .put("method", method)
            .put("params", params)
            .put("id", 1)
            .toString()
    }

    private fun parseHexResult(response: String, default: String = "0x0"): BigInteger {
        val obj = JSONObject(response)
        throwIfError(obj)
        val result = obj.optString("result", default)
        if (result == "0x" || result.isEmpty()) return BigInteger.ZERO
        return parseHex(result)
    }

    private fun parseHex(value: String): BigInteger {
        val normalized = value.removePrefix("0x").removePrefix("0X").ifBlank { "0" }
        return BigInteger(normalized, 16)
    }

    private fun topicToAddress(topic: String?): String {
        val value = topic.orEmpty().removePrefix("0x")
        if (value.length < 40) return ""
        return "0x" + value.takeLast(40)
    }

    private fun throwIfError(obj: JSONObject) {
        if (!obj.has("error")) return
        val error = obj.optJSONObject("error")
        throw RuntimeException(error?.optString("message", "RPC error") ?: "RPC error")
    }

    private fun post(chain: WalletChain, body: String): String {
        val request = TrustedNetwork.requestBuilder(chain.rpcUrl, allowedHosts)
            .post(body.toRequestBody(JSON_MEDIA.toMediaType()))
            .build()
        val response = client.newCall(request).execute()
        if (!response.isSuccessful) throw RuntimeException("RPC failed: ${response.code}")
        return response.body?.string() ?: throw RuntimeException("Empty response")
    }

    private val coingeckoSymbolMap = mapOf(
        "BTC" to "bitcoin",
        "ETH" to "ethereum",
        "USDC" to "usd-coin",
        "USDT" to "tether",
    )

    suspend fun getUsdPrice(chain: WalletChain, tokenAddress: String?): Double? = withContext(Dispatchers.IO) {
        val normalized = tokenAddress
            ?.removePrefix("0x")
            ?.lowercase()
            ?.let { if (it.isBlank()) "0" else it }
            ?: "0"
        val addr = "0x$normalized"
        val slug = "${chain.slug}:$addr"
        val url = "https://coins.llama.fi/prices/current/$slug"
        return@withContext try {
            val response = client.newCall(
                TrustedNetwork.requestBuilder(url, allowedHosts).get().build()
            ).execute()
            val body = response.body?.string().orEmpty()
            val coins = JSONObject(body).optJSONObject("coins") ?: return@withContext null
            val coin = coins.optJSONObject(slug) ?: return@withContext null
            val price = coin.optDouble("price", Double.NaN)
            if (!price.isNaN()) price else fetchCoingeckoPrice(chain)
        } catch (_: Throwable) {
            fetchCoingeckoPrice(chain)
        }
    }

    private suspend fun fetchCoingeckoPrice(chain: WalletChain): Double? = withContext(Dispatchers.IO) {
        val id = chain.coingeckoId ?: return@withContext null
        val encoded = java.net.URLEncoder.encode(id, "UTF-8")
        val url = "https://api.coingecko.com/api/v3/simple/price?ids=$encoded&vs_currencies=usd"
        return@withContext try {
            val response = client.newCall(
                TrustedNetwork.requestBuilder(url, allowedHosts).get().build()
            ).execute()
            val body = response.body?.string().orEmpty()
            val obj = JSONObject(body).optJSONObject(id) ?: return@withContext null
            val price = obj.optDouble("usd", Double.NaN)
            if (price.isNaN()) null else price
        } catch (_: Throwable) {
            null
        }
    }

    suspend fun fetchCoingeckoPriceForSymbol(symbol: String): Double? = withContext(Dispatchers.IO) {
        val id = coingeckoSymbolMap[symbol.uppercase()] ?: return@withContext null
        val encoded = java.net.URLEncoder.encode(id, "UTF-8")
        val url = "https://api.coingecko.com/api/v3/simple/price?ids=$encoded&vs_currencies=usd"
        return@withContext try {
            val response = client.newCall(
                TrustedNetwork.requestBuilder(url, allowedHosts).get().build()
            ).execute()
            val body = response.body?.string().orEmpty()
            val obj = JSONObject(body).optJSONObject(id) ?: return@withContext null
            val price = obj.optDouble("usd", Double.NaN)
            if (price.isNaN()) null else price
        } catch (_: Throwable) {
            null
        }
    }
}

object ArbitrumRpc {
    suspend fun getBalance(address: String): BigInteger = EvmRpc.getBalance(WalletChains.ARBITRUM, address)
    suspend fun getTokenBalance(tokenAddress: String, walletAddress: String): BigInteger =
        EvmRpc.getTokenBalance(WalletChains.ARBITRUM, tokenAddress, walletAddress)

    suspend fun getNonce(address: String): BigInteger = EvmRpc.getNonce(WalletChains.ARBITRUM, address)
    suspend fun sendRawTransaction(rawTxHex: String): String = EvmRpc.sendRawTransaction(WalletChains.ARBITRUM, rawTxHex)
    suspend fun estimateGas(tx: Map<String, String>): BigInteger = EvmRpc.estimateGas(WalletChains.ARBITRUM, tx)
    suspend fun getGasPrice(): BigInteger = EvmRpc.getGasPrice(WalletChains.ARBITRUM)
    suspend fun getBlockGasParams(): Pair<BigInteger, BigInteger> = EvmRpc.getBlockGasParams(WalletChains.ARBITRUM)
}

private fun String.ensureHexPrefix(): String = if (startsWith("0x", ignoreCase = true)) this else "0x$this"
