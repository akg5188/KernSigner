package io.arbitrum.wallet

import java.math.BigInteger

sealed interface RlpValue {
    data class Bytes(val value: ByteArray) : RlpValue
    data class ListValue(val value: List<RlpValue>) : RlpValue
}

object RlpCodec {
    fun decode(payload: ByteArray): RlpValue {
        val (value, nextOffset) = decodeAt(payload, 0)
        require(nextOffset == payload.size) { "RLP 数据存在多余字节" }
        return value
    }

    private fun decodeAt(payload: ByteArray, offset: Int): Pair<RlpValue, Int> {
        require(offset < payload.size) { "RLP 数据不完整" }
        val prefix = payload[offset].toInt() and 0xFF
        return when {
            prefix <= 0x7F -> {
                RlpValue.Bytes(byteArrayOf(payload[offset])) to (offset + 1)
            }

            prefix <= 0xB7 -> {
                val length = prefix - 0x80
                val start = offset + 1
                val end = start + length
                require(end <= payload.size) { "RLP 字节串越界" }
                RlpValue.Bytes(payload.copyOfRange(start, end)) to end
            }

            prefix <= 0xBF -> {
                val lengthOfLength = prefix - 0xB7
                val start = offset + 1
                val length = parseLength(payload, start, lengthOfLength)
                val dataStart = start + lengthOfLength
                val dataEnd = dataStart + length
                require(dataEnd <= payload.size) { "RLP 长字节串越界" }
                RlpValue.Bytes(payload.copyOfRange(dataStart, dataEnd)) to dataEnd
            }

            prefix <= 0xF7 -> {
                val length = prefix - 0xC0
                val start = offset + 1
                val end = start + length
                require(end <= payload.size) { "RLP 列表越界" }
                RlpValue.ListValue(decodeList(payload, start, end)) to end
            }

            else -> {
                val lengthOfLength = prefix - 0xF7
                val start = offset + 1
                val length = parseLength(payload, start, lengthOfLength)
                val dataStart = start + lengthOfLength
                val dataEnd = dataStart + length
                require(dataEnd <= payload.size) { "RLP 长列表越界" }
                RlpValue.ListValue(decodeList(payload, dataStart, dataEnd)) to dataEnd
            }
        }
    }

    private fun decodeList(payload: ByteArray, start: Int, end: Int): List<RlpValue> {
        val values = mutableListOf<RlpValue>()
        var cursor = start
        while (cursor < end) {
            val (item, next) = decodeAt(payload, cursor)
            values += item
            cursor = next
        }
        require(cursor == end) { "RLP 列表边界不匹配" }
        return values
    }

    private fun parseLength(payload: ByteArray, offset: Int, lengthOfLength: Int): Int {
        require(lengthOfLength in 1..8) { "RLP 长度字段不合法" }
        val end = offset + lengthOfLength
        require(end <= payload.size) { "RLP 长度字段越界" }
        var result = 0
        for (index in offset until end) {
            result = (result shl 8) or (payload[index].toInt() and 0xFF)
        }
        return result
    }
}

fun RlpValue.requireBytes(label: String): ByteArray =
    (this as? RlpValue.Bytes)?.value ?: throw IllegalArgumentException("$label 不是字节串")

fun RlpValue.requireList(label: String): List<RlpValue> =
    (this as? RlpValue.ListValue)?.value ?: throw IllegalArgumentException("$label 不是列表")

fun RlpValue.toQuantity(): BigInteger {
    val bytes = requireBytes("RLP 数值")
    return if (bytes.isEmpty()) BigInteger.ZERO else BigInteger(1, bytes)
}
