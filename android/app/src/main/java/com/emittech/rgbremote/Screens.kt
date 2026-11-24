package com.emittech.rgbremote

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt

object Ink {
    val Ground = Color(0xFF101216)
    val Card = Color(0xFF1B1F26)
    val Chip = Color(0xFF2A303A)
    val Text = Color(0xFFF1F3F6)
    val Soft = Color(0xFFA3ABB8)
    val Warn = Color(0xFFFFB454)
    val Good = Color(0xFF6FD08C)
}

private val Head = TextStyle(fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = Ink.Soft)

/** How a color of the LEDs looks on a screen: a quarter of the light looks like about half. */
private fun BoardColor.onScreen() = Color(lift(r), lift(g), lift(b))
private fun lift(p: Int) = (p / 100f).pow(1 / 2.2f).coerceIn(0f, 1f)

@Composable
private fun Card(content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(Ink.Card).padding(16.dp)) { content() }
}

@Composable
private fun Chip(text: String, on: Boolean = false, tint: Color = Color(0xFF8AB4FF), onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(14.dp)).background(if (on) tint else Ink.Chip).clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 13.dp),
    ) { Text(text, color = if (on) Color(0xFF10141B) else Ink.Text, fontSize = 15.sp, fontWeight = FontWeight.Medium) }
}

// Boards ----------------------------------------------------------------------------------------------------------
@Composable
fun BoardsScreen(vm: Vm) {
    var typing by remember { mutableStateOf(false) }
    var address by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.app_name), color = Ink.Text, fontSize = 30.sp, fontWeight = FontWeight.Bold)
        Text(stringResource(R.string.boards), style = Head)
        vm.boards.forEach { b ->
            Box(Modifier.clickable { vm.open(b) }) {
                Card {
                    Text(b.name, color = Ink.Text, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                    Text(b.address, color = Ink.Soft, fontSize = 14.sp)
                }
            }
        }
        if (vm.searching) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
            Text(stringResource(R.string.searching), color = Ink.Soft)
        } else {
            if (vm.boards.isEmpty()) Card { Text(stringResource(R.string.none_found), color = Ink.Warn, fontSize = 16.sp) }
            OutlinedButton(onClick = vm::findBoards, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.search)) }
        }
        Spacer(Modifier.height(8.dp))
        Button(onClick = vm::startSetup, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.setup_new)) }
        OutlinedButton(onClick = { vm.joinOwn(direct = true) }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.direct)) }
        if (!typing) TextButton(onClick = { typing = true }) { Text(stringResource(R.string.enter_address)) }
        else {
            OutlinedTextField(address, { address = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text(stringResource(R.string.address_hint)) })
            Button(onClick = { vm.openAddress(address) }) { Text(stringResource(R.string.ok)) }
        }
    }
}

// Setup -----------------------------------------------------------------------------------------------------------
@Composable
fun SetupScreen(vm: Vm) {
    var ssid by remember { mutableStateOf("") }
    var pass by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Text(stringResource(R.string.setup_title), color = Ink.Text, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        when (vm.step) {
            SetupStep.Start -> {
                Text(stringResource(R.string.setup_1), color = Ink.Text, fontSize = 16.sp)
                Button(onClick = { vm.joinOwn(direct = false) }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.setup_join)) }
            }
            SetupStep.Joining, SetupStep.Sent -> Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                Text(stringResource(if (vm.step == SetupStep.Joining) R.string.setup_joining else R.string.setup_sent), color = Ink.Text)
            }
            SetupStep.Pick -> {
                Text(stringResource(R.string.setup_pick), style = Head)
                vm.networks.forEach { n -> Chip(n, on = n == ssid) { ssid = n } }
                TextButton(onClick = vm::scanNetworks) { Text(stringResource(R.string.rescan)) }
                OutlinedTextField(ssid, { ssid = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text(stringResource(R.string.network_name)) })
                OutlinedTextField(pass, { pass = it }, Modifier.fillMaxWidth(), singleLine = true, visualTransformation = PasswordVisualTransformation(), label = { Text(stringResource(R.string.password)) })
                Button(enabled = ssid.isNotBlank() && (pass.isEmpty() || pass.length >= 8), onClick = { vm.sendWifi(ssid, pass) }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.setup_send)) }
            }
        }
        TextButton(onClick = vm::back) { Text(stringResource(R.string.cancel)) }
    }
}

// The light -------------------------------------------------------------------------------------------------------
private val MODE_COLORS = listOf("white" to Color.White, "yellow" to Color(0xFFFFE600), "cyan" to Color(0xFF00E5FF), "magenta" to Color(0xFFFF2BD6))

@Composable
private fun colorName(id: String) = stringResource(when (id) { "white" -> R.string.white; "yellow" -> R.string.yellow; "cyan" -> R.string.cyan; else -> R.string.magenta })

@Composable
private fun modeName(id: String) = stringResource(when (id) { "breathe3" -> R.string.mode_breathe3; "fade7" -> R.string.mode_fade7; "jump3" -> R.string.mode_jump3; else -> R.string.mode_jump7 })

@Composable
private fun effectName(id: String): String = when (id) {
    "rainbow" -> stringResource(R.string.fx_rainbow)
    "random" -> stringResource(R.string.fx_random)
    "party" -> stringResource(R.string.fx_party)
    "police" -> stringResource(R.string.fx_police)
    "fire" -> stringResource(R.string.fx_fire)
    "ocean" -> stringResource(R.string.fx_ocean)
    "forest" -> stringResource(R.string.fx_forest)
    "sunset" -> stringResource(R.string.fx_sunset)
    "strobe" -> stringResource(R.string.fx_strobe)
    "breathe" -> stringResource(R.string.fx_breathe)
    "custom" -> stringResource(R.string.fx_custom)
    else -> id
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LightScreen(vm: Vm) {
    val s = vm.state
    val info = vm.info
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(vm.board?.name ?: "", color = Ink.Text, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Text(
                    if (vm.online) vm.board?.address ?: "" else stringResource(if (s == null) R.string.connecting else R.string.lost),
                    color = if (vm.online) Ink.Good else Ink.Warn, fontSize = 13.sp,
                )
            }
            TextButton(onClick = vm::findBoards) { Text(stringResource(R.string.change_board)) }
        }
        if (s != null && s.noEcho) Text(stringResource(R.string.no_echo), color = Ink.Warn, fontSize = 14.sp)
        if (s == null || info == null) return@Column

        val modeColor = if (s.u4mode && s.effect == "none") MODE_COLORS.firstOrNull { it.first == s.mode }?.second else null
        val shown = modeColor ?: info.colors.getOrNull(s.color)?.onScreen()
        Ring(info.colors, s.color, s.on == 1, shown ?: Color(0xFF8AB4FF), onColor = vm::color) { vm.set("on" to if (s.on == 1) "0" else "1") }

        FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            MODE_COLORS.forEach { (id, c) ->
                Chip(colorName(id), on = s.u4mode && s.mode == id && s.effect == "none", tint = c) {
                    if (s.mode.isEmpty()) vm.note = R.string.calibrate_hint else vm.set("u4" to id)
                }
            }
        }

        Card {
            val b = vm.holdBright ?: s.bright
            Row {
                Text(stringResource(R.string.brightness), style = Head, modifier = Modifier.weight(1f))
                Text(if (b < 0) stringResource(R.string.not_known) else "${b + 1} / 16", style = Head)
            }
            Slider(value = (if (b < 0) 15 else b).toFloat(), onValueChange = { vm.holdBright = it.roundToInt() }, valueRange = 0f..15f, steps = 14,
                onValueChangeFinished = { vm.holdBright?.let { v -> vm.set("bright" to v.toString()) }; vm.holdBright = null })
        }

        Card {
            Text(stringResource(R.string.effects), style = Head)
            Spacer(Modifier.height(10.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                info.effects.forEach { e -> Chip(effectName(e), on = s.effect == e) { vm.set("effect" to if (s.effect == e) "none" else e) } }
            }
            Spacer(Modifier.height(12.dp))
            val sp = vm.holdSpeed ?: s.speed
            Text(stringResource(R.string.speed), style = Head)
            Slider(value = sp.toFloat(), onValueChange = { vm.holdSpeed = it.roundToInt() }, valueRange = 1f..100f,
                onValueChangeFinished = { vm.holdSpeed?.let { v -> vm.set("speed" to v.toString()) }; vm.holdSpeed = null })
            if (s.effect == "custom") CustomPicker(vm, info, s)
        }

        Card {
            Text(stringResource(R.string.modes), style = Head)
            Spacer(Modifier.height(10.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (s.mode.isNotEmpty()) listOf("breathe3", "fade7", "jump3", "jump7").forEach { m ->
                    Chip(modeName(m), on = s.u4mode && s.mode == m && s.effect == "none") { vm.set("u4" to m) }
                }
                Chip(stringResource(R.string.mode_prev)) { vm.set("mode" to "-") }
                Chip(stringResource(R.string.mode_next)) { vm.set("mode" to "+") }
                Chip(stringResource(R.string.spd_down)) { vm.set("spd" to "-") }
                Chip(stringResource(R.string.spd_up)) { vm.set("spd" to "+") }
            }
            Spacer(Modifier.height(12.dp))
            Text(stringResource(R.string.calibrate_hint), color = Ink.Soft, fontSize = 13.sp)
            TextButton(onClick = { vm.set("calibrate" to "white") }) { Text(stringResource(R.string.calibrate)) }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CustomPicker(vm: Vm, info: BoardInfo, s: LightState) {
    var list by remember { mutableStateOf(s.custom) }
    Spacer(Modifier.height(6.dp))
    Text(stringResource(R.string.custom_colors), style = Head)
    Spacer(Modifier.height(8.dp))
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        info.colors.forEachIndexed { i, c ->
            val at = list.indexOf(i)
            Box(
                Modifier.size(46.dp).clip(CircleShape).background(c.onScreen()).clickable {
                    val next = if (at >= 0) list - i else list + i
                    list = next
                    if (next.size >= 2) vm.set("custom" to next.joinToString(",")) else vm.note = R.string.custom_need
                },
                contentAlignment = Alignment.Center,
            ) { if (at >= 0) Text("${at + 1}", color = Color.Black, fontWeight = FontWeight.Bold, modifier = Modifier.clip(CircleShape).background(Color.White).padding(horizontal = 7.dp, vertical = 1.dp)) }
        }
    }
}

/** The color ring: touch or slide for a color, the middle switches the light. */
@Composable
private fun Ring(colors: List<BoardColor>, selected: Int, on: Boolean, glow: Color, onColor: (Int) -> Unit, onPower: () -> Unit) {
    val pick by rememberUpdatedState(onColor)
    val power by rememberUpdatedState(onPower)
    val label = stringResource(if (on) R.string.on else R.string.off)
    Box(Modifier.fillMaxWidth().padding(horizontal = 18.dp).aspectRatio(1f).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        Canvas(
            Modifier.fillMaxSize().pointerInput(colors.size) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val c = Offset(size.width / 2f, size.height / 2f)
                    val outer = min(size.width, size.height) / 2f
                    val d = down.position - c
                    val r = hypot(d.x, d.y)
                    if (r < outer * 0.5f) {
                        var inside = true
                        while (true) {
                            val e = awaitPointerEvent().changes.first()
                            val p = e.position - c
                            if (hypot(p.x, p.y) > outer * 0.55f) inside = false
                            if (!e.pressed) { if (inside) power(); break }
                        }
                    } else if (r <= outer) {
                        var last = Proto.ringSlice(d.x, d.y, colors.size)
                        down.consume()
                        pick(last)
                        while (true) {
                            val e = awaitPointerEvent().changes.first()
                            if (!e.pressed) break
                            val p = e.position - c
                            val k = Proto.ringSlice(p.x, p.y, colors.size)
                            if (k != last) { last = k; pick(k) }
                            e.consume()
                        }
                    }
                }
            },
        ) {
            val outer = size.minDimension / 2f
            val w = outer * 0.26f
            val mid = outer - w / 2f - 6.dp.toPx()
            val slice = 360f / colors.size
            val arc = Size(mid * 2, mid * 2)
            val at = Offset(center.x - mid, center.y - mid)
            colors.forEachIndexed { i, c ->
                val start = -90f + slice * i - slice / 2f
                drawArc(c.onScreen(), start + 1.2f, slice - 2.4f, false, at, arc, style = Stroke(w), alpha = if (on) 1f else 0.45f)
                if (i == selected && on) {
                    val edge = mid + w / 2f + 4.dp.toPx()
                    drawArc(Color.White, start + 2.5f, slice - 5f, false, Offset(center.x - edge, center.y - edge), Size(edge * 2, edge * 2), style = Stroke(4.dp.toPx(), cap = StrokeCap.Round))
                }
            }
            val inner = outer * 0.5f
            if (on) drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = 0.55f), Color.Transparent), center, outer * 0.72f), outer * 0.72f)
            drawCircle(Ink.Card, inner)
            val ic = if (on) glow else Ink.Soft
            val ir = inner * 0.34f
            drawArc(ic, -60f, 300f, false, Offset(center.x - ir, center.y - ir), Size(ir * 2, ir * 2), style = Stroke(6.dp.toPx(), cap = StrokeCap.Round))
            drawLine(ic, Offset(center.x, center.y - ir * 1.2f), Offset(center.x, center.y - ir * 0.25f), 6.dp.toPx(), StrokeCap.Round)
        }
        Text(label, color = Ink.Soft, fontSize = 14.sp, modifier = Modifier.padding(top = 96.dp))
    }
}
