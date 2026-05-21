package io.arbitrum.wallet

import java.io.File
import org.junit.Assert.assertNotNull
import org.junit.Test

class ManualVideoQrDecodeTest {
    @Test
    fun requestResolverCompletesFromCapturedFragmentsWhenProvided() {
        val fragmentsPath = System.getProperty("videoQrFragmentsFile")?.trim().orEmpty()
        if (fragmentsPath.isBlank()) return

        val fragments = File(fragmentsPath)
            .readLines()
            .map(String::trim)
            .filter(String::isNotBlank)
        if (fragments.isEmpty()) return

        val resolver = RequestQrPayloadResolver()
        var payload: String? = null
        for (fragment in fragments) {
            when (val result = resolver.accept(fragment)) {
                is RequestQrResolution.Complete -> {
                    payload = result.payload
                    break
                }

                is RequestQrResolution.Progress -> Unit
            }
        }

        assertNotNull("resolver should reconstruct a request payload from captured fragments", payload)
    }
}
