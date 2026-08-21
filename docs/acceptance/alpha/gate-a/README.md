# Gate A 模拟器试玩验收

日期：2026-08-21  
设备：Android Emulator `MIT_Phone_API35` / API 35 / x86_64  
屏幕：2340 × 1080 横屏，右侧系统导航栏安全区 132px

## 自动化结果

- `:app:assembleDebug`：通过
- `:app:lintDebug`：通过
- `:app:testDebugUnitTest`：通过
- `:app:connectedDebugAndroidTest`：3/3 通过
- START 与 App 暂停职责分离：通过
- `ACTION_CANCEL`/reset 清空所有触控输入：通过
- A 键 0ms 极短点击保持至少 50ms：通过

## 实际试玩路径

1. 冷启动内置 From Below，确认 ROM 正常加载并持续渲染。
2. 点击 NES START 进入游戏设置页；未出现 App 暂停菜单。
3. 再次点击 START 进入实际关卡。
4. 操作摇杆向左、向右，方块位置正常变化。
5. 分别点击 A、B，方块按两种方向正常旋转。
6. 点击右上角独立暂停图标，出现 `Game paused` 对话框。
7. 点击 `Continue`，对话框消失，音频线程和画面恢复，游戏继续推进。

## 输入与安全区证据

实际 `logcat` 输入掩码：

```text
START  input=0x8 -> input=0x0
LEFT   input=0x40 -> input=0x0
RIGHT  input=0x80 -> input=0x0
A      input=0x1 -> input=0x0
B      input=0x2 -> input=0x0
```

UI hierarchy 显示：暂停按钮位于 `[2032,44][2164,176]`，系统导航栏位于
`[2208,0][2340,1080]`，两者无重叠。A/B 触控区由同一份互斥 hit map
生成，并保留 16dp 间隔。

## 截图

- `01-running.png`：冷启动标题页及重新布局后的虚拟手柄。
- `02-started.png`：START 正常进入游戏设置页，且未误触 App 暂停。

最终实际关卡、暂停和恢复状态同时通过 Windows Graphics Capture 直接观察；
模拟器窗口旋转时 `adb screencap` 偶发返回上一 Surface 帧，因此不把该异常帧作为验收图。
