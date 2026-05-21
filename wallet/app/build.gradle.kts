import java.io.File
import java.util.Properties
import java.security.KeyStore
import java.security.MessageDigest

val localProperties = Properties().apply {
    val f = rootProject.file("local.properties")
    if (f.exists()) {
        f.inputStream().use { load(it) }
    }
}

val sdkDir = localProperties.getProperty("sdk.dir")
    ?: System.getenv("ANDROID_SDK_ROOT")
    ?: System.getenv("ANDROID_HOME")
    ?: ""

val coreSystemModulesJar = sdkDir
    .takeIf { it.isNotBlank() }
    ?.let { File(it, "platforms/android-34/core-for-system-modules.jar") }
    ?.takeIf { it.isFile }

val keystoreProperties = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) {
        f.inputStream().use { load(it) }
    }
}

val releaseStorePath = keystoreProperties.getProperty("storeFile").orEmpty().trim()
val releaseStoreFile = releaseStorePath.takeIf { it.isNotBlank() }?.let(::file)
val hasReleaseKeystoreConfig = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
    .all { !keystoreProperties.getProperty(it).isNullOrBlank() }
val hasReleaseKeystore = hasReleaseKeystoreConfig && (releaseStoreFile?.isFile == true)
val lightweightReleaseRequested = providers.gradleProperty("wallet.lightRelease").orNull == "1"
val releaseTaskRequested = gradle.startParameter.taskNames.any { taskName ->
    taskName.contains("Release", ignoreCase = true)
}
val releaseSignerSha256 = if (hasReleaseKeystore) {
    val storePath = releaseStorePath
    val storeType = keystoreProperties.getProperty("storeType")
        ?.takeIf { it.isNotBlank() }
        ?: when (storePath.substringAfterLast('.', "").lowercase()) {
            "jks" -> "JKS"
            "p12", "pfx", "pkcs12" -> "PKCS12"
            else -> KeyStore.getDefaultType()
        }
    val keyStore = KeyStore.getInstance(storeType)
    releaseStoreFile!!.inputStream().use { input ->
        keyStore.load(input, keystoreProperties.getProperty("storePassword").toCharArray())
    }
    val certificate = keyStore.getCertificate(keystoreProperties.getProperty("keyAlias"))
        ?: throw org.gradle.api.GradleException("Unable to load release signing certificate for wallet.")
    MessageDigest.getInstance("SHA-256")
        .digest(certificate.encoded)
        .joinToString("") { byte -> "%02x".format(byte) }
} else {
    ""
}

if (releaseTaskRequested && !hasReleaseKeystore) {
    throw org.gradle.api.GradleException(
        if (hasReleaseKeystoreConfig) {
            "Release build requires an existing keystore file; missing: $releaseStorePath"
        } else {
            "Release build requires a configured keystore.properties; debug signing is blocked for wallet release artifacts."
        }
    )
}

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.serialization")
}

kotlin {
    jvmToolchain(17)
}

android {
    namespace = "io.arbitrum.wallet"
    compileSdk = 34
    buildToolsVersion = "35.0.0"

    signingConfigs {
        if (hasReleaseKeystore) {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    defaultConfig {
        applicationId = "io.arbitrum.wallet"
        minSdk = 26
        targetSdk = 34
        versionCode = 7
        versionName = "0.1.6"
        resValue("string", "expected_signer_sha256", "")

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        debug {
            // Keep debug fast for local iteration.
            applicationIdSuffix = ".test"
            versionNameSuffix = "-test"
            isMinifyEnabled = false
            isShrinkResources = false
        }
        release {
            isMinifyEnabled = !lightweightReleaseRequested
            isShrinkResources = !lightweightReleaseRequested
            resValue("string", "expected_signer_sha256", releaseSignerSha256)
            if (hasReleaseKeystore) {
                signingConfig = signingConfigs.getByName("release")
            }
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlinOptions { jvmTarget = "11" }
    buildFeatures {
        compose = true
        buildConfig = false
    }
    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
    }
    lint {
        disable += setOf("ObsoleteSdkInt")
        checkReleaseBuilds = !lightweightReleaseRequested
    }

    packaging {
        resources {
            excludes += setOf(
                "META-INF/LICENSE*",
                "META-INF/NOTICE*",
                "META-INF/AL2.0",
                "META-INF/LGPL2.1",
                "META-INF/DEPENDENCIES",
                "META-INF/versions/9/OSGI-INF/MANIFEST.MF",
            )
        }
    }
}

configurations.all {
    resolutionStrategy {
        force("androidx.core:core:1.12.0")
        force("androidx.core:core-ktx:1.12.0")
        force("androidx.activity:activity:1.8.2")
        force("androidx.activity:activity-ktx:1.8.2")
        force("androidx.activity:activity-compose:1.8.2")
        force("androidx.lifecycle:lifecycle-runtime:2.7.0")
        force("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
        force("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
        force("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
        force("androidx.lifecycle:lifecycle-process:2.7.0")
        force("androidx.collection:collection:1.4.0")
        force("androidx.collection:collection-ktx:1.4.0")
        force("androidx.collection:collection-jvm:1.4.0")
    }
}

dependencies {
    implementation(platform("org.jetbrains.kotlin:kotlin-bom:1.9.24"))
    coreSystemModulesJar?.let { compileOnly(files(it)) }
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    implementation("androidx.biometric:biometric:1.1.0")
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-process:2.7.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-play-services:1.8.1")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.3")
    implementation("org.bitcoinj:bitcoinj-core:0.16.2") {
        exclude(group = "org.bouncycastle", module = "bcprov-jdk15to18")
    }

    implementation(platform("androidx.compose:compose-bom:2024.02.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")

    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.journeyapps:zxing-android-embedded:4.3.0")
    implementation("com.google.mlkit:barcode-scanning:17.3.0")
    implementation("org.msgpack:msgpack-core:0.9.11")
    implementation("com.sparrowwallet:hummingbird:1.7.4")
    implementation("co.nstant.in:cbor:0.9")

    implementation(platform("com.reown:android-bom:1.4.1"))
    implementation("com.reown:android-core")
    implementation("com.reown:walletkit")

    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
}
