package com.emittech.rgbremote

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin

object Ink {
    val Ground = Color(0xFF0B0D11)
    val Card = Color(0xFF171B22)
    val Raised = Color(0xFF222833)
    val Text = Color(0xFFF4F6FA)
    val Soft = Color(0xFF98A2B3)
    val Warn = Color(0xFFFFB454)
    val Good = Color(0xFF62D394)
    val Accent = Color(0xFF6EA8FF)
}

private val Title = TextStyle(fontSize = 22.sp, fontWeight = FontWeight.Bold, color = Ink.Text)
private val Body = TextStyle(fontSize = 16.sp, color = Ink.Text, lineHeight = 22.sp)
private val Label = TextStyle(fontSize = 14.sp, fontWeight = FontWeight.Medium, color = Ink.Soft)

/** How a color of the LEDs looks on a screen: a quarter of the light looks like about half. */
fun BoardColor.onScreen() = Color(lift(r), lift(g), lift(b))
private fun lift(p: Int) = (p / 100f).pow(1 / 2.2f).coerceIn(0f, 1f)
private fun inkOn(c: Color) = if (c.luminance() > 0.35f) Color(0xFF0B0D11) else Color.White

// Building blocks -------------------------------------------------------------------------------------------------
@Composable
private fun Big(text: String, modifier: Modifier = Modifier, fill: Color = Ink.Accent, busy: Boolean = false, onClick: () -> Unit) {
    Row(
        modifier.fillMaxWidth().height(60.dp).clip(RoundedCornerShape(20.dp)).background(fill).clickable(enabled = !busy, role = Role.Button, onClick = onClick),
        horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically,
    ) {
        if (busy) { CircularProgressIndicator(Modifier.size(22.dp), color = inkOn(fill), strokeWidth = 2.5.dp); Spacer(Modifier.width(12.dp)) }
        Text(text, color = inkOn(fill), fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun Link(text: String, onClick: () -> Unit) {
    Text(text, color = Ink.Soft, fontSize = 15.sp, fontWeight = FontWeight.Medium, modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable(role = Role.Button, onClick = onClick).padding(horizontal = 12.dp, vertical = 14.dp))
}

/** A thick bar to drag: the whole bar is the touch target. */
@Composable
private fun Bar(label: String, value: Float, text: String, tint: Color, onDrag: (Float) -> Unit, onEnd: () -> Unit) {
    val drag by rememberUpdatedState(onDrag)
    val end by rememberUpdatedState(onEnd)
    Box(
        Modifier.fillMaxWidth().height(64.dp).clip(RoundedCornerShape(22.dp)).background(Ink.Card)
            .drawBehind { drawRoundRect(Brush.horizontalGradient(listOf(tint.copy(alpha = 0.6f).compositeOver(Ink.Card), tint)), size = Size((size.width * value).coerceAtLeast(22.dp.toPx() * 2), size.height), cornerRadius = CornerRadius(22.dp.toPx())) }
            .semantics { contentDescription = "$label $text" }
            .pointerInput(Unit) {
                awaitEachGesture {
                    val d = awaitFirstDown()
                    drag((d.position.x / size.width).coerceIn(0f, 1f))
                    while (true) {
                        val e = awaitPointerEvent().changes.first()
                        if (!e.pressed) { end(); break }
                        drag((e.position.x / size.width).coerceIn(0f, 1f))
                        e.consume()
                    }
                }
            },
    ) {
        Row(Modifier.fillMaxSize().padding(horizontal = 22.dp), verticalAlignment = Alignment.CenterVertically) {
            val ink = if (value > 0.28f) inkOn(tint.copy(alpha = 0.68f).compositeOver(Ink.Card)) else Ink.Text
            Text(label, color = ink, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            Text(text, color = if (value > 0.86f) inkOn(tint) else Ink.Text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
        }
    }
}

private val RING = Demo.info.colors.map { it.onScreen() }

private fun DrawScope.ringArt(turn: Float, stroke: Float, alpha: Float = 1f) {
    val m = size.minDimension / 2 - stroke
    val slice = 360f / RING.size
    RING.forEachIndexed { i, c ->
        drawArc(c, turn + slice * i, slice + 0.6f, false, Offset(center.x - m, center.y - m), Size(m * 2, m * 2), style = Stroke(stroke), alpha = alpha)
    }
}

// First screen ----------------------------------------------------------------------------------------------------
@Composable
fun BoardsScreen(vm: Vm) {
    var typing by remember { mutableStateOf(false) }
    var address by remember { mutableStateOf("") }
    val spin by rememberInfiniteTransition(label = "spin").animateFloat(0f, 360f, infiniteRepeatable(tween(24_000, easing = LinearEasing), RepeatMode.Restart), label = "spin")
    Column(
        Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding().verticalScroll(rememberScrollState()).padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(36.dp))
        Canvas(Modifier.size(190.dp)) {
            drawCircle(Brush.radialGradient(listOf(Color(0x553D6BFF), Color.Transparent)), size.minDimension / 2)
            ringArt(spin, 22.dp.toPx())
        }
        Spacer(Modifier.height(26.dp))
        Text(stringResource(R.string.app_name), color = Ink.Text, fontSize = 34.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.welcome), style = Body, color = Ink.Soft, textAlign = TextAlign.Center)
        Spacer(Modifier.height(30.dp))

        if (vm.boards.isNotEmpty()) {
            Text(stringResource(R.string.on_network), style = Label, modifier = Modifier.fillMaxWidth())
            Spacer(Modifier.height(10.dp))
            vm.boards.forEach { b ->
                Row(
                    Modifier.fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(Ink.Card).clickable(role = Role.Button) { vm.open(b) }.padding(18.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Canvas(Modifier.size(34.dp)) { ringArt(0f, 5.dp.toPx()) }
                    Spacer(Modifier.width(16.dp))
                    Column(Modifier.weight(1f)) {
                        Text(b.name, color = Ink.Text, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                        Text(b.address, style = Label)
                    }
                }
                Spacer(Modifier.height(12.dp))
            }
            Spacer(Modifier.height(8.dp))
        }

        Big(stringResource(if (vm.joining) R.string.setup_joining else R.string.connect_board), busy = vm.joining, fill = if (vm.boards.isEmpty()) Ink.Accent else Ink.Raised) { vm.connectDirect() }
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.connect_hint), style = Label, textAlign = TextAlign.Center, lineHeight = 20.sp, modifier = Modifier.widthIn(max = 340.dp))
        Spacer(Modifier.height(22.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (vm.searching) {
                CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp, color = Ink.Soft)
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.searching), style = Label, modifier = Modifier.padding(vertical = 14.dp))
            } else Link(stringResource(R.string.search)) { vm.findBoards() }
        }
        Row {
            Link(stringResource(R.string.enter_address)) { typing = !typing }
            Link(stringResource(R.string.demo)) { vm.openDemo() }
        }
        if (typing) {
            OutlinedTextField(address, { address = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text(stringResource(R.string.address_hint)) })
            Spacer(Modifier.height(10.dp))
            Big(stringResource(R.string.ok), fill = Ink.Raised) { vm.openAddress(address) }
        }
        Spacer(Modifier.height(24.dp))
    }
}

// Home Wi-Fi ------------------------------------------------------------------------------------------------------
@Composable
fun SetupScreen(vm: Vm) {
    var ssid by remember { mutableStateOf("") }
    var pass by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding().verticalScroll(rememberScrollState()).padding(24.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Text(stringResource(R.string.setup_title), color = Ink.Text, fontSize = 28.sp, fontWeight = FontWeight.Bold)
        if (vm.step == SetupStep.Sent) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.5.dp)
                Spacer(Modifier.width(14.dp))
                Text(stringResource(R.string.setup_sent), style = Body)
            }
            return@Column
        }
        Text(stringResource(R.string.setup_pick), style = Label)
        vm.networks.forEach { n ->
            val on = n == ssid
            Text(
                n, color = if (on) inkOn(Ink.Accent) else Ink.Text, fontSize = 17.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(if (on) Ink.Accent else Ink.Card).clickable(role = Role.Button) { ssid = n }.padding(horizontal = 18.dp, vertical = 16.dp),
            )
        }
        Link(stringResource(R.string.rescan)) { vm.scanNetworks() }
        OutlinedTextField(ssid, { ssid = it }, Modifier.fillMaxWidth(), singleLine = true, label = { Text(stringResource(R.string.network_name)) })
        OutlinedTextField(pass, { pass = it }, Modifier.fillMaxWidth(), singleLine = true, visualTransformation = PasswordVisualTransformation(), label = { Text(stringResource(R.string.password)) })
        val ready = ssid.isNotBlank() && (pass.isEmpty() || pass.length >= 8)
        Big(stringResource(R.string.setup_send), Modifier.alpha(if (ready) 1f else 0.4f)) { if (ready) vm.sendWifi(ssid, pass) }
        Link(stringResource(R.string.cancel)) { vm.back() }
    }
}

// The light -------------------------------------------------------------------------------------------------------
private val MODE_COLORS = listOf("white" to Color(0xFFFFFFFF), "yellow" to Color(0xFFFFE24A), "cyan" to Color(0xFF34E7FF), "magenta" to Color(0xFFFF4FD8))

@Composable
private fun colorName(id: String) = stringResource(when (id) { "white" -> R.string.white; "yellow" -> R.string.yellow; "cyan" -> R.string.cyan; else -> R.string.magenta })

@Composable
private fun modeName(id: String) = stringResource(when (id) { "breathe3" -> R.string.mode_breathe3; "fade7" -> R.string.mode_fade7; "jump3" -> R.string.mode_jump3; else -> R.string.mode_jump7 })

private fun modeLook(id: String) = when (id) {
    "breathe3", "jump3" -> listOf(Color(0xFFFF3B3B), Color(0xFF35D96B), Color(0xFF3A6BFF))
    else -> listOf(Color(0xFFFF3B3B), Color(0xFFFFE24A), Color(0xFF35D96B), Color(0xFF34E7FF), Color(0xFF3A6BFF), Color(0xFFFF4FD8))
}

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

private fun effectLook(id: String, s: LightState, info: BoardInfo): List<Color> = when (id) {
    "rainbow" -> RING
    "random" -> listOf(RING[2], RING[8], RING[13], RING[5])
    "party" -> listOf(RING[13], RING[11], RING[7], RING[2])
    "police" -> listOf(Color(0xFFFF2D2D), Color(0xFFFF2D2D), Color(0xFF2D5BFF), Color(0xFF2D5BFF))
    "fire" -> listOf(Color(0xFFB81D0A), Color(0xFFFF5A1F), Color(0xFFFFB02E))
    "ocean" -> listOf(Color(0xFF0B3FB8), Color(0xFF1F8BFF), Color(0xFF35E0C2))
    "forest" -> listOf(Color(0xFF0E6B34), Color(0xFF35C95B), Color(0xFFB4E04A))
    "sunset" -> listOf(Color(0xFFFFB02E), Color(0xFFFF5A4F), Color(0xFFC93A9B))
    "strobe" -> listOf(Color(0xFF2A2F39), Color(0xFFF4F6FA), Color(0xFF2A2F39))
    "breathe" -> listOf(Color(0xFF20304F), Ink.Accent, Color(0xFF20304F))
    "custom" -> s.custom.mapNotNull { info.colors.getOrNull(it)?.onScreen() }
    else -> emptyList()
}.let { if (it.size < 2) listOf(Ink.Raised, Ink.Raised) else it }

@Composable
fun LightScreen(vm: Vm, startTab: Int = 0) {
    val s = vm.state
    val info = vm.info
    var tab by rememberSaveable { mutableIntStateOf(startTab) }
    val modeColor = if (s != null && s.u4mode && s.effect == "none") MODE_COLORS.firstOrNull { it.first == s.mode }?.second else null
    val target = when {
        s == null || s.on == 0 -> Color(0xFF3A4250)
        modeColor != null -> modeColor
        else -> info?.colors?.getOrNull(s.color)?.onScreen() ?: Ink.Accent
    }
    val glow by animateColorAsState(target, tween(500), label = "glow")

    Column(
        Modifier.fillMaxSize().drawBehind {
            drawRect(Brush.radialGradient(listOf(glow.copy(alpha = if (s?.on == 0) 0.05f else 0.30f), Color.Transparent), Offset(size.width / 2, size.height * 0.28f), size.width * 0.95f))
        }.statusBarsPadding().navigationBarsPadding(),
    ) {
        Header(vm)
        Box(Modifier.weight(1f).fillMaxWidth()) {
            if (s == null || info == null) {
                Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator(strokeWidth = 3.dp)
                    Spacer(Modifier.height(16.dp))
                    Text(stringResource(R.string.connecting), style = Body, color = Ink.Soft)
                }
            } else Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                if (s.noEcho && !vm.demo) Notice(stringResource(R.string.no_echo))
                if (vm.direct) Notice(stringResource(R.string.direct_banner), stringResource(R.string.direct_do)) { vm.moveToHome() }
                when (tab) {
                    0 -> ColorTab(vm, s, info, glow)
                    1 -> AnimTab(vm, s, info)
                    else -> ModesTab(vm, s)
                }
                Spacer(Modifier.height(12.dp))
            }
        }
        Tabs(tab) { tab = it }
    }
}

@Composable
private fun Header(vm: Vm) {
    Row(Modifier.fillMaxWidth().padding(start = 24.dp, end = 12.dp, top = 10.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(vm.board?.name ?: "", style = Title, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(if (vm.online) Ink.Good else Ink.Warn))
                Spacer(Modifier.width(8.dp))
                Text(
                    when {
                        vm.demo -> stringResource(R.string.demo)
                        !vm.online -> stringResource(if (vm.state == null) R.string.connecting else R.string.lost)
                        vm.direct -> stringResource(R.string.connected_direct)
                        else -> vm.board?.address ?: ""
                    }, style = Label,
                )
            }
        }
        Link(stringResource(R.string.change_board)) { vm.findBoards() }
    }
}

@Composable
private fun Notice(text: String, action: String? = null, onClick: () -> Unit = {}) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(Ink.Card).padding(18.dp)) {
        Text(text, color = Ink.Text, fontSize = 15.sp, lineHeight = 21.sp)
        if (action != null) Text(action, color = Ink.Accent, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.clip(RoundedCornerShape(10.dp)).clickable(role = Role.Button, onClick = onClick).padding(top = 12.dp, bottom = 4.dp, end = 12.dp))
    }
}

@Composable
private fun Tabs(tab: Int, onTab: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 12.dp).clip(RoundedCornerShape(24.dp)).background(Ink.Card).padding(6.dp)) {
        listOf(R.string.tab_color, R.string.tab_anim, R.string.tab_modes).forEachIndexed { i, t ->
            Box(
                Modifier.weight(1f).height(52.dp).clip(RoundedCornerShape(19.dp)).background(if (i == tab) Ink.Raised else Color.Transparent).clickable(role = Role.Tab) { onTab(i) },
                contentAlignment = Alignment.Center,
            ) { Text(stringResource(t), color = if (i == tab) Ink.Text else Ink.Soft, fontSize = 16.sp, fontWeight = if (i == tab) FontWeight.SemiBold else FontWeight.Medium) }
        }
    }
}

@Composable
private fun ColumnScope.ColorTab(vm: Vm, s: LightState, info: BoardInfo, glow: Color) {
    Ring(info.colors, s.color, s.on != 0, glow, onColor = vm::color) { vm.set("on" to if (s.on == 1) "0" else "1") }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        MODE_COLORS.forEach { (id, c) ->
            val on = s.u4mode && s.mode == id && s.effect == "none" && s.on != 0
            Column(
                Modifier.weight(1f).clip(RoundedCornerShape(18.dp)).clickable(role = Role.Button) { if (s.mode.isEmpty()) vm.note = R.string.calibrate_hint else vm.set("u4" to id) }.padding(vertical = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(Modifier.size(52.dp).border(if (on) 3.dp else 0.dp, if (on) Color.White else Color.Transparent, CircleShape).padding(6.dp).clip(CircleShape).background(c).alpha(if (s.on == 0) 0.5f else 1f))
                Spacer(Modifier.height(6.dp))
                Text(colorName(id), color = if (on) Ink.Text else Ink.Soft, fontSize = 13.sp, maxLines = 1)
            }
        }
    }
    val b = vm.holdBright ?: s.bright
    val shown = if (b < 0) 15 else b
    Bar(
        stringResource(R.string.brightness), (shown + 1) / 16f, if (b < 0) "–" else "${((shown + 1) * 100 / 16f).roundToInt()}%",
        glow,
        onDrag = { vm.holdBright = (it * 16 - 0.5f).roundToInt().coerceIn(0, 15) },
        onEnd = { vm.holdBright?.let { v -> vm.set("bright" to v.toString()) }; vm.holdBright = null },
    )
}

@Composable
private fun RowScope.Tile(name: String, look: List<Color>, on: Boolean, onClick: () -> Unit) {
    val move by rememberInfiniteTransition(label = "tile").animateFloat(0f, 1f, infiniteRepeatable(tween(5200, easing = LinearEasing), RepeatMode.Reverse), label = "tile")
    Column(
        Modifier.weight(1f).clip(RoundedCornerShape(22.dp)).background(Ink.Card).border(2.dp, if (on) Color.White else Color.Transparent, RoundedCornerShape(22.dp)).clickable(role = Role.Button, onClick = onClick).padding(8.dp),
    ) {
        Canvas(Modifier.fillMaxWidth().height(58.dp).clip(RoundedCornerShape(16.dp))) {
            val shift = if (on) size.width * move else 0f
            drawRect(Brush.horizontalGradient(look + look.first(), startX = -shift, endX = size.width * 2 - shift))
        }
        Text(name, color = if (on) Color.White else Ink.Text, fontSize = 15.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 6.dp, top = 9.dp, bottom = 4.dp))
    }
}

@Composable
private fun AnimTab(vm: Vm, s: LightState, info: BoardInfo) {
    val sp = vm.holdSpeed ?: s.speed
    Bar(
        stringResource(R.string.speed), sp / 100f, "$sp", Ink.Accent,
        onDrag = { vm.holdSpeed = (it * 100).roundToInt().coerceIn(1, 100) },
        onEnd = { vm.holdSpeed?.let { v -> vm.set("speed" to v.toString()) }; vm.holdSpeed = null },
    )
    info.effects.chunked(2).forEach { pair ->
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            pair.forEach { e -> Tile(effectName(e), effectLook(e, s, info), s.effect == e) { vm.set("effect" to if (s.effect == e) "none" else e) } }
            if (pair.size == 1) Spacer(Modifier.weight(1f))
        }
    }
    if (s.effect == "custom") CustomPicker(vm, info, s)
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CustomPicker(vm: Vm, info: BoardInfo, s: LightState) {
    var list by remember { mutableStateOf(s.custom) }
    Text(stringResource(R.string.custom_colors), style = Label)
    FlowRow(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        info.colors.forEachIndexed { i, c ->
            val at = list.indexOf(i)
            Box(
                Modifier.size(50.dp).clip(CircleShape).background(c.onScreen()).clickable(role = Role.Button) {
                    val next = if (at >= 0) list - i else list + i
                    list = next
                    if (next.size >= 2) vm.set("custom" to next.joinToString(",")) else vm.note = R.string.custom_need
                },
                contentAlignment = Alignment.Center,
            ) { if (at >= 0) Text("${at + 1}", color = Color.Black, fontWeight = FontWeight.Bold, modifier = Modifier.clip(CircleShape).background(Color.White).padding(horizontal = 8.dp, vertical = 1.dp)) }
        }
    }
}

@Composable
private fun ModesTab(vm: Vm, s: LightState) {
    var help by remember { mutableStateOf(s.mode.isEmpty()) }
    Text(stringResource(R.string.modes), style = Label)
    listOf("breathe3", "fade7", "jump3", "jump7").chunked(2).forEach { pair ->
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            pair.forEach { m ->
                Tile(modeName(m), modeLook(m), s.u4mode && s.mode == m && s.effect == "none") { if (s.mode.isEmpty()) { help = true; vm.note = R.string.calibrate_hint } else vm.set("u4" to m) }
            }
        }
    }
    Stepper(stringResource(R.string.step_modes), { vm.set("mode" to "-") }) { vm.set("mode" to "+") }
    Stepper(stringResource(R.string.step_speed), { vm.set("spd" to "-") }) { vm.set("spd" to "+") }
    if (!help) Link(stringResource(R.string.modes_wrong)) { help = true }
    else Notice(stringResource(R.string.calibrate_hint), stringResource(R.string.calibrate)) { vm.set("calibrate" to "white"); help = false }
}

@Composable
private fun Stepper(label: String, onMinus: () -> Unit, onPlus: () -> Unit) {
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp)).background(Ink.Card).padding(8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = Ink.Text, fontSize = 17.sp, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f).padding(start = 12.dp))
        listOf("−" to onMinus, "+" to onPlus).forEachIndexed { i, (t, f) ->
            if (i == 1) Spacer(Modifier.width(8.dp))
            Box(Modifier.size(76.dp, 52.dp).clip(RoundedCornerShape(16.dp)).background(Ink.Raised).clickable(role = Role.Button, onClick = f).semantics { contentDescription = "$label $t" }, contentAlignment = Alignment.Center) {
                Text(t, color = Ink.Text, fontSize = 24.sp, fontWeight = FontWeight.Medium)
            }
        }
    }
}

/** The color ring: touch or slide for a color, the middle switches the light. */
@Composable
private fun Ring(colors: List<BoardColor>, selected: Int, on: Boolean, glow: Color, onColor: (Int) -> Unit, onPower: () -> Unit) {
    val pick by rememberUpdatedState(onColor)
    val power by rememberUpdatedState(onPower)
    val label = stringResource(if (on) R.string.on else R.string.off)
    Box(Modifier.fillMaxWidth().padding(horizontal = 10.dp).aspectRatio(1f).semantics { contentDescription = label }, contentAlignment = Alignment.Center) {
        Canvas(
            Modifier.fillMaxSize().pointerInput(colors.size) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val c = Offset(size.width / 2f, size.height / 2f)
                    val outer = min(size.width, size.height) / 2f
                    val d = down.position - c
                    val r = hypot(d.x, d.y)
                    if (r < outer * 0.52f) {
                        var inside = true
                        while (true) {
                            val e = awaitPointerEvent().changes.first()
                            val p = e.position - c
                            if (hypot(p.x, p.y) > outer * 0.58f) inside = false
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
            val w = outer * 0.27f
            val mid = outer - w / 2f - 8.dp.toPx()
            val slice = 360f / colors.size
            val box = Size(mid * 2, mid * 2)
            val at = Offset(center.x - mid, center.y - mid)
            colors.forEachIndexed { i, c -> drawArc(c.onScreen(), -90f + slice * i - slice / 2f, slice + 0.5f, false, at, box, style = Stroke(w), alpha = if (on) 1f else 0.28f) }
            if (on && selected in colors.indices) {
                val a = (-90.0 + slice * selected) * PI / 180
                val k = Offset(center.x + (mid * cos(a)).toFloat(), center.y + (mid * sin(a)).toFloat())
                drawCircle(Color.White, w * 0.60f, k)
                drawCircle(colors[selected].onScreen(), w * 0.46f, k)
            }
            val inner = outer * 0.50f
            if (on) drawCircle(Brush.radialGradient(listOf(glow.copy(alpha = 0.55f), Color.Transparent), center, inner * 1.35f), inner * 1.35f)
            drawCircle(Color(0xFF12161C), inner)
            val ic = if (on) glow else Ink.Soft
            val ir = inner * 0.30f
            val ci = Offset(center.x, center.y - inner * 0.10f)
            drawArc(ic, -58f, 296f, false, Offset(ci.x - ir, ci.y - ir), Size(ir * 2, ir * 2), style = Stroke(6.dp.toPx(), cap = StrokeCap.Round))
            drawLine(ic, Offset(ci.x, ci.y - ir * 1.25f), Offset(ci.x, ci.y - ir * 0.25f), 6.dp.toPx(), StrokeCap.Round)
        }
        Text(label, color = if (on) Ink.Text else Ink.Soft, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 84.dp))
    }
}
