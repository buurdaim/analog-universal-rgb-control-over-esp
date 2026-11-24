package com.emittech.rgbremote

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.os.PatternMatcher
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import okhttp3.FormBody
import okhttp3.OkHttpClient
import okhttp3.Request
import java.net.Inet4Address
import java.util.concurrent.TimeUnit

/** HTTP to the board, over one given network when the board's own Wi-Fi is used. */
class Api(val address: String, network: Network? = null, readSeconds: Long = 4) {
    private val http = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(readSeconds, TimeUnit.SECONDS)
        .apply { if (network != null) socketFactory(network.socketFactory) }
        .build()

    private suspend fun call(request: Request): String? = withContext(Dispatchers.IO) {
        runCatching {
            http.newCall(request).execute().use { r -> if (r.isSuccessful) r.body?.string() else null }
        }.getOrNull()
    }

    suspend fun get(path: String): String? = call(Request.Builder().url("http://$address$path").build())

    suspend fun post(path: String, vararg args: Pair<String, String>): String? {
        val form = FormBody.Builder()
        args.forEach { (k, v) -> form.add(k, v) }
        return call(Request.Builder().url("http://$address$path").post(form.build()).build())
    }

    suspend fun state(): LightState? = get("/api/state")?.let(Proto::state)
    suspend fun info(): BoardInfo? = get("/api/info")?.let(Proto::info)
    suspend fun set(vararg args: Pair<String, String>): LightState? = post("/api/set", *args)?.let(Proto::state)
}

/** Finds boards with mDNS on the Wi-Fi the phone is on. */
class Finder(context: Context, private val scope: CoroutineScope) {
    private val app = context.applicationContext
    private val nsd = app.getSystemService(NsdManager::class.java)

    fun onWifi(): Boolean {
        val cm = app.getSystemService(ConnectivityManager::class.java) ?: return true
        @Suppress("DEPRECATION")
        return cm.allNetworks.mapNotNull { cm.getNetworkCapabilities(it) }.any {
            it.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || it.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
        }
    }

    /** Calls back for every board until the returned function is called. */
    fun discover(onBoard: (BoardAt) -> Unit): () -> Unit {
        val queue = Channel<NsdServiceInfo>(Channel.UNLIMITED)
        val listener = object : NsdManager.DiscoveryListener {
            override fun onServiceFound(info: NsdServiceInfo) { queue.trySend(info) }
            override fun onServiceLost(info: NsdServiceInfo) {}
            override fun onDiscoveryStarted(serviceType: String) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {}
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }
        val started = nsd != null && runCatching { nsd.discoverServices("_rgbremote._tcp", NsdManager.PROTOCOL_DNS_SD, listener) }.isSuccess
        val job = scope.launch {
            for (info in queue) {
                val r = resolve(info) ?: continue
                val host = hostOf(r) ?: continue
                onBoard(BoardAt(r.serviceName, if (r.port == 80 || r.port == 0) host else "$host:${r.port}"))
            }
        }
        return {
            if (started) runCatching { nsd?.stopServiceDiscovery(listener) }
            job.cancel()
            queue.close()
        }
    }

    suspend fun find(name: String, ms: Long): BoardAt? {
        val hit = CompletableDeferred<BoardAt>()
        val stop = discover { if (it.name == name) hit.complete(it) }
        return try { withTimeoutOrNull(ms) { hit.await() } } finally { stop() }
    }

    @Suppress("DEPRECATION")
    private suspend fun resolve(info: NsdServiceInfo): NsdServiceInfo? {
        val done = CompletableDeferred<NsdServiceInfo?>()
        val l = object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) { done.complete(null) }
            override fun onServiceResolved(serviceInfo: NsdServiceInfo) { done.complete(serviceInfo) }
        }
        if (runCatching { nsd?.resolveService(info, l) }.isFailure) return null
        return withTimeoutOrNull(4000) { done.await() }
    }

    @Suppress("DEPRECATION")
    private fun hostOf(r: NsdServiceInfo): String? {
        if (Build.VERSION.SDK_INT >= 34) {
            r.hostAddresses.firstOrNull { it is Inet4Address }?.hostAddress?.let { return it }
        }
        return (r.host as? Inet4Address)?.hostAddress
    }
}

/** The board's own Wi-Fi. Android shows its own window to choose it; the phone's internet stays as it is. */
class OwnWifi(context: Context) {
    private val cm = context.applicationContext.getSystemService(ConnectivityManager::class.java)
    private var callback: ConnectivityManager.NetworkCallback? = null

    suspend fun join(onLost: () -> Unit): Network? {
        leave()
        val got = CompletableDeferred<Network?>()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { got.complete(network) }
            override fun onUnavailable() { got.complete(null) }
            override fun onLost(network: Network) { onLost() }
        }
        val spec = WifiNetworkSpecifier.Builder()
            .setSsidPattern(PatternMatcher("RGB-Remote-", PatternMatcher.PATTERN_PREFIX))
            .setWpa2Passphrase("rgbremote")
            .build()
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(spec)
            .build()
        callback = cb
        if (runCatching { cm.requestNetwork(request, cb, 60_000) }.isFailure) { callback = null; return null }
        return withTimeoutOrNull(65_000) { got.await() }.also { if (it == null) leave() }
    }

    fun leave() {
        callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null
    }
}
