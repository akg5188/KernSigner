package io.arbitrum.wallet

import java.io.ByteArrayOutputStream
import java.math.BigDecimal
import java.math.RoundingMode
import java.net.URLEncoder
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import kotlin.math.ceil
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.bitcoinj.core.LegacyAddress
import org.bitcoinj.core.Transaction
import org.bitcoinj.core.SegwitAddress
import org.bitcoinj.core.Utils
import org.bitcoinj.script.ScriptBuilder
import org.bitcoinj.script.ScriptPattern
import org.json.JSONArray
import org.json.JSONObject
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private const val BITCOIN_DEFAULT_GAP_LIMIT = 20
private const val BITCOIN_MAX_DISCOVERY_INDEX = 1000
private const val BITCOIN_DISCOVERY_BATCH_SIZE = 10
private const val BITCOIN_ACTIVITY_FETCH_BATCH_SIZE = 8
private const val BITCOIN_HTTP_RETRY_PER_CANDIDATE = 2
private const val BITCOIN_DUST_THRESHOLD_SATS = 546L
private const val BITCOIN_HTTP_CACHE_TTL_MS = 30_000L
private const val BITCOIN_HOST_RATE_LIMIT_BACKOFF_MS = 180_000L
private const val BITCOIN_ELECTRUM_DISCOVERY_BATCH_SIZE = 40
private const val BITCOIN_ELECTRUM_ACTIVITY_LIMIT = 60

data class BitcoinAccountSnapshot(
    val balanceSats: Long,
    val priceUsd: Double?,
    val utxoCount: Int,
    val nextReceiveIndex: Int,
    val nextReceiveAddress: String,
    val nextChangeIndex: Int,
    val nextChangeAddress: String,
    val lastReceiveUsedIndex: Int,
    val lastChangeUsedIndex: Int,
    val receiveUsedIndices: List<Int>,
    val changeUsedIndices: List<Int>,
    val ownedAddresses: List<String>,
    val spendableUtxos: List<BitcoinSpendableUtxo>,
    val status: String,
    val activityComplete: Boolean,
    val recentActivity: List<WalletActivityItem>,
)

data class BitcoinPreparedTransfer(
    val requestPayload: String,
    val amountSats: Long,
    val feeSats: Long,
    val changeSats: Long,
    val destinationAddress: String,
    val changeAddress: String?,
    val inputCount: Int,
    val snapshot: BitcoinAccountSnapshot,
)

data class BitcoinTransactionDetail(
    val txid: String,
    val statusLabel: String,
    val fromSummary: String,
    val toSummary: String,
    val feeSats: Long,
    val blockHeight: Long?,
    val confirmations: Int?,
    val timestamp: Long,
    val inputCount: Int,
    val outputCount: Int,
    val size: Int,
    val weight: Int,
)

data class BitcoinSpendableUtxo(
    val txid: String,
    val vout: Int,
    val valueSats: Long,
    val keyMaterial: BitcoinDerivedKeyMaterial,
)

private data class BitcoinBranchDiscovery(
    val utxos: List<BitcoinSpendableUtxo>,
    val nextIndex: Int,
    val nextAddress: String,
    val lastUsedIndex: Int,
    val usedIndices: List<Int>,
    val usedAddresses: List<String>,
    val usedKeyMaterials: List<BitcoinDerivedKeyMaterial>,
)

private data class BitcoinChainActivity(
    val txid: String,
    val timestamp: Long,
    val incoming: Boolean,
    val netSats: Long,
    val counterparty: String,
    val detail: String,
    val statusLabel: String,
    val externalUrl: String,
)

private data class BitcoinCoinSelection(
    val selected: List<BitcoinSpendableUtxo>,
    val feeSats: Long,
    val changeSats: Long,
    val changeAddress: String?,
)

object BitcoinTransferService {
    private data class CachedHttpText(
        val body: String,
        val cachedAt: Long,
    )

    internal var electrumServerResolverOverride: ((String) -> BitcoinElectrumServer?)? = null
    internal var btcPriceFetcherOverride: (suspend () -> Double?)? = null

    private val allowedHosts = setOf("blockstream.info", "mempool.space", "mempool.emzy.de", "btcscan.org")
    private val client = TrustedNetwork.newPinnedClient(
        OkHttpClient.Builder()
            .callTimeout(20, TimeUnit.SECONDS)
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
    ).build()
    private val httpTextCache = ConcurrentHashMap<String, CachedHttpText>()
    private val hostCooldownUntilMs = ConcurrentHashMap<String, Long>()

    suspend fun syncAccount(
        account: BitcoinWatchAccount,
        includeActivity: Boolean = false,
        progress: ((String) -> Unit)? = null,
    ): BitcoinAccountSnapshot = withContext(Dispatchers.IO) {
        val electrumServer = resolveElectrumServer(account.prefix)
        var fallbackToQuickHttpSync = false
        if (electrumServer != null) {
            runCatching {
                syncAccountWithElectrum(
                    account = account,
                    includeActivity = includeActivity,
                    progress = progress,
                    electrumServer = electrumServer,
                )
            }.onFailure {
                fallbackToQuickHttpSync = true
                progress?.invoke("Electrum 快速同步失败，正在回退公共接口...")
            }.getOrNull()?.let { return@withContext it }
        }
        coroutineScope {
            progress?.invoke("正在扫描 BTC 收款地址...")
            val receiveDeferred = async { discoverBranch(account, branch = 0, progress = progress) }
            progress?.invoke("正在扫描 BTC 找零地址...")
            val changeDeferred = async { discoverBranch(account, branch = 1, progress = progress) }
            val priceDeferred = async { fetchBitcoinUsdPrice() }

            val receive = receiveDeferred.await()
            val change = changeDeferred.await()
            val utxos = (receive.utxos + change.utxos).sortedByDescending { it.valueSats }
            val balanceSats = utxos.sumOf { it.valueSats }
            val ownedAddresses = (receive.usedAddresses + change.usedAddresses).distinct()
            val priceUsd = priceDeferred.await()
            val activityWarning = StringBuilder()
            var activityComplete = ownedAddresses.isEmpty()
            val shouldFetchActivityInline = includeActivity && ownedAddresses.isNotEmpty() && !fallbackToQuickHttpSync
            val recentActivity = if (shouldFetchActivityInline) {
                progress?.invoke("正在拉取 BTC 最近交易...")
                runCatching {
                    fetchAccountActivity(
                        prefix = account.prefix,
                        ownedAddresses = ownedAddresses.toSet(),
                    )
                }.onSuccess {
                    activityComplete = true
                }.getOrElse {
                    activityComplete = false
                    activityWarning.append(" 最近交易暂未刷新。")
                    emptyList()
                }
            } else {
                emptyList()
            }
            val baseStatus = if (utxos.isEmpty()) {
                "已同步链上状态，当前没有可用 UTXO。"
            } else {
                "已发现 ${utxos.size} 个 UTXO，可用余额 ${formatBitcoinSats(balanceSats)}"
            }
            BitcoinAccountSnapshot(
                balanceSats = balanceSats,
                priceUsd = priceUsd,
                utxoCount = utxos.size,
                nextReceiveIndex = receive.nextIndex,
                nextReceiveAddress = receive.nextAddress,
                nextChangeIndex = change.nextIndex,
                nextChangeAddress = change.nextAddress,
                lastReceiveUsedIndex = receive.lastUsedIndex,
                lastChangeUsedIndex = change.lastUsedIndex,
                receiveUsedIndices = receive.usedIndices,
                changeUsedIndices = change.usedIndices,
                ownedAddresses = ownedAddresses,
                spendableUtxos = utxos,
                status = baseStatus + activityWarning.toString(),
                activityComplete = activityComplete,
                recentActivity = recentActivity,
            )
        }
    }

    suspend fun fetchRecentActivity(
        prefix: String,
        ownedAddresses: Set<String>,
    ): List<WalletActivityItem> = withContext(Dispatchers.IO) {
        val electrumServer = resolveElectrumServer(prefix)
        if (electrumServer != null) {
            runCatching {
                BitcoinElectrumClient(electrumServer).use { client ->
                    fetchAccountActivityWithElectrum(
                        prefix = prefix,
                        ownedAddresses = ownedAddresses,
                        client = client,
                    )
                }
            }.getOrNull()?.let { return@withContext it }
        }
        fetchAccountActivity(prefix = prefix, ownedAddresses = ownedAddresses)
    }

    private fun resolveElectrumServer(prefix: String): BitcoinElectrumServer? {
        return electrumServerResolverOverride?.invoke(prefix) ?: bitcoinElectrumServerForPrefix(prefix)
    }

    private suspend fun fetchBitcoinUsdPrice(): Double? {
        return btcPriceFetcherOverride?.invoke()
            ?: runCatching { EvmRpc.fetchCoingeckoPriceForSymbol("BTC") }.getOrNull()
    }

    suspend fun prepareTransfer(
        account: BitcoinWatchAccount,
        destinationAddress: String,
        amountText: String,
        feeRateText: String?,
        snapshot: BitcoinAccountSnapshot? = null,
    ): BitcoinPreparedTransfer = withContext(Dispatchers.IO) {
        val amountSats = parseBitcoinAmountToSats(amountText)
        require(amountSats > 0L) { "请输入大于 0 的 BTC 数量" }

        val effectiveSnapshot = snapshot ?: syncAccount(account, includeActivity = false)
        require(effectiveSnapshot.spendableUtxos.isNotEmpty()) { "当前 BTC 账户没有可用 UTXO" }

        val destinationScript = outputScriptForAddress(account.prefix, destinationAddress)
        val changeKey = deriveBitcoinKeyMaterial(account, branch = 1, index = effectiveSnapshot.nextChangeIndex)
        val feeRate = parseFeeRateOrDefault(account.prefix, feeRateText)
        val selection = selectCoins(
            utxos = effectiveSnapshot.spendableUtxos,
            amountSats = amountSats,
            feeRate = feeRate,
            destinationScriptSize = destinationScript.size,
            changeScriptSize = changeKey.scriptPubKey.size,
            prefix = account.prefix,
            changeAddress = changeKey.address,
        )

        val outputs = buildList {
            add(BitcoinTxOutput(destinationAddress, amountSats, destinationScript))
            if (selection.changeSats > 0 && selection.changeAddress != null) {
                add(BitcoinTxOutput(selection.changeAddress, selection.changeSats, changeKey.scriptPubKey))
            }
        }
        val unsignedTx = serializeUnsignedTransaction(selection.selected, outputs)
        val psbtBytes = buildPsbt(
            prefix = account.prefix,
            accountFingerprintHex = account.accountFingerprintHex,
            selectedUtxos = selection.selected,
            outputs = outputs,
            unsignedTx = unsignedTx,
        )
        val psbtBase64 = android.util.Base64.encodeToString(
            psbtBytes,
            android.util.Base64.NO_WRAP or android.util.Base64.NO_PADDING,
        )
        val requestPayload = buildSignPsbtRequest(
            prefix = account.prefix,
            requestId = "btc-psbt-${UUID.randomUUID()}",
            psbtBase64 = psbtBase64,
            destinationAddress = destinationAddress,
            amountSats = amountSats,
            feeSats = selection.feeSats,
            accountLabel = account.label,
        )

        BitcoinPreparedTransfer(
            requestPayload = requestPayload,
            amountSats = amountSats,
            feeSats = selection.feeSats,
            changeSats = selection.changeSats,
            destinationAddress = destinationAddress,
            changeAddress = selection.changeAddress,
            inputCount = selection.selected.size,
            snapshot = effectiveSnapshot,
        )
    }

    suspend fun broadcastTransaction(prefix: String, txHex: String): String = withContext(Dispatchers.IO) {
        val broadcastTargets = buildList {
            add("${bitcoinEsploraBaseUrl(prefix)}/tx")
            when (prefix.lowercase(Locale.US)) {
                "xpub", "ypub", "zpub" -> {
                    add("https://mempool.emzy.de/api/tx")
                    add("https://btcscan.org/api/tx")
                    add("https://mempool.space/api/tx")
                }
                "tpub", "upub", "vpub" -> add("https://mempool.space/testnet/api/tx")
            }
        }
        var lastError = "BTC 广播失败"
        for ((index, url) in broadcastTargets.withIndex()) {
            var shouldStop = false
            val request = TrustedNetwork.requestBuilder(url, allowedHosts)
                .post(txHex.trim().toRequestBody("text/plain; charset=utf-8".toMediaType()))
                .build()
            client.newCall(request).execute().use { response ->
                val body = response.body?.string().orEmpty().trim()
                if (response.isSuccessful) {
                    require(body.isNotBlank()) { "BTC 广播成功但未返回 txid" }
                    return@withContext body
                }
                lastError = body.ifBlank { "BTC 广播失败 (${response.code})" }
                val isRateLimited = response.code == 429 || body.contains("Too Many Requests", ignoreCase = true)
                if (!isRateLimited || index == broadcastTargets.lastIndex) {
                    shouldStop = true
                }
            }
            if (shouldStop) break
        }
        error(lastError)
    }

    suspend fun fetchRecentActivitySnapshot(
        prefix: String,
        ownedAddresses: Set<String>,
    ): List<WalletActivityItem> = withContext(Dispatchers.IO) {
        fetchRecentActivity(prefix, ownedAddresses)
    }

    suspend fun fetchTransactionDetail(
        externalUrl: String,
        txid: String,
    ): BitcoinTransactionDetail? = withContext(Dispatchers.IO) {
        if (txid.isBlank()) return@withContext null
        val cleanTxid = txid.removePrefix("0x")
        val baseUrl = externalUrl.substringBefore("/tx/").trim().ifBlank { return@withContext null }
        val tx = fetchJsonObject("$baseUrl/tx/$cleanTxid")
        val status = tx.optJSONObject("status")
        val confirmed = status?.optBoolean("confirmed") == true
        val blockHeight = status?.optLong("block_height")?.takeIf { it > 0L }
        val blockTime = status?.optLong("block_time")?.takeIf { it > 0L }?.times(1000)
            ?: System.currentTimeMillis()
        val confirmations = if (confirmed && blockHeight != null) {
            val tipHeight = fetchText("$baseUrl/blocks/tip/height").trim().toLongOrNull()
            tipHeight?.let { (it - blockHeight + 1L).coerceAtLeast(1L).toInt() }
        } else {
            0
        }
        val vin = tx.optJSONArray("vin") ?: JSONArray()
        val vout = tx.optJSONArray("vout") ?: JSONArray()
        val fromAddresses = linkedSetOf<String>()
        val toAddresses = linkedSetOf<String>()
        for (index in 0 until vin.length()) {
            val address = vin.optJSONObject(index)
                ?.optJSONObject("prevout")
                ?.optString("scriptpubkey_address")
                .orEmpty()
                .trim()
            if (address.isNotBlank()) fromAddresses += address
        }
        for (index in 0 until vout.length()) {
            val address = vout.optJSONObject(index)
                ?.optString("scriptpubkey_address")
                .orEmpty()
                .trim()
            if (address.isNotBlank()) toAddresses += address
        }
        BitcoinTransactionDetail(
            txid = cleanTxid,
            statusLabel = if (confirmed) "链上确认" else "待确认",
            fromSummary = summarizeAddresses(fromAddresses.toList()),
            toSummary = summarizeAddresses(toAddresses.toList()),
            feeSats = tx.optLong("fee", 0L),
            blockHeight = blockHeight,
            confirmations = confirmations,
            timestamp = blockTime,
            inputCount = vin.length(),
            outputCount = vout.length(),
            size = tx.optInt("size", 0),
            weight = tx.optInt("weight", 0),
        )
    }

    private suspend fun syncAccountWithElectrum(
        account: BitcoinWatchAccount,
        includeActivity: Boolean,
        progress: ((String) -> Unit)?,
        electrumServer: BitcoinElectrumServer,
    ): BitcoinAccountSnapshot = coroutineScope {
        BitcoinElectrumClient(electrumServer).use { client ->
            progress?.invoke("正在通过 Electrum 快速同步 BTC 收款地址...")
            val receive = discoverBranchWithElectrum(
                account = account,
                branch = 0,
                client = client,
                progress = progress,
            )
            progress?.invoke("正在通过 Electrum 快速同步 BTC 找零地址...")
            val change = discoverBranchWithElectrum(
                account = account,
                branch = 1,
                client = client,
                progress = progress,
            )
            val priceDeferred = async { fetchBitcoinUsdPrice() }
            val usedMaterials = (receive.usedKeyMaterials + change.usedKeyMaterials)
                .associateBy { it.address }
                .values
                .toList()
            val utxos = fetchElectrumUtxos(
                usedMaterials = usedMaterials,
                client = client,
            ).sortedByDescending { it.valueSats }
            val balanceSats = utxos.sumOf { it.valueSats }
            val ownedAddresses = usedMaterials.map { it.address }
            var activityComplete = ownedAddresses.isEmpty()
            val activityWarning = StringBuilder()
            val recentActivity = if (includeActivity && ownedAddresses.isNotEmpty()) {
                progress?.invoke("正在通过 Electrum 刷新 BTC 最近交易...")
                runCatching {
                    fetchAccountActivityWithElectrum(
                        prefix = account.prefix,
                        ownedAddresses = ownedAddresses.toSet(),
                        client = client,
                    )
                }.onSuccess {
                    activityComplete = true
                }.getOrElse {
                    activityComplete = false
                    activityWarning.append(" 最近交易暂未刷新。")
                    emptyList()
                }
            } else {
                emptyList()
            }
            val baseStatus = if (utxos.isEmpty()) {
                "已通过 Electrum 同步链上状态，当前没有可用 UTXO。"
            } else {
                "已通过 Electrum 发现 ${utxos.size} 个 UTXO，可用余额 ${formatBitcoinSats(balanceSats)}"
            }
            BitcoinAccountSnapshot(
                balanceSats = balanceSats,
                priceUsd = priceDeferred.await(),
                utxoCount = utxos.size,
                nextReceiveIndex = receive.nextIndex,
                nextReceiveAddress = receive.nextAddress,
                nextChangeIndex = change.nextIndex,
                nextChangeAddress = change.nextAddress,
                lastReceiveUsedIndex = receive.lastUsedIndex,
                lastChangeUsedIndex = change.lastUsedIndex,
                receiveUsedIndices = receive.usedIndices,
                changeUsedIndices = change.usedIndices,
                ownedAddresses = ownedAddresses,
                spendableUtxos = utxos,
                status = baseStatus + activityWarning.toString(),
                activityComplete = activityComplete,
                recentActivity = recentActivity,
            )
        }
    }

    private fun discoverBranchWithElectrum(
        account: BitcoinWatchAccount,
        branch: Int,
        client: BitcoinElectrumClient,
        progress: ((String) -> Unit)? = null,
    ): BitcoinBranchDiscovery {
        val branchLabel = if (branch == 0) "收款" else "找零"
        val knownUsedIndices = if (branch == 0) {
            account.receiveUsedIndices.toMutableSet()
        } else {
            account.changeUsedIndices.toMutableSet()
        }
        val usedMaterials = linkedMapOf<String, BitcoinDerivedKeyMaterial>()
        val lastUsedHint = if (branch == 0) account.lastReceiveUsedIndex else account.lastChangeUsedIndex
        val initialLastUsedIndex = maxOf(lastUsedHint, knownUsedIndices.maxOrNull() ?: -1)
        var index = if (initialLastUsedIndex >= 0) {
            (initialLastUsedIndex - BITCOIN_DEFAULT_GAP_LIMIT + 1).coerceAtLeast(0)
        } else {
            0
        }
        var consecutiveUnused = 0
        var lastUsedIndex = initialLastUsedIndex
        var shouldStop = false

        knownUsedIndices.sorted().forEach { usedIndex ->
            val material = deriveBitcoinKeyMaterial(account, branch = branch, index = usedIndex)
            usedMaterials[material.address] = material
        }

        while (
            index < BITCOIN_MAX_DISCOVERY_INDEX &&
            consecutiveUnused < BITCOIN_DEFAULT_GAP_LIMIT &&
            !shouldStop
        ) {
            val batchEndExclusive = minOf(index + BITCOIN_ELECTRUM_DISCOVERY_BATCH_SIZE, BITCOIN_MAX_DISCOVERY_INDEX)
            progress?.invoke("正在通过 Electrum 扫描 BTC ${branchLabel}地址 ${index}-${batchEndExclusive - 1}...")
            val materialsByIndex = (index until batchEndExclusive).associateWith { currentIndex ->
                deriveBitcoinKeyMaterial(account, branch = branch, index = currentIndex)
            }
            val historyByScriptHash = client.fetchScriptHashHistory(
                materialsByIndex.values.map { material ->
                    electrumScriptHash(material.scriptPubKey)
                }
            )
            for (currentIndex in index until batchEndExclusive) {
                val material = materialsByIndex[currentIndex] ?: continue
                val history = historyByScriptHash[electrumScriptHash(material.scriptPubKey)].orEmpty()
                val isUsed = history.isNotEmpty() || currentIndex in knownUsedIndices
                if (isUsed) {
                    knownUsedIndices += currentIndex
                    lastUsedIndex = maxOf(lastUsedIndex, currentIndex)
                    consecutiveUnused = 0
                    usedMaterials[material.address] = material
                } else {
                    consecutiveUnused += 1
                    if (consecutiveUnused >= BITCOIN_DEFAULT_GAP_LIMIT) {
                        shouldStop = true
                        break
                    }
                }
            }
            index = batchEndExclusive
        }

        val nextIndex = (lastUsedIndex + 1).coerceAtLeast(0)
        val nextAddress = deriveBitcoinKeyMaterial(account, branch = branch, index = nextIndex).address
        return BitcoinBranchDiscovery(
            utxos = emptyList(),
            nextIndex = nextIndex,
            nextAddress = nextAddress,
            lastUsedIndex = lastUsedIndex,
            usedIndices = knownUsedIndices.toList().sorted(),
            usedAddresses = usedMaterials.keys.toList(),
            usedKeyMaterials = usedMaterials.values.toList(),
        )
    }

    private fun fetchElectrumUtxos(
        usedMaterials: List<BitcoinDerivedKeyMaterial>,
        client: BitcoinElectrumClient,
    ): List<BitcoinSpendableUtxo> {
        if (usedMaterials.isEmpty()) return emptyList()
        val materialByScriptHash = usedMaterials.associateBy { material ->
            electrumScriptHash(material.scriptPubKey)
        }
        val utxosByScriptHash = client.fetchScriptHashUtxos(materialByScriptHash.keys.toList())
        return buildList {
            utxosByScriptHash.forEach { (scriptHash, utxos) ->
                val material = materialByScriptHash[scriptHash] ?: return@forEach
                utxos.forEach { utxo ->
                    add(
                        BitcoinSpendableUtxo(
                            txid = utxo.txid,
                            vout = utxo.vout,
                            valueSats = utxo.valueSats,
                            keyMaterial = material,
                        )
                    )
                }
            }
        }
    }

    private fun fetchAccountActivityWithElectrum(
        prefix: String,
        ownedAddresses: Set<String>,
        client: BitcoinElectrumClient,
    ): List<WalletActivityItem> {
        if (ownedAddresses.isEmpty()) return emptyList()
        val addressScriptHashes = ownedAddresses.associateWith { address ->
            electrumScriptHash(outputScriptForAddress(prefix, address))
        }
        val historyByScriptHash = client.fetchScriptHashHistory(addressScriptHashes.values.toList())
        val txHeights = linkedMapOf<String, Int>()
        historyByScriptHash.values.flatten().forEach { entry ->
            txHeights[entry.txid] = mergeElectrumHeights(txHeights[entry.txid], entry.height)
        }
        val selectedTxs = txHeights.entries
            .sortedWith(
                compareByDescending<Map.Entry<String, Int>> { if (it.value > 0) it.value else Int.MAX_VALUE }
                    .thenByDescending { it.key }
            )
            .take(BITCOIN_ELECTRUM_ACTIVITY_LIMIT)
        if (selectedTxs.isEmpty()) return emptyList()

        val rawTxMap = client.fetchTransactions(selectedTxs.map { it.key })
        if (rawTxMap.isEmpty()) return emptyList()

        val params = bitcoinNetworkParamsForPrefix(prefix)
        val currentTransactions = rawTxMap.mapValues { (_, rawHex) ->
            Transaction(params, hexToBytes(rawHex))
        }
        val prevTxIds = buildSet {
            currentTransactions.values.forEach { tx ->
                tx.inputs.forEach { input ->
                    if (!input.isCoinBase) {
                        add(input.outpoint.hash.toString())
                    }
                }
            }
        } - currentTransactions.keys
        val previousTransactions = client.fetchTransactions(prevTxIds.toList()).mapValues { (_, rawHex) ->
            Transaction(params, hexToBytes(rawHex))
        }
        val allTransactions = currentTransactions + previousTransactions
        val headerTimes = client.fetchBlockHeaders(selectedTxs.mapNotNull { it.value.takeIf { height -> height > 0 } }.distinct())
            .mapValues { (_, headerHex) -> parseElectrumHeaderTimestamp(headerHex) }

        return selectedTxs.mapNotNull { (txid, height) ->
            val tx = currentTransactions[txid] ?: return@mapNotNull null
            val parsed = parseElectrumTransaction(
                prefix = prefix,
                tx = tx,
                height = height,
                timestamp = headerTimes[height] ?: System.currentTimeMillis(),
                ownedAddresses = ownedAddresses,
                allTransactions = allTransactions,
            ) ?: return@mapNotNull null
            WalletActivityItem(
                id = "btc-chain-${parsed.txid}",
                chainId = WalletChains.DEFAULT.chainId,
                kind = WalletActivityKind.ONCHAIN,
                title = if (parsed.incoming) "收到 BTC" else "转出 BTC",
                subtitle = parsed.counterparty,
                detail = parsed.detail,
                amountLabel = "${if (parsed.netSats >= 0) "+" else "-"}${formatBitcoinSats(kotlin.math.abs(parsed.netSats))}",
                statusLabel = parsed.statusLabel,
                timestamp = parsed.timestamp,
                txHash = parsed.txid,
                externalUrl = parsed.externalUrl,
            )
        }.sortedByDescending { it.timestamp }
    }

    private fun parseElectrumTransaction(
        prefix: String,
        tx: Transaction,
        height: Int,
        timestamp: Long,
        ownedAddresses: Set<String>,
        allTransactions: Map<String, Transaction>,
    ): BitcoinChainActivity? {
        var receivedSats = 0L
        var spentSats = 0L
        var externalInput = ""
        var externalOutput = ""

        tx.inputs.forEach { input ->
            if (input.isCoinBase) return@forEach
            val prevTx = allTransactions[input.outpoint.hash.toString()] ?: return@forEach
            val prevOutput = prevTx.outputs.getOrNull(input.outpoint.index.toInt()) ?: return@forEach
            val address = decodeOutputAddress(prefix, prevOutput.scriptBytes)
            val value = prevOutput.value.value
            if (ownedAddresses.contains(address)) {
                spentSats += value
            } else if (externalInput.isBlank() && address.isNotBlank()) {
                externalInput = address
            }
        }

        tx.outputs.forEach { output ->
            val address = decodeOutputAddress(prefix, output.scriptBytes)
            val value = output.value.value
            if (ownedAddresses.contains(address)) {
                receivedSats += value
            } else if (externalOutput.isBlank() && address.isNotBlank()) {
                externalOutput = address
            }
        }

        val netSats = receivedSats - spentSats
        if (netSats == 0L && receivedSats == 0L && spentSats == 0L) return null

        return BitcoinChainActivity(
            txid = tx.txId.toString(),
            timestamp = timestamp,
            incoming = netSats >= 0L,
            netSats = if (netSats == 0L && receivedSats > 0L) receivedSats else netSats,
            counterparty = if (netSats >= 0L) {
                shortBitcoinCounterparty(externalInput.ifBlank { "链上账户" })
            } else {
                shortBitcoinCounterparty(externalOutput.ifBlank { "链上账户" })
            },
            detail = "${shortBitcoinCounterparty(externalInput)} -> ${shortBitcoinCounterparty(externalOutput)}",
            statusLabel = if (height > 0) "链上确认" else "待确认",
            externalUrl = "${bitcoinEsploraBaseUrl(prefix)}/tx/${tx.txId}",
        )
    }

    private fun mergeElectrumHeights(
        existing: Int?,
        candidate: Int,
    ): Int {
        if (existing == null) return candidate
        return when {
            existing > 0 && candidate > 0 -> maxOf(existing, candidate)
            existing > 0 -> existing
            candidate > 0 -> candidate
            else -> maxOf(existing, candidate)
        }
    }

    private suspend fun discoverBranch(
        account: BitcoinWatchAccount,
        branch: Int,
        progress: ((String) -> Unit)? = null,
    ): BitcoinBranchDiscovery {
        val utxos = mutableListOf<BitcoinSpendableUtxo>()
        val branchLabel = if (branch == 0) "收款" else "找零"
        val knownUsedIndices = if (branch == 0) {
            account.receiveUsedIndices.toMutableSet()
        } else {
            account.changeUsedIndices.toMutableSet()
        }
        val usedMaterials = linkedMapOf<String, BitcoinDerivedKeyMaterial>()
        val lastUsedHint = if (branch == 0) account.lastReceiveUsedIndex else account.lastChangeUsedIndex
        val initialLastUsedIndex = maxOf(lastUsedHint, knownUsedIndices.maxOrNull() ?: -1)
        var index = if (initialLastUsedIndex >= 0) {
            (initialLastUsedIndex - BITCOIN_DEFAULT_GAP_LIMIT + 1).coerceAtLeast(0)
        } else {
            0
        }
        var consecutiveUnused = 0
        var lastUsedIndex = initialLastUsedIndex

        coroutineScope {
            knownUsedIndices.sorted().chunked(BITCOIN_DISCOVERY_BATCH_SIZE).forEach { batch ->
                progress?.invoke(
                    "正在刷新 BTC ${branchLabel}地址 ${batch.first()}-${batch.last()}..."
                )
                val materials = batch.map { usedIndex ->
                    usedIndex to deriveBitcoinKeyMaterial(account, branch = branch, index = usedIndex)
                }
                val addressInfoFetches = materials.associate { (usedIndex, material) ->
                    usedIndex to async { fetchAddressInfo(account.prefix, material.address) }
                }
                materials.forEach { (usedIndex, material) ->
                    usedMaterials[material.address] = material
                    val addressInfo = addressInfoFetches[usedIndex]?.await() ?: BitcoinAddressInfo(isUsed = true, hasUnspent = false)
                    val utxoArray = if (addressInfo.hasUnspent) {
                        fetchAddressUtxos(account.prefix, material.address)
                    } else {
                        JSONArray()
                    }
                    for (position in 0 until utxoArray.length()) {
                        val utxo = utxoArray.optJSONObject(position) ?: continue
                        utxos += BitcoinSpendableUtxo(
                            txid = utxo.getString("txid"),
                            vout = utxo.getInt("vout"),
                            valueSats = utxo.getLong("value"),
                            keyMaterial = material,
                        )
                    }
                }
            }
        }

        while (
            index < BITCOIN_MAX_DISCOVERY_INDEX &&
            consecutiveUnused < BITCOIN_DEFAULT_GAP_LIMIT
        ) {
            val batchEndExclusive = minOf(index + BITCOIN_DISCOVERY_BATCH_SIZE, BITCOIN_MAX_DISCOVERY_INDEX)
            progress?.invoke(
                "正在扫描 BTC ${branchLabel}地址 ${index}-${batchEndExclusive - 1}..."
            )
            val unknownIndices = (index until batchEndExclusive).filterNot { it in knownUsedIndices }
            val materials = unknownIndices.map { currentIndex ->
                currentIndex to deriveBitcoinKeyMaterial(account, branch = branch, index = currentIndex)
            }
            val addressInfos = coroutineScope {
                materials.map { (currentIndex, material) ->
                    async {
                        Triple(currentIndex, material, fetchAddressInfo(account.prefix, material.address))
                    }
                }.awaitAll()
            }
            val utxoFetches = coroutineScope {
                addressInfos.filter { (_, _, info) -> info.hasUnspent }.associate { (_, material, _) ->
                    material.address to async { fetchAddressUtxos(account.prefix, material.address) }
                }
            }

            val addressInfoMap = addressInfos.associateBy { it.first }

            for (currentIndex in index until batchEndExclusive) {
                if (currentIndex in knownUsedIndices) {
                    consecutiveUnused = 0
                    lastUsedIndex = maxOf(lastUsedIndex, currentIndex)
                    continue
                }
                val (_, material, addressInfo) = addressInfoMap[currentIndex] ?: continue
                if (addressInfo.isUsed) {
                    knownUsedIndices += currentIndex
                    lastUsedIndex = currentIndex
                    consecutiveUnused = 0
                    usedMaterials[material.address] = material
                    val utxoArray = utxoFetches[material.address]?.await() ?: JSONArray()
                    for (position in 0 until utxoArray.length()) {
                        val utxo = utxoArray.optJSONObject(position) ?: continue
                        utxos += BitcoinSpendableUtxo(
                            txid = utxo.getString("txid"),
                            vout = utxo.getInt("vout"),
                            valueSats = utxo.getLong("value"),
                            keyMaterial = material,
                        )
                    }
                } else {
                    consecutiveUnused += 1
                    if (consecutiveUnused >= BITCOIN_DEFAULT_GAP_LIMIT) {
                        break
                    }
                }
            }
            index = batchEndExclusive
        }

        val nextIndex = (lastUsedIndex + 1).coerceAtLeast(0)
        val nextAddress = deriveBitcoinKeyMaterial(account, branch = branch, index = nextIndex).address
        return BitcoinBranchDiscovery(
            utxos = utxos,
            nextIndex = nextIndex,
            nextAddress = nextAddress,
            lastUsedIndex = lastUsedIndex,
            usedIndices = knownUsedIndices.toList().sorted(),
            usedAddresses = usedMaterials.keys.toList(),
            usedKeyMaterials = usedMaterials.values.toList(),
        )
    }

    private suspend fun fetchAddressInfo(prefix: String, address: String): BitcoinAddressInfo {
        val json = fetchJsonObject("${bitcoinEsploraBaseUrl(prefix)}/address/$address")
        val chainStats = json.optJSONObject("chain_stats") ?: JSONObject()
        val mempoolStats = json.optJSONObject("mempool_stats") ?: JSONObject()
        val fundedCount = chainStats.optLong("funded_txo_count", 0) + mempoolStats.optLong("funded_txo_count", 0)
        val spentCount = chainStats.optLong("spent_txo_count", 0) + mempoolStats.optLong("spent_txo_count", 0)
        val used = (
            fundedCount > 0 || spentCount > 0
            )
        return BitcoinAddressInfo(
            isUsed = used,
            hasUnspent = fundedCount > spentCount,
        )
    }

    private suspend fun fetchAddressUtxos(prefix: String, address: String): JSONArray {
        return fetchJsonArray("${bitcoinEsploraBaseUrl(prefix)}/address/$address/utxo")
    }

    private suspend fun fetchAddressTransactions(prefix: String, address: String): List<JSONObject> {
        val baseUrl = "${bitcoinEsploraBaseUrl(prefix)}/address/$address/txs"
        val all = mutableListOf<JSONObject>()
        var page = fetchJsonArray(baseUrl)
        while (page.length() > 0 && all.size < 120) {
            for (index in 0 until page.length()) {
                val tx = page.optJSONObject(index) ?: continue
                all += tx
            }
            if (page.length() < 25) break
            val lastTxid = page.optJSONObject(page.length() - 1)?.optString("txid").orEmpty()
            if (lastTxid.isBlank()) break
            page = fetchJsonArray("$baseUrl/chain/$lastTxid")
        }
        return all
    }

    private suspend fun fetchAccountActivity(
        prefix: String,
        ownedAddresses: Set<String>,
    ): List<WalletActivityItem> {
        if (ownedAddresses.isEmpty()) return emptyList()
        val txMap = linkedMapOf<String, JSONObject>()
        coroutineScope {
            ownedAddresses.toList().chunked(BITCOIN_ACTIVITY_FETCH_BATCH_SIZE).forEach { batch ->
                batch.map { address ->
                    async { fetchAddressTransactions(prefix, address) }
                }.awaitAll().forEach { transactions ->
                    transactions.forEach { tx ->
                        val txid = tx.optString("txid").trim()
                        if (txid.isNotBlank()) {
                            txMap.putIfAbsent(txid, tx)
                        }
                    }
                }
            }
        }
        return txMap.values.mapNotNull { tx ->
            val parsed = parseAccountTransaction(prefix, tx, ownedAddresses) ?: return@mapNotNull null
            WalletActivityItem(
                id = "btc-chain-${parsed.txid}",
                chainId = WalletChains.DEFAULT.chainId,
                kind = WalletActivityKind.ONCHAIN,
                title = if (parsed.incoming) "收到 BTC" else "转出 BTC",
                subtitle = parsed.counterparty,
                detail = parsed.detail,
                amountLabel = "${if (parsed.netSats >= 0) "+" else "-"}${formatBitcoinSats(kotlin.math.abs(parsed.netSats))}",
                statusLabel = parsed.statusLabel,
                timestamp = parsed.timestamp,
                txHash = parsed.txid,
                externalUrl = parsed.externalUrl,
            )
        }.sortedByDescending { it.timestamp }
    }

    private fun parseAccountTransaction(
        prefix: String,
        tx: JSONObject,
        ownedAddresses: Set<String>,
    ): BitcoinChainActivity? {
        val txid = tx.optString("txid").trim()
        if (txid.isBlank()) return null
        val vinArray = tx.optJSONArray("vin") ?: JSONArray()
        val voutArray = tx.optJSONArray("vout") ?: JSONArray()

        var receivedSats = 0L
        var spentSats = 0L
        var externalInput = ""
        var externalOutput = ""

        for (index in 0 until vinArray.length()) {
            val vin = vinArray.optJSONObject(index) ?: continue
            val prevout = vin.optJSONObject("prevout") ?: continue
            val address = prevout.optString("scriptpubkey_address").trim()
            val value = prevout.optLong("value", 0L)
            if (ownedAddresses.contains(address)) {
                spentSats += value
            } else if (externalInput.isBlank() && address.isNotBlank()) {
                externalInput = address
            }
        }

        for (index in 0 until voutArray.length()) {
            val vout = voutArray.optJSONObject(index) ?: continue
            val address = vout.optString("scriptpubkey_address").trim()
            val value = vout.optLong("value", 0L)
            if (ownedAddresses.contains(address)) {
                receivedSats += value
            } else if (externalOutput.isBlank() && address.isNotBlank()) {
                externalOutput = address
            }
        }

        val netSats = receivedSats - spentSats
        if (netSats == 0L && receivedSats == 0L && spentSats == 0L) return null

        val status = tx.optJSONObject("status")
        val timestamp = status?.optLong("block_time", 0L)?.takeIf { it > 0L }?.times(1000)
            ?: System.currentTimeMillis()
        val incoming = netSats >= 0L
        val detail = "${shortBitcoinCounterparty(externalInput)} -> ${shortBitcoinCounterparty(externalOutput)}"
        return BitcoinChainActivity(
            txid = txid,
            timestamp = timestamp,
            incoming = incoming,
            netSats = if (netSats == 0L && receivedSats > 0L) receivedSats else netSats,
            counterparty = if (incoming) {
                shortBitcoinCounterparty(externalInput.ifBlank { "链上账户" })
            } else {
                shortBitcoinCounterparty(externalOutput.ifBlank { "链上账户" })
            },
            detail = detail,
            statusLabel = if (status?.optBoolean("confirmed") == true) "链上确认" else "待确认",
            externalUrl = "${bitcoinEsploraBaseUrl(prefix)}/tx/$txid",
        )
    }

    private fun shortBitcoinCounterparty(address: String): String {
        val trimmed = address.trim()
        if (trimmed.isBlank()) return "链上账户"
        if (trimmed.length <= 18) return trimmed
        return "${trimmed.take(8)}...${trimmed.takeLast(6)}"
    }

    private fun summarizeAddresses(addresses: List<String>): String {
        if (addresses.isEmpty()) return "链上账户"
        if (addresses.size == 1) return addresses.first()
        val preview = addresses.take(2).joinToString("\n")
        return "$preview\n等 ${addresses.size} 个地址"
    }

    private suspend fun fetchTransactionHex(prefix: String, txid: String): ByteArray {
        return hexToBytes(fetchText("${bitcoinEsploraBaseUrl(prefix)}/tx/$txid/hex"))
    }

    private suspend fun fetchRecommendedFeeRate(prefix: String): Double {
        val json = fetchJsonObject("${bitcoinEsploraBaseUrl(prefix)}/fee-estimates")
        val fast = json.optDouble("2", Double.NaN)
        val fallback = json.optDouble("6", Double.NaN)
        val value = when {
            !fast.isNaN() && fast > 0.0 -> fast
            !fallback.isNaN() && fallback > 0.0 -> fallback
            else -> 3.0
        }
        return value.coerceAtLeast(1.0)
    }

    private suspend fun fetchJsonObject(url: String): JSONObject {
        val text = fetchText(url)
        return JSONObject(text)
    }

    private suspend fun fetchJsonArray(url: String): JSONArray {
        val text = fetchText(url)
        return JSONArray(text)
    }

    private fun bitcoinApiCandidateUrls(url: String): List<String> {
        val candidates = when {
            url.startsWith("https://blockstream.info/testnet/api/") -> {
                val suffix = url.removePrefix("https://blockstream.info/testnet/api/")
                listOf(
                    "https://mempool.space/testnet/api/$suffix",
                    url,
                )
            }
            url.startsWith("https://blockstream.info/api/") -> {
                val suffix = url.removePrefix("https://blockstream.info/api/")
                listOf(
                    "https://mempool.emzy.de/api/$suffix",
                    "https://btcscan.org/api/$suffix",
                    "https://mempool.space/api/$suffix",
                    url,
                )
            }
            else -> listOf(url)
        }.distinct()
        val now = System.currentTimeMillis()
        val (available, coolingDown) = candidates.partition { candidate ->
            val host = runCatching { java.net.URI(candidate).host.orEmpty().lowercase(Locale.US) }.getOrDefault("")
            val cooldownUntil = hostCooldownUntilMs[host] ?: 0L
            cooldownUntil <= now
        }
        return if (available.isNotEmpty()) available + coolingDown else candidates
    }

    private suspend fun fetchText(url: String): String {
        val now = System.currentTimeMillis()
        httpTextCache[url]?.takeIf { now - it.cachedAt <= BITCOIN_HTTP_CACHE_TTL_MS }?.let { cached ->
            return cached.body
        }
        val candidates = bitcoinApiCandidateUrls(url)
        var lastError = "BTC 公共接口请求失败"
        candidateLoop@ for ((index, candidateUrl) in candidates.withIndex()) {
            for (attempt in 0 until BITCOIN_HTTP_RETRY_PER_CANDIDATE) {
                val request = TrustedNetwork.requestBuilder(candidateUrl, allowedHosts).build()
                try {
                    var advanceCandidate = false
                    client.newCall(request).execute().use { response ->
                        val body = response.body?.string().orEmpty()
                        if (response.isSuccessful) {
                            val cached = CachedHttpText(body = body, cachedAt = now)
                            httpTextCache[url] = cached
                            httpTextCache[candidateUrl] = cached
                            return body
                        }
                        val isRateLimited = response.code == 429 || body.contains("Too Many Requests", ignoreCase = true)
                        if (isRateLimited) {
                            markHostRateLimited(candidateUrl)
                            lastError = "BTC 公共接口限流，请稍后重试"
                            if (attempt < BITCOIN_HTTP_RETRY_PER_CANDIDATE - 1) {
                                return@use
                            }
                            if (index != candidates.lastIndex) {
                                advanceCandidate = true
                                return@use
                            }
                            error(lastError)
                        } else {
                            lastError = body.ifBlank { "请求失败 (${response.code})" }
                            if (index != candidates.lastIndex) {
                                advanceCandidate = true
                                return@use
                            }
                            error(lastError)
                        }
                    }
                    if (advanceCandidate) {
                        continue@candidateLoop
                    }
                } catch (error: Exception) {
                    lastError = error.message?.takeIf { it.isNotBlank() } ?: "BTC 公共接口请求失败"
                    if (attempt < BITCOIN_HTTP_RETRY_PER_CANDIDATE - 1) {
                        continue
                    }
                    if (index != candidates.lastIndex) {
                        continue@candidateLoop
                    }
                    error(lastError)
                }
            }
        }
        error(lastError)
    }

    private fun markHostRateLimited(url: String) {
        val host = runCatching { java.net.URI(url).host.orEmpty().lowercase(Locale.US) }.getOrDefault("")
        if (host.isBlank()) return
        hostCooldownUntilMs[host] = System.currentTimeMillis() + BITCOIN_HOST_RATE_LIMIT_BACKOFF_MS
    }

    private suspend fun buildPsbt(
        prefix: String,
        accountFingerprintHex: String,
        selectedUtxos: List<BitcoinSpendableUtxo>,
        outputs: List<BitcoinTxOutput>,
        unsignedTx: ByteArray,
    ): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(byteArrayOf(0x70, 0x73, 0x62, 0x74, 0xff.toByte()))
        writeKeyValue(out, byteArrayOf(0x00), unsignedTx)
        writeVarInt(out, 0)

        for (utxo in selectedUtxos) {
            val inputMap = ByteArrayOutputStream()
            when (prefix.lowercase(Locale.US)) {
                "xpub", "tpub" -> {
                    writeKeyValue(inputMap, byteArrayOf(0x00), fetchTransactionHex(prefix, utxo.txid))
                }
                "ypub", "upub", "zpub", "vpub" -> {
                    writeKeyValue(inputMap, byteArrayOf(0x01), serializeWitnessUtxo(utxo))
                    if (prefix.equals("ypub", ignoreCase = true) || prefix.equals("upub", ignoreCase = true)) {
                        writeKeyValue(inputMap, byteArrayOf(0x04), buildNestedSegwitRedeemScript(utxo.keyMaterial.publicKey))
                    }
                }
                else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
            }
            writeKeyValue(
                inputMap,
                byteArrayOf(0x06) + utxo.keyMaterial.publicKey,
                buildDerivationValue(accountFingerprintHex, utxo.keyMaterial.path),
            )
            writeVarInt(inputMap, 0)
            out.write(inputMap.toByteArray())
        }

        repeat(outputs.size) {
            writeVarInt(out, 0)
        }
        return out.toByteArray()
    }

    private fun serializeUnsignedTransaction(
        utxos: List<BitcoinSpendableUtxo>,
        outputs: List<BitcoinTxOutput>,
    ): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(intToLittleEndian(2))
        writeVarInt(out, utxos.size.toLong())
        utxos.forEach { utxo ->
            out.write(hexToBytes(utxo.txid).reversedArray())
            out.write(intToLittleEndian(utxo.vout))
            writeVarInt(out, 0)
            out.write(intToLittleEndian(0xFFFFFFFD.toInt()))
        }
        writeVarInt(out, outputs.size.toLong())
        outputs.forEach { output ->
            out.write(longToLittleEndian(output.valueSats))
            writeVarBytes(out, output.scriptPubKey)
        }
        out.write(intToLittleEndian(0))
        return out.toByteArray()
    }

    private fun selectCoins(
        utxos: List<BitcoinSpendableUtxo>,
        amountSats: Long,
        feeRate: Double,
        destinationScriptSize: Int,
        changeScriptSize: Int,
        prefix: String,
        changeAddress: String,
    ): BitcoinCoinSelection {
        require(utxos.isNotEmpty()) { "没有可用 UTXO" }
        var total = 0L
        val selected = mutableListOf<BitcoinSpendableUtxo>()
        val outputSizeNoChange = serializedOutputSize(destinationScriptSize)
        val outputSizeWithChange = outputSizeNoChange + serializedOutputSize(changeScriptSize)

        utxos.forEach { utxo ->
            selected += utxo
            total += utxo.valueSats

            val feeWithChange = estimateFee(selected.size, prefix, feeRate, outputSizeWithChange)
            val changeSats = total - amountSats - feeWithChange
            if (changeSats >= BITCOIN_DUST_THRESHOLD_SATS) {
                return BitcoinCoinSelection(
                    selected = selected.toList(),
                    feeSats = feeWithChange,
                    changeSats = changeSats,
                    changeAddress = changeAddress,
                )
            }

            val feeNoChange = estimateFee(selected.size, prefix, feeRate, outputSizeNoChange)
            val remaining = total - amountSats - feeNoChange
            if (remaining >= 0) {
                return BitcoinCoinSelection(
                    selected = selected.toList(),
                    feeSats = feeNoChange + remaining,
                    changeSats = 0L,
                    changeAddress = null,
                )
            }
        }

        error("余额不足，请减少发送数量或等待更多 BTC 到账")
    }

    private fun estimateFee(
        inputCount: Int,
        prefix: String,
        feeRate: Double,
        outputsSerializedSize: Int,
    ): Long {
        val inputVbytes = when (prefix.lowercase(Locale.US)) {
            "xpub", "tpub" -> 148
            "ypub", "upub" -> 91
            "zpub", "vpub" -> 68
            else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
        }
        val vbytes = 10 + (inputCount * inputVbytes) + outputsSerializedSize
        return ceil(vbytes * feeRate).toLong().coerceAtLeast(150L)
    }

    private fun parseBitcoinAmountToSats(raw: String): Long {
        val value = raw.trim()
        require(value.isNotBlank()) { "请输入 BTC 数量" }
        val decimal = value.toBigDecimal()
        require(decimal > BigDecimal.ZERO) { "BTC 数量必须大于 0" }
        return decimal.multiply(BigDecimal(100_000_000L))
            .setScale(0, RoundingMode.DOWN)
            .longValueExact()
    }

    private suspend fun parseFeeRateOrDefault(prefix: String, raw: String?): Double {
        val candidate = raw.orEmpty().trim()
        if (candidate.isBlank()) return fetchRecommendedFeeRate(prefix)
        val parsed = candidate.toDoubleOrNull()
        require(parsed != null && parsed > 0.0) { "手续费率请输入正数 sat/vB" }
        return parsed
    }

    private fun electrumScriptHash(scriptPubKey: ByteArray): String {
        return MessageDigest.getInstance("SHA-256")
            .digest(scriptPubKey)
            .reversedArray()
            .joinToString("") { byte -> "%02x".format(byte) }
    }

    private fun parseElectrumHeaderTimestamp(headerHex: String): Long {
        val bytes = hexToBytes(headerHex)
        if (bytes.size < 72) return System.currentTimeMillis()
        val seconds = ByteBuffer.wrap(bytes, 68, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .int
            .toLong() and 0xffffffffL
        return seconds * 1000L
    }

    private fun decodeOutputAddress(prefix: String, scriptPubKey: ByteArray): String {
        val params = bitcoinNetworkParamsForPrefix(prefix)
        val script = org.bitcoinj.script.Script(scriptPubKey)
        return runCatching {
            when {
                ScriptPattern.isP2PKH(script) -> LegacyAddress.fromPubKeyHash(params, ScriptPattern.extractHashFromP2PKH(script)).toString()
                ScriptPattern.isP2SH(script) -> LegacyAddress.fromScriptHash(params, ScriptPattern.extractHashFromP2SH(script)).toString()
                ScriptPattern.isP2WPKH(script) || ScriptPattern.isP2WSH(script) -> {
                    SegwitAddress.fromHash(params, ScriptPattern.extractHashFromP2WH(script)).toString()
                }
                ScriptPattern.isP2TR(script) -> {
                    SegwitAddress.fromProgram(params, 1, ScriptPattern.extractOutputKeyFromP2TR(script)).toString()
                }
                else -> ""
            }
        }.getOrDefault("")
    }

    private fun outputScriptForAddress(prefix: String, address: String): ByteArray {
        val params = bitcoinNetworkParamsForPrefix(prefix)
        return if (address.lowercase(Locale.US).startsWith("bc1") || address.lowercase(Locale.US).startsWith("tb1")) {
            ScriptBuilder.createOutputScript(SegwitAddress.fromBech32(params, address)).program
        } else {
            ScriptBuilder.createOutputScript(LegacyAddress.fromBase58(params, address)).program
        }
    }

    private fun serializeWitnessUtxo(utxo: BitcoinSpendableUtxo): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(longToLittleEndian(utxo.valueSats))
        writeVarBytes(out, utxo.keyMaterial.scriptPubKey)
        return out.toByteArray()
    }

    private fun buildNestedSegwitRedeemScript(publicKey: ByteArray): ByteArray {
        val pubKeyHash = Utils.sha256hash160(publicKey)
        return byteArrayOf(0x00, 0x14) + pubKeyHash
    }

    private fun buildDerivationValue(accountFingerprintHex: String, path: String): ByteArray {
        val fingerprint = accountFingerprintHex.takeIf { it.length == 8 }?.let(::hexToBytes) ?: byteArrayOf(0, 0, 0, 0)
        val out = ByteArrayOutputStream()
        out.write(fingerprint)
        parseDerivationPath(path).forEach { index ->
            out.write(intToLittleEndian(index))
        }
        return out.toByteArray()
    }

    private fun parseDerivationPath(path: String): List<Int> {
        val normalized = path.trim()
        if (normalized == "m") return emptyList()
        require(normalized.startsWith("m/")) { "无效的 BIP32 路径: $path" }
        return normalized.split("/").drop(1).map { part ->
            val hardened = part.endsWith("'")
            val value = part.removeSuffix("'").toInt()
            if (hardened) value or 0x80000000.toInt() else value
        }
    }

    private fun buildSignPsbtRequest(
        prefix: String,
        requestId: String,
        psbtBase64: String,
        destinationAddress: String,
        amountSats: Long,
        feeSats: Long,
        accountLabel: String,
    ): String {
        val dataJson = JSONObject().apply {
            put("psbt", psbtBase64)
            put("to", destinationAddress)
            put("amountSats", amountSats)
            put("feeSats", feeSats)
            put("accountLabel", accountLabel)
        }
        val network = when (prefix.lowercase(Locale.US)) {
            "xpub", "ypub", "zpub" -> "bitcoin-mainnet"
            "tpub", "upub", "vpub" -> "bitcoin-testnet"
            else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
        }
        val query = listOf(
            "version" to "1.0",
            "protocol" to "BitcoinWallet",
            "network" to network,
            "requestId" to requestId,
            "data" to dataJson.toString(),
        ).joinToString("&") { (key, value) ->
            "$key=${URLEncoder.encode(value, StandardCharsets.UTF_8.name())}"
        }
        return "tp:signPsbt-$query"
    }

    private fun writeKeyValue(out: ByteArrayOutputStream, key: ByteArray, value: ByteArray) {
        writeVarBytes(out, key)
        writeVarBytes(out, value)
    }

    private fun writeVarBytes(out: ByteArrayOutputStream, value: ByteArray) {
        writeVarInt(out, value.size.toLong())
        out.write(value)
    }

    private fun writeVarInt(out: ByteArrayOutputStream, value: Long) {
        when {
            value < 0xfd -> out.write(value.toInt())
            value <= 0xffff -> {
                out.write(0xfd)
                out.write(shortToLittleEndian(value.toInt()))
            }
            value <= 0xffffffffL -> {
                out.write(0xfe)
                out.write(intToLittleEndian(value.toInt()))
            }
            else -> {
                out.write(0xff)
                out.write(longToLittleEndian(value))
            }
        }
    }

    private fun serializedOutputSize(scriptSize: Int): Int {
        return 8 + varIntSize(scriptSize.toLong()) + scriptSize
    }

    private fun varIntSize(value: Long): Int {
        return when {
            value < 0xfd -> 1
            value <= 0xffff -> 3
            value <= 0xffffffffL -> 5
            else -> 9
        }
    }

    private fun shortToLittleEndian(value: Int): ByteArray {
        return ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort(value.toShort()).array()
    }

    private fun intToLittleEndian(value: Int): ByteArray {
        return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array()
    }

    private fun longToLittleEndian(value: Long): ByteArray {
        return ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putLong(value).array()
    }

    private fun hexToBytes(raw: String): ByteArray {
        val clean = raw.trim().removePrefix("0x")
        require(clean.length % 2 == 0) { "十六进制长度无效" }
        return ByteArray(clean.length / 2) { index ->
            clean.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }
}

private data class BitcoinAddressInfo(
    val isUsed: Boolean,
    val hasUnspent: Boolean,
)

private data class BitcoinTxOutput(
    val address: String,
    val valueSats: Long,
    val scriptPubKey: ByteArray,
)
