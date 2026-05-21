package io.arbitrum.wallet

import co.nstant.`in`.cbor.CborDecoder
import co.nstant.`in`.cbor.CborEncoder
import co.nstant.`in`.cbor.model.ByteString
import co.nstant.`in`.cbor.model.DataItem
import co.nstant.`in`.cbor.model.Map as CborMap
import co.nstant.`in`.cbor.model.Number as CborNumber
import co.nstant.`in`.cbor.model.UnicodeString
import co.nstant.`in`.cbor.model.UnsignedInteger
import com.sparrowwallet.hummingbird.UR
import com.sparrowwallet.hummingbird.URDecoder
import com.sparrowwallet.hummingbird.UREncoder
import com.sparrowwallet.hummingbird.registry.CryptoCoinInfo
import com.sparrowwallet.hummingbird.registry.CryptoHDKey
import com.sparrowwallet.hummingbird.registry.CryptoKeypath
import com.sparrowwallet.hummingbird.registry.pathcomponent.IndexPathComponent
import com.sparrowwallet.hummingbird.registry.pathcomponent.PathComponent
import com.sparrowwallet.hummingbird.registry.pathcomponent.WildcardPathComponent
import java.io.ByteArrayOutputStream
import java.math.BigInteger
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.util.UUID

data class Web3BridgeAccount(
    val address: String,
    val addressPath: String,
    val accountPath: String,
    val masterFingerprint: String,
    val compressedPubKeyHex: String,
    val chainCodeHex: String,
    val xpub: String,
    val sourceLabel: String,
    val importedAt: Long,
    val label: String = "Web3 账户",
    val childrenPath: String = "0/*",
)

enum class Web3RequestDataType(val code: Int) {
    TRANSACTION(1),
    TYPED_DATA(2),
    PERSONAL_MESSAGE(3),
    TYPED_TRANSACTION(4),
    ;

    companion object {
        fun fromCode(code: Int): Web3RequestDataType =
            entries.firstOrNull { it.code == code }
                ?: throw IllegalArgumentException("暂不支持的 Web3 请求类型: $code")
    }
}

data class Web3EthSignRequest(
    val requestId: String?,
    val requestIdDataItem: DataItem? = null,
    val signData: ByteArray,
    val dataType: Web3RequestDataType,
    val chainId: Long,
    val derivationPath: String,
    val address: String?,
    val origin: String?,
    val rawUr: String,
)

object Web3UrCodec {
    private const val ETH_SIGN_REQUEST_TYPE = "eth-sign-request"
    private const val ETH_SIGNATURE_TYPE = "eth-signature"
    private const val CRYPTO_HDKEY_TYPE = "crypto-hdkey"
    private const val DEFAULT_MAX_FRAGMENT_LEN = 120
    private const val ETH_SIGNATURE_MAX_FRAGMENT_LEN = 260
    private const val LEGACY_SIGNATURE_V_MODE = "recovery_id"
    private const val ETH_SIGNATURE_INCLUDE_ORIGIN = true

    fun buildConnectQrPages(account: Web3BridgeAccount): List<String> {
        val originComponents = parsePathComponents(account.accountPath)
        val childrenComponents = parsePathComponents(account.childrenPath, allowWildcard = true)
        val origin = CryptoKeypath(
            originComponents,
            hexToBytes(account.masterFingerprint),
            originComponents.size,
        )
        val children = CryptoKeypath(childrenComponents, null)
        val hdKey = CryptoHDKey(
            false,
            hexToBytes(account.compressedPubKeyHex),
            hexToBytes(account.chainCodeHex),
            CryptoCoinInfo(CryptoCoinInfo.Type.ETHEREUM, CryptoCoinInfo.Network.MAINNET),
            origin,
            children,
            null,
            null,
            "account.standard",
        )
        val ur = UR(CRYPTO_HDKEY_TYPE, encodeCbor(hdKey.toCbor()))
        return encodeUrPages(ur)
    }

    fun parseEthSignRequestUr(urText: String): Web3EthSignRequest {
        val ur = URDecoder.decode(urText.trim())
        require(ur.type.equals(ETH_SIGN_REQUEST_TYPE, ignoreCase = true)) {
            "当前只支持 $ETH_SIGN_REQUEST_TYPE"
        }
        val root = decodeMap(ur.cborBytes)
        val requestIdItem = root.get(unsignedKey(1))
        val requestId = requestIdItem?.let(::decodeRequestId)
        val signData = root.byteString(2) ?: throw IllegalArgumentException("签名请求缺少 signData")
        val dataType = Web3RequestDataType.fromCode(root.longValue(3)?.toInt() ?: 1)
        val chainId = root.longValue(4) ?: 1L
        val derivationPathItem = root.get(unsignedKey(5))
            ?: throw IllegalArgumentException("签名请求缺少 derivationPath")
        val derivationPath = CryptoKeypath.fromCbor(derivationPathItem).path
            ?.let { "m/$it" }
            ?: throw IllegalArgumentException("签名请求 derivationPath 为空")
        val address = root.byteString(6)?.let(::ethAddressFromBytes)
        val origin = root.text(7)?.trim()?.takeIf { it.isNotBlank() }
        return Web3EthSignRequest(
            requestId = requestId,
            requestIdDataItem = requestIdItem?.let(::copyRequestIdDataItem),
            signData = signData,
            dataType = dataType,
            chainId = chainId,
            derivationPath = derivationPath,
            address = address,
            origin = origin,
            rawUr = ur.toString(),
        )
    }

    @Suppress("UNUSED_PARAMETER")
    fun buildEthSignatureQrPages(
        requestId: String?,
        requestIdDataItem: DataItem? = null,
        signatureBytes: ByteArray,
        origin: String? = null,
    ): List<String> {
        require(signatureBytes.size >= 65) { "签名结果长度不正确" }
        val map = CborMap()
        when {
            requestIdDataItem != null -> map.put(unsignedKey(1), copyRequestIdDataItem(requestIdDataItem))
            !requestId.isNullOrBlank() -> map.put(unsignedKey(1), buildRequestIdDataItem(requestId))
        }
        map.put(unsignedKey(2), ByteString(signatureBytes))
        origin?.trim()?.takeIf { it.isNotBlank() && ETH_SIGNATURE_INCLUDE_ORIGIN }?.let {
            map.put(unsignedKey(3), UnicodeString(it))
        }
        val ur = UR(ETH_SIGNATURE_TYPE, encodeCbor(map))
        return encodeUrPages(ur, ETH_SIGNATURE_MAX_FRAGMENT_LEN)
    }

    fun buildTpRequest(
        request: Web3EthSignRequest,
        account: Web3BridgeAccount? = null,
    ): String {
        val normalizedPath = normalizeWeb3DerivationPath(request.derivationPath)
        account?.let { bridgeAccount ->
            require(normalizedPath == bridgeAccount.addressPath) {
                "当前只支持已导入地址路径 ${bridgeAccount.addressPath}，实际请求为 $normalizedPath"
            }
            request.address?.let { requestedAddress ->
                require(requestedAddress.equals(bridgeAccount.address, ignoreCase = true)) {
                    "请求地址与当前导入地址不一致"
                }
            }
        }

        val chain = bridgeChain(request.chainId)
        val effectiveAddress = account?.address ?: request.address
        return when (request.dataType) {
            Web3RequestDataType.TRANSACTION,
            Web3RequestDataType.TYPED_TRANSACTION -> {
                val txData = parseUnsignedTransaction(request)
                TpRequestBuilder.buildSignTransactionRequest(
                    fromAddress = effectiveAddress,
                    txData = txData,
                    chain = chain,
                    requestId = request.requestId ?: UUID.randomUUID().toString(),
                    derivationPath = normalizedPath,
                )
            }

            Web3RequestDataType.PERSONAL_MESSAGE -> {
                TpRequestBuilder.buildPersonalSignRequest(
                    address = effectiveAddress,
                    message = "0x${request.signData.toHexString()}",
                    chain = chain,
                    requestId = request.requestId ?: UUID.randomUUID().toString(),
                    derivationPath = normalizedPath,
                )
            }

            Web3RequestDataType.TYPED_DATA -> {
                val typedDataJson = request.signData.toString(StandardCharsets.UTF_8)
                TpRequestBuilder.buildSignTypedDataRequest(
                    address = effectiveAddress,
                    typedDataJson = typedDataJson,
                    chain = chain,
                    requestId = request.requestId ?: UUID.randomUUID().toString(),
                    derivationPath = normalizedPath,
                    dappName = request.origin,
                    dappSource = request.origin,
                )
            }
        }
    }

    fun buildRequestSummary(request: Web3EthSignRequest): String {
        val typeLabel = when (request.dataType) {
            Web3RequestDataType.TRANSACTION -> "交易签名"
            Web3RequestDataType.TYPED_TRANSACTION -> "EIP-1559 交易签名"
            Web3RequestDataType.PERSONAL_MESSAGE -> "消息签名"
            Web3RequestDataType.TYPED_DATA -> "TypedData 签名"
        }
        return buildString {
            appendLine("来源: ${request.origin ?: "OKX / Bitget / Keystone"}")
            appendLine("类型: $typeLabel")
            appendLine("链 ID: ${request.chainId}")
            appendLine("路径: ${request.derivationPath}")
            request.address?.let { appendLine("地址: $it") }
            appendLine("请求 ID: ${request.requestId ?: "-"}")
            walletSupportNote(request.origin, request.chainId)?.let { appendLine(it) }
        }.trim()
    }

    private fun walletSupportNote(origin: String?, chainId: Long): String? {
        val normalized = origin.orEmpty().trim().lowercase()
        val walletName = when {
            "bitget" in normalized || "bitkeep" in normalized -> "Bitget Wallet"
            "okx" in normalized || "okex" in normalized -> "OKX Wallet"
            else -> return null
        }
        return if (chainId != 1L) {
            "$walletName 官方当前只标注支持 BTC/ETH；当前链 ID $chainId 很可能在钱包端失败。"
        } else {
            null
        }
    }

    fun extractSignatureBytes(
        request: Web3EthSignRequest,
        response: ParsedResponse,
    ): ByteArray {
        return when (request.dataType) {
            Web3RequestDataType.TRANSACTION -> {
                val rawTx = response.rawTransaction ?: throw IllegalArgumentException("树莓派未返回签名交易")
                extractLegacyTransactionSignature(rawTx, request.chainId, request.origin)
            }

            Web3RequestDataType.TYPED_TRANSACTION -> {
                val rawTx = response.rawTransaction ?: throw IllegalArgumentException("树莓派未返回签名交易")
                extractTypedTransactionSignature(rawTx)
            }

            Web3RequestDataType.PERSONAL_MESSAGE,
            Web3RequestDataType.TYPED_DATA -> {
                val signatureHex = response.signature ?: throw IllegalArgumentException("树莓派未返回签名结果")
                val signatureBytes = normalizeEthMessageSignatureBytes(hexToBytes(signatureHex))
                require(signatureBytes.size == 65) { "树莓派返回的签名长度不正确" }
                signatureBytes
            }
        }
    }

    private fun bridgeChain(chainId: Long): WalletChain {
        return WalletChains.byId(chainId) ?: WalletChain(
            chainId = chainId,
            chainIdHex = "0x${chainId.toString(16)}",
            slug = "evm",
            displayName = "EVM Chain $chainId",
            shortName = "EVM",
            nativeSymbol = "ETH",
            rpcUrl = "",
            explorerUrl = "",
            accentColor = 0xFF3E63FF,
            historyLookbackBlocks = 0,
            tokens = listOf(TokenInfo("ETH", "Ether", 18)),
            coingeckoId = null,
        )
    }

    private fun parseUnsignedTransaction(request: Web3EthSignRequest): TxData {
        return when (request.dataType) {
            Web3RequestDataType.TRANSACTION -> parseLegacyUnsignedTransaction(request)
            Web3RequestDataType.TYPED_TRANSACTION -> parseTypedUnsignedTransaction(request)
            else -> throw IllegalArgumentException("当前请求不是交易类型")
        }
    }

    private fun parseLegacyUnsignedTransaction(request: Web3EthSignRequest): TxData {
        val values = RlpCodec.decode(request.signData).requireList("Legacy unsigned tx")
        require(values.size >= 6) { "Legacy unsigned tx 字段不足" }
        return TxData(
            from = request.address,
            to = values[3].requireBytes("to").toAddressOrNull(),
            value = values[4].toQuantity(),
            data = "0x${values[5].requireBytes("data").toHexString()}",
            gasLimit = values[2].toQuantity(),
            nonce = values[0].toQuantity(),
            gasPrice = values[1].toQuantity(),
            maxFeePerGas = null,
            maxPriorityFeePerGas = null,
            type = 0,
            accessList = emptyList(),
        )
    }

    private fun parseTypedUnsignedTransaction(request: Web3EthSignRequest): TxData {
        val raw = request.signData
        require(raw.isNotEmpty()) { "Typed unsigned tx 为空" }
        val txType = raw[0].toInt() and 0xFF
        val body = raw.copyOfRange(1, raw.size)
        val values = RlpCodec.decode(body).requireList("Typed unsigned tx")
        return when (txType) {
            0x01 -> {
                require(values.size >= 8) { "EIP-2930 unsigned tx 字段不足" }
                TxData(
                    from = request.address,
                    to = values[4].requireBytes("to").toAddressOrNull(),
                    value = values[5].toQuantity(),
                    data = "0x${values[6].requireBytes("data").toHexString()}",
                    gasLimit = values[3].toQuantity(),
                    nonce = values[1].toQuantity(),
                    gasPrice = values[2].toQuantity(),
                    maxFeePerGas = null,
                    maxPriorityFeePerGas = null,
                    type = 1,
                    accessList = parseAccessList(values[7]),
                )
            }

            0x02 -> {
                require(values.size >= 9) { "EIP-1559 unsigned tx 字段不足" }
                TxData(
                    from = request.address,
                    to = values[5].requireBytes("to").toAddressOrNull(),
                    value = values[6].toQuantity(),
                    data = "0x${values[7].requireBytes("data").toHexString()}",
                    gasLimit = values[4].toQuantity(),
                    nonce = values[1].toQuantity(),
                    gasPrice = null,
                    maxFeePerGas = values[3].toQuantity(),
                    maxPriorityFeePerGas = values[2].toQuantity(),
                    type = 2,
                    accessList = parseAccessList(values[8]),
                )
            }

            else -> throw IllegalArgumentException("当前暂不支持的 typed transaction 类型: 0x${txType.toString(16)}")
        }
    }

    private fun parseAccessList(value: RlpValue): List<AccessListEntry> {
        return value.requireList("accessList").map { item ->
            val entry = item.requireList("accessList entry")
            require(entry.size == 2) { "accessList entry 格式错误" }
            val address = entry[0].requireBytes("accessList.address").toAddressOrNull()
                ?: throw IllegalArgumentException("accessList.address 格式错误")
            val storageKeys = entry[1].requireList("accessList.storageKeys").map { keyValue ->
                "0x${keyValue.requireBytes("storageKey").toHexString()}"
            }
            AccessListEntry(address = address, storageKeys = storageKeys)
        }
    }

    private fun extractLegacyTransactionSignature(
        rawTransaction: String,
        chainId: Long,
        origin: String?,
    ): ByteArray {
        val values = RlpCodec.decode(hexToBytes(rawTransaction)).requireList("Signed legacy tx")
        require(values.size >= 9) { "Signed legacy tx 字段不足" }
        val v = values[6].toQuantity()
        val r = values[7].toQuantity().toFixedBytes(32)
        val s = values[8].toQuantity().toFixedBytes(32)
        val recoveryId = when {
            v == BigInteger.ZERO || v == BigInteger.ONE -> v.toInt()
            v == BigInteger.valueOf(27) || v == BigInteger.valueOf(28) -> v.toInt() - 27
            else -> {
                val eip155Base = BigInteger.valueOf(chainId).multiply(BigInteger.TWO).add(BigInteger.valueOf(35))
                v.subtract(eip155Base).toInt()
            }
        }
        require(recoveryId == 0 || recoveryId == 1) { "Signed legacy tx recoveryId 不正确: $recoveryId" }
        val outputBytes = legacySignatureOutputBytes(recoveryId, chainId, origin)
        return r + s + outputBytes
    }

    private fun extractTypedTransactionSignature(rawTransaction: String): ByteArray {
        val txBytes = hexToBytes(rawTransaction)
        require(txBytes.isNotEmpty()) { "Signed typed tx 为空" }
        val txType = txBytes[0].toInt() and 0xFF
        val values = RlpCodec.decode(txBytes.copyOfRange(1, txBytes.size)).requireList("Signed typed tx")
        return when (txType) {
            0x01 -> {
                require(values.size >= 11) { "Signed EIP-2930 tx 字段不足" }
                val yParity = values[8].toQuantity().toInt()
                val r = values[9].toQuantity().toFixedBytes(32)
                val s = values[10].toQuantity().toFixedBytes(32)
                r + s + byteArrayOf(yParity.toByte())
            }

            0x02 -> {
                require(values.size >= 12) { "Signed EIP-1559 tx 字段不足" }
                val yParity = values[9].toQuantity().toInt()
                val r = values[10].toQuantity().toFixedBytes(32)
                val s = values[11].toQuantity().toFixedBytes(32)
                r + s + byteArrayOf(yParity.toByte())
            }

            else -> throw IllegalArgumentException("当前暂不支持的 typed transaction 回传类型: 0x${txType.toString(16)}")
        }
    }

    private fun normalizeEthMessageSignatureBytes(signatureBytes: ByteArray): ByteArray {
        require(signatureBytes.size == 65) { "签名结果必须是 65 字节" }
        val v = signatureBytes.last().toInt() and 0xFF
        val recoveryId = when (v) {
            27, 28 -> v - 27
            0, 1 -> v
            else -> (v - 35) and 1
        }
        return signatureBytes.copyOf().also { it[it.lastIndex] = recoveryId.toByte() }
    }

    private fun normalizeLegacyVMode(value: String = LEGACY_SIGNATURE_V_MODE): String {
        return when (value.trim().lowercase()) {
            "eip155_v", "eip155", "chain_v", "chainid_v", "37_38", "37/38" -> "eip155_v"
            "ethereum_v", "ethereum", "eth_v", "27_28", "27/28" -> "ethereum_v"
            else -> "recovery_id"
        }
    }

    private fun web3LegacyVMode(origin: String?, chainId: Long): String {
        val defaultMode = normalizeLegacyVMode()
        if (defaultMode != "recovery_id") return defaultMode
        val normalizedOrigin = origin.orEmpty().trim().lowercase()
        val walletCompat = "bitget" in normalizedOrigin || "bitkeep" in normalizedOrigin ||
            "okx" in normalizedOrigin || "okex" in normalizedOrigin
        return if (walletCompat && chainId > 0L) "eip155_v" else defaultMode
    }

    private fun legacySignatureOutputBytes(recoveryId: Int, chainId: Long, origin: String?): ByteArray {
        return when (web3LegacyVMode(origin, chainId)) {
            "ethereum_v" -> byteArrayOf((recoveryId + 27).toByte())
            "eip155_v" -> {
                val value = chainId * 2 + 35 + recoveryId
                BigInteger.valueOf(value).toMinimalUnsignedBytes()
            }
            else -> byteArrayOf(recoveryId.toByte())
        }
    }

    private fun encodeUrPages(ur: UR, maxFragmentLen: Int = DEFAULT_MAX_FRAGMENT_LEN): List<String> {
        val encoder = UREncoder(ur, maxFragmentLen, 10, 0)
        if (encoder.isSinglePart) {
            return listOf(UREncoder.encode(ur))
        }
        val pageCount = encoder.seqLen.coerceAtLeast(1)
        return buildList(pageCount) {
            repeat(pageCount) {
                add(encoder.nextPart())
            }
        }
    }

    private fun decodeMap(cborBytes: ByteArray): CborMap {
        val item = CborDecoder.decode(cborBytes).firstOrNull()
            ?: throw IllegalArgumentException("CBOR 内容为空")
        return item as? CborMap ?: throw IllegalArgumentException("CBOR 不是对象")
    }

    private fun encodeCbor(item: DataItem): ByteArray {
        val out = ByteArrayOutputStream()
        CborEncoder(out).encode(item)
        return out.toByteArray()
    }

    private fun parsePathComponents(path: String, allowWildcard: Boolean = false): List<PathComponent> {
        val trimmed = path.trim()
        if (trimmed.isBlank() || trimmed == "m") return emptyList()
        val normalized = if (trimmed.startsWith("m")) {
            normalizeWeb3DerivationPath(trimmed)
        } else {
            trimmed
        }
        val components = mutableListOf<PathComponent>()
        normalized.removePrefix("m/").removePrefix("m").trimStart('/').split('/').filter { it.isNotBlank() }.forEach { segment ->
            when {
                allowWildcard && segment == "*" -> components += WildcardPathComponent(false)
                else -> {
                    val hardened = segment.endsWith("'") || segment.endsWith("h", ignoreCase = true)
                    val numeric = segment.removeSuffix("'").removeSuffix("h").removeSuffix("H")
                    val index = numeric.toIntOrNull()
                        ?: throw IllegalArgumentException("派生路径片段无效: $segment")
                    components += IndexPathComponent(index, hardened)
                }
            }
        }
        return components
    }

    private fun normalizeWeb3DerivationPath(path: String): String {
        val trimmed = path.trim()
        require(trimmed.startsWith("m")) { "派生路径必须以 m 开头" }
        return trimmed
    }

    private fun uuidToBytes(uuid: UUID): ByteArray {
        return ByteBuffer.allocate(16)
            .putLong(uuid.mostSignificantBits)
            .putLong(uuid.leastSignificantBits)
            .array()
    }

    private fun decodeRequestId(item: DataItem): String {
        return when (item) {
            is UnicodeString -> item.string?.takeIf { it.isNotBlank() }
                ?: throw IllegalArgumentException("requestId 为空")
            is ByteString -> decodeRequestIdBytes(item.bytes ?: throw IllegalArgumentException("requestId 为空"))
            else -> throw IllegalArgumentException("requestId 格式不支持")
        }
    }

    private fun decodeRequestIdBytes(bytes: ByteArray): String {
        bytes.toUtf8RequestIdOrNull()?.let { return it }
        if (bytes.size == 16) {
            val buffer = ByteBuffer.wrap(bytes)
            return UUID(buffer.long, buffer.long).toString()
        }
        return "0x${bytes.toHexString()}"
    }

    private fun buildRequestIdDataItem(requestId: String): DataItem {
        val trimmed = requestId.trim()
        return try {
            ByteString(uuidToBytes(UUID.fromString(trimmed))).also { it.setTag(37) }
        } catch (_: IllegalArgumentException) {
            UnicodeString(trimmed)
        }
    }

    private fun copyRequestIdDataItem(item: DataItem): DataItem {
        val copied: DataItem = when (item) {
            is ByteString -> ByteString(item.bytes?.copyOf() ?: byteArrayOf())
            is UnicodeString -> UnicodeString(item.string)
            else -> throw IllegalArgumentException("requestId 格式不支持")
        }
        if (item.hasTag()) {
            copied.setTag(item.tag)
        }
        return copied
    }

    private fun CborMap.byteString(key: Int): ByteArray? {
        return (get(unsignedKey(key)) as? ByteString)?.bytes
    }

    private fun CborMap.text(key: Int): String? {
        return (get(unsignedKey(key)) as? UnicodeString)?.string
    }

    private fun CborMap.longValue(key: Int): Long? {
        return (get(unsignedKey(key)) as? CborNumber)?.value?.toLong()
    }

    private fun ethAddressFromBytes(bytes: ByteArray): String {
        require(bytes.size == 20) { "地址长度不正确" }
        return "0x${bytes.toHexString()}"
    }

    private fun ByteArray.toAddressOrNull(): String? {
        if (isEmpty()) return null
        require(size == 20) { "地址长度不正确" }
        return "0x${toHexString()}"
    }

    private fun BigInteger.toFixedBytes(size: Int): ByteArray {
        val raw = toByteArray().let { bytes ->
            when {
                bytes.size == size -> bytes
                bytes.size == size + 1 && bytes.first() == 0.toByte() -> bytes.copyOfRange(1, bytes.size)
                bytes.size < size -> ByteArray(size - bytes.size) + bytes
                else -> throw IllegalArgumentException("数值长度超过 $size 字节")
            }
        }
        return raw
    }

    private fun BigInteger.toMinimalUnsignedBytes(): ByteArray {
        if (this == BigInteger.ZERO) return byteArrayOf(0)
        val raw = toByteArray()
        return if (raw.size > 1 && raw.first() == 0.toByte()) raw.copyOfRange(1, raw.size) else raw
    }

    private fun ByteArray.toHexString(): String =
        joinToString(separator = "") { "%02x".format(it) }

    private fun ByteArray.toUtf8RequestIdOrNull(): String? {
        val utf8 = toString(StandardCharsets.UTF_8)
        val roundTrip = utf8.toByteArray(StandardCharsets.UTF_8).contentEquals(this)
        val printable = utf8.all { ch -> !ch.isISOControl() || ch == '\n' || ch == '\r' || ch == '\t' }
        return utf8.takeIf { roundTrip && printable && utf8.isNotBlank() }
    }

    private fun hexToBytes(value: String): ByteArray {
        val clean = value.removePrefix("0x").removePrefix("0X")
        require(clean.length % 2 == 0) { "十六进制长度不正确" }
        return clean.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    private fun unsignedKey(value: Int): UnsignedInteger = UnsignedInteger(value.toLong())
}
