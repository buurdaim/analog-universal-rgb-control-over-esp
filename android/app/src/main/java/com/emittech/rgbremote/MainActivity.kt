package com.emittech.rgbremote

import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Snackbar
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

class MainActivity : ComponentActivity() {
    private val vm: Vm by viewModels()
    private val ask = registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        // Newer Androids may ask before an app reaches devices on the local network.
        val local = "android.permission.ACCESS_LOCAL_NETWORK"
        if (Build.VERSION.SDK_INT >= 37 && runCatching { packageManager.getPermissionInfo(local, 0) }.isSuccess &&
            checkSelfPermission(local) != PackageManager.PERMISSION_GRANTED) ask.launch(local)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Color(0xFF8AB4FF), background = Ink.Ground, surface = Ink.Card)) {
                Box(Modifier.fillMaxSize().background(Ink.Ground)) {
                    BackHandler(enabled = vm.screen != Screen.Boards) { vm.back() }
                    when (vm.screen) {
                        Screen.Boards -> BoardsScreen(vm)
                        Screen.Setup -> SetupScreen(vm)
                        Screen.Light -> LightScreen(vm)
                    }
                    vm.note?.let { n ->
                        LaunchedEffect(n) { delay(3500); if (vm.note == n) vm.note = null }
                        Snackbar(Modifier.align(Alignment.BottomCenter).navigationBarsPadding().padding(16.dp)) { Text(stringResource(n)) }
                    }
                }
            }
        }
    }

    override fun onStart() { super.onStart(); vm.onForeground(true) }
    override fun onStop() { vm.onForeground(false); super.onStop() }
}
