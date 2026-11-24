package com.emittech.rgbremote

import android.app.Application
import android.net.Network
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

enum class Screen { Boards, Setup, Light }
enum class SetupStep { Pick, Sent }

class Vm(app: Application) : AndroidViewModel(app) {
    private val prefs = app.getSharedPreferences("rgb", 0)
    private val finder = Finder(app, viewModelScope)
    private val own = OwnWifi(app)

    var screen by mutableStateOf(Screen.Boards); private set
    var boards by mutableStateOf<List<BoardAt>>(emptyList()); private set
    var searching by mutableStateOf(false); private set
    var board by mutableStateOf<BoardAt?>(null); private set
    var state by mutableStateOf<LightState?>(null); private set
    var info by mutableStateOf<BoardInfo?>(null); private set
    var online by mutableStateOf(false); private set
    var note by mutableStateOf<Int?>(null)
    var step by mutableStateOf(SetupStep.Pick); private set
    var networks by mutableStateOf<List<String>>(emptyList()); private set
    /** While a finger is on a control, the board's answer must not move it. */
    var holdBright by mutableStateOf<Int?>(null)
    var holdSpeed by mutableStateOf<Int?>(null)

    private var api: Api? = null
    private var network: Network? = null
    private var poll: Job? = null
    private var search: Job? = null
    private var foreground = false
    var direct by mutableStateOf(false); private set
    var demo by mutableStateOf(false); private set
    var joining by mutableStateOf(false); private set

    init {
        val name = prefs.getString("name", null)
        val address = prefs.getString("address", null)
        if (name != null && address != null) open(BoardAt(name, address)) else findBoards()
    }

    // Boards ------------------------------------------------------------------------------------------------------
    fun findBoards() {
        screen = Screen.Boards
        demo = false
        poll?.cancel()
        leaveOwn()
        search?.cancel()
        boards = emptyList()
        searching = true
        search = viewModelScope.launch {
            val stop = finder.discover { b -> boards = (boards.filter { it.name != b.name } + b).sortedBy { it.name } }
            try { delay(12_000) } finally { stop(); searching = false }
        }
    }

    fun openAddress(typed: String) {
        val a = Proto.address(typed)
        if (a == null) note = R.string.bad_address else open(BoardAt(a, a))
    }

    fun open(b: BoardAt, over: Network? = null) {
        search?.cancel()
        if (over == null) {
            leaveOwn()
            prefs.edit().putString("name", b.name).putString("address", b.address).apply()
        }
        board = b
        demo = false
        direct = over != null
        api = Api(b.address, over)
        state = null
        info = null
        online = false
        screen = Screen.Light
        startPolling()
    }

    fun onForeground(on: Boolean) {
        foreground = on
        if (on) startPolling() else poll?.cancel()
    }

    private fun startPolling() {
        poll?.cancel()
        if (screen != Screen.Light || !foreground) return
        poll = viewModelScope.launch {
            var misses = 0
            while (true) {
                val a = api ?: break
                if (info == null) info = a.info()
                val s = a.state()
                if (s != null) {
                    misses = 0
                    online = true
                    state = s
                    val b = board
                    if (b != null && b.name != s.name) {   // opened by address: now its real name is known
                        board = BoardAt(s.name, b.address)
                        if (network == null) prefs.edit().putString("name", s.name).apply()
                    }
                } else if (++misses >= 2) {
                    online = false
                    // The router may have given it another address.
                    val b = board
                    if (misses % 4 == 2 && network == null && b != null && b.name.startsWith("RGB-Remote")) {
                        finder.find(b.name, 6000)?.let { if (it.address != b.address) { open(it); return@launch } }
                    }
                }
                delay(if (online) 800 else 2000)
            }
        }
    }

    // Control -----------------------------------------------------------------------------------------------------
    private var sending: Job? = null

    /** The screens without a board: only the picture changes. */
    fun openDemo() {
        search?.cancel()
        poll?.cancel()
        searching = false
        api = null
        demo = true
        direct = false
        board = BoardAt(getApplication<Application>().getString(R.string.demo_name), "")
        info = Demo.info
        state = Demo.state
        online = true
        screen = Screen.Light
    }

    fun set(vararg args: Pair<String, String>) {
        if (demo) { state = state?.let { Demo.apply(it, args.toMap()) }; return }
        val a = api ?: return
        viewModelScope.launch {
            val s = a.set(*args)
            if (s != null) { state = s; online = true } else note = R.string.send_failed
        }
    }

    /** Dragging on the ring: only the newest color goes out, one request at a time. */
    private var nextColor: Int? = null
    fun color(i: Int) {
        if (demo) { set("color" to i.toString()); return }
        nextColor = i
        if (sending?.isActive == true) return
        val a = api ?: return
        sending = viewModelScope.launch {
            while (true) {
                val c = nextColor ?: break
                nextColor = null
                val s = a.set("color" to c.toString())
                if (s != null) state = s else note = R.string.send_failed
            }
        }
    }

    // Setup -------------------------------------------------------------------------------------------------------
    private fun leaveOwn() {
        own.leave()
        network = null
    }

    /** Joins the board's own Wi-Fi and goes straight to the light. */
    fun connectDirect() {
        if (joining) return
        joining = true
        search?.cancel()
        searching = false
        viewModelScope.launch {
            val found = joinOwn()
            joining = false
            if (found != null) open(BoardAt(found.name, OWN), network) else note = R.string.setup_fail
        }
    }

    private suspend fun joinOwn(): LightState? {
        val n = own.join { viewModelScope.launch { if (direct) online = false } } ?: return null
        network = n
        val a = Api(OWN, n, 15)
        for (k in 0 until 6) {
            a.state()?.let { ownName = it.name; return it }
            delay(700)
        }
        leaveOwn()
        return null
    }

    /** From a direct connection: choose the home network. */
    fun moveToHome() {
        poll?.cancel()
        step = SetupStep.Pick
        screen = Screen.Setup
        scanNetworks()
    }

    private var ownName = ""

    fun scanNetworks() {
        val n = network ?: return
        viewModelScope.launch { networks = Api(OWN, n, 20).get("/api/scan")?.let(Proto::names) ?: networks }
    }

    fun sendWifi(ssid: String, pass: String) {
        val n = network ?: return
        step = SetupStep.Sent
        viewModelScope.launch {
            if (Api(OWN, n, 8).post("/api/wifi", "ssid" to ssid, "pass" to pass) == null) {
                step = SetupStep.Pick
                note = R.string.send_failed
                return@launch
            }
            leaveOwn()                       // back to the home Wi-Fi, where the board is going too
            delay(3000)
            val found = finder.find(ownName, 45_000)
            if (found != null) open(found) else { note = R.string.setup_notfound; findBoards() }
        }
    }

    fun back() {
        if (screen == Screen.Setup && network != null) { screen = Screen.Light; startPolling() } else findBoards()
    }

    override fun onCleared() = leaveOwn()

    private companion object { const val OWN = "192.168.4.1" }
}
