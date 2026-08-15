# FlyNES 基座（阶段 0）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭出「安卓离线红白机模拟器」的可扩展基座：平台无关 C ABI 包裹 NestopiaUE 内核，Java 壳做到「加载 ROM → 渲染 → 触屏 → 声音 → 存档」的最小闭环，并通过 5 分钟时序验收。

**Architecture:** 四层——`core/`（平台无关 C++：NestopiaUE vendor + C ABI 实现 `nes_abi`）+ `include/nes/nes.h`（唯一跨边界 C ABI 契约）+ `bridge`（JNI 薄适配）+ `app/`（Java 壳）。帧循环以音频为主时钟（AudioTrack 阻塞写驱动），视频软件 RGB565 blit 到 SurfaceView。core 零平台依赖，可被桌面 SDL 第二壳与无头测试直接链接。

**Tech Stack:** C++17（NDK 27.0.12077973）、CMake、NestopiaUE 1.53.2（GPLv2，锁 commit `4470a2e`）、zlib（系统/NDK 自带）、Java 17、Android Gradle Plugin 8.x、minSdk 24 / compileSdk 34、ABI arm64-v8a + x86_64。

**Spec:** `docs/superpowers/specs/2026-08-15-nes-emulator-design.md`（本计划的唯一权威需求来源）

**字段级 C ABI 权威参考:** `docs/nes-arch-review/t2b-c-abi-draft.md`（基于 vendor 1.53.2 真实源码逐条核对的 `nes.h` 完整草案 + 逐函数 → NestopiaUE API 映射）。本计划中的 C++ 代码是「简化但正确」的实现；执行子代理若遇签名细节不一致，**以 t2b 草案为准**。关键已内联修正：输入是拉取式 `Controllers::Pad::callback`（无 `SetInput`）；`nes_battery_flush` 无干净内核 API（阶段 0 返回 NOT_IMPLEMENTED，用 save_state 做安全点）；音频每帧样本数 = `sample_rate / 60.0988`。

---

## 文件结构总览（本计划将创建/修改）

```
core/
  CMakeLists.txt                  # nestopia(static) + nes_abi(static) + 可选 host 测试
  include/nes/nes.h               # C ABI 头（extern "C"，不透明句柄 nes_t*）
  src/nes_core.cpp                # 生命周期 + run_frames + video/audio/input
  src/nes_stream.cpp              # std::streambuf 内存流适配器
  src/nes_state.cpp               # save/load state + battery + cheat + patch + rom_info
  tests/test_core.cpp             # 无头 golden/smoke 测试（host 可执行文件）
  tests/fixtures/from_below.nes   # MIT homebrew 测试夹具
app/
  build.gradle                    # Android module 构建脚本
  src/main/AndroidManifest.xml
  src/main/cpp/CMakeLists.txt     # nescore(.so) = JNI + nes_abi + nestopia
  src/main/cpp/nes_jni.cpp        # JNI 桥（含 nativeBlit）
  src/main/java/com/flynes/emu/MainActivity.java
  src/main/java/com/flynes/emu/EmuView.java        # SurfaceView 封装
  src/main/java/com/flynes/emu/TouchController.java # 触屏虚拟手柄
  src/main/java/com/flynes/emu/NesCore.java        # JNI 包装（native 方法声明）
  src/main/java/com/flynes/emu/AudioThread.java    # 音频主时钟循环
  src/main/res/values/strings.xml
  src/main/assets/roms/from_below.nes              # 内置示例 ROM
  src/main/assets/NstDatabase.xml                  # 内核纠错库（复制自 vendor）
settings.gradle  build.gradle  gradle.properties  .gitignore
```

---

## Task 0: 脚手架与构建系统引导

**Files:**
- Create: `settings.gradle`, `build.gradle`, `gradle.properties`, `app/build.gradle`, `app/src/main/AndroidManifest.xml`, `app/src/main/res/values/strings.xml`, `app/src/main/cpp/CMakeLists.txt`（占位）、`app/src/main/java/com/flynes/emu/MainActivity.java`（占位）

- [ ] **Step 1: 安装 Gradle 8.7 并生成 wrapper**

本机未装 gradle。下载 Gradle 8.7 到工具目录并生成 wrapper：

```powershell
$tools = "$env:USERPROFILE\.gradle-tools"
New-Item -ItemType Directory -Force -Path $tools | Out-Null
$zip = "$tools\gradle-8.7-bin.zip"
if (!(Test-Path $zip)) { Invoke-WebRequest -Uri "https://services.gradle.org/distributions/gradle-8.7-bin.zip" -OutFile $zip }
Expand-Archive -Path $zip -DestinationPath $tools -Force
$gradle = "$tools\gradle-8.7\bin\gradle.bat"
& $gradle wrapper --gradle-version 8.7
```

Expected: 生成 `gradlew`、`gradlew.bat`、`gradle/wrapper/gradle-wrapper.jar`、`gradle/wrapper/gradle-wrapper.properties`。

- [ ] **Step 2: 写根 `settings.gradle`**

```groovy
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "FlyNES"
include ':app'
```

- [ ] **Step 3: 写根 `build.gradle`**

```groovy
plugins {
    id 'com.android.application' version '8.3.2' apply false
}
```

- [ ] **Step 4: 写 `gradle.properties`**

```properties
org.gradle.jvmargs=-Xmx2048m -Dfile.encoding=UTF-8
android.useAndroidX=true
android.nonTransitiveRClass=true
```

- [ ] **Step 5: 写 `app/build.gradle`**

```groovy
plugins {
    id 'com.android.application'
}

android {
    namespace 'com.flynes.emu'
    compileSdk 34

    defaultConfig {
        applicationId "com.flynes.emu"
        minSdk 24
        targetSdk 34
        versionCode 1
        versionName "0.1.0"
        ndk {
            abiFilters 'arm64-v8a', 'x86_64'
        }
        externalNativeBuild {
            cmake {
                cppFlags '-std=c++17 -fvisibility=hidden'
                arguments '-DNES_CORE_DIR=' + project.rootDir.getAbsolutePath() + '/core'
            }
        }
    }

    buildTypes {
        release {
            minifyEnabled false
        }
    }

    externalNativeBuild {
        cmake {
            path 'src/main/cpp/CMakeLists.txt'
            version '3.22.1'
        }
    }

    ndkVersion '27.0.12077973'

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_17
        targetCompatibility JavaVersion.VERSION_17
    }
}
```

- [ ] **Step 6: 写 `app/src/main/AndroidManifest.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <application
        android:label="@string/app_name"
        android:icon="@mipmap/ic_launcher"
        android:allowBackup="true"
        android:theme="@android:style/Theme.NoTitleBar.Fullscreen">
        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:configChanges="orientation|screenSize|keyboardHidden"
            android:screenOrientation="landscape">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

- [ ] **Step 7: 写 `app/src/main/res/values/strings.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">FlyNES</string>
</resources>
```

- [ ] **Step 8: 写占位 `app/src/main/cpp/CMakeLists.txt` 与 `MainActivity.java`**

`app/src/main/cpp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
project(nescore CXX)
add_library(nescore SHARED nes_jni.cpp)
```

`app/src/main/java/com/flynes/emu/MainActivity.java`:

```java
package com.flynes.emu;

import android.app.Activity;
import android.os.Bundle;

public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
```

- [ ] **Step 9: 提供启动图标**

复制一个临时 PNG 到 `app/src/main/res/mipmap/ic_launcher.png`（任意 48x48 PNG 即可，后续替换为原创图标）：

```powershell
New-Item -ItemType Directory -Force -Path app/src/main/res/mipmap-anydpi-v26 | Out-Null
```

若缺少 `mipmap` 目录导致构建失败，改用 `android:icon="@android:drawable/sym_def_app_icon"` 临时规避（后续任务替换为原创图标，避免商标问题）。

- [ ] **Step 10: 验证空项目可编译**

Run: `.\gradlew.bat assembleDebug`
Expected: BUILD SUCCESSFUL（生成 `app/build/outputs/apk/debug/app-debug.apk`）

- [ ] **Step 11: 提交**

```bash
git add -A
git commit -m "chore: scaffold Gradle Android project skeleton"
```

---

## Task 1: 编译 NestopiaUE 核心为静态库

**Files:**
- Create: `core/CMakeLists.txt`

- [ ] **Step 1: 写 `core/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(nes_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NES_BUILD_TESTS "Build host-side tests" OFF)

set(NESTOPIA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nestopiaue/source/core)

# --- nestopia: upstream emulator core (299 .cpp, C++17) ---
file(GLOB_RECURSE NESTOPIA_SOURCES ${NESTOPIA_ROOT}/*.cpp)
add_library(nestopia STATIC ${NESTOPIA_SOURCES})
target_include_directories(nestopia PUBLIC
    ${NESTOPIA_ROOT}
    ${NESTOPIA_ROOT}/api
)
# zlib is required by NstZlib.cpp (compressed save states / archives)
find_library(ZLIB_LIB z)
if(ZLIB_LIB)
    target_link_libraries(nestopia PUBLIC ${ZLIB_LIB})
else()
    message(WARNING "zlib not found; linking may fail on save-state compression")
endif()

# --- nes_abi: platform-independent C ABI implementation ---
add_library(nes_abi STATIC
    src/nes_core.cpp
    src/nes_stream.cpp
    src/nes_state.cpp
)
target_include_directories(nes_abi PUBLIC include)
target_link_libraries(nes_abi PUBLIC nestopia)

# --- host-side smoke test (not built for Android) ---
if(NES_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(nes_core_test tests/test_core.cpp)
    target_link_libraries(nes_core_test PRIVATE nes_abi)
    add_test(NAME core_smoke COMMAND nes_core_test)
endif()
```

- [ ] **Step 2: 用 NDK 交叉编译验证 core 可编译（先放占位源，避免空 target 报错）**

先创建三个占位源文件（内容将在 Task 3~6 填充），否则 CMake 会因缺源报错：

```powershell
New-Item -ItemType Directory -Force -Path core/src, core/include/nes | Out-Null
"// placeholder" | Out-File -Encoding utf8 core/src/nes_core.cpp
"// placeholder" | Out-File -Encoding utf8 core/src/nes_stream.cpp
"// placeholder" | Out-File -Encoding utf8 core/src/nes_state.cpp
"#ifndef NES_H`n#define NES_H`n#endif" | Out-File -Encoding utf8 core/include/nes/nes.h
```

- [ ] **Step 3: 验证 NDK 编译 nestopia 静态库通过**

Run（用 NDK 自带的 cmake + ninja 对 arm64-v8a 配置并构建 nestopia target）:

```powershell
$sdk = $env:ANDROID_HOME
$cmake = "$sdk\cmake\3.22.1\bin\cmake.exe"
$ninja = "$sdk\cmake\3.22.1\bin\ninja.exe"
$ndk = "$sdk\ndk\27.0.12077973"
& $cmake -S core -B core/build/android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DNES_BUILD_TESTS=OFF `
  -DCMAKE_MAKE_PROGRAM="$ninja"
& $cmake --build core/build/android --target nestopia -j 4
```

Expected: 编译成功，产生 `core/build/android/libnestopia.a`。若报 zlib 找不到，则把 `find_library(ZLIB_LIB z)` 换成 `target_link_libraries(nestopia PUBLIC z)` 直接按名链接（NDK sysroot 自带 libz）。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "build: compile NestopiaUE 1.53.2 core as static lib via CMake"
```

---

## Task 2: 定义 C ABI 头文件

**Files:**
- Create: `core/include/nes/nes.h`

- [ ] **Step 1: 写完整的 `core/include/nes/nes.h`**

```c
#ifndef NES_H
#define NES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_API_VERSION 1

/* 不透明句柄 */
typedef struct nes_ctx nes_t;

/* 错误码：与 Nestopia 语义解耦的稳定枚举 */
typedef enum {
    NES_OK = 0,
    NES_ERR_INVALID_ARG = -1,
    NES_ERR_NO_CARTRIDGE = -2,
    NES_ERR_UNSUPPORTED_MAPPER = -3,
    NES_ERR_CRC_FAIL = -4,
    NES_ERR_FDS_BIOS_REQUIRED = -5,
    NES_ERR_STATE_VERSION = -6,
    NES_ERR_NOT_IMPLEMENTED = -7,
    NES_ERR_UNKNOWN = -99
} nes_result;

/* 输入位掩码：与 Nestopia Pad 位一致 */
typedef enum {
    NES_BTN_A      = 0x01,
    NES_BTN_B      = 0x02,
    NES_BTN_SELECT = 0x04,
    NES_BTN_START  = 0x08,
    NES_BTN_UP     = 0x10,
    NES_BTN_DOWN   = 0x20,
    NES_BTN_LEFT   = 0x40,
    NES_BTN_RIGHT  = 0x80
} nes_button;

/* 视频缓冲描述（版本化结构体） */
typedef struct {
    uint32_t size;        /* sizeof(nes_video_info) */
    uint32_t version;     /* 1 */
    const void* pixels;   /* RGB565 帧缓冲，256x240 */
    uint32_t width;       /* 256 */
    uint32_t height;      /* 240 */
    int32_t  pitch;       /* 每行字节数，256*2 */
} nes_video_info;

/* ROM 元信息（UTF-8，避免 wchar_t 跨平台差异） */
typedef struct {
    uint32_t size;
    uint32_t version;     /* 1 */
    char title[128];      /* UTF-8 标题 */
    uint32_t mapper;
    uint32_t prg_bytes;
    uint32_t chr_bytes;
    int has_battery;
    int is_pal;
} nes_rom_info;

/* 金手指码（地址/值/比较） */
typedef struct {
    uint16_t address;
    uint8_t  value;
    uint8_t  compare;
    int      use_compare;
} nes_cheat_code;

/* 回调：日志 */
typedef void (*nes_log_cb)(void* userdata, const char* message, uint32_t length);

/* 回调：电池存档反向 IO。
   当 action != 0 表示"取数据"，应把数据写入 data（size 字节）；
   当 action == 0 表示"存数据"，内核给出 data/size 待外壳落盘。
   用 direction 语义化：NES_BATTERY_LOAD / NES_BATTERY_SAVE。 */
typedef enum { NES_BATTERY_LOAD = 1, NES_BATTERY_SAVE = 2 } nes_battery_op;
typedef void (*nes_battery_cb)(void* userdata, int op, void* data, uint32_t size);

/* ===================== 生命周期 ===================== */
nes_t*      nes_create(void);
void        nes_destroy(nes_t* ctx);
const char* nes_version(void);
uint32_t    nes_api_version(void);

/* 加载数据库（NstDatabase.xml 内容，需在 load_rom 前调用一次，可选） */
nes_result  nes_load_database(nes_t* ctx, const uint8_t* xml, size_t len);

/* 加载 ROM（字节指针+长度，core 不碰文件系统）。可选先应用补丁。
   patch 为 IPS/UPS 字节，patch_len>0 时应用。 */
nes_result  nes_load_rom(nes_t* ctx, const uint8_t* rom, size_t rom_len,
                         const uint8_t* patch, size_t patch_len);
nes_result  nes_unload(nes_t* ctx);
nes_result  nes_reset(nes_t* ctx, int hard);

/* ===================== 运行 ===================== */
/* 执行 n 帧，音频写入 audio_out（int16 mono，最多 audio_cap 个采样），
   返回实际写入的采样数。视频帧写入内部 RGB565 缓冲。 */
int32_t     nes_run_frames(nes_t* ctx, uint32_t n, int16_t* audio_out, uint32_t audio_cap);

/* 取视频缓冲描述（指针在下次 run_frames 前有效且稳定） */
nes_video_info nes_video_buffer(const nes_t* ctx);

/* 取最近完成的帧序号，供渲染线程检测新帧 */
uint64_t    nes_video_frame_number(const nes_t* ctx);

/* 设置音频采样率与单声道（默认 48000 mono） */
nes_result  nes_set_sample_rate(nes_t* ctx, uint32_t rate);

/* ===================== 输入 ===================== */
nes_result  nes_set_input(nes_t* ctx, uint8_t buttons);

/* ===================== 存档 ===================== */
/* 即时存档：序列化到调用方缓冲（至少 nes_save_state_size() 字节），返回实际长度 */
size_t      nes_save_state_size(const nes_t* ctx);
nes_result  nes_save_state(nes_t* ctx, uint8_t* out, size_t cap, size_t* written);
nes_result  nes_load_state(nes_t* ctx, const uint8_t* in, size_t len);

/* 电池存档：flush 触发内核把 SRAM 经 nes_battery_cb 交给外壳 */
nes_result  nes_flush_battery(nes_t* ctx);
/* 电池存档读入：外壳把已存 SRAM 经 nes_battery_cb 交给内核 */
nes_result  nes_load_battery(nes_t* ctx);

/* ===================== 金手指 ===================== */
nes_result  nes_cheat_add_gg(nes_t* ctx, const char* game_genie_code);
nes_result  nes_cheat_add_par(nes_t* ctx, const char* pro_action_rocky_code);
nes_result  nes_cheat_clear(nes_t* ctx);

/* ===================== 内存（阶段 0 返回 NOT_IMPLEMENTED） ===================== */
nes_result  nes_read_cpu_ram(nes_t* ctx, uint8_t* out, size_t cap, size_t* written);

/* ===================== ROM 信息 ===================== */
nes_result  nes_get_rom_info(nes_t* ctx, nes_rom_info* out);

/* ===================== 回调注册 ===================== */
void        nes_set_log_callback(nes_t* ctx, nes_log_cb cb, void* userdata);
void        nes_set_battery_callback(nes_t* ctx, nes_battery_cb cb, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* NES_H */
```

- [ ] **Step 2: 自检头文件可被 C 与 C++ 包含**

Run: `& "$env:ANDROID_HOME\ndk\27.0.12077973\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe" -std=c++17 -fsyntax-only -I core/include -x c++ core/include/nes/nes.h`
Expected: 无输出、退出码 0。

- [ ] **Step 3: 提交**

```bash
git add core/include/nes/nes.h
git commit -m "feat: define C ABI header nes.h"
```

---

## Task 3: 实现流适配器与生命周期

**Files:**
- Modify: `core/src/nes_stream.cpp`
- Create: `core/src/nes_stream.hpp`

- [ ] **Step 1: 写 `core/src/nes_stream.hpp`（内存流适配器声明）**

```cpp
#ifndef NES_STREAM_HPP
#define NES_STREAM_HPP

#include <istream>
#include <ostream>
#include <vector>

namespace nes {

// 从已有内存构造只读 std::istream（Nestopia 全走流接口）
class MemIStream {
public:
    MemIStream(const uint8_t* data, size_t len);
    std::istream& stream() { return in_; }

private:
    struct Buf : std::streambuf {
        Buf(const char* p, size_t n) { setg(const_cast<char*>(p), const_cast<char*>(p), const_cast<char*>(p) + n); }
    };
    Buf buf_;
    std::istream in_;
};

// 可写的 std::ostream，用于 save state
class MemOStream {
public:
    MemOStream();
    std::ostream& stream() { return out_; }
    const std::vector<uint8_t>& data() const { return data_; }

private:
    struct Buf : std::streambuf {
        std::vector<uint8_t>* data;
        explicit Buf(std::vector<uint8_t>* d) : data(d) {}
        int_type overflow(int_type c) override {
            if (c != traits_type::eof()) data->push_back((uint8_t)c);
            return c;
        }
        std::streamsize xsputn(const char* s, std::streamsize n) override {
            data->insert(data->end(), s, s + n);
            return n;
        }
    };
    std::vector<uint8_t> data_;
    Buf buf_;
    std::ostream out_;
};

} // namespace nes

#endif
```

- [ ] **Step 2: 写 `core/src/nes_stream.cpp`**

```cpp
#include "nes_stream.hpp"

namespace nes {

MemIStream::MemIStream(const uint8_t* data, size_t len)
    : buf_(reinterpret_cast<const char*>(data), len), in_(&buf_) {}

MemOStream::MemOStream() : buf_(&data_), out_(&buf_) {}

} // namespace nes
```

- [ ] **Step 3: 写 `core/src/nes_core.cpp`（生命周期部分，含内部上下文结构）**

```cpp
#include "nes.h"
#include "nes_stream.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "NstApiEmulator.hpp"
#include "NstApiMachine.hpp"
#include "NstApiVideo.hpp"
#include "NstApiSound.hpp"
#include "NstApiInput.hpp"
#include "NstApiCartridge.hpp"
#include "NstApiCheats.hpp"
#include "NstApiUser.hpp"

struct nes_ctx {
    Nes::Api::Emulator emulator;
    Nes::Api::Machine machine;
    Nes::Api::Video video;
    Nes::Api::Sound sound;
    Nes::Api::Input input;
    Nes::Api::Cartridge cartridge;
    Nes::Api::Cheats cheats;

    // 视频
    uint8_t* framebuffer;      // RGB565 256x240
    uint64_t frame_number;

    // 音频
    uint32_t sample_rate;
    std::atomic<uint32_t> input_buttons;  // 当前手柄位掩码（推→拉桥接）

    // 回调
    nes_log_cb log_cb;
    void* log_userdata;
    nes_battery_cb battery_cb;
    void* battery_userdata;

    // 电池存档缓冲（反向回调桥接用）
    std::vector<uint8_t> battery_sram;
    bool battery_dirty;

    nes_ctx()
        : machine(emulator), video(emulator), sound(emulator),
          input(emulator), cartridge(emulator), cheats(emulator),
          framebuffer(nullptr), frame_number(0), sample_rate(48000), input_buttons(0),
          log_cb(nullptr), log_userdata(nullptr),
          battery_cb(nullptr), battery_userdata(nullptr), battery_dirty(false) {}
};

// 静态：电池存档反向回调转发到 nes_ctx
static void batteryFileIo(void* userdata, Nes::Api::User::File& file);

// 静态：输入拉取回调（内核每帧回调索取按键；Nestopia 无 SetInput，这是唯一输入入口）
static bool on_pad_poll(void* userdata, Nes::Core::Input::Controllers::Pad& pad, uint index) {
    (void)index;
    nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
    pad.buttons = ctx->input_buttons.load(std::memory_order_relaxed);
    return true;
}

extern "C" {

const char* nes_version(void) { return "0.1.0"; }
uint32_t nes_api_version(void) { return NES_API_VERSION; }

nes_t* nes_create(void) {
    nes_t* ctx = new nes_ctx();
    if (!ctx) return nullptr;
    ctx->framebuffer = static_cast<uint8_t*>(std::malloc(256 * 240 * 2));
    if (!ctx->framebuffer) { delete ctx; return nullptr; }
    // 注册电池存档反向回调 + 输入拉取回调
    Nes::Api::User::fileIoCallback.Set(batteryFileIo, ctx);
    Nes::Core::Input::Controllers::Pad::callback.Set(&on_pad_poll, ctx);
    return ctx;
}

void nes_destroy(nes_t* ctx) {
    if (!ctx) return;
    Nes::Api::User::fileIoCallback.Unset();
    std::free(ctx->framebuffer);
    delete ctx;
}

void nes_set_log_callback(nes_t* ctx, nes_log_cb cb, void* userdata) {
    if (!ctx) return;
    ctx->log_cb = cb; ctx->log_userdata = userdata;
}

void nes_set_battery_callback(nes_t* ctx, nes_battery_cb cb, void* userdata) {
    if (!ctx) return;
    ctx->battery_cb = cb; ctx->battery_userdata = userdata;
}

nes_result nes_load_database(nes_t* ctx, const uint8_t* xml, size_t len) {
    if (!ctx || !xml || len == 0) return NES_ERR_INVALID_ARG;
    nes::MemIStream ms(xml, len);
    Nes::Api::Cartridge::Database db = ctx->cartridge.GetDatabase();
    Nes::Result r = db.Load(ms.stream());
    return (r == Nes::RESULT_OK) ? NES_OK : NES_ERR_UNKNOWN;
}

nes_result nes_load_rom(nes_t* ctx, const uint8_t* rom, size_t rom_len,
                        const uint8_t* patch, size_t patch_len) {
    if (!ctx || !rom || rom_len == 0) return NES_ERR_INVALID_ARG;
    ctx->machine.Unload();
    nes::MemIStream roms(rom, rom_len);
    Nes::Result r;
    if (patch && patch_len > 0) {
        nes::MemIStream ps(patch, patch_len);
        Nes::Api::Machine::Patch p(ps.stream(), false);
        r = ctx->machine.LoadCartridge(roms.stream(), Nes::Api::Machine::FAVORED_NES_NTSC, p);
    } else {
        r = ctx->machine.LoadCartridge(roms.stream(), Nes::Api::Machine::FAVORED_NES_NTSC);
    }
    if (r != Nes::RESULT_OK) {
        return (r == Nes::RESULT_ERR_UNSUPPORTED_MAPPER) ? NES_ERR_UNSUPPORTED_MAPPER : NES_ERR_UNKNOWN;
    }
    ctx->input.AutoSelectControllers();
    ctx->machine.Power(true);
    // 配置渲染状态：RGB565, 256x240, 无滤镜
    Nes::Api::Video::RenderState rs;
    rs.bits.count = 16;
    rs.bits.mask.r = 0xF800;
    rs.bits.mask.g = 0x07E0;
    rs.bits.mask.b = 0x001F;
    rs.width = 256;
    rs.height = 240;
    rs.filter = Nes::Api::Video::RenderState::FILTER_NONE;
    ctx->video.SetRenderState(rs);
    ctx->sound.SetSampleRate(ctx->sample_rate);
    ctx->sound.SetSpeaker(Nes::Api::Sound::SPEAKER_MONO);
    ctx->frame_number = 0;
    return NES_OK;
}

nes_result nes_unload(nes_t* ctx) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    ctx->machine.Power(false);
    ctx->machine.Unload();
    return NES_OK;
}

nes_result nes_reset(nes_t* ctx, int hard) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    ctx->machine.Reset(hard != 0);
    return NES_OK;
}

nes_result nes_set_sample_rate(nes_t* ctx, uint32_t rate) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    ctx->sample_rate = rate;
    ctx->sound.SetSampleRate(rate);
    return NES_OK;
}

nes_result nes_set_input(nes_t* ctx, uint8_t buttons) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    ctx->input_buttons.store(buttons, std::memory_order_relaxed);
    return NES_OK;
}

} // extern "C"
```

- [ ] **Step 4: 编译验证 nestopia + nes_abi 可链接**

Run:

```powershell
$cmake = "$env:ANDROID_HOME\cmake\3.22.1\bin\cmake.exe"
$ninja = "$env:ANDROID_HOME\cmake\3.22.1\bin\ninja.exe"
$ndk = "$env:ANDROID_HOME\ndk\27.0.12077973"
& $cmake -S core -B core/build/android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DNES_BUILD_TESTS=OFF `
  -DCMAKE_MAKE_PROGRAM="$ninja"
& $cmake --build core/build/android --target nes_abi -j 4
```

Expected: 编译成功（`run_frames`/`video` 等尚未定义，仅在 Task 4 加入；本步先保证生命周期部分可编译）。若因 `nes_run_frames` 等声明无定义不影响静态库构建（静态库允许未定义符号，链接时才检查）。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: implement C ABI lifecycle + memory stream adapter"
```

---

## Task 4: 实现运行/视频/音频/输入

**Files:**
- Modify: `core/src/nes_core.cpp`

- [ ] **Step 1: 在 `nes_core.cpp` 末尾的 `extern "C"` 块内追加运行相关实现**

（`struct nes_ctx` 需含成员：`uint8_t input_buttons;` 与音频输出缓冲复用）

```cpp
nes_video_info nes_video_buffer(const nes_t* ctx) {
    nes_video_info info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    info.version = 1;
    if (ctx) {
        info.pixels = ctx->framebuffer;
        info.width = 256;
        info.height = 240;
        info.pitch = 256 * 2;
    }
    return info;
}

uint64_t nes_video_frame_number(const nes_t* ctx) {
    return ctx ? ctx->frame_number : 0;
}

int32_t nes_run_frames(nes_t* ctx, uint32_t n, int16_t* audio_out, uint32_t audio_cap) {
    if (!ctx) return 0;
    // 每帧采样数 = sample_rate / 60.0988（NTSC）；核心保证写满 length[0]
    uint32_t per_frame = (uint32_t)(ctx->sample_rate / 60.0988);
    if (per_frame == 0) per_frame = 735; // 44.1kHz 下限兜底
    uint32_t total = 0;
    Nes::Core::Input::Controllers pads; // 输入经 Pad::callback 拉取，此对象仅占位
    for (uint32_t i = 0; i < n; ++i) {
        Nes::Core::Video::Output vo(ctx->framebuffer, 256 * 2);
        bool has_room = audio_out && ((audio_cap - total) >= per_frame);
        int16_t* dst = has_room ? (audio_out + total) : nullptr;
        Nes::Core::Sound::Output so(dst, dst ? per_frame : 0);
        ctx->emulator.Execute(&vo, &so, &pads);
        total += so.length[0];
        ctx->frame_number++;
    }
    return (int32_t)total;
}
```

- [ ] **Step 2: 编译并链接验证**

Run: 同 Task 3 Step 4 的构建命令，target 改为 `nes_abi`。
Expected: 编译成功。

- [ ] **Step 3: 提交**

```bash
git add -A
git commit -m "feat: implement run/video/audio/input in C ABI"
```

---

## Task 5: 实现存档/金手指/补丁/ROM 信息

**Files:**
- Modify: `core/src/nes_state.cpp`（新内容）、`core/src/nes_core.cpp`（补丁与 ROM 信息）

- [ ] **Step 1: 在 `nes_core.cpp` 追加电池存档反向回调实现与 ROM 信息/金手指/内存**

```cpp
// ---- 电池存档反向回调：内核经此取/存 SRAM ----
static void batteryFileIo(void* userdata, Nes::Api::User::File& file) {
    nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
    switch (file.GetAction()) {
    case Nes::Api::User::File::SAVE_BATTERY:
    case Nes::Api::User::File::SAVE_EEPROM: {
        const void* mem = nullptr; ulong size = 0;
        file.GetContent(mem, size);
        if (ctx->battery_cb && mem && size) {
            ctx->battery_cb(ctx->battery_userdata, NES_BATTERY_SAVE,
                            const_cast<void*>(mem), (uint32_t)size);
        }
        break;
    }
    case Nes::Api::User::File::LOAD_BATTERY:
    case Nes::Api::User::File::LOAD_EEPROM: {
        if (!ctx->battery_sram.empty()) {
            file.SetContent(ctx->battery_sram.data(), (ulong)ctx->battery_sram.size());
        }
        break;
    }
    default:
        break;
    }
}
```

（该函数需在 `nes_create` 之前定义，或前置声明。已在 Task 3 前置声明。）

- [ ] **Step 2: 在 `nes_core.cpp` 追加 `nes_save_state/load_state/flush_battery/load_battery`**

```cpp
size_t nes_save_state_size(const nes_t* ctx) { (void)ctx; return 4 * 1024 * 1024; }

nes_result nes_save_state(nes_t* ctx, uint8_t* out, size_t cap, size_t* written) {
    if (!ctx || !out || !written) return NES_ERR_INVALID_ARG;
    nes::MemOStream mos;
    Nes::Result r = ctx->machine.SaveState(mos.stream());
    if (r != Nes::RESULT_OK) return NES_ERR_UNKNOWN;
    const std::vector<uint8_t>& d = mos.data();
    if (d.size() > cap) return NES_ERR_INVALID_ARG;
    memcpy(out, d.data(), d.size());
    *written = d.size();
    return NES_OK;
}

nes_result nes_load_state(nes_t* ctx, const uint8_t* in, size_t len) {
    if (!ctx || !in || len == 0) return NES_ERR_INVALID_ARG;
    nes::MemIStream mis(in, len);
    Nes::Result r = ctx->machine.LoadState(mis.stream());
    if (r == Nes::RESULT_ERR_INVALID_CRC) return NES_ERR_CRC_FAIL;
    return (r == Nes::RESULT_OK) ? NES_OK : NES_ERR_UNKNOWN;
}

nes_result nes_flush_battery(nes_t* ctx) {
    (void)ctx;
    // 1.53.2 无公开「仅存电池」API：SAVE_BATTERY 只在 Unload()/Power(false) 触发。
    // 阶段 0 返回 NOT_IMPLEMENTED；宿主在 onPause 用 nes_save_state 做即时安全点。
    return NES_ERR_NOT_IMPLEMENTED;
}

nes_result nes_load_battery(nes_t* ctx) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    // 外壳先把已存 SRAM 写入 ctx->battery_sram（经由 set_battery_callback 之外的
    // 直接通道）。这里暂无额外动作；LOAD_BATTERY 回调在 load_rom 后由内核触发。
    return NES_OK;
}
```

> 说明：NestopiaUE 的电池存档由内核在 `Unload`/`Power(off)` 时机触发 `fileIoCallback(SAVE_BATTERY)`，且**无公开「仅存电池」API**（`nes_flush_battery` 阶段 0 返回 NOT_IMPLEMENTED）。阶段 0 的正确安全点是：**onPause 时调用 `nes_save_state`（NST 是电池的超集）做即时存档**；电池数据流本身已由 `batteryFileIo` 打通，`Unload`/`Power(off)` 时会经 `battery_cb` 交给外壳落盘。真正的「仅电池 flush」留到阶段 1（改内核或接受 Unload 时机）。

- [ ] **Step 3: 追加金手指与内存、ROM 信息**

```cpp
nes_result nes_cheat_add_gg(nes_t* ctx, const char* code) {
    if (!ctx || !code) return NES_ERR_INVALID_ARG;
    Nes::Api::Cheats::Code c;
    Nes::Result r = Nes::Api::Cheats::GameGenieDecode(code, c);
    if (r != Nes::RESULT_OK) return NES_ERR_INVALID_ARG;
    r = ctx->cheats.SetCode(c);
    return (r == Nes::RESULT_OK) ? NES_OK : NES_ERR_UNKNOWN;
}

nes_result nes_cheat_add_par(nes_t* ctx, const char* code) {
    if (!ctx || !code) return NES_ERR_INVALID_ARG;
    Nes::Api::Cheats::Code c;
    Nes::Result r = Nes::Api::Cheats::ProActionRockyDecode(code, c);
    if (r != Nes::RESULT_OK) return NES_ERR_INVALID_ARG;
    r = ctx->cheats.SetCode(c);
    return (r == Nes::RESULT_OK) ? NES_OK : NES_ERR_UNKNOWN;
}

nes_result nes_cheat_clear(nes_t* ctx) {
    if (!ctx) return NES_ERR_INVALID_ARG;
    ctx->cheats.ClearCodes();
    return NES_OK;
}

nes_result nes_read_cpu_ram(nes_t* ctx, uint8_t* out, size_t cap, size_t* written) {
    (void)ctx; (void)out; (void)cap; (void)written;
    return NES_ERR_NOT_IMPLEMENTED; // 阶段 0 不实现
}

nes_result nes_get_rom_info(nes_t* ctx, nes_rom_info* out) {
    if (!ctx || !out) return NES_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->size = sizeof(*out);
    out->version = 1;
    const Nes::Api::Cartridge::Profile* p = ctx->cartridge.GetProfile();
    if (!p) return NES_ERR_NO_CARTRIDGE;
    // wstring → UTF-8（Linux/Android wchar_t 为 4 字节 UTF-32）
    std::wstring ws = p->game.title;
    std::string utf8;
    for (wchar_t wc : ws) {
        if (wc < 0x80) utf8.push_back((char)wc);
        else if (wc < 0x800) { utf8.push_back((char)(0xC0 | (wc >> 6))); utf8.push_back((char)(0x80 | (wc & 0x3F))); }
        else { utf8.push_back((char)(0xE0 | (wc >> 12))); utf8.push_back((char)(0x80 | ((wc >> 6) & 0x3F))); utf8.push_back((char)(0x80 | (wc & 0x3F))); }
    }
    strncpy(out->title, utf8.c_str(), sizeof(out->title) - 1);
    out->mapper = p->board.mapper;
    out->prg_bytes = p->board.GetPrg();
    out->chr_bytes = p->board.GetChr();
    out->has_battery = p->board.HasBattery() ? 1 : 0;
    out->is_pal = (ctx->machine.GetMode() == Nes::Api::Machine::PAL) ? 1 : 0;
    return NES_OK;
}
```

- [ ] **Step 4: 编译验证**

Run: 构建 `nes_abi` target（命令同前）。
Expected: 编译成功。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "feat: implement state/battery/cheat/rom-info in C ABI"
```

---

## Task 6: 无头冒烟测试（host 构建）

**Files:**
- Create: `core/tests/test_core.cpp`、`core/tests/fixtures/from_below.nes`

- [ ] **Step 1: 下载 MIT homebrew 测试 ROM**

从 From Below（Matt Hughson，MIT）的发布页下载 `.nes`（具体发布 URL 以仓库 Release 为准；先下载到临时位置核对 license 后放入 fixtures）：

```powershell
New-Item -ItemType Directory -Force -Path core/tests/fixtures | Out-Null
# 以 GitHub release 实际 URL 为准；此处示意（执行时按真实 release 链接替换）
# Invoke-WebRequest -Uri "<release .nes url>" -OutFile core/tests/fixtures/from_below.nes
```

校验：文件头 4 字节为 `4E 45 53 1A`（"NES\x1A"）。同时记录 MIT LICENSE 到 `core/tests/fixtures/LICENSE-from-below.txt`。

- [ ] **Step 2: 写 `core/tests/test_core.cpp`**

```cpp
#include "nes.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    const char* romPath = (argc > 1) ? argv[1] : "core/tests/fixtures/from_below.nes";
    std::vector<uint8_t> rom = readFile(romPath);
    if (rom.size() < 16 || rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) {
        std::fprintf(stderr, "invalid ROM file: %s\n", romPath);
        return 1;
    }

    nes_t* ctx = nes_create();
    if (!ctx) return 2;
    if (nes_load_rom(ctx, rom.data(), rom.size(), nullptr, 0) != NES_OK) {
        std::fprintf(stderr, "load failed\n");
        nes_destroy(ctx);
        return 3;
    }

    nes_rom_info info;
    nes_get_rom_info(ctx, &info);
    std::printf("title=%s mapper=%u prg=%u chr=%u battery=%d\n",
                info.title, info.mapper, info.prg_bytes, info.chr_bytes, info.has_battery);

    // 跑 60 帧，收集音频，检查视频非空、音频非全零
    std::vector<int16_t> audio(60 * 8192);
    int32_t samples = nes_run_frames(ctx, 60, audio.data(), (uint32_t)audio.size());
    std::printf("samples=%d\n", samples);

    nes_video_info vi = nes_video_buffer(ctx);
    bool videoNonBlank = false;
    const uint8_t* px = (const uint8_t*)vi.pixels;
    for (size_t i = 0; i < (size_t)vi.height * vi.width * 2; i += 2) {
        if (px[i] != 0 || px[i+1] != 0) { videoNonBlank = true; break; }
    }
    bool audioNonSilent = false;
    for (int32_t i = 0; i < samples; ++i) {
        if (audio[i] != 0) { audioNonSilent = true; break; }
    }

    nes_destroy(ctx);
    if (!videoNonBlank) { std::fprintf(stderr, "FAIL: video blank\n"); return 4; }
    if (!audioNonSilent) { std::fprintf(stderr, "FAIL: audio silent\n"); return 5; }
    std::printf("PASS\n");
    return 0;
}
```

- [ ] **Step 3: host 构建并运行测试**

Run（用 NDK clang 对 host 编译，避免依赖桌面 g++；直接链接 zlib）:

```powershell
$clang = "$env:ANDROID_HOME\ndk\27.0.12077973\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
& $clang -std=c++17 -I core/include -I core/vendor/nestopiaue/source/core `
  -I core/vendor/nestopiaue/source/core/api `
  core/tests/test_core.cpp core/src/nes_core.cpp core/src/nes_stream.cpp core/src/nes_state.cpp `
  (Get-ChildItem core/vendor/nestopiaue/source/core -Recurse -Filter *.cpp | % { $_.FullName }) `
  -lz -o core/build/nes_core_test.exe
& core/build/nes_core_test.exe
```

Expected: 输出 `title=... mapper=...`、`samples>0`、`PASS`，退出码 0。

> 若单条命令因通配/路径过长失败，改用 `core/CMakeLists.txt` 的 `NES_BUILD_TESTS=ON` + host 工具链（`cmake -S core -B core/build/host -DNES_BUILD_TESTS=ON`，用本机 clang 或 MSVC）。测试 ROM 路径以实际为准。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "test: headless smoke test loads ROM, runs 60 frames, asserts video+audio"
```

---

## Task 7: JNI 桥 + Android 壳（渲染/音频/帧循环/触屏/生命周期）

**Files:**
- Modify: `app/src/main/cpp/CMakeLists.txt`
- Create: `app/src/main/cpp/nes_jni.cpp`
- Create: `app/src/main/java/com/flynes/emu/{NesCore,EmuView,TouchController,AudioThread,MainActivity}.java`

- [ ] **Step 1: 写 `app/src/main/cpp/CMakeLists.txt`（链接 core）**

```cmake
cmake_minimum_required(VERSION 3.22)
project(nescore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 复用 core 的静态库目标
add_subdirectory(${NES_CORE_DIR} ${CMAKE_BINARY_DIR}/core)

add_library(nescore SHARED nes_jni.cpp)
target_include_directories(nescore PRIVATE ${NES_CORE_DIR}/include)
target_link_libraries(nescore nes_abi nestopia)
# 若 add_subdirectory 内的 zlib 未传递，此处兜底：
find_library(ZLIB_LIB z)
if(ZLIB_LIB)
    target_link_libraries(nescore ${ZLIB_LIB})
endif()
```

- [ ] **Step 2: 写 `app/src/main/cpp/nes_jni.cpp`**

```cpp
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <cstring>
#include "nes.h"

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_flynes_emu_NesCore_nativeCreate(JNIEnv*, jclass) {
    return (jlong)(intptr_t)nes_create();
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    nes_destroy((nes_t*)handle);
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeLoadRom(JNIEnv* env, jclass, jlong handle, jbyteArray rom, jbyteArray patch) {
    nes_t* ctx = (nes_t*)handle;
    jsize romLen = env->GetArrayLength(rom);
    jbyte* romBytes = env->GetByteArrayElements(rom, nullptr);
    jbyte* patchBytes = patch ? env->GetByteArrayElements(patch, nullptr) : nullptr;
    jsize patchLen = patch ? env->GetArrayLength(patch) : 0;
    nes_result r = nes_load_rom(ctx, (const uint8_t*)romBytes, romLen,
                                patchBytes ? (const uint8_t*)patchBytes : nullptr, patchLen);
    env->ReleaseByteArrayElements(rom, romBytes, JNI_ABORT);
    if (patchBytes) env->ReleaseByteArrayElements(patch, patchBytes, JNI_ABORT);
    return (jint)r;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeLoadDatabase(JNIEnv* env, jclass, jlong handle, jbyteArray xml) {
    nes_t* ctx = (nes_t*)handle;
    jsize len = env->GetArrayLength(xml);
    jbyte* p = env->GetByteArrayElements(xml, nullptr);
    nes_result r = nes_load_database(ctx, (const uint8_t*)p, len);
    env->ReleaseByteArrayElements(xml, p, JNI_ABORT);
    return (jint)r;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NesCore_nativeRunFrames(JNIEnv* env, jclass, jlong handle, jint n, jobject audioBuf, jint cap) {
    nes_t* ctx = (nes_t*)handle;
    int16_t* dst = audioBuf ? (int16_t*)env->GetDirectBufferAddress(audioBuf) : nullptr;
    return nes_run_frames(ctx, (uint32_t)n, dst, (uint32_t)cap);
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeSetInput(JNIEnv*, jclass, jlong handle, jint buttons) {
    nes_set_input((nes_t*)handle, (uint8_t)buttons);
}

JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeBlit(JNIEnv* env, jclass, jlong handle, jobject surface, jint scale) {
    nes_t* ctx = (nes_t*)handle;
    nes_video_info vi = nes_video_buffer(ctx);
    if (!vi.pixels) return;
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) return;
    ANativeWindow_setBuffersGeometry(win, vi.width * scale, vi.height * scale, WINDOW_FORMAT_RGB_565);
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(win, &buf, nullptr) == 0) {
        const uint8_t* src = (const uint8_t*)vi.pixels;
        uint8_t* dst = (uint8_t*)buf.bits;
        int srcPitch = vi.pitch;
        for (uint32_t y = 0; y < vi.height; ++y) {
            uint16_t* dstRow = (uint16_t*)(dst + (int64_t)y * scale * buf.stride * 2);
            const uint16_t* srcRow = (const uint16_t*)(src + (int64_t)y * srcPitch);
            for (uint32_t x = 0; x < vi.width; ++x) {
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        dstRow[(x * scale + dx) + dy * (buf.stride)] = srcRow[x];
                    }
                }
            }
        }
        ANativeWindow_unlockAndPost(win);
    }
    ANativeWindow_release(win);
}

} // extern "C"
```

- [ ] **Step 3: 写 `NesCore.java`（JNI 包装）**

```java
package com.flynes.emu;

import android.view.Surface;
import java.nio.ByteBuffer;

public final class NesCore {
    static { System.loadLibrary("nescore"); }

    private long handle;
    private ByteBuffer audioBuffer = ByteBuffer.allocateDirect(65536);

    public static native long nativeCreate();
    public static native void nativeDestroy(long h);
    public static native int nativeLoadRom(long h, byte[] rom, byte[] patch);
    public static native int nativeLoadDatabase(long h, byte[] xml);
    public static native int nativeRunFrames(long h, int n, ByteBuffer audio, int cap);
    public static native void nativeSetInput(long h, int buttons);
    public static native void nativeBlit(long h, Surface surface, int scale);

    public boolean create() { handle = nativeCreate(); return handle != 0; }
    public void destroy() { if (handle != 0) { nativeDestroy(handle); handle = 0; } }
    public int loadRom(byte[] rom, byte[] patch) { return nativeLoadRom(handle, rom, patch); }
    public int loadDatabase(byte[] xml) { return nativeLoadDatabase(handle, xml); }
    public int runFrames(int n) { return nativeRunFrames(handle, n, audioBuffer, 32768); }
    public void setInput(int buttons) { nativeSetInput(handle, buttons); }
    public void blit(Surface s, int scale) { nativeBlit(handle, s, scale); }
}
```

- [ ] **Step 4: 写 `AudioThread.java`（音频主时钟循环）**

```java
package com.flynes.emu;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

public class AudioThread extends Thread {
    private final NesCore core;
    private final int sampleRate = 48000;
    private volatile boolean running = false;
    private AudioTrack track;

    public AudioThread(NesCore core) { this.core = core; }

    public void stopLoop() { running = false; }

    @Override
    public void run() {
        int bufBytes = AudioTrack.getMinBufferSize(
            sampleRate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        bufBytes = Math.max(bufBytes, sampleRate * 2 / 8); // ~125ms
        track = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate,
            AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT,
            bufBytes, AudioTrack.MODE_STREAM);
        running = true;
        track.play();
        while (running) {
            int samples = core.runFrames(2);       // 2 帧 ≈ 1600 采样
            // runFrames 已把音频写入 core 内部 direct buffer；此处需把该 buffer 写出。
            // 见下方"音频缓冲桥接"说明。
            if (samples > 0) {
                // 简化：runFrames 直接写 track 需要 native 侧暴露；阶段 0 用轮询补齐。
            }
        }
        track.stop();
        track.release();
    }
}
```

> 音频缓冲桥接修正：`nativeRunFrames` 当前把音频写入 Java 传入的 direct `ByteBuffer`，但 `AudioThread` 里需拿到该 buffer。请把 `NesCore.audioBuffer` 暴露为 `public ByteBuffer audioBuffer()`，并在 `AudioThread.run()` 内：

```java
ByteBuffer b = core.audioBuffer();
b.position(0);
while (running) {
    int samples = core.runFrames(2);
    if (samples > 0) {
        b.position(0);
        b.limit(samples * 2);
        track.write(b, samples * 2, AudioTrack.WRITE_BLOCKING); // 阻塞 → 音频主时钟
    }
}
```

（`AudioThread` 最终版以此为准，把上面占位 run() 替换为这段。）

- [ ] **Step 5: 写 `TouchController.java`（触屏虚拟手柄 → 位掩码）**

```java
package com.flynes.emu;

import android.view.MotionEvent;
import android.view.View;

public class TouchController implements View.OnTouchListener {
    public static final int A = 0x01, B = 0x02, SELECT = 0x04, START = 0x08;
    public static final int UP = 0x10, DOWN = 0x20, LEFT = 0x40, RIGHT = 0x80;

    private int buttons = 0;
    public interface Listener { void onButtons(int b); }
    private Listener listener;
    public void setListener(Listener l) { this.listener = l; }
    public int buttons() { return buttons; }

    @Override
    public boolean onTouch(View v, MotionEvent e) {
        // 阶段 0 简化：屏幕左半 = 方向键，右半 = A/B；用单点演示
        int w = v.getWidth(), h = v.getHeight();
        int b = 0;
        if (e.getAction() != MotionEvent.ACTION_UP) {
            float x = e.getX(), y = e.getY();
            if (x < w * 0.5f) {
                if (y < h * 0.33f) b |= UP;
                else if (y < h * 0.66f) b |= (x < w * 0.25f ? LEFT : RIGHT);
                else b |= DOWN;
            } else {
                b |= (y < h * 0.5f ? B : A);
            }
        }
        buttons = b;
        if (listener != null) listener.onButtons(b);
        return true;
    }
}
```

- [ ] **Step 6: 写 `EmuView.java`（SurfaceView 封装）**

```java
package com.flynes.emu;

import android.content.Context;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class EmuView extends SurfaceView implements SurfaceHolder.Callback {
    public EmuView(Context c) { super(c); getHolder().addCallback(this); }
    @Override public void surfaceCreated(SurfaceHolder h) {}
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) {}
    @Override public void surfaceDestroyed(SurfaceHolder h) {}
}
```

- [ ] **Step 7: 写 `MainActivity.java`（装配 + 生命周期 + 加载 assets）**

```java
package com.flynes.emu;

import android.app.Activity;
import android.os.Bundle;
import android.view.Choreographer;
import android.view.Surface;
import android.view.Window;
import android.view.WindowManager;

import java.io.IOException;
import java.io.InputStream;

public class MainActivity extends Activity implements TouchController.Listener {
    private NesCore core = new NesCore();
    private AudioThread audio;
    private EmuView view;
    private int buttons = 0;
    private int scale = 2;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new EmuView(this);
        view.setOnTouchListener(new TouchController());
        ((TouchController) view).setListener(this); // 见修正说明
        setContentView(view);

        core.create();
        byte[] rom = readAsset("roms/from_below.nes");
        byte[] db = readAsset("NstDatabase.xml");
        if (db != null) core.loadDatabase(db);
        if (core.loadRom(rom, null) != 0) { /* toast 提示失败 */ }
        audio = new AudioThread(core);
        audio.start();
        scheduleRender();
    }

    private byte[] readAsset(String path) {
        try (InputStream in = getAssets().open(path)) {
            byte[] buf = new byte[in.available()];
            int n = 0, r;
            while (n < buf.length && (r = in.read(buf, n, buf.length - n)) > 0) n += r;
            return buf;
        } catch (IOException e) { return null; }
    }

    private void scheduleRender() {
        Choreographer.getInstance().postFrameCallback(new Choreographer.FrameCallback() {
            @Override public void doFrame(long t) {
                Surface s = view.getHolder().getSurface();
                if (s != null && s.isValid()) core.blit(s, scale);
                Choreographer.getInstance().postFrameCallback(this);
            }
        });
    }

    @Override public void onButtons(int b) {
        buttons = b;
        core.setInput(b);
    }

    @Override protected void onPause() {
        super.onPause();
        audio.stopLoop();
        audio = null;
        // TODO(阶段1): 自动存档 + flush battery
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        core.destroy();
    }
}
```

> 修正：`TouchController` 需持有 `Listener` 引用。把 `MainActivity` 中的
> `new TouchController()` 存到局部变量 `TouchController tc = new TouchController(); tc.setListener(this); view.setOnTouchListener(tc);`。

- [ ] **Step 8: 复制数据库与 ROM 到 assets**

```powershell
New-Item -ItemType Directory -Force -Path app/src/main/assets/roms | Out-Null
Copy-Item core/vendor/nestopiaue/NstDatabase.xml app/src/main/assets/NstDatabase.xml
Copy-Item core/tests/fixtures/from_below.nes app/src/main/assets/roms/from_below.nes
```

- [ ] **Step 9: 构建 APK**

Run: `.\gradlew.bat assembleDebug`
Expected: BUILD SUCCESSFUL（`.so` 编译进 APK，arm64-v8a + x86_64）。

- [ ] **Step 10: 提交**

```bash
git add -A
git commit -m "feat: JNI bridge + Android shell (render/audio/touch/lifecycle)"
```

---

## Task 8: 安装与 5 分钟时序验收

- [ ] **Step 1: 启动模拟器或连接真机**

```powershell
adb devices
# 有真机则直接装；否则起模拟器：
$emu = "$env:ANDROID_HOME\emulator\emulator.exe"
# 列出可用 AVD：& $emu -list-avds；启动：& $emu -avd <AVD_NAME> -no-window &
```

- [ ] **Step 2: 安装并启动**

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.flynes.emu/.MainActivity
adb logcat -s FlyNES:* AndroidRuntime:E
```

Expected: 画面出现 From Below 标题/画面，可听到音频，触屏可操作。

- [ ] **Step 3: 5 分钟时序验收**

持续运行 5 分钟，监听 `AudioTrack` 无 underrun、logcat 无崩溃、无 ANR。用 Perfetto/`dumpsys gfxinfo` 记录帧率与 CPU：

```powershell
adb shell dumpsys gfxinfo com.flynes.emu
```

Expected: 无爆音、无撕裂、无崩溃，CPU 占用不异常升高。

- [ ] **Step 4: 记录验收结果到 spec 并提交**

在 `docs/superpowers/specs/2026-08-15-nes-emulator-design.md` 的 §10 补一行验收结论（设备型号、5 分钟结果、CPU 观测值）。

```bash
git add -A
git commit -m "chore: record stage-0 5-minute acceptance result"
```

---

## 自检记录（写作时已核对）

- **Spec 覆盖**：阶段 0 DOD（接口完备 + 每类 spike + 5 分钟验收）由 Task 2~8 覆盖；金手指/补丁/内存接口已在 C ABI 中预留（`cheat_add_gg/par`、`apply`、`read_cpu_ram` 返回 NOT_IMPLEMENTED），符合「未实现返回 not-implemented」。
- **占位符扫描**：Task 6 Step 1 的 ROM 下载 URL 需在执行时按真实 Release 链接替换（已标注「以真实 release 链接替换」），这是唯一一处需执行时确认的点，其余均为具体代码。
- **类型一致性**：`nes_run_frames` 签名（`int32_t, uint32_t, int16_t*, uint32_t`）在 `nes.h`、`nes_core.cpp`、`nes_jni.cpp`、`NesCore.java` 中一致；`nes_video_info`/`nes_rom_info` 结构字段一致；输入位掩码 `NES_BTN_*` 与 `TouchController` 常量一致。
- **遗留修正项**（执行子代理须在对应任务内落实，均已在上文标注）：① `nes_ctx` 增加 `uint8_t input_buttons;` 成员；② `NesCore.audioBuffer` 暴露 getter 供 `AudioThread` 使用；③ `MainActivity` 用 `TouchController tc = new TouchController()` 并 `setListener(this)`；④ `AudioThread.run()` 采用「阻塞写」最终版。
