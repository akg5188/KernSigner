package io.arbitrum.wallet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Base64
import java.math.BigInteger
import java.io.ByteArrayOutputStream
import java.net.URLDecoder
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import co.nstant.`in`.cbor.CborEncoder
import co.nstant.`in`.cbor.model.ByteString
import co.nstant.`in`.cbor.model.DataItem
import com.sparrowwallet.hummingbird.URDecoder
import com.sparrowwallet.hummingbird.ResultType
import com.sparrowwallet.hummingbird.UR
import com.sparrowwallet.hummingbird.UREncoder
import com.sparrowwallet.hummingbird.registry.CryptoKeypath
import com.sparrowwallet.hummingbird.registry.pathcomponent.IndexPathComponent
import co.nstant.`in`.cbor.CborDecoder
import co.nstant.`in`.cbor.model.UnicodeString
import co.nstant.`in`.cbor.model.UnsignedInteger
import co.nstant.`in`.cbor.model.Map as CborMap
import java.util.zip.Inflater
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long

class Web3UrCodecTest {
    private val sampleAccount = Web3BridgeAccount(
        address = "0x1111111111111111111111111111111111111111",
        addressPath = "m/44'/60'/0'/0/0",
        accountPath = "m/44'/60'/0'",
        masterFingerprint = "f23f9fd2",
        compressedPubKeyHex = "02" + "11".repeat(32),
        chainCodeHex = "22".repeat(32),
        xpub = "xpub661MyMwAqRbcFtest",
        sourceLabel = "已加载助记词",
        importedAt = 1_776_000_000_000L,
    )

    @Test
    fun buildsWeb3ConnectQrPages() {
        val pages = Web3UrCodec.buildConnectQrPages(sampleAccount)
        assertTrue(pages.isNotEmpty())
        assertTrue(pages.first().startsWith("ur:crypto-hdkey", ignoreCase = true))
    }

    @Test
    fun buildsWeb3SignatureQrPages() {
        val signature = ByteArray(65) { index -> index.toByte() }
        val pages = Web3UrCodec.buildEthSignatureQrPages(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            requestIdDataItem = null,
            signatureBytes = signature,
            origin = "OKX Wallet",
        )
        assertTrue(pages.isNotEmpty())
        assertTrue(pages.first().startsWith("ur:eth-signature", ignoreCase = true))
        assertEquals(1, pages.size)
        assertTrue(pages.maxOf { it.length } <= 260)

        val decoder = URDecoder()
        pages.forEach { page ->
            assertTrue(decoder.receivePart(page))
        }
        val result = decoder.result
        assertEquals(ResultType.SUCCESS, result.type)
        val root = CborDecoder.decode(result.ur.cborBytes).first() as CborMap
        assertEquals(3, root.keys.size)
        assertEquals(65, (root[UnsignedInteger(2)] as co.nstant.`in`.cbor.model.ByteString).bytes.size)
        assertEquals("OKX Wallet", (root[UnsignedInteger(3)] as UnicodeString).string)
    }

    @Test
    fun omitsOriginKeyWhenSignatureOriginBlank() {
        val signature = ByteArray(65) { index -> index.toByte() }
        val pages = Web3UrCodec.buildEthSignatureQrPages(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            requestIdDataItem = null,
            signatureBytes = signature,
            origin = "   ",
        )

        val decoder = URDecoder()
        pages.forEach { page ->
            assertTrue(decoder.receivePart(page))
        }
        val result = decoder.result
        assertEquals(ResultType.SUCCESS, result.type)
        val root = CborDecoder.decode(result.ur.cborBytes).first() as CborMap
        assertEquals(2, root.keys.size)
        assertEquals(null, root[UnsignedInteger(3)])
    }

    @Test
    fun buildsNativeWeb3RelayEnvelopeForPi() {
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = byteArrayOf(0x01, 0x02, 0x03),
            dataType = Web3RequestDataType.PERSONAL_MESSAGE,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "Bitkeep",
            rawUr = "ur:eth-sign-request/test",
        )
        val tpPayload = TpRequestBuilder.buildPersonalSignRequest(
            address = sampleAccount.address,
            message = "0x010203",
            chain = WalletChains.ARBITRUM,
            requestId = request.requestId ?: "fallback",
            derivationPath = sampleAccount.addressPath,
        )

        val bundle = Web3RelayCodec.buildNativeRelayPayloads(request, tpPayload)

        assertFalse(bundle.payloads.isEmpty())
        assertTrue(bundle.payloads.first().startsWith("w3r1:"))

        val joined = bundle.payloads.joinToString(separator = "") { it.substringAfterLast('.') }
        val decoded = inflateUrlBase64(joined)
        val json = Json.parseToJsonElement(decoded).jsonObject
        assertEquals("BITGET", json["wallet"]?.jsonPrimitive?.content)
        assertEquals("eth-signature", json["response_protocol"]?.jsonPrimitive?.content)
        assertEquals(tpPayload, json["payload"]?.jsonPrimitive?.content)
        assertEquals(sampleAccount.addressPath, json["address_path"]?.jsonPrimitive?.content)
        assertEquals("Bitkeep", json["origin"]?.jsonPrimitive?.content)
        assertEquals(Web3RequestDataType.PERSONAL_MESSAGE.code.toString(), json["request_data_type_id"]?.jsonPrimitive?.content)
        assertEquals("010203", json["request_sign_data_hex"]?.jsonPrimitive?.content)
    }

    @Test
    fun parsesWeb3AccountResponse() {
        val dataJson = """
            {"web3Account":{
              "address":"${sampleAccount.address}",
              "addressPath":"${sampleAccount.addressPath}",
              "accountPath":"${sampleAccount.accountPath}",
              "masterFingerprint":"${sampleAccount.masterFingerprint}",
              "compressedPubKeyHex":"${sampleAccount.compressedPubKeyHex}",
              "chainCodeHex":"${sampleAccount.chainCodeHex}",
              "xpub":"${sampleAccount.xpub}",
              "sourceLabel":"${sampleAccount.sourceLabel}",
              "importedAt":${sampleAccount.importedAt},
              "label":"Web3 账户",
              "childrenPath":"0/*"
            }}
        """.trimIndent().replace("\n", "")
        val payload = "tp:exportWeb3AccountResult-version=1.0&data=" +
            URLEncoder.encode(dataJson, StandardCharsets.UTF_8.name())
        val parsed = TpResponseParser.parse(payload)
        assertEquals(sampleAccount.address, parsed.web3Account?.address)
        assertEquals(sampleAccount.addressPath, parsed.web3Account?.addressPath)
        assertEquals("0/*", parsed.web3Account?.childrenPath)
    }

    @Test
    fun exportWeb3AccountRequestCarriesSelectedObservationAddress() {
        val payload = TpRequestBuilder.buildExportWeb3AccountRequest(
            addressPath = sampleAccount.addressPath,
            expectedAddress = sampleAccount.address,
            chain = WalletChains.ARBITRUM,
            requestId = "web3-account-test",
        )
        assertTrue(payload.startsWith("tp:exportWeb3Account-", ignoreCase = true))
        assertTrue(payload.contains("data="))
        val dataEncoded = payload.substringAfter("data=")
        val data = Json.parseToJsonElement(URLDecoder.decode(dataEncoded, StandardCharsets.UTF_8.name())).jsonObject
        assertEquals(sampleAccount.address, data["address"]?.jsonPrimitive?.content)
        assertEquals(sampleAccount.address, data["expectedAddress"]?.jsonPrimitive?.content)
        assertEquals(sampleAccount.addressPath, data["path"]?.jsonPrimitive?.content)
        assertEquals(WalletChains.ARBITRUM.chainId, data["chainId"]?.jsonPrimitive?.long)
    }

    @Test
    fun normalizesMessageSignatureToRecoveryIdForEthSignatureUr() {
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = "hello".toByteArray(StandardCharsets.UTF_8),
            dataType = Web3RequestDataType.PERSONAL_MESSAGE,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "Bitget",
            rawUr = "ur:eth-sign-request/test",
        )
        val signatureHex = "0x" + "11".repeat(32) + "22".repeat(32) + "1c"
        val signatureBytes = Web3UrCodec.extractSignatureBytes(
            request = request,
            response = ParsedResponse(
                rawTransaction = null,
                bitcoinTxHex = null,
                signature = signatureHex,
                address = sampleAccount.address,
            ),
        )

        assertEquals(65, signatureBytes.size)
        assertEquals(1, signatureBytes.last().toInt() and 0xFF)
    }

    @Test
    fun parsesAndPreservesNonUuidRequestIdBytes() {
        val requestIdBytes = "tp-request-20260418".toByteArray(StandardCharsets.UTF_8)
        val request = Web3UrCodec.parseEthSignRequestUr(
            buildEthSignRequestUr(ByteString(requestIdBytes)),
        )

        assertEquals("tp-request-20260418", request.requestId)
        assertTrue(request.requestIdDataItem is ByteString)

        val pages = Web3UrCodec.buildEthSignatureQrPages(
            requestId = request.requestId,
            requestIdDataItem = request.requestIdDataItem,
            signatureBytes = ByteArray(65) { index -> index.toByte() },
            origin = "TokenPocket",
        )

        val decoder = URDecoder()
        pages.forEach { page ->
            assertTrue(decoder.receivePart(page))
        }
        val result = decoder.result
        assertEquals(ResultType.SUCCESS, result.type)
        val root = CborDecoder.decode(result.ur.cborBytes).first() as CborMap
        val requestIdItem = root[UnsignedInteger(1)] as ByteString
        assertTrue(requestIdItem.bytes.contentEquals(requestIdBytes))
        assertFalse(requestIdItem.hasTag())
    }

    @Test
    fun extractsEip2930TransactionSignatureParity() {
        val signedTxHex = "0x01f8650180843b9aca008252089411111111111111111111111111111111111111118080c001a0" +
            "11".repeat(32) +
            "a0" +
            "22".repeat(32)
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = byteArrayOf(),
            dataType = Web3RequestDataType.TYPED_TRANSACTION,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "OKX Wallet",
            rawUr = "ur:eth-sign-request/test",
        )

        val signatureBytes = Web3UrCodec.extractSignatureBytes(
            request = request,
            response = ParsedResponse(
                rawTransaction = signedTxHex,
                bitcoinTxHex = null,
                signature = null,
                address = sampleAccount.address,
            ),
        )

        assertEquals(65, signatureBytes.size)
        assertEquals(1, signatureBytes.last().toInt())
        assertEquals(BigInteger("11".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(0, 32)))
        assertEquals(BigInteger("22".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(32, 64)))
    }

    @Test
    fun extractsLegacyTransactionRecoveryIdForNonCompatibleWalletOnNonMainnetChain() {
        val chainId = 57L
        val signedTxHex = "0xf86480843b9aca00825208941111111111111111111111111111111111111111808081" +
            "95" +
            "a0" +
            "11".repeat(32) +
            "a0" +
            "22".repeat(32)
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = byteArrayOf(),
            dataType = Web3RequestDataType.TRANSACTION,
            chainId = chainId,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "Keystone",
            rawUr = "ur:eth-sign-request/test",
        )

        val signatureBytes = Web3UrCodec.extractSignatureBytes(
            request = request,
            response = ParsedResponse(
                rawTransaction = signedTxHex,
                bitcoinTxHex = null,
                signature = null,
                address = sampleAccount.address,
            ),
        )

        assertEquals(65, signatureBytes.size)
        assertEquals(0, signatureBytes.last().toInt())
        assertEquals(BigInteger("11".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(0, 32)))
        assertEquals(BigInteger("22".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(32, 64)))
    }

    @Test
    fun extractsLegacyTransactionMultiByteEip155VForBitgetArbitrum() {
        val signedTxHex = "0xf86680843b9aca00825208941111111111111111111111111111111111111111808083014986" +
            "a0" +
            "11".repeat(32) +
            "a0" +
            "22".repeat(32)
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = byteArrayOf(),
            dataType = Web3RequestDataType.TRANSACTION,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "Bitkeep",
            rawUr = "ur:eth-sign-request/test",
        )

        val signatureBytes = Web3UrCodec.extractSignatureBytes(
            request = request,
            response = ParsedResponse(
                rawTransaction = signedTxHex,
                bitcoinTxHex = null,
                signature = null,
                address = sampleAccount.address,
            ),
        )

        assertEquals(67, signatureBytes.size)
        assertEquals(BigInteger("14986", 16), BigInteger(1, signatureBytes.copyOfRange(64, 67)))
        assertEquals(BigInteger("11".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(0, 32)))
        assertEquals(BigInteger("22".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(32, 64)))
    }

    @Test
    fun extractsLegacyTransactionEip155VForOkxMainnet() {
        val signedTxHex = "0xf86380843b9aca00825208941111111111111111111111111111111111111111808025" +
            "a0" +
            "11".repeat(32) +
            "a0" +
            "22".repeat(32)
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = byteArrayOf(),
            dataType = Web3RequestDataType.TRANSACTION,
            chainId = 1L,
            derivationPath = sampleAccount.addressPath,
            address = sampleAccount.address,
            origin = "OKX Wallet",
            rawUr = "ur:eth-sign-request/test",
        )

        val signatureBytes = Web3UrCodec.extractSignatureBytes(
            request = request,
            response = ParsedResponse(
                rawTransaction = signedTxHex,
                bitcoinTxHex = null,
                signature = null,
                address = sampleAccount.address,
            ),
        )

        assertEquals(65, signatureBytes.size)
        assertEquals(37, signatureBytes.last().toInt() and 0xFF)
        assertEquals(BigInteger("11".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(0, 32)))
        assertEquals(BigInteger("22".repeat(32), 16), BigInteger(1, signatureBytes.copyOfRange(32, 64)))
    }

    @Test
    fun buildsTransactionTpRequestEvenWhenWeb3RequestOmitsAddress() {
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = hexToBytes("dc80018252089411111111111111111111111111111111111111118080"),
            dataType = Web3RequestDataType.TRANSACTION,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = null,
            origin = "OKX Wallet",
            rawUr = "ur:eth-sign-request/test",
        )

        val payload = Web3UrCodec.buildTpRequest(request)

        assertTrue(payload.startsWith("tp:signTransaction-"))
        val data = parsePayloadDataJson(payload)
        val txData = data["txData"]?.jsonObject
        assertEquals(sampleAccount.addressPath, parseQueryValue(payload, "path"))
        assertEquals(null, data.string("address"))
        assertEquals(null, txData?.string("from"))
        assertEquals("0x1111111111111111111111111111111111111111", txData?.string("to"))
    }

    @Test
    fun buildTpRequestUsesBoundAccountWhenWalletRequestOmitsAddress() {
        val request = Web3EthSignRequest(
            requestId = "123e4567-e89b-12d3-a456-426614174000",
            signData = hexToBytes("dc80018252089411111111111111111111111111111111111111118080"),
            dataType = Web3RequestDataType.TRANSACTION,
            chainId = WalletChains.ARBITRUM.chainId,
            derivationPath = sampleAccount.addressPath,
            address = null,
            origin = "Bitget Wallet",
            rawUr = "ur:eth-sign-request/test",
        )

        val payload = Web3UrCodec.buildTpRequest(request, sampleAccount)

        val data = parsePayloadDataJson(payload)
        val txData = data["txData"]?.jsonObject
        assertEquals(sampleAccount.address, data.string("address"))
        assertEquals(sampleAccount.address, txData?.string("from"))
    }

    private fun parseQueryValue(payload: String, key: String): String? {
        val query = payload.substringAfter('-', missingDelimiterValue = "")
        return query.split('&')
            .mapNotNull { piece ->
                val separator = piece.indexOf('=')
                if (separator <= 0) {
                    null
                } else {
                    piece.substring(0, separator) to URLDecoder.decode(piece.substring(separator + 1), StandardCharsets.UTF_8.name())
                }
            }
            .firstOrNull { it.first == key }
            ?.second
    }

    private fun parsePayloadDataJson(payload: String) =
        Json.parseToJsonElement(parseQueryValue(payload, "data") ?: error("payload 缺少 data")).jsonObject

    private fun inflateUrlBase64(encoded: String): String {
        val padded = encoded + "=".repeat((4 - encoded.length % 4) % 4)
        val inflater = Inflater()
        val input = Base64.getUrlDecoder().decode(padded)
        inflater.setInput(input)
        val out = ByteArray(2048)
        val count = inflater.inflate(out)
        inflater.end()
        return String(out, 0, count, StandardCharsets.UTF_8)
    }

    private fun buildEthSignRequestUr(requestIdItem: DataItem): String {
        val keypath = CryptoKeypath(
            listOf(
                IndexPathComponent(44, true),
                IndexPathComponent(60, true),
                IndexPathComponent(0, true),
                IndexPathComponent(0, false),
                IndexPathComponent(0, false),
            ),
            null,
        )
        val map = CborMap()
        map.put(UnsignedInteger(1), requestIdItem)
        map.put(UnsignedInteger(2), ByteString("hello".toByteArray(StandardCharsets.UTF_8)))
        map.put(UnsignedInteger(3), UnsignedInteger(Web3RequestDataType.PERSONAL_MESSAGE.code.toLong()))
        map.put(UnsignedInteger(4), UnsignedInteger(WalletChains.ARBITRUM.chainId))
        map.put(UnsignedInteger(5), keypath.toCbor())
        map.put(UnsignedInteger(6), ByteString(hexToBytes(sampleAccount.address)))
        map.put(UnsignedInteger(7), UnicodeString("TokenPocket"))
        val out = ByteArrayOutputStream()
        CborEncoder(out).encode(map)
        return UREncoder.encode(UR("eth-sign-request", out.toByteArray()))
    }

    private fun hexToBytes(value: String): ByteArray {
        val clean = value.removePrefix("0x").removePrefix("0X")
        require(clean.length % 2 == 0) { "十六进制长度不正确" }
        return ByteArray(clean.length / 2) { index ->
            clean.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun kotlinx.serialization.json.JsonObject.string(key: String): String? =
        this[key]?.jsonPrimitive?.contentOrNull
}
