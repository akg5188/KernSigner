package io.arbitrum.wallet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ResponseQrPayloadResolverTest {
    @Test
    fun returnsSinglePayloadImmediately() {
        val resolver = ResponseQrPayloadResolver()
        val result = resolver.accept("signed-result")

        assertEquals(ResponseQrResolution.Complete("signed-result"), result)
    }

    @Test
    fun assemblesPMofNResponseParts() {
        val resolver = ResponseQrPayloadResolver()

        val first = resolver.accept("p1of3 abc")
        val third = resolver.accept("p3of3 ghi")
        val second = resolver.accept("p2of3 def")

        assertTrue(first is ResponseQrResolution.Progress)
        assertTrue(third is ResponseQrResolution.Progress)
        assertEquals(ResponseQrResolution.Complete("abcdefghi"), second)
    }
}
