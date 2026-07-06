plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "com.histogramapp.histogram_app"
    compileSdk = flutter.compileSdkVersion
    // 使用已安装的 NDK 版本编译共享 C++ 计算核心。
    ndkVersion = "27.0.12077973"

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "com.histogramapp.histogram_app"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        // 只为需要的 ABI 编译：arm64-v8a（Apple Silicon 模拟器 + 主流真机）、
        // x86_64（Intel 模拟器备用）。
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                // 单一 .so，静态链入 C++ STL，免打包 libc++_shared.so。
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    // 复用平台无关的共享核心 CMake（同一份 native/histogram.cpp，
    // bench target 在 ANDROID 下被跳过）。产物 libhistogram.so 打包进 APK。
    externalNativeBuild {
        cmake {
            path = file("../../native/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
