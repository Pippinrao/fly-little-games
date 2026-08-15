# NestopiaUE 集成与功能面 — 内核技术评估（t2 / emulator-core）

> 结论基于对 NestopiaUE 上游 master（1.53.2）`source/core` 源码的逐文件核查（API 头文件、configure.ac、文件清单），非二手转述。

---

## 0. 先纠偏：路线中的 6 个技术事实错误（会直接影响方案）

这些不是风格问题，而是会写进 C ABI / CMake / 里程碑的硬事实，必须先对齐：

| # | 路线里的说法 | 源码事实 | 影响 |
|---|---|---|---|
| 1 | 「C++14」 | 当前 master 要求 **C++17**（`configure.ac: AX_CXX_COMPILE_STDCXX([17],...,[mandatory])`）。NDK27/28 的 clang 完全支持，无阻塞 | 描述改掉即可；**必须锁定一个具体 commit/版本**，因为上游在升标准 |
| 2 | 「IPS/BPS 补丁」 | 核心只内置 **IPS 与 UPS**（`NstPatcher.hpp`、`NstApiUser::PATCH_UPS/PATCH_IPS`）。**没有 BPS** | BPS 需引外部库（libbps/flips）或 BPS→IPS/UPS 转换，或从路线砍掉 |
| 3 | 阶段2「调试器」 | Nestopia core **没有调试器/单步/断点/trace API**；`Cheats::GetRam()` 只给 2KB CPU RAM | 「调试器」≠ 调内核 API，是自建反汇编+内存读写的一大块工程，或换 Mesen/FCEUX |
| 4 | 「load/run/...」C ABI | 内核**所有 IO 走 `std::istream/std::ostream`**（ROM、存档、FDS BIOS、补丁全吃 C++ 流） | C ABI 必须自建 `std::streambuf` 适配器（内存/文件/Java 侧 ByteBuffer） |
| 5 | 「电池存档 SRAM 读写」 | 电池存档**不是直接指针**，走 `NstApiUser::File` 的 `fileIoCallback`（`SAVE_BATTERY/LOAD_BATTERY/LOAD_EEPROM...`） | C ABI 要桥一个「内核反向回调 Java 取/存数据」的通道，方向与 load/run 相反 |
| 6 | 「FDS」列入功能面 | FDS 需要 `SetBIOS(std::istream*)`，**BIOS 是任天堂版权，不可内置**，且无广为认可的免费替代 | FDS 功能=做了也只能等用户自备 BIOS，MVP 阶段大概率空转 |

---

## 1. 按优先级排序的尖锐问题 / 决策点清单

### P0 — 架构级、不可逆或决定体验下限，必须先拷问

**Q1. 帧循环与时钟模型：音频做主时钟，还是视频 vsync 做主时钟？**
- **为什么重要**：NTSC 真机是 60.0988Hz，安卓屏是 60Hz vsync；两者不整除。用 vsync 驱动会相对真机越跑越偏（音频漂移/爆音、音乐变调）；用音频驱动则视频要丢/补帧。这是「时序」领域的核心，做错整条音视频管线都错，后面难返工。
- **选项与取舍**：① 音频为主时钟（AudioTrack 消费到阈值→跑够对应 cycle 数→丢最新一帧给屏）——音准最稳、延迟可控，代价是画面偶尔跳帧；② vsync 为主（每 vsync 跑一帧 NES 帧）——画面最稳，代价是 60.0988 vs 60 的 0.16% 漂移需周期性补/跳 + 音频要做重采样或缓冲伸缩；③ 混合：音频为主、Choreographer 只做展示。核心 API 已支持②：`Sound::Output.length[]` 每帧可给不同样本数、内部缓冲「多退少补」，天然适配音频主时钟。
- **推荐**：①音频为主时钟，视频只显示「最新完成的帧」。NES 是 2D 帧游戏，0.16% 的偶发跳帧不可感知，而爆音/变调立刻可感知。C ABI 里 `run(frame_count)` 应设计成「按 cycle/样本驱动」而非「按 vsync 驱动」。

**Q2. C ABI 的 IO 桥接：走 `std::istream` 适配，还是绕过流、改内存直读？**
- **为什么重要**：内核强制流接口（Q1 纠偏 #4）。这决定 C ABI 形状、JNI 拷贝次数、以及能否零拷贝。
- **选项与取舍**：① 自写 `std::streambuf` 子类包一块内存（ROM 已在内存里）→ 改动最小、不动上游，代价是 ABI 层要暴露 C++ 对象指针（`void* stream`），JNI 侧要小心生命周期；② fork 内核把 `Load(istream)` 改成 `Load(const void*, size)` → 干净但违背「少手搓、少改上游」，且每次上游更新要重打 patch；③ 两者：核心保留流，C ABI 只暴露 `(const uint8_t*, size_t)`，适配器藏在 C++ 壳里（**推荐**，Java 侧只看到字节数组，流适配是 C++ 内部实现细节）。
- **推荐**：③。C ABI 面用字节指针+长度，流适配器是 C++ 外壳的私有实现，这样 Java/JNI 无需知道 C++ 流。

**Q3. 视频输出格式与零拷贝路径：RGB565 / RGB888 / ARGB，要不要直接画进 ANativeWindow？**
- **为什么重要**：`Video::Output` 是「往你给的缓冲区里画」（`pixels`+`pitch`，pitch 可为负=自底向上），`SetRenderState` 可设 RGB 位掩码与 bpp。能不能零拷贝直接画进 ANativeWindow/纹理，直接决定 60fps 下的 CPU 占用与功耗（飞机场景=省电是硬指标）。
- **选项与取舍**：① 内核直接画进 `ANativeWindow` 的 buffer（零拷贝）——最快最省电，但要把 `pitch` 负值/行对齐/锁屏 buffer 语义摸对，且和渲染管线（GLSurfaceView 还是 SurfaceView）耦合；② 内核画进一块私有 RGB565/RGBA 缓冲，Java 侧上纹理——简单、解耦，多一次 memcpy（256×240×2B≈123KB/帧，很小，实际可忽略）。③ 关键其实是**色彩格式选 RGB565 还是 RGBA8888**：NES 只有 64 色，RGB565 在真彩上够用且省一半带宽，但滤镜/整数缩放时 RGB888 更平滑。
- **推荐**：先②（私有缓冲 + 上纹理，格式 RGB565 起步），把 `pitch`/负值/对齐这些坑在 C 层隔离；等 mobile-perf 定了渲染管线再升级到①零拷贝。不要让阶段0就绑死零拷贝。

**Q4. 音频缓冲：目标延迟多少 ms？缓冲策略是固定块还是变长帧样本？**
- **为什么重要**：飞机上玩对输入延迟不敏感，但「爆音」零容忍。缓冲太小爆音、太大延迟。这是音频管线唯一要提前钉死、否则返工的点。
- **选项与取舍**：① AudioTrack 大缓冲（100–200ms）+ 内核每帧变长样本（`length[]` 让内核自己凑）——最稳、几乎不爆音，延迟对单机 NES 无感；② Oboe 低延迟小缓冲（<40ms）——手感最好但要做精确的 sample 计数与 underrun 补偿，复杂度高、收益在 NES 上几乎为零。采样率：内核支持 44100–96000，安卓设备原生 48000 更常见；`SetSampleRate(48000)` 避免系统重采样。
- **推荐**：① AudioTrack + 48000Hz + 单声道（NES 本来就是 mono，`SPEAKER_MONO` 默认），缓冲 80–150ms。Oboe 放进「以后要手感再说」而非 MVP。**C ABI 的 `run()` 返回本帧产出样本数，音频线程据此驱动**。

**Q5. 即时存档（NST）格式：锁定核心版本换兼容性，还是接受跨版本作废？**
- **为什么重要**：Nestopia 的 `.nst` 是内部版本绑定的二进制（load 时带 CRC 校验，`QUESTION_NST_PRG_CRC_FAIL_CONTINUE` 就是证据），**不保证跨 Nestopia 版本兼容，更不能跨模拟器互用**。飞机场景存档很关键，若将来升级内核导致用户旧存档读不了=事故。
- **选项与取舍**：① 锁定内核 commit，存「核心版本号」进存档元数据，读前校验、不匹配则拒绝并提示（**推荐**）——诚实且安全；② 承诺跨版本兼容——做不到，除非自研格式；③ 用上游 Rewinder 内存快照做「轻存档」、NST 做「重存档」——多一套机制。
- **推荐**：①。把「NST 版本号」写进 C ABI 的 `save_state` 头里，这是「可扩展基座」里最该预留的字段。

**Q6. 电池存档（SRAM）落盘语义：什么时候写盘？脏标记+延迟写吗？**
- **为什么重要**：电池存档走 `fileIoCallback` 的 `SAVE_BATTERY`，是内核**反向回调**拿数据（`GetContent(stream)` 或 `GetRawStorage()`）。飞机上随时可能息屏/切 app/掉电，写盘时机直接决定丢不丢进度。
- **选项与取舍**：① 每次 `SAVE_BATTERY` 回调都立即写盘——最安全但某些游戏高频触发会写放大；② 回调时只取数据、置脏标记，固定间隔（如 1s）或 onPause/onStop 时落盘——省 IO 但掉电可能丢最后一秒（**推荐**，叠加 onPause 强制 flush）；③ `GetRawStorage()` 拿到 SRAM 的持久指针，直接 mmap 到文件——最省但依赖上游实现细节。
- **推荐**：②，且 C ABI 显式暴露一个 `flush_battery()` 供 onPause 调用。这是「改存档」功能的基础设施（同一个回调通道）。

**Q7. FDS 与扩展 mapper 是否进 MVP？BIOS 从哪来？**
- **为什么重要**：FDS 无内置 BIOS 就完全不能玩（纠偏 #6），mapper 覆盖则决定「兼容性」卖点与测试成本。
- **选项与取舍**：① FDS 进 MVP 但只做「有 BIOS 才启用」的骨架——成本中等、几乎无收益；② FDS 明确延后到阶段2，阶段0 只保证 NES/FC 卡带 + 主流 mapper——聚焦（**推荐**）；mapper 覆盖：Nestopia 有约 290 个 board 实现（`board/` 目录 205 个 .cpp），覆盖几乎所有授权 mapper + 大量盗版/合卡/自制 mapper，**无需自选，全部编进来即可**，代价只是 .so 体积（几十~百 KB 级，可忽略）。
- **推荐**：②。FDS 延后；mapper 全量编译，不搞「挑 mapper」。

### P1 — 体验级，阶段1 前需定

**Q8. 金手指范围：只 Game Genie / Pro Action Rocky，还是扩展？**
- **为什么重要**：内核已原生提供 GG 与 PAR 的编解码静态函数（`GameGenieEncode/Decode`、`ProActionRockyEncode/Decode`），`Code{address,value,compare,useCompare}` 模型也够。路线阶段2 的「作弊码增强」要定义清楚「增强」指什么——如果只是 GG/PAR，阶段1 就做完了。
- **推荐**：阶段1 直接交付 GG+PAR（近乎零成本），阶段2 的「增强」明确为「自定义地址/数值/比较值 + 搜索式内存修改」，别再发明新金手指格式。

**Q9. NSF / VS System / PlayChoice-10 / Dendy / PAL 要不要做？**
- **为什么重要**：内核都支持（`LoadSound` 加载 NSF、`FavoredSystem` 含 Dendy、`Mode` NTSC/PAL、`NstApiMachine::Is(VS/PC10)`），但每一项都有 UI/测试成本，尤其 NSF 是「纯听」的另一个交互模式。
- **推荐**：PAL/Dendy 属于「加载 ROM 时自动判定」，零成本保留；VS/PC10 属冷门、默认不支持不宣传；NSF 明确问用户——飞机上「听 NSF」是真实场景，但建议阶段2 再说。

**Q10. 内置 homebrew 示例的选型，以及是否随包分发 NstDatabase.xml？**
- **为什么重要**：① homebrew 示例要「能体现功能面」（电池存档、扩展音频、mapper、多控制器）且体积小、允许再分发（合规是 t5 的活，但选型会影响内核测试面）；② `NstDatabase.xml` 是内核的「坏 dump 纠错库」，不装它部分游戏 mirroring/board 判错，装了是 GPLv2 资源。
- **推荐**：选型与 roadmap/compliance 协同；NstDatabase.xml **必须随包分发**（否则兼容性回退），合规上它是 GPLv2 的一部分、与内核一体开源，无额外义务。

### P2 — 魔改期，需提前排雷

**Q11. 「调试器」的真实范围：自建还是换核？**
- **为什么重要**：纠偏 #3。Nestopia 无调试 API，`GetRam()` 只有 2KB。要做「调试器」= 自建 6502 反汇编 + 断点（改 PC 或轮询）+ 内存/寄存器/PPU 观测，这是独立于「基座」的一大块工程。
- **选项与取舍**：① 砍掉调试器，只留「内存查看/编辑」+「金手指搜索」——够用且诚实；② 真做调试器——建议换/嵌 Mesen 或 FCEUX 的调试内核，而不是在 Nestopia 上补；③ 只做「CPU RAM + 有限地址空间」的只读查看。
- **推荐**：①。路线阶段2 的「调试器」建议降级为「内存查看/编辑 + 金手指搜索」，若要真调试器，单独立项换核。

**Q12. BPS 补丁：引入外部库还是砍掉？**
- **为什么重要**：纠偏 #2。BPS 是现在 homebrew 社区的主流补丁格式（比 IPS/UPS 严格），但内核不原生支持。
- **选项与取舍**：① 引 libbps（BSD 类许可）在 C 壳里做 BPS→ROM 字节，再喂给内核——成本中等、价值高（homebrew 分发场景刚需）；② 只支持 IPS/UPS，BPS 让用户自己转换——零成本但体验割裂。
- **推荐**：①，但排到阶段1 之后（先 IPS/UPS 打通，BPS 作为增量）。注意这是「外部库许可」问题，交合规核。

### P3 — 低成本高价值，别漏

**Q13. Rewinder（即时回退）与扩展音频/伪立体声要不要暴露？**
- **为什么重要**：`NstApiRewinder` 是上游白送的内存快照回退，价值高、成本低；`SPEAKER_STEREO` 伪立体声与 VRC6/VRC7/N163/FDS 扩展音频也都在内核里（`Sound::Channel` 全枚举）。
- **推荐**：Rewinder 进阶段1 末尾或阶段2 头部（近乎零成本的高感知功能）；扩展音频通道默认开（影响 homebrew 音乐游戏），伪立体声做成设置项。

---

## 2. 该领域主要风险与修正建议

1. **【最高】帧循环时序做错 → 爆音/变调/抖动**。修正：阶段0 就写死「音频主时钟」模型（Q1），并把「连续播放 10 分钟无爆音、无漂移」设为阶段0 的验收硬指标，而不是「能出声就行」。
2. **【高】自写 CMake 文件清单维护成本**。上游是 autotools + VS 工程，无 CMake；core 共 **299 个 .cpp**（board 205 + core 49 + api 16 + input 25 + vssystem 4）。修正：用 `file(GLOB)` 或一次性生成的源清单，并写一个「清单完整性」检查；别手抄 299 个文件名。
3. **【高】NST 存档版本绑定 → 升级内核丢用户存档**。修正：存档头写核心版本号，读前校验（Q5）；任何「升级 NestopiaUE」都作为破坏性变更对待。
4. **【高】电池存档丢档**。修正：onPause/onStop 强制 `flush_battery()`（Q6），存档写完再进后台。
5. **【中】「可扩展基座」把 C ABI 抽象过头 → 每加一个功能都要双向透传、ABI 频繁变动**。修正：C ABI 用「不透明句柄 + 版本化结构体 + 能力查询」一次成型；不要一开始就为「未来的调试器/魔改」预留一堆空槽，按需增补并 bump 版本。
6. **【中】高精度 vs 省电的张力**：Nestopia 是逐周期精确模拟，若空跑满速会吃满 CPU 烧电。修正：帧循环里「产出即停」，空闲时内核线程阻塞在音频/帧信号上，不忙等（与 mobile-perf 协同，但 C ABI 的 `run()` 必须支持「跑 N 帧就返回」而非「无限循环」）。
7. **【中】`wchar_t` 编码边界**：API 里游戏标题/文件名/board 名是 `std::wstring`/`wchar_t*`，Android/Linux 上 wchar_t=4 字节（UTF-32）、Windows=2 字节（UTF-16）。修正：C ABI 的「ROM 头信息」一律转成 UTF-8 字节输出到 Java，不要暴露 wchar_t。
8. **【低】Bad dump / unknown mapper 的错误处理**：加载失败要有明确的 Result 码映射到用户可读提示（`RESULT_ERR_UNSUPPORTED_MAPPER` 等），而不是「黑屏」。
9. **【低】NSF / FDS / VS 等「另一个时钟域」**：NSF 的时钟与卡带不同，若做 NSF 会踩「帧循环假设一帧=一屏」的坑；建议 NSF 延后，避免污染阶段0 的帧循环。

---

## 3. 路线中遗漏的东西（内核视角）

1. **没有「帧循环/时钟模型」这个里程碑**。它是阶段0 真正最难、最该显式立项的模块，却只写成「load ROM→渲染→触屏→声音→存档」。
2. **没有「存档格式版本与迁移策略」**（NST 版本、电池存档文件命名/索引，按 SHA1 key 化）。
3. **没有「输入映射层」细节**：触屏→NES 手柄位掩码、多控制器（`Input::Controllers` 有 4 口 + Zapper/等扩展）、外部手柄、连发 turbo 的归属（阶段1 才提外部手柄，但触屏虚拟键是阶段0）。
4. **没有「暂停/后台自动暂停/热切换 ROM」**：飞机场景切 app/息屏必须有，且要触发 `flush_battery` + 停止音频（省电）。
5. **没有「加载失败与兼容性报告」**：bad dump、unknown mapper、FDS 缺 BIOS、CRC 不匹配的用户提示。
6. **没有「无爆音/无漂移」的验收标准**（这是内核集成质量的唯一可量化硬指标）。
7. **没有「核心升级策略」**：NestopiaUE 上游更新怎么回灌、怎么回归（mapper 回归集）。
8. **没有「性能基准与最低目标机型」**：逐周期模拟在最老支持的 SoC 上能否稳 60fps，决定 minSdk/ABI 下限。
9. **漏了 `NstApiRewinder`（回退）**：近乎零成本的高价值功能，路线完全没提。
10. **漏了「扩展音频通道默认策略」**：VRC6/VRC7/N163 等是否默认开（影响 homebrew 音乐），没有决策点。
