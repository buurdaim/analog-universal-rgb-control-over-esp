package com.emittech.rgbremote

import org.json.JSONObject

/** Where a board is. The name stays, the address may change. */
data class BoardAt(val name: String, val address: String)

/** What the board says about the light. U4 never answers, so -1 means not known yet. */
data class LightState(
    val name: String,
    val fw: String,
    val on: Int,
    val color: Int,
    val bright: Int,
    val effect: String,
    val speed: Int,
    val u4mode: Boolean,
    val mode: String,
    val custom: List<Int>,
    val sent: Long,
    val heard: Long,
    val ip: String,
) {
    /** Codes go out but never come back on the line: the jumper is off. */
    val noEcho get() = sent >= 4 && heard == 0L
}

data class BoardColor(val r: Int, val g: Int, val b: Int)
data class BoardInfo(val colors: List<BoardColor>, val effects: List<String>)

/** The plain parts: no Android in here, so they are tested on the computer. */
object Proto {
    fun state(text: String): LightState? = runCatching {
        val j = JSONObject(text)
        val c = j.optJSONArray("custom")
        LightState(
            name = j.getString("name"),
            fw = j.optString("fw"),
            on = j.getInt("on"),
            color = j.getInt("color"),
            bright = j.getInt("bright"),
            effect = j.getString("effect"),
            speed = j.getInt("speed"),
            u4mode = j.optBoolean("u4mode"),
            mode = j.optString("mode"),
            custom = if (c == null) emptyList() else List(c.length()) { c.getInt(it) },
            sent = j.optLong("sent"),
            heard = j.optLong("heard"),
            ip = j.optString("ip"),
        )
    }.getOrNull()

    fun info(text: String): BoardInfo? = runCatching {
        val j = JSONObject(text)
        val c = j.getJSONArray("colors")
        val e = j.getJSONArray("effects")
        BoardInfo(
            List(c.length()) { val k = c.getJSONArray(it); BoardColor(k.getInt(1), k.getInt(2), k.getInt(3)) },
            List(e.length()) { e.getString(it) },
        )
    }.getOrNull()

    fun names(text: String): List<String> = runCatching {
        val a = org.json.JSONArray(text)
        List(a.length()) { a.getString(it) }
    }.getOrDefault(emptyList())

    /** What a person typed: 192.168.1.50, http://192.168.1.50/, rgb-remote-ed20.local */
    fun address(typed: String): String? {
        val s = typed.trim().removePrefix("http://").substringBefore('/')
        val host = s.substringBefore(':')
        if (host.isEmpty() || s.length > 253) return null
        if (s.contains(':') && (s.substringAfter(':').toIntOrNull() ?: return null) !in 1..65535) return null
        if (!host.all { it.isLetterOrDigit() || it == '.' || it == '-' }) return null
        if (host.all { it.isDigit() || it == '.' }) {
            val p = host.split('.')
            if (p.size != 4 || p.any { it.isEmpty() || it.length > 3 || it.toInt() > 255 }) return null
        }
        return s
    }

    /** Which of n equal slices of a ring a touch is on; slice 0 is centred at the top, going clockwise. */
    fun ringSlice(dx: Float, dy: Float, n: Int): Int {
        val turn = (Math.atan2(dx.toDouble(), -dy.toDouble()) / (2 * Math.PI) + 1.0) % 1.0
        return Math.floor(turn * n + 0.5).toInt() % n
    }
}
