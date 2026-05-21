package io.arbitrum.wallet

import java.math.BigInteger
import java.security.MessageDigest
import java.util.Locale
import org.bitcoinj.core.LegacyAddress
import org.bitcoinj.core.NetworkParameters
import org.bitcoinj.core.SegwitAddress
import org.bitcoinj.core.Utils
import org.bitcoinj.crypto.ChildNumber
import org.bitcoinj.crypto.DeterministicKey
import org.bitcoinj.crypto.HDKeyDerivation
import org.bitcoinj.params.MainNetParams
import org.bitcoinj.params.TestNet3Params
import org.bitcoinj.script.ScriptBuilder

private const val BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
private const val RECEIVE_PREVIEW_COUNT = 5
private const val CHANGE_PREVIEW_COUNT = 3
private val base58CandidateRegex = Regex("[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]{16,}")
private val base58Radix = BigInteger.valueOf(58L)
private val mainnetXpubVersion = byteArrayOf(0x04, 0x88.toByte(), 0xB2.toByte(), 0x1E)
private val testnetTpubVersion = byteArrayOf(0x04, 0x35, 0x87.toByte(), 0xCF.toByte())

data class BitcoinWatchAccountImport(
    val xpub: String,
    val prefix: String,
    val networkLabel: String,
    val scriptTypeLabel: String,
    val accountPathHint: String,
    val defaultLabel: String,
)

data class BitcoinDerivedKeyMaterial(
    val branch: Int,
    val branchLabel: String,
    val index: Int,
    val path: String,
    val address: String,
    val scriptPubKey: ByteArray,
    val publicKey: ByteArray,
)

private data class BitcoinXpubPrefixMeta(
    val prefix: String,
    val networkLabel: String,
    val scriptTypeLabel: String,
    val accountPathHint: String,
    val defaultLabel: String,
)

private val bitcoinXpubPrefixMeta = listOf(
    BitcoinXpubPrefixMeta(
        prefix = "xpub",
        networkLabel = "Bitcoin Mainnet",
        scriptTypeLabel = "Legacy / BIP44",
        accountPathHint = "m/44'/0'/0'",
        defaultLabel = "BTC Legacy",
    ),
    BitcoinXpubPrefixMeta(
        prefix = "ypub",
        networkLabel = "Bitcoin Mainnet",
        scriptTypeLabel = "Nested SegWit / BIP49",
        accountPathHint = "m/49'/0'/0'",
        defaultLabel = "BTC Nested SegWit",
    ),
    BitcoinXpubPrefixMeta(
        prefix = "zpub",
        networkLabel = "Bitcoin Mainnet",
        scriptTypeLabel = "Native SegWit / BIP84",
        accountPathHint = "m/84'/0'/0'",
        defaultLabel = "BTC Native SegWit",
    ),
    BitcoinXpubPrefixMeta(
        prefix = "tpub",
        networkLabel = "Bitcoin Testnet",
        scriptTypeLabel = "Legacy / BIP44",
        accountPathHint = "m/44'/1'/0'",
        defaultLabel = "BTC Testnet Legacy",
    ),
    BitcoinXpubPrefixMeta(
        prefix = "upub",
        networkLabel = "Bitcoin Testnet",
        scriptTypeLabel = "Nested SegWit / BIP49",
        accountPathHint = "m/49'/1'/0'",
        defaultLabel = "BTC Testnet Nested SegWit",
    ),
    BitcoinXpubPrefixMeta(
        prefix = "vpub",
        networkLabel = "Bitcoin Testnet",
        scriptTypeLabel = "Native SegWit / BIP84",
        accountPathHint = "m/84'/1'/0'",
        defaultLabel = "BTC Testnet Native SegWit",
    ),
)

fun parseBitcoinWatchAccountImport(raw: String?): BitcoinWatchAccountImport? {
    val candidate = extractBitcoinXpubCandidate(raw) ?: return null
    val meta = bitcoinXpubPrefixMeta.firstOrNull { candidate.startsWith(it.prefix) } ?: return null
    if (candidate.any { it !in BASE58_ALPHABET }) return null
    return BitcoinWatchAccountImport(
        xpub = candidate,
        prefix = meta.prefix,
        networkLabel = meta.networkLabel,
        scriptTypeLabel = meta.scriptTypeLabel,
        accountPathHint = meta.accountPathHint,
        defaultLabel = meta.defaultLabel,
    )
}

private fun extractBitcoinXpubCandidate(raw: String?): String? {
    val input = raw.orEmpty().trim()
    if (input.isBlank()) return null

    val matches = base58CandidateRegex.findAll(input)
        .map { it.value }
        .toList()

    if (matches.isNotEmpty()) {
        return matches.firstOrNull { candidate ->
            bitcoinXpubPrefixMeta.any { candidate.startsWith(it.prefix) }
        }
    }

    val compact = input.filterNot(Char::isWhitespace)
    return compact.takeIf { candidate ->
        bitcoinXpubPrefixMeta.any { candidate.startsWith(it.prefix) }
    }
}

fun defaultBitcoinPrototypeStatus(accounts: Int): String {
    return if (accounts > 0) {
        "已导入 $accounts 个 BTC 观察账户。首页只保留摘要，点进账户后可同步余额、查看地址，并准备树莓派签名的 BTC 转账。"
    } else {
        "先从树莓派导出 xpub/ypub/zpub，再导入这里只读保存。导入后可在账户详情里同步余额、查看地址，并准备 BTC 转账签名。"
    }
}

fun normalizeExtendedPublicKeyForDecode(xpub: String, prefix: String): String {
    return when (prefix.lowercase()) {
        "xpub", "tpub" -> xpub
        "ypub", "zpub", "upub", "vpub" -> {
            val decoded = decodeBase58Check(xpub)
            require(decoded.size > 4) { "扩展公钥长度无效" }
            val normalizedVersion = when (prefix.lowercase()) {
                "ypub", "zpub" -> mainnetXpubVersion
                "upub", "vpub" -> testnetTpubVersion
                else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
            }
            encodeBase58Check(normalizedVersion + decoded.copyOfRange(4, decoded.size))
        }
        else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
    }
}

fun enrichBitcoinWatchAccount(account: BitcoinWatchAccount): BitcoinWatchAccount {
    val parsed = parseBitcoinWatchAccountImport(account.xpub)
        ?: return account.copy(
            receivePreview = emptyList(),
            changePreview = emptyList(),
            derivationError = "无效的 BTC 扩展公钥前缀",
        )

    return runCatching {
        val normalizedXpub = normalizeExtendedPublicKeyForDecode(parsed.xpub, parsed.prefix)
        val params = bitcoinNetworkParamsForPrefix(parsed.prefix)
        val xpub = DeterministicKey.deserializeB58(normalizedXpub, params)
        val receivePreview = deriveAddressPreviewBranch(
            xpub = xpub,
            prefix = parsed.prefix,
            params = params,
            accountPathHint = parsed.accountPathHint,
            branch = 0,
            branchLabel = "收款",
            count = RECEIVE_PREVIEW_COUNT,
        )
        val changePreview = deriveAddressPreviewBranch(
            xpub = xpub,
            prefix = parsed.prefix,
            params = params,
            accountPathHint = parsed.accountPathHint,
            branch = 1,
            branchLabel = "找零",
            count = CHANGE_PREVIEW_COUNT,
        )

        account.copy(
            prefix = parsed.prefix,
            networkLabel = parsed.networkLabel,
            scriptTypeLabel = parsed.scriptTypeLabel,
            accountPathHint = parsed.accountPathHint,
            accountFingerprintHex = String.format(Locale.US, "%08x", xpub.fingerprint.toLong() and 0xffffffffL),
            receivePreview = receivePreview,
            changePreview = changePreview,
            derivationError = "",
        )
    }.getOrElse { error ->
        account.copy(
            receivePreview = emptyList(),
            changePreview = emptyList(),
            derivationError = "BTC 地址派生失败: ${error.message ?: "未知错误"}",
        )
    }
}

private fun deriveAddressPreviewBranch(
    xpub: DeterministicKey,
    prefix: String,
    params: NetworkParameters,
    accountPathHint: String,
    branch: Int,
    branchLabel: String,
    count: Int,
): List<BitcoinDerivedAddressPreview> {
    val branchKey = HDKeyDerivation.deriveChildKey(xpub, ChildNumber(branch, false))
    return List(count) { index ->
        val child = HDKeyDerivation.deriveChildKey(branchKey, ChildNumber(index, false))
        BitcoinDerivedAddressPreview(
            branch = branch,
            branchLabel = branchLabel,
            index = index,
            path = "$accountPathHint/$branch/$index",
            address = deriveAddressForPrefix(
                childKey = child,
                prefix = prefix,
                params = params,
            ),
        )
    }
}

fun deriveBitcoinKeyMaterial(
    account: BitcoinWatchAccount,
    branch: Int,
    index: Int,
): BitcoinDerivedKeyMaterial {
    val parsed = parseBitcoinWatchAccountImport(account.xpub)
        ?: error("无效的 BTC 扩展公钥前缀")
    val normalizedXpub = normalizeExtendedPublicKeyForDecode(parsed.xpub, parsed.prefix)
    val params = bitcoinNetworkParamsForPrefix(parsed.prefix)
    val xpub = DeterministicKey.deserializeB58(normalizedXpub, params)
    val branchLabel = if (branch == 1) "找零" else "收款"
    val child = HDKeyDerivation.deriveChildKey(
        HDKeyDerivation.deriveChildKey(xpub, ChildNumber(branch, false)),
        ChildNumber(index, false),
    )
    return BitcoinDerivedKeyMaterial(
        branch = branch,
        branchLabel = branchLabel,
        index = index,
        path = "${parsed.accountPathHint}/$branch/$index",
        address = deriveAddressForPrefix(child, parsed.prefix, params),
        scriptPubKey = deriveOutputScriptForPrefix(child, parsed.prefix, params),
        publicKey = child.pubKey,
    )
}

private fun deriveAddressForPrefix(
    childKey: DeterministicKey,
    prefix: String,
    params: NetworkParameters,
): String {
    return when (prefix.lowercase()) {
        "xpub", "tpub" -> LegacyAddress.fromKey(params, childKey).toString()
        "ypub", "upub" -> {
            val redeemScript = ScriptBuilder.createP2WPKHOutputScript(childKey)
            val redeemScriptHash = Utils.sha256hash160(redeemScript.program)
            LegacyAddress.fromScriptHash(params, redeemScriptHash).toString()
        }
        "zpub", "vpub" -> SegwitAddress.fromKey(params, childKey).toString()
        else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
    }
}

private fun deriveOutputScriptForPrefix(
    childKey: DeterministicKey,
    prefix: String,
    params: NetworkParameters,
): ByteArray {
    return when (prefix.lowercase()) {
        "xpub", "tpub" -> ScriptBuilder.createOutputScript(LegacyAddress.fromKey(params, childKey)).program
        "ypub", "upub" -> {
            val redeemScript = ScriptBuilder.createP2WPKHOutputScript(childKey)
            val redeemScriptHash = Utils.sha256hash160(redeemScript.program)
            ScriptBuilder.createOutputScript(LegacyAddress.fromScriptHash(params, redeemScriptHash)).program
        }
        "zpub", "vpub" -> ScriptBuilder.createOutputScript(SegwitAddress.fromKey(params, childKey)).program
        else -> error("不支持的 BTC 扩展公钥前缀: $prefix")
    }
}

fun bitcoinNetworkParamsForPrefix(prefix: String): NetworkParameters {
    return when (prefix.lowercase()) {
        "xpub", "ypub", "zpub" -> MainNetParams.get()
        "tpub", "upub", "vpub" -> TestNet3Params.get()
        else -> error("不支持的 BTC 网络前缀: $prefix")
    }
}

fun bitcoinEsploraBaseUrl(prefix: String): String {
    return when (prefix.lowercase()) {
        "xpub", "ypub", "zpub" -> "https://blockstream.info/api"
        "tpub", "upub", "vpub" -> "https://blockstream.info/testnet/api"
        else -> error("不支持的 BTC 网络前缀: $prefix")
    }
}

fun formatBitcoinSats(sats: Long): String {
    return "%,.8f BTC".format(Locale.US, sats.toDouble() / 100_000_000.0)
}

private fun decodeBase58Check(value: String): ByteArray {
    var decoded = BigInteger.ZERO
    value.forEach { char ->
        val digit = BASE58_ALPHABET.indexOf(char)
        require(digit >= 0) { "扩展公钥包含无效字符" }
        decoded = decoded.multiply(base58Radix).add(BigInteger.valueOf(digit.toLong()))
    }

    val decodedBytes = decoded.toByteArray().let { bytes ->
        if (bytes.size > 1 && bytes.first() == 0.toByte()) {
            bytes.copyOfRange(1, bytes.size)
        } else {
            bytes
        }
    }
    val leadingZeroCount = value.takeWhile { it == '1' }.count()
    val raw = ByteArray(leadingZeroCount + decodedBytes.size)
    decodedBytes.copyInto(raw, destinationOffset = leadingZeroCount)

    require(raw.size >= 4) { "扩展公钥校验和缺失" }
    val payload = raw.copyOfRange(0, raw.size - 4)
    val checksum = raw.copyOfRange(raw.size - 4, raw.size)
    require(doubleSha256(payload).copyOfRange(0, 4).contentEquals(checksum)) { "扩展公钥校验和无效" }
    return payload
}

private fun encodeBase58Check(payload: ByteArray): String {
    val checksum = doubleSha256(payload).copyOfRange(0, 4)
    val raw = payload + checksum
    val prefix = buildString {
        repeat(raw.takeWhile { it == 0.toByte() }.count()) {
            append('1')
        }
    }
    val value = BigInteger(1, raw)
    if (value == BigInteger.ZERO) return prefix.ifEmpty { "1" }

    var number = value
    val encoded = StringBuilder()
    while (number > BigInteger.ZERO) {
        val divRem = number.divideAndRemainder(base58Radix)
        encoded.append(BASE58_ALPHABET[divRem[1].toInt()])
        number = divRem[0]
    }
    return prefix + encoded.reverse().toString()
}

private fun doubleSha256(payload: ByteArray): ByteArray {
    val digest = MessageDigest.getInstance("SHA-256")
    return digest.digest(digest.digest(payload))
}
