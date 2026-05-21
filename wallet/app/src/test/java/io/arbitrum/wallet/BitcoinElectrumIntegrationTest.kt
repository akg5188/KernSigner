package io.arbitrum.wallet

import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.Closeable
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.math.BigInteger
import java.net.ServerSocket
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonPrimitive
import org.bitcoinj.core.Coin
import org.bitcoinj.core.ECKey
import org.bitcoinj.core.LegacyAddress
import org.bitcoinj.core.NetworkParameters
import org.bitcoinj.core.Sha256Hash
import org.bitcoinj.core.Transaction
import org.bitcoinj.crypto.ChildNumber
import org.bitcoinj.crypto.HDKeyDerivation
import org.bitcoinj.params.MainNetParams
import org.bitcoinj.script.Script
import org.bitcoinj.script.ScriptBuilder
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BitcoinElectrumIntegrationTest {
    private var mockServer: MockElectrumServer? = null

    @After
    fun tearDown() {
        mockServer?.close()
        mockServer = null
        BitcoinTransferService.electrumServerResolverOverride = null
        BitcoinTransferService.btcPriceFetcherOverride = null
    }

    @Test
    fun syncAccount_withMockElectrum_returnsBalanceUtxosAndRecentActivity() = runBlocking {
        val account = createWatchAccount()
        val fixture = buildFixture(account)
        startMockServer(fixture)
        BitcoinElectrumClient(
            BitcoinElectrumServer(
                host = "127.0.0.1",
                sslPort = mockServer!!.port,
                label = "Mock Electrum Probe",
                useTls = false,
            )
        ).use { client ->
            client.fetchScriptHashHistory(listOf(electrumScriptHash(fixture.receive0.scriptPubKey)))
            client.fetchScriptHashUtxos(listOf(electrumScriptHash(fixture.change1.scriptPubKey)))
        }

        val snapshot = BitcoinTransferService.syncAccount(
            account = account,
            includeActivity = true,
            progress = {},
        )

        assertEquals(100_000L, snapshot.balanceSats)
        assertEquals(1, snapshot.utxoCount)
        assertEquals(3, snapshot.nextReceiveIndex)
        assertEquals(2, snapshot.nextChangeIndex)
        assertEquals(2, snapshot.lastReceiveUsedIndex)
        assertEquals(1, snapshot.lastChangeUsedIndex)
        assertEquals(setOf(0, 2), snapshot.receiveUsedIndices.toSet())
        assertEquals(setOf(1), snapshot.changeUsedIndices.toSet())
        assertEquals(
            setOf(fixture.receive0.address, fixture.receive2.address, fixture.change1.address),
            snapshot.ownedAddresses.toSet(),
        )
        assertEquals(fixture.spendTx.txId.toString(), snapshot.spendableUtxos.single().txid)
        assertEquals(100_000L, snapshot.spendableUtxos.single().valueSats)
        assertTrue(snapshot.activityComplete)
        assertEquals(3, snapshot.recentActivity.size)
        assertEquals(fixture.spendTx.txId.toString(), snapshot.recentActivity.first().txHash)
        assertTrue(snapshot.recentActivity.any { it.title == "收到 BTC" && it.txHash == fixture.fundingTx.txId.toString() })
        assertTrue(snapshot.recentActivity.any { it.title == "收到 BTC" && it.txHash == fixture.historyOnlyTx.txId.toString() })
        assertTrue(snapshot.recentActivity.any { it.title == "转出 BTC" && it.amountLabel.startsWith("-") })
        assertTrue(snapshot.status.contains("Electrum"))
    }

    @Test
    fun fetchRecentActivity_withMockElectrum_matchesSyncPath() = runBlocking {
        val account = createWatchAccount()
        val fixture = buildFixture(account)
        startMockServer(fixture)

        val fastSnapshot = BitcoinTransferService.syncAccount(
            account = account,
            includeActivity = false,
            progress = {},
        )
        val recentActivity = BitcoinTransferService.fetchRecentActivity(
            prefix = account.prefix,
            ownedAddresses = fastSnapshot.ownedAddresses.toSet(),
        )

        assertEquals(100_000L, fastSnapshot.balanceSats)
        assertTrue(!fastSnapshot.activityComplete)
        assertEquals(3, recentActivity.size)
        assertEquals(fixture.spendTx.txId.toString(), recentActivity.first().txHash)
    }

    private fun startMockServer(fixture: ElectrumFixture) {
        mockServer = MockElectrumServer(fixture)
        BitcoinTransferService.electrumServerResolverOverride = { prefix ->
            if (prefix.equals("xpub", ignoreCase = true)) {
                BitcoinElectrumServer(
                    host = "127.0.0.1",
                    sslPort = mockServer!!.port,
                    label = "Mock Electrum",
                    useTls = false,
                )
            } else {
                null
            }
        }
        BitcoinTransferService.btcPriceFetcherOverride = { 65_000.0 }
    }

    private fun createWatchAccount(): BitcoinWatchAccount {
        val params = MainNetParams.get()
        val seedBytes = hexToBytes("000102030405060708090a0b0c0d0e0f")
        val master = HDKeyDerivation.createMasterPrivateKey(seedBytes)
        val purpose = HDKeyDerivation.deriveChildKey(master, ChildNumber(44, true))
        val coinType = HDKeyDerivation.deriveChildKey(purpose, ChildNumber(0, true))
        val accountKey = HDKeyDerivation.deriveChildKey(coinType, ChildNumber(0, true))
        val xpub = accountKey.serializePubB58(params)
        val parsed = parseBitcoinWatchAccountImport(xpub) ?: error("test xpub parse failed")
        return enrichBitcoinWatchAccount(
            BitcoinWatchAccount(
                id = "btc-electrum-test",
                label = "BTC Electrum Test",
                xpub = xpub,
                prefix = parsed.prefix,
                networkLabel = parsed.networkLabel,
                scriptTypeLabel = parsed.scriptTypeLabel,
                accountPathHint = parsed.accountPathHint,
                importedAt = 0L,
            )
        )
    }

    private fun buildFixture(account: BitcoinWatchAccount): ElectrumFixture {
        val params = MainNetParams.get()
        val receive0 = deriveBitcoinKeyMaterial(account, branch = 0, index = 0)
        val receive2 = deriveBitcoinKeyMaterial(account, branch = 0, index = 2)
        val change1 = deriveBitcoinKeyMaterial(account, branch = 1, index = 1)

        val externalReceiveAddress = LegacyAddress.fromKey(params, ECKey.fromPrivate(BigInteger.valueOf(1111L)))
        val externalSpendAddress = LegacyAddress.fromKey(params, ECKey.fromPrivate(BigInteger.valueOf(2222L)))

        val historyOnlyTx = buildCoinbaseLikeTx(
            params = params,
            outputPlans = listOf(
                TxOutputPlan(30_000L, receive2.scriptPubKey),
                TxOutputPlan(10_000L, ScriptBuilder.createOutputScript(externalReceiveAddress).program),
            ),
        )
        val fundingTx = buildCoinbaseLikeTx(
            params = params,
            outputPlans = listOf(
                TxOutputPlan(150_000L, receive0.scriptPubKey),
                TxOutputPlan(25_000L, ScriptBuilder.createOutputScript(externalReceiveAddress).program),
            ),
        )
        val spendTx = Transaction(params).apply {
            setVersion(2)
            addInput(fundingTx.getOutput(0))
            addOutput(Coin.valueOf(40_000L), externalSpendAddress)
            addOutput(Coin.valueOf(100_000L), Script(change1.scriptPubKey))
        }

        val histories = mapOf(
            electrumScriptHash(receive0.scriptPubKey) to listOf(
                ElectrumHistoryRecord(fundingTx.txId.toString(), 100),
                ElectrumHistoryRecord(spendTx.txId.toString(), 120),
            ),
            electrumScriptHash(receive2.scriptPubKey) to listOf(
                ElectrumHistoryRecord(historyOnlyTx.txId.toString(), 90),
            ),
            electrumScriptHash(change1.scriptPubKey) to listOf(
                ElectrumHistoryRecord(spendTx.txId.toString(), 120),
            ),
        )
        val utxos = mapOf(
            electrumScriptHash(change1.scriptPubKey) to listOf(
                ElectrumUtxoRecord(
                    txid = spendTx.txId.toString(),
                    vout = 1,
                    valueSats = 100_000L,
                    height = 120,
                )
            )
        )
        val transactions = mapOf(
            historyOnlyTx.txId.toString() to historyOnlyTx.toHexString(),
            fundingTx.txId.toString() to fundingTx.toHexString(),
            spendTx.txId.toString() to spendTx.toHexString(),
        )
        val headers = mapOf(
            90 to buildHeaderHex(timestampSeconds = 1_700_000_090L),
            100 to buildHeaderHex(timestampSeconds = 1_700_000_100L),
            120 to buildHeaderHex(timestampSeconds = 1_700_000_120L),
        )

        return ElectrumFixture(
            receive0 = receive0,
            receive2 = receive2,
            change1 = change1,
            historyOnlyTx = historyOnlyTx,
            fundingTx = fundingTx,
            spendTx = spendTx,
            histories = histories,
            utxos = utxos,
            transactions = transactions,
            headers = headers,
        )
    }

    private fun buildCoinbaseLikeTx(
        params: NetworkParameters,
        outputPlans: List<TxOutputPlan>,
    ): Transaction {
        return Transaction(params).apply {
            setVersion(2)
            addInput(Sha256Hash.ZERO_HASH, 0xffffffffL, Script(byteArrayOf(0x51.toByte())))
            outputPlans.forEach { output ->
                addOutput(Coin.valueOf(output.valueSats), Script(output.scriptPubKey))
            }
        }
    }

    private fun electrumScriptHash(scriptPubKey: ByteArray): String {
        return MessageDigest.getInstance("SHA-256")
            .digest(scriptPubKey)
            .reversedArray()
            .joinToString("") { byte -> "%02x".format(byte) }
    }

    private fun buildHeaderHex(timestampSeconds: Long): String {
        val header = ByteArray(80)
        header[0] = 0x02
        ByteBuffer.wrap(header, 68, 4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .putInt(timestampSeconds.toInt())
        return header.joinToString("") { byte -> "%02x".format(byte) }
    }

    private fun hexToBytes(value: String): ByteArray {
        return ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }
}

private data class TxOutputPlan(
    val valueSats: Long,
    val scriptPubKey: ByteArray,
)

private data class ElectrumHistoryRecord(
    val txid: String,
    val height: Int,
)

private data class ElectrumUtxoRecord(
    val txid: String,
    val vout: Int,
    val valueSats: Long,
    val height: Int,
)

private data class ElectrumFixture(
    val receive0: BitcoinDerivedKeyMaterial,
    val receive2: BitcoinDerivedKeyMaterial,
    val change1: BitcoinDerivedKeyMaterial,
    val historyOnlyTx: Transaction,
    val fundingTx: Transaction,
    val spendTx: Transaction,
    val histories: Map<String, List<ElectrumHistoryRecord>>,
    val utxos: Map<String, List<ElectrumUtxoRecord>>,
    val transactions: Map<String, String>,
    val headers: Map<Int, String>,
)

private class MockElectrumServer(
    private val fixture: ElectrumFixture,
) : Closeable {
    private val json = Json { ignoreUnknownKeys = true }
    private val serverSocket = ServerSocket(0)
    private val acceptExecutor = Executors.newSingleThreadExecutor()
    private val connectionExecutor = Executors.newCachedThreadPool()

    val port: Int = serverSocket.localPort

    init {
        acceptExecutor.submit {
            while (!serverSocket.isClosed) {
                val socket = runCatching { serverSocket.accept() }.getOrNull() ?: break
                connectionExecutor.submit { handleClient(socket) }
            }
        }
    }

    override fun close() {
        runCatching { serverSocket.close() }
        acceptExecutor.shutdownNow()
        connectionExecutor.shutdownNow()
        acceptExecutor.awaitTermination(5, TimeUnit.SECONDS)
        connectionExecutor.awaitTermination(5, TimeUnit.SECONDS)
    }

    private fun handleClient(socket: Socket) {
        try {
            socket.use { client ->
                val reader = BufferedReader(InputStreamReader(client.getInputStream(), Charsets.UTF_8))
                val writer = BufferedWriter(OutputStreamWriter(client.getOutputStream(), Charsets.UTF_8))
                while (true) {
                    val line = reader.readLine() ?: break
                    if (line.isBlank()) continue
                    val parsed = json.parseToJsonElement(line)
                    val responseText = when (parsed) {
                        is JsonArray -> {
                            parsed.joinToString(prefix = "[", postfix = "]") { request ->
                                handleRequest(request as JsonObject)
                            }
                        }
                        is JsonObject -> handleRequest(parsed)
                        else -> errorResponse(null, "invalid request")
                    }
                    writer.write(responseText)
                    writer.newLine()
                    writer.flush()
                }
            }
        } catch (error: Throwable) {
            error.printStackTrace()
            throw error
        }
    }

    private fun handleRequest(request: JsonObject): String {
        val id = request["id"]
        val method = request["method"]?.jsonPrimitive?.contentOrNull.orEmpty()
        val params = request["params"]?.jsonArray ?: JsonArray(emptyList())
        return when (method) {
            "server.version" -> successResponse(id, "[\"mock-electrum\",\"1.4\"]")
            "blockchain.scripthash.get_history" -> {
                val scriptHash = params.getOrNull(0)?.jsonPrimitive?.contentOrNull.orEmpty()
                val result = fixture.histories[scriptHash].orEmpty()
                    .joinToString(prefix = "[", postfix = "]") { record ->
                        "{\"tx_hash\":${jsonValue(record.txid)},\"height\":${record.height}}"
                    }
                successResponse(id, result)
            }
            "blockchain.scripthash.listunspent" -> {
                val scriptHash = params.getOrNull(0)?.jsonPrimitive?.contentOrNull.orEmpty()
                val result = fixture.utxos[scriptHash].orEmpty()
                    .joinToString(prefix = "[", postfix = "]") { utxo ->
                        "{\"tx_hash\":${jsonValue(utxo.txid)},\"tx_pos\":${utxo.vout},\"value\":${utxo.valueSats},\"height\":${utxo.height}}"
                    }
                successResponse(id, result)
            }
            "blockchain.transaction.get" -> {
                val txid = params.getOrNull(0)?.jsonPrimitive?.contentOrNull.orEmpty()
                val rawTx = fixture.transactions[txid] ?: return errorResponse(id, "missing tx $txid")
                successResponse(id, jsonValue(rawTx))
            }
            "blockchain.block.header" -> {
                val height = params.getOrNull(0)?.jsonPrimitive?.intOrNull ?: -1
                val header = fixture.headers[height] ?: return errorResponse(id, "missing header $height")
                successResponse(id, jsonValue(header))
            }
            else -> errorResponse(id, "unsupported method $method")
        }
    }

    private fun successResponse(id: JsonElement?, resultJson: String): String {
        return """{"jsonrpc":"2.0","id":${id?.toString() ?: "null"},"result":$resultJson}"""
    }

    private fun errorResponse(id: JsonElement?, message: String): String {
        return """{"jsonrpc":"2.0","id":${id?.toString() ?: "null"},"error":{"message":${jsonValue(message)}}}"""
    }

    private fun jsonValue(value: String): String {
        return quoteJsonString(value)
    }

    private fun quoteJsonString(value: String): String {
        val escaped = buildString {
            value.forEach { char ->
                when (char) {
                    '\\' -> append("\\\\")
                    '"' -> append("\\\"")
                    '\b' -> append("\\b")
                    '\u000c' -> append("\\f")
                    '\n' -> append("\\n")
                    '\r' -> append("\\r")
                    '\t' -> append("\\t")
                    else -> {
                        if (char.code < 0x20) {
                            append("\\u")
                            append(char.code.toString(16).padStart(4, '0'))
                        } else {
                            append(char)
                        }
                    }
                }
            }
        }
        return "\"$escaped\""
    }
}
