package io.arbitrum.wallet

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import java.math.BigInteger
import java.net.URLEncoder
import java.nio.charset.StandardCharsets

/**
 * 构建与 pi-signer 兼容的离线签名请求字符串。
 * 内部仍使用历史前缀以保持与现有树莓派固件兼容。
 */
object TpRequestBuilder {
    private const val NAMESPACE = "tp"
    private const val VERSION = "1.0"
    private const val PROTOCOL = "ArbitrumWallet"
    // Compatibility for older Pi signer builds:
    // use signTypeDataV4 (single 'd' after Type) instead of signTypedDataV4.
    private const val TYPED_DATA_ACTION = "signTypeDataV4"

    fun buildSignTransactionRequest(
        fromAddress: String? = null,
        txData: TxData,
        chain: WalletChain = WalletChains.DEFAULT,
        requestId: String = java.util.UUID.randomUUID().toString(),
        derivationPath: String? = null,
    ): String {
        val effectiveFrom = txData.from ?: fromAddress
        val txJson = buildJsonObject {
            effectiveFrom?.takeIf { it.isNotBlank() }?.let { put("from", it) }
            put("to", txData.to?.let(::JsonPrimitive) ?: JsonNull)
            put("value", txData.value.toString())
            put("data", txData.data)
            put("gasLimit", txData.gasLimit.toString())
            put("chainId", chain.chainIdHex)
            txData.gasPrice?.let { put("gasPrice", it.toString()) }
            txData.maxFeePerGas?.let { put("maxFeePerGas", it.toString()) }
            txData.maxPriorityFeePerGas?.let { put("maxPriorityFeePerGas", it.toString()) }
            txData.nonce?.let { put("nonce", it.toString()) }
            put("type", txData.type)
            if (txData.accessList.isNotEmpty()) {
                put(
                    "accessList",
                    buildJsonArray {
                        txData.accessList.forEach { entry ->
                            add(
                                buildJsonObject {
                                    put("address", entry.address)
                                    put(
                                        "storageKeys",
                                        buildJsonArray {
                                            entry.storageKeys.forEach { add(JsonPrimitive(it)) }
                                        }
                                    )
                                }
                            )
                        }
                    }
                )
            }
        }
        val dataJson = buildJsonObject {
            effectiveFrom?.takeIf { it.isNotBlank() }?.let { put("address", it) }
            put("txData", txJson)
        }
        val query = buildQuery(
            "version" to VERSION,
            "protocol" to PROTOCOL,
            "network" to chain.slug,
            "chain_id" to chain.chainId.toString(),
            "requestId" to requestId,
            "path" to derivationPath.orEmpty(),
            "data" to dataJson.toString(),
        )
        return "$NAMESPACE:signTransaction-$query"
    }

    fun buildPersonalSignRequest(
        address: String? = null,
        message: String,
        chain: WalletChain = WalletChains.DEFAULT,
        requestId: String = java.util.UUID.randomUUID().toString(),
        derivationPath: String? = null,
    ): String {
        val dataJson = buildJsonObject {
            address?.takeIf { it.isNotBlank() }?.let { put("address", it) }
            put("message", message)
        }
        val query = buildQuery(
            "version" to VERSION,
            "protocol" to PROTOCOL,
            "network" to chain.slug,
            "chain_id" to chain.chainId.toString(),
            "requestId" to requestId,
            "path" to derivationPath.orEmpty(),
            "data" to dataJson.toString(),
        )
        return "$NAMESPACE:personalSign-$query"
    }

    fun buildSignTypedDataRequest(
        address: String? = null,
        typedDataJson: String,
        chain: WalletChain = WalletChains.DEFAULT,
        requestId: String = java.util.UUID.randomUUID().toString(),
        derivationPath: String? = null,
        dappName: String? = null,
        dappUrl: String? = null,
        dappSource: String? = null,
    ): String {
        val messageObj = Json.parseToJsonElement(typedDataJson)
        val dataStr = buildJsonObject {
            address?.takeIf { it.isNotBlank() }?.let { put("address", it) }
            put("message", messageObj)
            dappName?.takeIf { it.isNotBlank() }?.let { put("dappName", it) }
            dappUrl?.takeIf { it.isNotBlank() }?.let { put("dappUrl", it) }
            dappSource?.takeIf { it.isNotBlank() }?.let { put("source", it) }
        }.toString()
        val query = buildQuery(
            "version" to VERSION,
            "protocol" to PROTOCOL,
            "network" to chain.slug,
            "chain_id" to chain.chainId.toString(),
            "requestId" to requestId,
            "path" to derivationPath.orEmpty(),
            "dappName" to dappName.orEmpty(),
            "dappUrl" to dappUrl.orEmpty(),
            "source" to dappSource.orEmpty(),
            "data" to dataStr,
        )
        return "$NAMESPACE:$TYPED_DATA_ACTION-$query"
    }

    fun buildExportWeb3AccountRequest(
        addressPath: String,
        expectedAddress: String,
        chain: WalletChain = WalletChains.DEFAULT,
        requestId: String = java.util.UUID.randomUUID().toString(),
    ): String {
        val dataJson = buildJsonObject {
            put("address", expectedAddress)
            put("expectedAddress", expectedAddress)
            put("path", addressPath)
            put("chainId", chain.chainId)
            put("purpose", "web3-bridge")
        }.toString()
        val query = buildQuery(
            "version" to VERSION,
            "protocol" to PROTOCOL,
            "network" to "evm",
            "chain_id" to chain.chainId.toString(),
            "requestId" to requestId,
            "path" to addressPath,
            "data" to dataJson,
        )
        return "$NAMESPACE:exportWeb3Account-$query"
    }

    private fun buildQuery(vararg pairs: Pair<String, String?>): String =
        pairs.mapNotNull { (key, value) ->
            value?.takeIf { it.isNotBlank() }?.let { key to it }
        }.joinToString("&") { (k, v) ->
            "$k=${URLEncoder.encode(v, StandardCharsets.UTF_8.name())}"
        }
}

data class TxData(
    val from: String?,
    val to: String?,
    val value: BigInteger,
    val data: String,
    val gasLimit: BigInteger,
    val nonce: BigInteger? = null,
    val gasPrice: BigInteger? = null,
    val maxFeePerGas: BigInteger? = null,
    val maxPriorityFeePerGas: BigInteger? = null,
    val type: Int = 2,
    val accessList: List<AccessListEntry> = emptyList(),
)

data class AccessListEntry(
    val address: String,
    val storageKeys: List<String>,
)
