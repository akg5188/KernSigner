package io.arbitrum.wallet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.math.BigInteger
import java.util.Base64
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.util.Locale

class TpResponseParserTest {
    @Test
    fun parsesBtctxSignedBitcoinTransaction() {
        val parsed = TpResponseParser.parse("btctx:$signedTxHex")

        assertFalse(parsed.isError)
        assertEquals(signedTxHex, parsed.bitcoinTxHex)
    }

    @Test
    fun parsesElectrumBase43SignedBitcoinTransaction() {
        val parsed = TpResponseParser.parse(base43Encode(hexToBytes(signedTxHex)))

        assertFalse(parsed.isError)
        assertEquals(signedTxHex, parsed.bitcoinTxHex)
    }

    @Test
    fun parsesElectrumBase43SignedBitcoinTransactionWithPrefix() {
        val parsed = TpResponseParser.parse("electrum:${base43Encode(hexToBytes(signedTxHex))}")

        assertFalse(parsed.isError)
        assertEquals(signedTxHex, parsed.bitcoinTxHex)
    }

    @Test
    fun parsesBitcoinUriSignedTransactionQuery() {
        val encoded = URLEncoder.encode(base43Encode(hexToBytes(signedTxHex)), StandardCharsets.UTF_8.name())
        val parsed = TpResponseParser.parse("bitcoin:?tx=$encoded")

        assertFalse(parsed.isError)
        assertEquals(signedTxHex, parsed.bitcoinTxHex)
    }

    @Test
    fun parsesRawBitcoinTransactionHexWithWhitespace() {
        val wrapped = signedTxHex.chunked(32).joinToString("\n")
        val parsed = TpResponseParser.parse(wrapped)

        assertFalse(parsed.isError)
        assertEquals(signedTxHex, parsed.bitcoinTxHex)
    }

    @Test
    fun doesNotTreatEthSignatureAsBitcoinTransaction() {
        val parsed = TpResponseParser.parse("0x${"11".repeat(65)}")

        assertTrue(parsed.isError)
        assertNull(parsed.bitcoinTxHex)
    }

    @Test
    fun parsesElectrumBase43Psbt() {
        val psbtBytes = byteArrayOf(
            0x70,
            0x73,
            0x62,
            0x74,
            0xff.toByte(),
            0x01,
            0x00,
        )
        val parsed = ElectrumQrCodec.parsePsbt(base43Encode(psbtBytes))

        assertEquals(Base64.getEncoder().withoutPadding().encodeToString(psbtBytes), parsed?.base64)
        assertEquals(psbtBytes.size, parsed?.byteCount)
    }

    @Test
    fun encodesPsbtBase64BackToElectrumBase43() {
        val psbtBytes = byteArrayOf(
            0x70,
            0x73,
            0x62,
            0x74,
            0xff.toByte(),
            0x01,
            0x00,
        )
        val base64 = Base64.getEncoder().withoutPadding().encodeToString(psbtBytes)

        assertEquals(base43Encode(psbtBytes), ElectrumQrCodec.encodeBase43FromPsbtBase64(base64))
    }

    private fun hexToBytes(value: String): ByteArray {
        val clean = value.lowercase(Locale.US)
        return ByteArray(clean.length / 2) { index ->
            clean.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun base43Encode(bytes: ByteArray): String {
        val alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:"
        val leadingZeros = bytes.takeWhile { it == 0.toByte() }.size
        var number = BigInteger(1, bytes)
        val base = BigInteger.valueOf(43L)
        val encoded = StringBuilder()
        while (number > BigInteger.ZERO) {
            val divRem = number.divideAndRemainder(base)
            encoded.append(alphabet[divRem[1].toInt()])
            number = divRem[0]
        }
        repeat(leadingZeros) { encoded.append(alphabet.first()) }
        return encoded.reverse().toString()
    }

    private companion object {
        private const val signedTxHex =
            "0100000001" +
                "0000000000000000000000000000000000000000000000000000000000000000" +
                "00000000" +
                "6a" +
                "47304402204e45e16932b8af514961a1d3a1a25fdf3f4f7732e9d624c6c61548ab5fb8cd410220181522ec8eca07de4860a4acdd12909d831cc56cbbac4622082221a8768d1d0901" +
                "21" +
                "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798" +
                "ffffffff" +
                "01" +
                "00f2052a01000000" +
                "19" +
                "76a91489abcdefabbaabbaabbaabbaabbaabbaabbaabba88ac" +
                "00000000"
    }
}
