package com.emittech.rgbremote

/** A pretend board, so the screens can be tried without one. */
object Demo {
    private val rgb = listOf(
        100 to (0 to 0), 100 to (25 to 0), 100 to (50 to 0), 50 to (100 to 0), 25 to (100 to 0), 0 to (100 to 0), 0 to (100 to 25), 0 to (100 to 50),
        0 to (50 to 100), 0 to (25 to 100), 0 to (0 to 100), 25 to (0 to 100), 50 to (0 to 100), 100 to (0 to 50), 100 to (0 to 25),
    )
    val info = BoardInfo(
        rgb.map { (r, gb) -> BoardColor(r, gb.first, gb.second) },
        listOf("rainbow", "random", "party", "police", "fire", "ocean", "forest", "sunset", "strobe", "breathe", "custom"),
    )
    val state = LightState("Demo", "", 1, 12, 11, "none", 50, false, "white", listOf(0, 5, 10), 0, 0, "")

    fun apply(s: LightState, a: Map<String, String>): LightState {
        var t = s
        a["on"]?.let { t = t.copy(on = it.toInt(), effect = if (it == "0") "none" else t.effect) }
        a["color"]?.let { t = t.copy(color = it.toInt(), on = 1, effect = "none", u4mode = false) }
        a["bright"]?.let { t = t.copy(bright = it.toInt()) }
        a["speed"]?.let { t = t.copy(speed = it.toInt()) }
        a["effect"]?.let { t = t.copy(effect = it, on = 1, u4mode = false) }
        a["custom"]?.let { t = t.copy(custom = it.split(',').map(String::toInt)) }
        a["u4"]?.let { t = t.copy(mode = it, u4mode = true, on = 1, effect = "none", color = -1) }
        a["mode"]?.let { t = t.copy(u4mode = true, effect = "none", color = -1) }
        return t
    }
}
