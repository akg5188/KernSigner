package io.arbitrum.wallet

import java.net.URLDecoder
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.zip.CRC32
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

private val json = Json {
    ignoreUnknownKeys = true
    isLenient = true
}

private val TRANSACTION_ACTIONS = setOf(
    "signtransaction",
    "sendtransaction",
    "ethsendtransaction"
)

private val PERSONAL_ACTIONS = setOf(
    "personalsign",
    "signpersonalmessage",
    "ethsign",
    "signmessage"
)

private val TYPED_DATA_ACTIONS = setOf(
    "signtypeddata",
    "signtypeddatav4",
    "signtypedatav4",
    "signtypeddatalegacy",
    "signtypedata",
    "ethsigntypeddata",
    "ethsigntypeddatav4",
    "ethsigntypedatav4"
)

enum class TpRequestFormat {
    MODERN,
    LEGACY
}

sealed interface TpSignRequest {
    val format: TpRequestFormat
    val namespace: String
    val action: String
    val version: String
    val protocol: String
    val network: String
    val chainId: Long
    val requestId: String?
    val actionId: String?
    val dataId: String?
    val dappName: String?
    val dappUrl: String?
    val dappSource: String?
    val rawPayload: String
}

data class TpSignTransactionRequest(
    override val format: TpRequestFormat,
    override val namespace: String,
    override val action: String,
    override val version: String,
    override val protocol: String,
    override val network: String,
    override val chainId: Long,
    override val requestId: String?,
    override val actionId: String?,
    override val dataId: String?,
    override val dappName: String?,
    override val dappUrl: String?,
    override val dappSource: String?,
    val address: String?,
    val txData: JsonObject,
    override val rawPayload: String
) : TpSignRequest

data class TpSignPersonalMessageRequest(
    override val format: TpRequestFormat,
    override val namespace: String,
    override val action: String,
    override val version: String,
    override val protocol: String,
    override val network: String,
    override val chainId: Long,
    override val requestId: String?,
    override val actionId: String?,
    override val dataId: String?,
    override val dappName: String?,
    override val dappUrl: String?,
    override val dappSource: String?,
    val address: String?,
    val message: String,
    override val rawPayload: String
) : TpSignRequest

data class TpSignTypedDataRequest(
    override val format: TpRequestFormat,
    override val namespace: String,
    override val action: String,
    override val version: String,
    override val protocol: String,
    override val network: String,
    override val chainId: Long,
    override val requestId: String?,
    override val actionId: String?,
    override val dataId: String?,
    override val dappName: String?,
    override val dappUrl: String?,
    override val dappSource: String?,
    val address: String?,
    val typedDataJson: String,
    val primaryType: String?,
    val isLegacy: Boolean,
    override val rawPayload: String
) : TpSignRequest

data class TpMultiFragment(
    val index: Int,
    val total: Int,
    val chunk: String,
    val crc32: String
)

sealed class AssemblyResult {
    data class Progress(val received: Int, val total: Int) : AssemblyResult()
    data class Complete(val payload: String) : AssemblyResult()
    data class Error(val reason: String) : AssemblyResult()
}

class MultiFragmentAssembler {
    private var expectedTotal: Int? = null
    private var expectedCrc: String? = null
    private val rawFragments = mutableMapOf<Int, String>()

    fun reset() {
        expectedTotal = null
        expectedCrc = null
        rawFragments.clear()
    }

    fun accept(fragment: TpMultiFragment): AssemblyResult {
        val total = fragment.total
        val index = fragment.index
        if (total <= 0) {
            return AssemblyResult.Error("分片总数不合法")
        }

        // 历史实现存在 0-based 与 1-based 两种分片编号，统一在完成时再判断。
        val validOneBased = index in 1..total
        val validZeroBased = index in 0 until total
        if (!validOneBased && !validZeroBased) {
            return AssemblyResult.Error("分片索引不合法 (index=$index total=$total)")
        }

        if (expectedTotal == null) {
            expectedTotal = total
            expectedCrc = fragment.crc32
        } else if (expectedTotal != fragment.total || expectedCrc != fragment.crc32) {
            reset()
            return AssemblyResult.Error("分片属于不同二维码序列，已重置")
        }

        rawFragments[index] = fragment.chunk

        val expected = expectedTotal ?: total
        if (rawFragments.size < expected) {
            return AssemblyResult.Progress(rawFragments.size, expected)
        }

        val payload = assemblePayload(expected)
            ?: run {
                reset()
                return AssemblyResult.Error("分片索引基准不一致或有缺片")
            }

        val crc = TpQrCodec.crc32Decimal(payload)
        if (crc != expectedCrc) {
            reset()
            return AssemblyResult.Error("分片 CRC 校验失败")
        }

        reset()
        return AssemblyResult.Complete(payload)
    }

    private fun assemblePayload(total: Int): String? {
        if ((1..total).all { rawFragments.containsKey(it) }) {
            return buildString {
                for (i in 1..total) {
                    append(rawFragments[i])
                }
            }
        }
        if ((0 until total).all { rawFragments.containsKey(it) }) {
            return buildString {
                for (i in 0 until total) {
                    append(rawFragments[i])
                }
            }
        }
        return null
    }
}

object TpQrCodec {
    fun parseInput(raw: String): ParseResult {
        val normalized = raw.trim()
        if (normalized.startsWith("tp:multiFragment-", ignoreCase = true)) {
            return ParseResult.Fragment(parseMultiFragment(normalized))
        }
        return ParseResult.SignRequest(parseSignRequest(normalized))
    }

    fun parseSignRequest(raw: String): TpSignRequest {
        val context = parseRequestContext(raw)
        val normalizedAction = normalizeAction(context.action)
        return when {
            TRANSACTION_ACTIONS.contains(normalizedAction) -> parseSignTransaction(context)
            PERSONAL_ACTIONS.contains(normalizedAction) -> parsePersonalSign(context)
            TYPED_DATA_ACTIONS.contains(normalizedAction) -> parseTypedDataSign(context)
            else -> throw IllegalArgumentException(
                "当前仅支持 signTransaction / personalSign / signTypedData，实际 action=${context.action}"
            )
        }
    }

    fun buildSignResponse(request: TpSignRequest, signatureHex: String, signerAddress: String): String {
        return when (request) {
            is TpSignTransactionRequest -> buildSignTransactionResponse(request, signatureHex)
            else -> buildGenericSignatureResponse(request, signatureHex, signerAddress)
        }
    }

    fun buildRelayRequest(request: TpSignRequest): String {
        val dataJson = buildRelayData(request).toString()
        return if (request.format == TpRequestFormat.LEGACY) {
            val query = linkedMapOf(
                "v" to request.version,
                "requestId" to request.requestId,
                "action" to request.action,
                "actionId" to (request.actionId ?: request.dataId),
                "data" to dataJson
            )
            "${request.namespace}:${request.action}-?" + buildQuery(query)
        } else {
            val query = linkedMapOf(
                "version" to request.version,
                "protocol" to request.protocol,
                "network" to request.network,
                "chain_id" to request.chainId.toString(),
                "requestId" to request.requestId,
                "data" to dataJson
            )
            "${request.namespace}:${request.action}-" + buildQuery(query)
        }
    }

    fun buildSignTransactionResponse(request: TpSignTransactionRequest, rawTransaction: String): String {
        val responseData = buildJsonObject {
            put("rawTransaction", JsonPrimitive(rawTransaction))
            val id = request.actionId ?: request.dataId
            if (!id.isNullOrBlank()) {
                put("id", JsonPrimitive(id))
            }
        }.toString()

        val responseAction = request.action + "Signature"
        return if (request.format == TpRequestFormat.LEGACY) {
            buildLegacyResponse(request, responseAction, request.action, responseData)
        } else {
            buildModernResponse(request, responseAction, responseData)
        }
    }

    private fun buildGenericSignatureResponse(
        request: TpSignRequest,
        signatureHex: String,
        signerAddress: String
    ): String {
        val responseData = buildJsonObject {
            put("signature", JsonPrimitive(signatureHex))
            put("address", JsonPrimitive(signerAddress))
        }.toString()

        val responseAction = request.action + "Signature"
        return if (request.format == TpRequestFormat.LEGACY) {
            buildLegacyResponse(request, responseAction, request.action, responseData)
        } else {
            buildModernResponse(request, responseAction, responseData)
        }
    }

    private fun buildRelayData(request: TpSignRequest): JsonObject {
        return buildJsonObject {
            request.dappName?.takeIf { it.isNotBlank() }?.let { put("dappName", JsonPrimitive(it)) }
            request.dappUrl?.takeIf { it.isNotBlank() }?.let { put("dappUrl", JsonPrimitive(it)) }
            request.dappSource?.takeIf { it.isNotBlank() }?.let { put("source", JsonPrimitive(it)) }
            request.dataId?.takeIf { it.isNotBlank() }?.let { put("id", JsonPrimitive(it)) }

            when (request) {
                is TpSignTransactionRequest -> {
                    request.address?.takeIf { it.isNotBlank() }?.let { put("address", JsonPrimitive(it)) }
                    put("txData", request.txData)
                }
                is TpSignPersonalMessageRequest -> {
                    request.address?.takeIf { it.isNotBlank() }?.let { put("address", JsonPrimitive(it)) }
                    put("message", JsonPrimitive(request.message))
                }
                is TpSignTypedDataRequest -> {
                    request.address?.takeIf { it.isNotBlank() }?.let { put("address", JsonPrimitive(it)) }
                    val parsedJson = runCatching { json.parseToJsonElement(request.typedDataJson) }.getOrNull()
                    if (parsedJson != null) {
                        put("message", parsedJson)
                    } else {
                        put("message", JsonPrimitive(request.typedDataJson))
                    }
                }
            }
        }
    }

    private fun buildModernResponse(
        request: TpSignRequest,
        responseAction: String,
        dataJson: String
    ): String {
        val query = linkedMapOf(
            "version" to request.version,
            "protocol" to request.protocol,
            "network" to request.network,
            "chain_id" to request.chainId.toString(),
            "requestId" to request.requestId,
            "data" to dataJson
        )
        return "${request.namespace}:$responseAction-" + buildQuery(query)
    }

    private fun buildLegacyResponse(
        request: TpSignRequest,
        responseAction: String,
        actionParam: String,
        dataJson: String
    ): String {
        val query = linkedMapOf(
            "v" to request.version,
            "requestId" to request.requestId,
            "action" to actionParam,
            "actionId" to (request.actionId ?: request.dataId),
            "data" to dataJson
        )
        return "${request.namespace}:$responseAction-?" + buildQuery(query)
    }

    private fun parseSignTransaction(context: RequestContext): TpSignTransactionRequest {
        val txData = when (val tx = context.dataJson["txData"]) {
            null -> context.dataJson
            else -> tx.jsonObject
        }
        val address = context.dataJson["address"]?.jsonPrimitive?.contentOrNull
            ?: txData["from"]?.jsonPrimitive?.contentOrNull
            ?: txData["fromAddress"]?.jsonPrimitive?.contentOrNull

        val txChainId = txData["chainId"]?.jsonPrimitive?.contentOrNull
        val chainId = parseOptionalChainId(txChainId ?: context.queryChainIdRaw)
            ?: throw IllegalArgumentException("无法解析 chainId")

        return TpSignTransactionRequest(
            format = context.format,
            namespace = context.namespace,
            action = context.action,
            version = context.version,
            protocol = context.protocol,
            network = context.network,
            chainId = chainId,
            requestId = context.requestId,
            actionId = context.actionId,
            dataId = context.dataId,
            dappName = context.dappName,
            dappUrl = context.dappUrl,
            dappSource = context.dappSource,
            address = address,
            txData = txData,
            rawPayload = context.rawPayload
        )
    }

    private fun parsePersonalSign(context: RequestContext): TpSignPersonalMessageRequest {
        val message = context.dataJson["message"]?.jsonPrimitive?.contentOrNull
            ?: throw IllegalArgumentException("personalSign 缺少 message 字段")
        val address = context.dataJson["address"]?.jsonPrimitive?.contentOrNull
        val chainId = parseOptionalChainId(context.queryChainIdRaw) ?: 1L

        return TpSignPersonalMessageRequest(
            format = context.format,
            namespace = context.namespace,
            action = context.action,
            version = context.version,
            protocol = context.protocol,
            network = context.network,
            chainId = chainId,
            requestId = context.requestId,
            actionId = context.actionId,
            dataId = context.dataId,
            dappName = context.dappName,
            dappUrl = context.dappUrl,
            dappSource = context.dappSource,
            address = address,
            message = message,
            rawPayload = context.rawPayload
        )
    }

    private fun parseTypedDataSign(context: RequestContext): TpSignTypedDataRequest {
        val messageElement = context.dataJson["message"]
            ?: throw IllegalArgumentException("signTypedData 缺少 message 字段")

        val typedDataJson = extractTypedDataJson(messageElement)
        val chainIdFromDomain = extractDomainChainId(typedDataJson)
        val chainId = parseOptionalChainId(context.queryChainIdRaw) ?: chainIdFromDomain ?: 1L
        val primaryType = extractPrimaryType(typedDataJson)
        val address = context.dataJson["address"]?.jsonPrimitive?.contentOrNull
        val isLegacy = normalizeAction(context.action).contains("legacy")

        return TpSignTypedDataRequest(
            format = context.format,
            namespace = context.namespace,
            action = context.action,
            version = context.version,
            protocol = context.protocol,
            network = context.network,
            chainId = chainId,
            requestId = context.requestId,
            actionId = context.actionId,
            dataId = context.dataId,
            dappName = context.dappName,
            dappUrl = context.dappUrl,
            dappSource = context.dappSource,
            address = address,
            typedDataJson = typedDataJson,
            primaryType = primaryType,
            isLegacy = isLegacy,
            rawPayload = context.rawPayload
        )
    }

    private fun extractTypedDataJson(messageElement: JsonElement): String {
        val text = when (messageElement) {
            is JsonObject -> messageElement.toString()
            is JsonArray -> messageElement.toString()
            is JsonPrimitive -> messageElement.contentOrNull
                ?: throw IllegalArgumentException("typedData message 为空")
            else -> throw IllegalArgumentException("typedData message 格式无效")
        }.trim()

        require(text.startsWith("{") || text.startsWith("[")) {
            "typedData message 必须是 JSON 字符串"
        }
        runCatching { json.parseToJsonElement(text) }
            .getOrElse { throw IllegalArgumentException("typedData message 不是合法 JSON") }
        return text
    }

    private fun extractPrimaryType(typedDataJson: String): String? {
        val parsed = runCatching { json.parseToJsonElement(typedDataJson) }.getOrNull() ?: return null
        val obj = parsed as? JsonObject ?: return null
        return obj["primaryType"]?.jsonPrimitive?.contentOrNull
    }

    private fun extractDomainChainId(typedDataJson: String): Long? {
        val parsed = runCatching { json.parseToJsonElement(typedDataJson) }.getOrNull() ?: return null
        val obj = parsed as? JsonObject ?: return null
        val chainRaw = runCatching {
            obj["domain"]?.jsonObject?.get("chainId")?.jsonPrimitive?.contentOrNull
        }.getOrNull() ?: return null
        return parseOptionalChainId(chainRaw)
    }

    private fun parseRequestContext(raw: String): RequestContext {
        val dash = raw.indexOf('-')
        require(dash > 0) { "不是受支持的请求字符串" }

        val left = raw.substring(0, dash)
        val namespace = left.substringBefore(':')
        val action = left.substringAfter(':', "")
        require(namespace.isNotBlank() && action.isNotBlank()) { "协议前缀不合法" }

        val queryRaw = raw.substring(dash + 1).removePrefix("?")
        val params = parseQuery(queryRaw)
        val dataRaw = params["data"] ?: throw IllegalArgumentException("缺少 data 字段")
        val dataJson = parseDataJson(dataRaw)

        val format = if (params.containsKey("v") || params.containsKey("action")) {
            TpRequestFormat.LEGACY
        } else {
            TpRequestFormat.MODERN
        }

        val dapp = dataJson["dapp"]?.jsonObject
        val dappName = firstNonBlank(
            primitiveContentOrNull(dataJson["dappName"]),
            primitiveContentOrNull(dapp?.get("name")),
            primitiveContentOrNull(dataJson["name"])
        )
        val dappUrl = firstNonBlank(
            primitiveContentOrNull(dataJson["dappUrl"]),
            primitiveContentOrNull(dataJson["url"]),
            primitiveContentOrNull(dataJson["website"]),
            primitiveContentOrNull(dapp?.get("url"))
        )
        val dappSource = firstNonBlank(
            primitiveContentOrNull(dataJson["origin"]),
            primitiveContentOrNull(dataJson["source"]),
            primitiveContentOrNull(dataJson["host"]),
            params["source"]
        )

        return RequestContext(
            rawPayload = raw,
            namespace = namespace,
            action = action,
            format = format,
            version = params["version"] ?: params["v"] ?: "1.0",
            protocol = params["protocol"] ?: "ArbitrumWallet",
            network = params["network"] ?: "ethereum",
            chainIdRaw = params["chain_id"] ?: params["blockchainId"],
            requestId = params["requestId"],
            actionId = params["actionId"],
            dataId = dataJson["id"]?.jsonPrimitive?.contentOrNull,
            dappName = dappName,
            dappUrl = dappUrl,
            dappSource = dappSource,
            dataJson = dataJson
        )
    }

    private fun normalizeAction(action: String): String {
        return action.lowercase(Locale.US).filter { it.isLetterOrDigit() }
    }

    private fun parseMultiFragment(raw: String): TpMultiFragment {
        val dash = raw.indexOf('-')
        require(dash > 0) { "无效分片格式" }

        val queryRaw = raw.substring(dash + 1).removePrefix("?")
        val params = parseQuery(queryRaw)
        val dataRaw = params["data"] ?: throw IllegalArgumentException("分片缺少 data")
        val data = parseDataJson(dataRaw)

        val content = data["content"]?.jsonPrimitive?.content
            ?: throw IllegalArgumentException("分片缺少 content")
        val (index, total) = parseFragmentPosition(data)

        val split = content.lastIndexOf('_')
        require(split > 0 && split < content.length - 1) { "分片 content 格式无效" }

        val chunk = content.substring(0, split)
        val crc = content.substring(split + 1)

        return TpMultiFragment(index = index, total = total, chunk = chunk, crc32 = crc)
    }

    private fun parseFragmentPosition(data: JsonObject): Pair<Int, Int> {
        val indexRaw = data["index"]?.jsonPrimitive?.contentOrNull?.trim()
        val totalRaw = data["total"]?.jsonPrimitive?.contentOrNull?.trim()
            ?: data["count"]?.jsonPrimitive?.contentOrNull?.trim()
            ?: data["size"]?.jsonPrimitive?.contentOrNull?.trim()

        if (!indexRaw.isNullOrBlank()) {
            parseIndexAndTotal(indexRaw)?.let { return it }
            if (!totalRaw.isNullOrBlank()) {
                return indexRaw.toInt() to totalRaw.toInt()
            }
        }
        throw IllegalArgumentException("分片缺少 index/total 信息")
    }

    private fun parseIndexAndTotal(raw: String): Pair<Int, Int>? {
        val slash = raw.indexOf('/')
        if (slash > 0 && slash < raw.length - 1) {
            val index = raw.substring(0, slash).trim().toInt()
            val total = raw.substring(slash + 1).trim().toInt()
            return index to total
        }
        val dash = raw.indexOf('-')
        if (dash > 0 && dash < raw.length - 1) {
            val index = raw.substring(0, dash).trim().toInt()
            val total = raw.substring(dash + 1).trim().toInt()
            return index to total
        }
        return null
    }

    private fun parseQuery(queryRaw: String): MutableMap<String, String> {
        val map = linkedMapOf<String, String>()

        val dataMarker = queryRaw.indexOf("data=")
        if (dataMarker >= 0) {
            val beforeData = queryRaw.substring(0, dataMarker)
            if (beforeData.isNotBlank()) {
                beforeData.split('&')
                    .filter { it.isNotBlank() }
                    .forEach { piece ->
                        val idx = piece.indexOf('=')
                        if (idx > 0) {
                            val key = piece.substring(0, idx)
                            val value = piece.substring(idx + 1)
                            map[key] = smartUrlDecode(value)
                        }
                    }
            }
            map["data"] = smartUrlDecode(queryRaw.substring(dataMarker + 5))
            return map
        }

        queryRaw.split('&')
            .filter { it.isNotBlank() }
            .forEach { piece ->
                val idx = piece.indexOf('=')
                if (idx > 0) {
                    val key = piece.substring(0, idx)
                    val value = piece.substring(idx + 1)
                    map[key] = smartUrlDecode(value)
                }
            }

        return map
    }

    private fun parseDataJson(raw: String): JsonObject {
        return json.parseToJsonElement(raw).jsonObject
    }

    private fun parseOptionalChainId(raw: String?): Long? {
        if (raw.isNullOrBlank()) return null
        return parseChainId(raw)
    }

    private fun parseChainId(raw: String): Long {
        val value = raw.trim().substringAfterLast(':')
        return if (value.startsWith("0x") || value.startsWith("0X")) {
            value.substring(2).toLong(16)
        } else {
            value.toLong()
        }
    }

    private fun smartUrlDecode(value: String): String {
        return runCatching {
            URLDecoder.decode(value, StandardCharsets.UTF_8.name())
        }.getOrDefault(value)
    }

    private fun firstNonBlank(vararg values: String?): String? {
        return values.firstOrNull { !it.isNullOrBlank() }?.trim()
    }

    private fun primitiveContentOrNull(value: JsonElement?): String? {
        return (value as? JsonPrimitive)?.contentOrNull
    }

    private fun buildQuery(values: LinkedHashMap<String, String?>): String {
        return values.entries
            .filter { !it.value.isNullOrBlank() }
            .joinToString("&") { (k, v) -> "$k=$v" }
    }

    fun crc32Decimal(value: String): String {
        val crc32 = CRC32()
        val bytes = value.toByteArray(StandardCharsets.UTF_8)
        crc32.update(bytes, 0, bytes.size)
        return crc32.value.toString()
    }
}

private data class RequestContext(
    val rawPayload: String,
    val namespace: String,
    val action: String,
    val format: TpRequestFormat,
    val version: String,
    val protocol: String,
    val network: String,
    val chainIdRaw: String?,
    val requestId: String?,
    val actionId: String?,
    val dataId: String?,
    val dappName: String?,
    val dappUrl: String?,
    val dappSource: String?,
    val dataJson: JsonObject
) {
    val queryChainIdRaw: String?
        get() = chainIdRaw
}

sealed class ParseResult {
    data class Fragment(val fragment: TpMultiFragment) : ParseResult()
    data class SignRequest(val request: TpSignRequest) : ParseResult()
}
