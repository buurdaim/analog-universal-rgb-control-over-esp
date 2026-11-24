package com.emittech.rgbremote

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtoTest {
    // As the firmware printed it.
    private val real = """{"name":"RGB-Remote-ED20","fw":"1.0","changes":1,"on":-1,"color":-1,"bright":-1,"effect":"none","speed":50,"u4mode":false,"mode":"","custom":[0,5,10],"sent":0,"heard":0,"remote":0,"wifi":"","ip":"","rssi":0}"""

    @Test fun readsTheFirmwaresState() {
        val s = Proto.state(real)!!
        assertEquals("RGB-Remote-ED20", s.name)
        assertEquals(-1, s.on)
        assertEquals(listOf(0, 5, 10), s.custom)
        assertFalse(s.noEcho)
        assertTrue(s.copy(sent = 4).noEcho)
        assertNull(Proto.state("<html>"))
        assertNull(Proto.state("{}"))
    }

    @Test fun readsInfo() {
        val i = Proto.info("""{"name":"x","fw":"1.0","colors":[[59,100,0,0],[63,100,25,0]],"bright":[16,20],"effects":["rainbow","random"],"modes":["breathe3"]}""")!!
        assertEquals(BoardColor(100, 25, 0), i.colors[1])
        assertEquals(listOf("rainbow", "random"), i.effects)
        assertNull(Proto.info("[]"))
    }

    @Test fun addresses() {
        assertEquals("192.168.1.50", Proto.address(" http://192.168.1.50/ "))
        assertEquals("192.168.1.50:8080", Proto.address("192.168.1.50:8080"))
        assertEquals("rgb-remote-ed20.local", Proto.address("rgb-remote-ed20.local"))
        for (bad in listOf("", "192.168.1", "300.1.1.1", "a b", "x:0", "x:y", "1.2.3.4.5")) assertNull(bad, Proto.address(bad))
    }

    @Test fun ring() {
        assertEquals(0, Proto.ringSlice(0f, -10f, 15))
        assertEquals(0, Proto.ringSlice(0.5f, -10f, 15))
        assertEquals(0, Proto.ringSlice(-0.5f, -10f, 15))
        assertEquals(5, Proto.ringSlice(8.66f, 5f, 15))    // 120 degrees clockwise: green
        assertEquals(10, Proto.ringSlice(-8.66f, 5f, 15))  // 240 degrees: blue
        for (k in 0 until 15) {
            val a = Math.toRadians(k * 24.0)
            assertEquals(k, Proto.ringSlice(Math.sin(a).toFloat(), -Math.cos(a).toFloat(), 15))
        }
        assertNotNull(Proto.names("""["a","b"]"""))
        assertEquals(emptyList<String>(), Proto.names("nope"))
    }
}
