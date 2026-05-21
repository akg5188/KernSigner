pluginManagement {
    repositories {
        mavenLocal()
        maven("https://jitpack.io")
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/central")
        maven("https://maven.aliyun.com/repository/gradle-plugin")
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(org.gradle.api.initialization.resolve.RepositoriesMode.PREFER_SETTINGS)
    repositories {
        mavenLocal()
        maven("https://jitpack.io")
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/central") {
            content {
                excludeGroupByRegex("com\\.reown(\\..*)?")
                excludeGroupByRegex("com\\.walletconnect(\\..*)?")
            }
        }
        google()
        mavenCentral()
    }
}
rootProject.name = "arbitrum-wallet"
include(":app")
