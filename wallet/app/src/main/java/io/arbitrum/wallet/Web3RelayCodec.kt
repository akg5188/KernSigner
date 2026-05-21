package io.arbitrum.wallet

import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.util.Base64
import java.util.zip.Deflater
import java.util.zip.DeflaterOutputStream
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject

enum class Web3RelayWallet(
    val code: String,
    val displayName: String,
) {
    OKX("OKX", "OKX Wallet"),
    BITGET("BITGET", "Bitget Wallet"),
    KEYSTONE("KEYSTONE", "Keystone"),
    ;

    companion object {
        fun detect(origin: String?): Web3RelayWallet {
            val normalized = origin.orEmpty().trim().lowercase()
            return when {
                "bitget" in normalized || "bitkeep" in normalized -> BITGET
                "okx" in normalized || "okex" in normalized -> OKX
                else -> KEYSTONE
            }
        }
    }
}

object Web3RelayCodec {
    private const val PREFIX = "w3r1:"
    private const val CHUNK_CHARS = 56

    fun buildNativeRelayPayloads(
        request: Web3EthSignRequest,
        tpPayload: String,
    ): RelayQrCodec.RelayQrBundle {
        val wallet = Web3RelayWallet.detect(request.origin)
        val requestAddress = request.address.orEmpty()
        val envelope = buildJsonObject {
            put("version", JsonPrimitive(1))
            put("wallet", JsonPrimitive(wallet.code))
            put("wallet_name", JsonPrimitive(wallet.displayName))
            put("protocol", JsonPrimitive("keystone-eth"))
            put("format", JsonPrimitive("Keystone / AirGap UR"))
            put("qr_type", JsonPrimitive("eth-sign-request"))
            put("action", JsonPrimitive("EVM 签名请求"))
            put("chain", JsonPrimitive(chainHint(request.chainId)))
            put("payload", JsonPrimitive(tpPayload))
            put("response_protocol", JsonPrimitive("eth-signature"))
            put("request_id", JsonPrimitive(request.requestId.orEmpty()))
            put("origin", JsonPrimitive(request.origin.orEmpty()))
            put("data_type", JsonPrimitive(request.dataType.name))
            put("request_data_type_id", JsonPrimitive(request.dataType.code))
            put(
                "request_sign_data_hex",
                JsonPrimitive(request.signData.joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xFF) })
            )
            put("chain_id", JsonPrimitive(request.chainId))
            put("address", JsonPrimitive(requestAddress))
            put("address_path", JsonPrimitive(request.derivationPath))
            put("expected_address", JsonPrimitive(requestAddress))
        }.toString()

        val encoded = Base64.getUrlEncoder().withoutPadding().encodeToString(deflateUtf8(envelope))
        val crc = RelayQrCodec.crc32Decimal(encoded)
        val chunks = encoded.chunked(CHUNK_CHARS)
        val pages = chunks.mapIndexed { index, chunk ->
            "$PREFIX${index + 1}/${chunks.size}.$crc.$chunk"
        }
        return RelayQrCodec.RelayQrBundle(payloads = pages, isFragmented = pages.size > 1)
    }

    private fun chainHint(chainId: Long): String {
        return WalletChains.byId(chainId)?.displayName ?: "EVM Chain $chainId"
    }

    private fun deflateUtf8(value: String): ByteArray {
        val out = ByteArrayOutputStream()
        val deflater = Deflater(Deflater.BEST_COMPRESSION)
        DeflaterOutputStream(out, deflater).use { it.write(value.toByteArray(StandardCharsets.UTF_8)) }
        return out.toByteArray()
    }
}
