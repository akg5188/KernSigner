package io.arbitrum.wallet

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class VideoFrameSamplerTest {
    @Test
    fun zeroDurationFallsBackToFirstFrame() {
        assertEquals(listOf(0L), VideoFrameSampler.buildTimestamps(0L))
    }

    @Test
    fun shortVideosUseDenseSampling() {
        val durationUs = 5_000_000L
        val timestamps = VideoFrameSampler.buildTimestamps(durationUs)

        assertEquals(0L, timestamps.first())
        assertEquals(durationUs, timestamps.last())
        assertTrue(timestamps.size >= 20)
        assertTrue(timestamps.zipWithNext().all { (left, right) -> right > left })
    }

    @Test
    fun longVideosStayWithinFrameBudget() {
        val durationUs = 5L * 60L * 1_000_000L
        val timestamps = VideoFrameSampler.buildTimestamps(durationUs)

        assertEquals(durationUs, timestamps.last())
        assertTrue(timestamps.size <= VideoFrameSampler.MAX_FRAMES + 1)
    }
}
