package io.arbitrum.wallet

import android.app.Application
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Debug
import android.os.SystemClock
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import java.io.File
import java.security.MessageDigest

private object AppLockState : DefaultLifecycleObserver {
    private var installed = false
    private var unlocked = false

    fun install(@Suppress("UNUSED_PARAMETER") application: Application) {
        if (installed) return
        installed = true
        ProcessLifecycleOwner.get().lifecycle.addObserver(this)
    }

    fun isUnlocked(): Boolean = unlocked

    fun markUnlocked() {
        unlocked = true
    }

    override fun onStop(owner: LifecycleOwner) {
        unlocked = false
    }
}

private object RuntimeSecurityGuard {
    private const val SIDE_BY_SIDE_TEST_PACKAGE = "io.arbitrum.wallet.test"
    private val obviousRootPaths = listOf(
        "/system/bin/su",
        "/system/xbin/su",
        "/sbin/su",
        "/system/app/Superuser.apk",
        "/system/bin/.ext/su",
        "/system/usr/we-need-root/su",
        "/system/vendor/bin/su",
        "/data/local/xbin/su",
        "/data/local/bin/su",
        "/data/local/su",
        "/data/adb/magisk",
    )
    private val suspiciousPackages = listOf(
        "com.topjohnwu.magisk",
        "me.weishu.kernelsu",
        "com.rifsxd.ksunext",
        "de.robv.android.xposed.installer",
        "org.meowcat.edxposed.manager",
        "com.saurik.substrate",
        "com.devadvance.rootcloak2",
        "catch_.me_.if_.you_.can_",
        "org.lsposed.manager",
        "com.frida.server",
    )
    private val suspiciousMapKeywords = listOf(
        "frida",
        "xposed",
        "edxposed",
        "lsposed",
        "substrate",
    )
    private val suspiciousLoopbackPortsHex = listOf("69A2", "69A3")

    fun blockingIssue(activity: FragmentActivity): String? {
        val appFlags = activity.applicationInfo.flags
        val allowDebuggableTestPackage = activity.packageName == SIDE_BY_SIDE_TEST_PACKAGE
        if (!allowDebuggableTestPackage && (appFlags and ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            return "检测到可调试构建，已阻止启动高安全钱包界面"
        }
        if (Debug.isDebuggerConnected() || Debug.waitingForDebugger()) {
            return "检测到调试器连接，已阻止启动高安全钱包界面"
        }
        if (Build.TAGS?.contains("test-keys", ignoreCase = true) == true) {
            return "检测到测试签名系统镜像，已阻止启动高安全钱包界面"
        }
        if (obviousRootPaths.any { File(it).exists() }) {
            return "检测到 Root / Magisk 痕迹，已阻止启动高安全钱包界面"
        }
        val adbEnabled = runCatching {
            Settings.Global.getInt(activity.contentResolver, Settings.Global.ADB_ENABLED, 0) != 0
        }.getOrDefault(false)
        if (adbEnabled) {
            return "检测到 USB 调试已开启，已阻止启动高安全钱包界面"
        }
        val developerOptionsEnabled = runCatching {
            Settings.Global.getInt(activity.contentResolver, Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 0) != 0
        }.getOrDefault(false)
        if (developerOptionsEnabled) {
            return "检测到开发者选项已开启，已阻止启动高安全钱包界面"
        }
        val suspiciousPackage = suspiciousPackages.firstOrNull { pkg ->
            hasInstalledPackage(activity, pkg)
        }
        if (suspiciousPackage != null) {
            return "检测到高风险 Root / Hook 工具包 ($suspiciousPackage)，已阻止启动高安全钱包界面"
        }
        if (hasSuspiciousInstrumentationMaps()) {
            return "检测到进程注入 / Hook 痕迹，已阻止启动高安全钱包界面"
        }
        if (hasSuspiciousLoopbackPorts()) {
            return "检测到本机注入调试端口，已阻止启动高安全钱包界面"
        }
        val expectedSigner = runCatching {
            activity.getString(R.string.expected_signer_sha256).trim().lowercase()
        }.getOrDefault("")
        if (expectedSigner.isNotBlank()) {
            val currentSigner = currentSigningCertificateSha256(activity)
                ?: return "无法验证应用签名，已阻止启动高安全钱包界面"
            if (currentSigner != expectedSigner) {
                return "检测到应用签名与受信版本不一致，已阻止启动高安全钱包界面"
            }
        }
        return null
    }

    @Suppress("DEPRECATION")
    private fun currentSigningCertificateSha256(activity: FragmentActivity): String? {
        val packageInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            activity.packageManager.getPackageInfo(
                activity.packageName,
                PackageManager.GET_SIGNING_CERTIFICATES,
            )
        } else {
            activity.packageManager.getPackageInfo(
                activity.packageName,
                PackageManager.GET_SIGNATURES,
            )
        }
        val signatures = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val signingInfo = packageInfo.signingInfo ?: return null
            if (signingInfo.hasMultipleSigners()) {
                signingInfo.apkContentsSigners
            } else {
                signingInfo.signingCertificateHistory
            }
        } else {
            packageInfo.signatures
        }
        val signatureBytes = signatures.firstOrNull()?.toByteArray() ?: return null
        return MessageDigest.getInstance("SHA-256")
            .digest(signatureBytes)
            .joinToString("") { byte -> "%02x".format(byte) }
    }

    @Suppress("DEPRECATION")
    private fun hasInstalledPackage(activity: FragmentActivity, packageName: String): Boolean {
        return runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                activity.packageManager.getPackageInfo(
                    packageName,
                    PackageManager.PackageInfoFlags.of(0),
                )
            } else {
                activity.packageManager.getPackageInfo(packageName, 0)
            }
        }.isSuccess
    }

    private fun hasSuspiciousInstrumentationMaps(): Boolean {
        val maps = runCatching { File("/proc/self/maps").readText() }.getOrNull()?.lowercase() ?: return false
        return suspiciousMapKeywords.any { keyword -> maps.contains(keyword) }
    }

    private fun hasSuspiciousLoopbackPorts(): Boolean {
        return listOf("/proc/net/tcp", "/proc/net/tcp6").any { path ->
            val lines = runCatching { File(path).readLines() }.getOrNull().orEmpty()
            lines.drop(1).any { line ->
                val columns = line.trim().split(Regex("\\s+"))
                if (columns.size < 4) {
                    false
                } else {
                    val localAddress = columns[1].uppercase()
                    val state = columns[3].uppercase()
                    state == "0A" && suspiciousLoopbackPortsHex.any { portHex ->
                        localAddress.endsWith(":$portHex")
                    }
                }
            }
        }
    }
}

abstract class BiometricGateActivity : FragmentActivity() {
    private var biometricPrompt: BiometricPrompt? = null
    private var authInProgress = false
    private var lockOverlay: FrameLayout? = null
    private var securityBlockOverlay: FrameLayout? = null
    private var lastObscuredTouchWarningAtMs = 0L

    private fun requiredAuthenticators(): Int = BiometricManager.Authenticators.BIOMETRIC_STRONG
    private fun biometricNegativeButtonText(): String = "取消"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.setFlags(
            WindowManager.LayoutParams.FLAG_SECURE,
            WindowManager.LayoutParams.FLAG_SECURE,
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            window.setHideOverlayWindows(true)
        }
        AppLockState.install(application)
    }

    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (isObscuredTouch(ev)) {
            val now = SystemClock.elapsedRealtime()
            if (now - lastObscuredTouchWarningAtMs >= 1_500L) {
                lastObscuredTouchWarningAtMs = now
                Toast.makeText(this, "检测到屏幕遮挡或悬浮窗，已拦截当前触摸操作", Toast.LENGTH_SHORT).show()
            }
            return true
        }
        return super.dispatchTouchEvent(ev)
    }

    override fun onPostCreate(savedInstanceState: Bundle?) {
        super.onPostCreate(savedInstanceState)
        ensureLockOverlayVisible()
    }

    override fun onStart() {
        super.onStart()
        if (!ensureRuntimeSecurityReady()) {
            return
        }
        ensureUnlocked()
    }

    protected open fun biometricTitle(): String = "解锁钱包"

    protected open fun biometricSubtitle(): String = "请验证指纹以进入 Satochip"

    protected open fun onBiometricUnlocked() = Unit

    protected fun confirmSensitiveAction(
        title: String,
        subtitle: String,
        onConfirmed: () -> Unit,
    ) {
        if (!ensureRuntimeSecurityReady()) {
            return
        }
        if (authInProgress) return

        val authenticators = requiredAuthenticators()
        val biometricManager = BiometricManager.from(this)
        if (biometricManager.canAuthenticate(authenticators) != BiometricManager.BIOMETRIC_SUCCESS) {
            showSecurityBlockOverlay(
                title = "需要强生物识别",
                message = "高安全模式要求已配置可用的强生物识别",
                steps = listOf(
                    "先在系统设置里录入指纹等强生物识别。",
                    "当前版本不接受仅密码、图案或弱面容解锁。",
                    "录入完成后返回这里，点击“重新检查”。",
                ),
            )
            return
        }

        authInProgress = true
        biometricPrompt = BiometricPrompt(
            this,
            ContextCompat.getMainExecutor(this),
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    authInProgress = false
                    onConfirmed()
                }

                override fun onAuthenticationFailed() = Unit

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    authInProgress = false
                    if (errorCode == BiometricPrompt.ERROR_USER_CANCELED ||
                        errorCode == BiometricPrompt.ERROR_CANCELED ||
                        errorCode == BiometricPrompt.ERROR_NEGATIVE_BUTTON
                    ) {
                        return
                    }
                    if (lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) {
                        Toast.makeText(this@BiometricGateActivity, errString, Toast.LENGTH_SHORT).show()
                    }
                }
            },
        )
        runCatching {
            biometricPrompt?.authenticate(
                BiometricPrompt.PromptInfo.Builder()
                    .setTitle(title)
                    .setSubtitle(subtitle)
                    .setNegativeButtonText(biometricNegativeButtonText())
                    .setAllowedAuthenticators(authenticators)
                    .build(),
            )
        }.onFailure { error ->
            authInProgress = false
            showSecurityBlockOverlay(
                title = "生物验证初始化失败",
                message = error.message ?: "当前设备无法拉起强生物识别验证",
                steps = listOf(
                    "确认系统已经录入可用指纹。",
                    "如果是旧系统或定制 ROM，请先锁屏再解锁后重试。",
                    "若仍失败，这台设备可能不兼容当前高安全生物验证配置。",
                ),
            )
        }
    }

    private fun ensureUnlocked() {
        if (AppLockState.isUnlocked()) {
            dismissLockOverlay()
            dismissSecurityBlockOverlay()
            return
        }
        ensureLockOverlayVisible()
        if (authInProgress) return

        val authenticators = requiredAuthenticators()
        val biometricManager = BiometricManager.from(this)
        when (biometricManager.canAuthenticate(authenticators)) {
            BiometricManager.BIOMETRIC_SUCCESS -> {
                dismissSecurityBlockOverlay()
                showBiometricPrompt(authenticators)
            }
            else -> {
                showSecurityBlockOverlay(
                    title = "需要强生物识别",
                    message = "高安全模式要求已配置可用的强生物识别",
                    steps = listOf(
                        "先在系统设置里录入指纹等强生物识别。",
                        "当前版本不接受仅密码、图案或弱面容解锁。",
                        "录入完成后返回这里，点击“重新检查”。",
                    ),
                )
            }
        }
    }

    private fun ensureRuntimeSecurityReady(): Boolean {
        val issue = RuntimeSecurityGuard.blockingIssue(this)
        return if (issue == null) {
            dismissSecurityBlockOverlay()
            true
        } else {
            showSecurityBlockOverlay(
                title = "高安全模式已阻止启动",
                message = issue,
                steps = securityResolutionSteps(issue),
            )
            false
        }
    }

    private fun showBiometricPrompt(authenticators: Int) {
        authInProgress = true
        biometricPrompt = BiometricPrompt(
            this,
            ContextCompat.getMainExecutor(this),
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    authInProgress = false
                    AppLockState.markUnlocked()
                    dismissLockOverlay()
                    onBiometricUnlocked()
                }

                override fun onAuthenticationFailed() {
                    ensureLockOverlayVisible()
                }

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    authInProgress = false
                    if (AppLockState.isUnlocked()) return
                    if (!lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) return
                    ensureLockOverlayVisible()
                    showSecurityBlockOverlay(
                        title = "解锁未完成",
                        message = errString.toString().ifBlank { "当前无法完成强生物识别验证" },
                        steps = listOf(
                            "确认设备已录入可用的指纹等强生物识别。",
                            "如果刚关闭了开发者选项或 USB 调试，返回这里点“重新检查”。",
                            "若连续失败，先锁屏再解锁设备后重试。",
                        ),
                    )
                    when (errorCode) {
                        BiometricPrompt.ERROR_USER_CANCELED,
                        BiometricPrompt.ERROR_NEGATIVE_BUTTON,
                        BiometricPrompt.ERROR_CANCELED,
                        -> Unit
                        else -> {
                            Toast.makeText(this@BiometricGateActivity, errString, Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            },
        )
        runCatching {
            biometricPrompt?.authenticate(
                BiometricPrompt.PromptInfo.Builder()
                    .setTitle(biometricTitle())
                    .setSubtitle(biometricSubtitle())
                    .setNegativeButtonText(biometricNegativeButtonText())
                    .setAllowedAuthenticators(authenticators)
                    .build(),
            )
        }.onFailure { error ->
            authInProgress = false
            ensureLockOverlayVisible()
            showSecurityBlockOverlay(
                title = "生物验证初始化失败",
                message = error.message ?: "当前设备无法拉起强生物识别验证",
                steps = listOf(
                    "确认系统已经录入可用指纹。",
                    "如果是旧系统或定制 ROM，请先锁屏再解锁后重试。",
                    "若仍失败，这台设备可能不兼容当前高安全生物验证配置。",
                ),
            )
        }
    }

    private fun ensureLockOverlayVisible() {
        if (AppLockState.isUnlocked()) {
            dismissLockOverlay()
            return
        }
        val root = findViewById<ViewGroup>(android.R.id.content) ?: return
        val overlay = lockOverlay ?: buildLockOverlay().also {
            lockOverlay = it
            root.addView(
                it,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                ),
            )
        }
        overlay.visibility = View.VISIBLE
        overlay.bringToFront()
    }

    private fun dismissLockOverlay() {
        lockOverlay?.visibility = View.GONE
    }

    private fun dismissSecurityBlockOverlay() {
        securityBlockOverlay?.visibility = View.GONE
    }

    private fun buildLockOverlay(): FrameLayout {
        return FrameLayout(this).apply {
            setBackgroundColor(0xFF07111C.toInt())
            isClickable = true
            isFocusable = true

            val card = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_HORIZONTAL
                background = GradientDrawable().apply {
                    shape = GradientDrawable.RECTANGLE
                    cornerRadius = dp(28).toFloat()
                    setColor(0xFF0F172A.toInt())
                }
                setPadding(dp(24), dp(24), dp(24), dp(24))
            }
            card.addView(TextView(context).apply {
                text = context.getString(R.string.brand_name)
                setTextColor(0xFFFFFFFF.toInt())
                textSize = 28f
                typeface = Typeface.DEFAULT_BOLD
                gravity = Gravity.CENTER
            })
            card.addView(space(dp(10)))
            card.addView(TextView(context).apply {
                text = "请使用强生物识别进入钱包"
                setTextColor(0xFFE2E8F0.toInt())
                textSize = 16f
                gravity = Gravity.CENTER
            })
            card.addView(space(dp(18)))
            card.addView(TextView(context).apply {
                text = "强生物识别"
                setTextColor(0xFF67E8F9.toInt())
                textSize = 14f
                gravity = Gravity.CENTER
            })

            addView(
                card,
                FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    Gravity.CENTER,
                ).apply {
                    marginStart = dp(20)
                    marginEnd = dp(20)
                },
            )
        }
    }

    private fun showSecurityBlockOverlay(
        title: String,
        message: String,
        steps: List<String>,
    ) {
        val root = findViewById<ViewGroup>(android.R.id.content) ?: return
        val overlay = securityBlockOverlay ?: FrameLayout(this).also {
            securityBlockOverlay = it
            root.addView(
                it,
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                ),
            )
        }
        overlay.removeAllViews()
        overlay.setBackgroundColor(0xFF07111C.toInt())
        overlay.isClickable = true
        overlay.isFocusable = true

        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            background = GradientDrawable().apply {
                shape = GradientDrawable.RECTANGLE
                cornerRadius = dp(28).toFloat()
                setColor(0xFF0F172A.toInt())
            }
            setPadding(dp(24), dp(24), dp(24), dp(24))
        }
        card.addView(TextView(this).apply {
            text = title
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        })
        card.addView(space(dp(12)))
        card.addView(TextView(this).apply {
            text = message
            setTextColor(0xFFE2E8F0.toInt())
            textSize = 15f
            gravity = Gravity.CENTER
        })
        if (steps.isNotEmpty()) {
            card.addView(space(dp(16)))
            steps.forEachIndexed { index, step ->
                card.addView(TextView(this).apply {
                    text = "${index + 1}. $step"
                    setTextColor(0xFFCBD5E1.toInt())
                    textSize = 14f
                    gravity = Gravity.START
                })
                if (index != steps.lastIndex) {
                    card.addView(space(dp(8)))
                }
            }
        }
        card.addView(space(dp(20)))
        card.addView(buildOverlayActionButton("重新检查") {
            if (ensureRuntimeSecurityReady()) {
                ensureUnlocked()
            }
        })
        card.addView(space(dp(10)))
        card.addView(buildOverlayActionButton("退出") {
            finishAffinity()
        })

        overlay.addView(
            card,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER,
            ).apply {
                marginStart = dp(20)
                marginEnd = dp(20)
            },
        )
        overlay.visibility = View.VISIBLE
        overlay.bringToFront()
    }

    private fun buildOverlayActionButton(
        label: String,
        onClick: () -> Unit,
    ): TextView {
        return TextView(this).apply {
            text = label
            gravity = Gravity.CENTER
            textSize = 15f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(0xFF081018.toInt())
            background = GradientDrawable().apply {
                shape = GradientDrawable.RECTANGLE
                cornerRadius = dp(999).toFloat()
                setColor(0xFF67E8F9.toInt())
            }
            setPadding(dp(18), dp(12), dp(18), dp(12))
            isClickable = true
            isFocusable = true
            setOnClickListener { onClick() }
        }
    }

    private fun securityResolutionSteps(issue: String): List<String> {
        return when {
            issue.contains("USB 调试") -> listOf(
                "去系统设置关闭“USB 调试”。",
                "保持开发者工具链不接入这台资金手机。",
                "返回应用后点击“重新检查”。",
            )
            issue.contains("开发者选项") -> listOf(
                "去系统设置关闭“开发者选项”。",
                "高安全模式不允许开发者环境常驻。",
                "返回应用后点击“重新检查”。",
            )
            issue.contains("Root") || issue.contains("Magisk") || issue.contains("Hook") || issue.contains("注入") -> listOf(
                "移除 Root、Magisk、LSPosed、Frida 等高风险组件。",
                "不要在改机或注入环境里使用大额资金钱包。",
                "返回应用后点击“重新检查”。",
            )
            issue.contains("测试签名系统镜像") -> listOf(
                "使用官方稳定版系统，不要用 test-keys 或开发版 ROM。",
                "高安全模式默认拒绝开发镜像。",
                "更换到干净设备后重新安装。 ",
            )
            issue.contains("应用签名") -> listOf(
                "确认安装的是我给你打出的原始 APK。",
                "不要通过会重签名的分发平台或打包工具转装。",
                "核对 APK 哈希后重新安装。 ",
            )
            else -> listOf(
                "先按提示处理当前风险项。",
                "建议在干净、非开发环境的专用手机上使用。",
                "处理完成后返回应用点击“重新检查”。",
            )
        }
    }

    private fun space(height: Int): View {
        return View(this).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                height,
            )
        }
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

    private fun isObscuredTouch(event: MotionEvent): Boolean {
        val flags = event.flags
        if ((flags and MotionEvent.FLAG_WINDOW_IS_OBSCURED) != 0) {
            return true
        }
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
            (flags and MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED) != 0
    }
}
