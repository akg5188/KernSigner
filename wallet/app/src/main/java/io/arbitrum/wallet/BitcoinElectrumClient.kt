package io.arbitrum.wallet

import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.Closeable
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.Socket
import java.net.SocketTimeoutException
import javax.net.ssl.HostnameVerifier
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull

private const val BITCOIN_ELECTRUM_READ_TIMEOUT_MS = 20_000
private const val BITCOIN_ELECTRUM_PROTOCOL_VERSION = "1.4"

data class BitcoinElectrumServer(
    val host: String,
    val sslPort: Int,
    val label: String,
    val useTls: Boolean = true,
    val socketFactory: SSLSocketFactory? = null,
    val hostnameVerifier: HostnameVerifier? = null,
)

data class BitcoinElectrumHistoryEntry(
    val txid: String,
    val height: Int,
    val feeSats: Long?,
)

data class BitcoinElectrumUtxo(
    val txid: String,
    val vout: Int,
    val valueSats: Long,
    val height: Int,
)

private data class BitcoinElectrumRpcCall(
    val key: String,
    val method: String,
    val params: List<Any?> = emptyList(),
)

fun bitcoinElectrumServerForPrefix(prefix: String): BitcoinElectrumServer? {
    return when (prefix.lowercase()) {
        "xpub", "ypub", "zpub" -> BitcoinElectrumServer(
            host = "electrum.blockstream.info",
            sslPort = 50002,
            label = "Blockstream Electrum",
        )
        else -> null
    }
}

class BitcoinElectrumClient(
    private val server: BitcoinElectrumServer,
) : Closeable {
    private val json = Json { ignoreUnknownKeys = true }
    private val socket: Socket
    private val reader: BufferedReader
    private val writer: BufferedWriter
    private var nextRequestId = 1L

    init {
        val rawSocket = if (server.useTls) {
            val sslSocketFactory = server.socketFactory ?: (SSLSocketFactory.getDefault() as SSLSocketFactory)
            (sslSocketFactory.createSocket(server.host, server.sslPort) as SSLSocket).apply {
                useClientMode = true
            }
        } else {
            Socket(server.host, server.sslPort)
        }
        rawSocket.soTimeout = BITCOIN_ELECTRUM_READ_TIMEOUT_MS
        rawSocket.tcpNoDelay = true
        rawSocket.keepAlive = true
        if (rawSocket is SSLSocket) {
            rawSocket.startHandshake()
            val verifier = server.hostnameVerifier ?: HttpsURLConnection.getDefaultHostnameVerifier()
            require(verifier.verify(server.host, rawSocket.session)) {
                "Electrum 服务器证书校验失败: ${server.host}"
            }
        }
        socket = rawSocket
        reader = BufferedReader(InputStreamReader(socket.inputStream, Charsets.UTF_8))
        writer = BufferedWriter(OutputStreamWriter(socket.outputStream, Charsets.UTF_8))
        handshake()
    }

    fun fetchScriptHashHistory(scriptHashes: List<String>): Map<String, List<BitcoinElectrumHistoryEntry>> {
        val distinct = scriptHashes.distinct()
        if (distinct.isEmpty()) return emptyMap()
        return executeCalls(
            distinct.map { scriptHash ->
                BitcoinElectrumRpcCall(
                    key = scriptHash,
                    method = "blockchain.scripthash.get_history",
                    params = listOf(scriptHash),
                )
            }
        ).mapValues { (_, response) ->
            val result = requireResult(response, "获取 Electrum 历史记录")
            val array = result as? JsonArray ?: JsonArray(emptyList())
            buildList {
                array.forEach { item ->
                    val txid = item.jsonObject["tx_hash"]?.jsonPrimitive?.contentOrNull?.trim().orEmpty()
                    if (txid.isBlank()) return@forEach
                    add(
                        BitcoinElectrumHistoryEntry(
                            txid = txid,
                            height = item.jsonObject["height"]?.jsonPrimitive?.intOrNull ?: 0,
                            feeSats = item.jsonObject["fee"]?.jsonPrimitive?.longOrNull,
                        )
                    )
                }
            }
        }
    }

    fun fetchScriptHashUtxos(scriptHashes: List<String>): Map<String, List<BitcoinElectrumUtxo>> {
        val distinct = scriptHashes.distinct()
        if (distinct.isEmpty()) return emptyMap()
        return executeCalls(
            distinct.map { scriptHash ->
                BitcoinElectrumRpcCall(
                    key = scriptHash,
                    method = "blockchain.scripthash.listunspent",
                    params = listOf(scriptHash),
                )
            }
        ).mapValues { (_, response) ->
            val result = requireResult(response, "获取 Electrum UTXO")
            val array = result as? JsonArray ?: JsonArray(emptyList())
            buildList {
                array.forEach { item ->
                    val txid = item.jsonObject["tx_hash"]?.jsonPrimitive?.contentOrNull?.trim().orEmpty()
                    if (txid.isBlank()) return@forEach
                    add(
                        BitcoinElectrumUtxo(
                            txid = txid,
                            vout = item.jsonObject["tx_pos"]?.jsonPrimitive?.intOrNull ?: 0,
                            valueSats = item.jsonObject["value"]?.jsonPrimitive?.longOrNull ?: 0L,
                            height = item.jsonObject["height"]?.jsonPrimitive?.intOrNull ?: 0,
                        )
                    )
                }
            }
        }
    }

    fun fetchTransactions(txids: List<String>): Map<String, String> {
        val distinct = txids.distinct()
        if (distinct.isEmpty()) return emptyMap()
        return executeCalls(
            distinct.map { txid ->
                BitcoinElectrumRpcCall(
                    key = txid,
                    method = "blockchain.transaction.get",
                    params = listOf(txid),
                )
            }
        ).mapValues { (_, response) ->
            requireResult(response, "获取 Electrum 交易").jsonPrimitive.contentOrNull.orEmpty().trim()
        }.filterValues { it.isNotBlank() }
    }

    fun fetchBlockHeaders(heights: List<Int>): Map<Int, String> {
        val distinct = heights.filter { it > 0 }.distinct()
        if (distinct.isEmpty()) return emptyMap()
        return executeCalls(
            distinct.map { height ->
                BitcoinElectrumRpcCall(
                    key = height.toString(),
                    method = "blockchain.block.header",
                    params = listOf(height),
                )
            }
        ).mapKeys { (key, _) -> key.toInt() }
            .mapValues { (_, response) ->
                requireResult(response, "获取 Electrum 区块头").jsonPrimitive.contentOrNull.orEmpty().trim()
            }
            .filterValues { it.isNotBlank() }
    }

    override fun close() {
        runCatching { writer.close() }
        runCatching { reader.close() }
        runCatching { socket.close() }
    }

    private fun handshake() {
        val response = executeCalls(
            listOf(
                BitcoinElectrumRpcCall(
                    key = "server.version",
                    method = "server.version",
                    params = listOf("tp-satochip-signer", BITCOIN_ELECTRUM_PROTOCOL_VERSION),
                )
            )
        )["server.version"] ?: error("Electrum 握手失败")
        requireResult(response, "Electrum 握手")
    }

    private fun executeCalls(calls: List<BitcoinElectrumRpcCall>): Map<String, JsonObject> {
        if (calls.isEmpty()) return emptyMap()
        val results = linkedMapOf<String, JsonObject>()
        calls.chunked(64).forEach { chunk ->
            results += executeChunk(chunk)
        }
        return results
    }

    private fun executeChunk(calls: List<BitcoinElectrumRpcCall>): Map<String, JsonObject> {
        val keyById = linkedMapOf<Long, String>()
        val payloadText = calls.joinToString(prefix = "[", postfix = "]") { call ->
            val requestId = nextRequestId++
            keyById[requestId] = call.key
            buildString {
                append("{\"jsonrpc\":\"2.0\",\"id\":")
                append(requestId)
                append(",\"method\":")
                append(serializeJsonValue(call.method))
                append(",\"params\":[")
                append(call.params.joinToString(",") { param -> serializeJsonValue(param) })
                append("]}")
            }
        }

        writer.write(payloadText)
        writer.newLine()
        writer.flush()

        val responses = linkedMapOf<String, JsonObject>()
        while (responses.size < calls.size) {
            val line = try {
                reader.readLine()
            } catch (error: SocketTimeoutException) {
                throw IllegalStateException("Electrum 响应超时: ${server.label}", error)
            } ?: error("Electrum 连接已断开: ${server.label}")
            if (line.isBlank()) continue
            val parsed = json.parseToJsonElement(line)
            when (parsed) {
                is JsonObject -> {
                    val key = responseKey(parsed, keyById)
                    if (key != null) {
                        responses[key] = parsed
                    }
                }
                is JsonArray -> {
                    parsed.forEach { item ->
                        val objectItem = item as? JsonObject ?: return@forEach
                        val key = responseKey(objectItem, keyById)
                        if (key != null) {
                            responses[key] = objectItem
                        }
                    }
                }
                else -> Unit
            }
        }
        return responses
    }

    private fun responseKey(
        response: JsonObject,
        keyById: Map<Long, String>,
    ): String? {
        val id = response["id"]?.jsonPrimitive?.longOrNull ?: return null
        return keyById[id]
    }

    private fun requireResult(
        response: JsonObject,
        actionLabel: String,
    ): JsonElement {
        response["error"]?.let { errorPayload ->
            val message = errorPayload.jsonObject["message"]?.jsonPrimitive?.contentOrNull
                ?.takeIf { it.isNotBlank() }
                ?: errorPayload.toString()
            error("${actionLabel}失败: $message")
        }
        return response["result"] ?: error("${actionLabel}失败: 缺少 result")
    }

    private fun serializeJsonValue(value: Any?): String {
        return when (value) {
            null -> "null"
            is String -> quoteJsonString(value)
            is Number, is Boolean -> value.toString()
            is JsonElement -> value.toString()
            else -> quoteJsonString(value.toString())
        }
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
