# FlyNES 游戏库（ROM 管理器）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement task-by-task. Steps use `- [ ]` syntax.

**Goal:** 给 FlyNES 加一个「游戏库」：从手机存储（SAF）扫描 .nes/.zip，支持**搜索**、**按受欢迎程度/名称/大小排序**、点击即玩。内置的 CC0/MIT homebrew（From Below）同样出现在库里。App 不内置、不下载商业 ROM——只加载用户自己设备上的文件（个人使用）。

**Architecture:** 纯 Android 壳层功能，不改 core（C ABI 已是最终面）。数据层（扫描/解析/持久化/热度）与 UI 层（列表/搜索/排序/启动）分离。ROM 经 SAF 树选择后 URI 持久化（takePersistableUriPermission），.zip 在 Java 侧解出 .nes 缓存到私有目录，加载走既有的 `NesCore.loadRom(byte[])`。

**Tech Stack:** Java 17 / SAF (ACTION_OPEN_DOCUMENT_TREE + DocumentFile) / java.util.zip / SharedPreferences(JSON) / ListView 或 RecyclerView（选更简单的 ListView + ArrayAdapter，避免引入依赖）。

**依据:** 本会话阶段 1 待办「ROM 管理 UX」；spec §9.1 阶段 1；合规基线（不内置商业 ROM、库内展示"仅加载你合法拥有的 ROM"提示）。

---

## 功能需求

1. **入口**：主界面（MainActivity）加"游戏库"按钮 → 打开 GameLibraryActivity。
2. **扫描**：首次点"选择 ROM 目录"→ SAF 目录选择 → 扫描其中 .nes/.zip（含子目录，限深 3 层防卡）→ 解析 iNES 头（标题优先文件名，中文名直接可用；PRG/CHR/mapper/镜像用于展示）→ 条目持久化（URI + 元数据存 SharedPreferences JSON），下次启动免重扫。
3. **内置游戏**：assets 里的 From Below 也作为一条固定条目出现在库顶（标注"内置"）。
4. **搜索**：顶部 EditText，按名称模糊过滤（不区分大小写）。
5. **排序**：三种——受欢迎程度（默认）/ 名称 / 大小。受欢迎程度 = 内置「经典 NES 游戏热度榜」（约 80 个知名游戏的中英文名关键词 → 分数），按文件名匹配打分，未命中得 0 分（排最后，按名称兜底）。
6. **启动**：点击条目 → 若 .zip 则先解出 .nes 缓存到 `filesDir/roms_cache/` → 读字节 → `NesCore.loadRom(bytes, null)`（rc<0 提示失败）→ 返回主界面并开始游玩（复用现有 AudioThread/渲染）。
7. **合规提示**：库底部常驻一行"仅加载你合法拥有的 ROM"。
8. **移除目录**：库内可"移除当前目录"（清空其条目，可选——不做也行，保持 YAGNI，仅做清空全部）。

---

## 文件结构

```
app/src/main/java/com/flynes/emu/
  GameEntry.java            # 模型：name, uri, source(assets|saf), size, mapper, prg, chr, popularity
  RomScanner.java           # SAF 扫描 + iNES 头解析 + zip 探测（不真正解压到扫描阶段）
  Popularity.java           # 热度榜：关键词→分数，match(filename)
  RomStore.java             # SharedPreferences JSON 持久化（条目列表 + 已授权目录 URI）
  RomLoader.java            # 读条目 → byte[]（assets / SAF / zip 解压缓存）
  GameLibraryActivity.java  # 列表 + 搜索框 + 排序切换 + 启动
  MainActivity.java         # 改：加"游戏库"按钮；loadRomFromBytes() 复用；assets 启动逻辑保留
app/src/main/AndroidManifest.xml   # +GameLibraryActivity 注册
```

---

## Task G1: 数据层（GameEntry / Popularity / RomScanner / RomStore / RomLoader）

**Files:**
- Create: `GameEntry.java`, `Popularity.java`, `RomScanner.java`, `RomStore.java`, `RomLoader.java`

- [ ] **Step 1: `GameEntry.java`** — 纯数据类：`String name; String uri; String source; long size; int mapper, prgKb, chrKb; boolean zipped; int popularity;` + 空构造 + 全字段构造 + getters/setters（或 public 字段，按项目现有风格）。

- [ ] **Step 2: `Popularity.java`** — 内置热度榜。要求：**至少 80 个知名 NES/FC 游戏**，每项 = 一组匹配关键词（中英文/别名）+ 分数（100 递减）。示例条目必须含：超级马里奥/Super Mario、魂斗罗/Contra、冒险岛/Adventure Island、坦克大战/Battle City、忍者神龟/TMNT、恶魔城/Castlevania、沙罗曼蛇/Salamander、赤色要塞/Jackal、松鼠大战/Chip 'n Dale、双截龙/Double Dragon、热血系列（热血物语/格斗/篮球…）、洛克人/Mega Man、塞尔达/Zelda、银河战士/Metroid、俄罗斯方块/Tetris、泡泡龙/Bubble Bobble、大金刚/Donkey Kong、吃豆人/Pac-Man、绿色兵团/Rush'n Attack、兵蜂/TwinBee、宇宙巡航机/Gradius、马戏团/Circus Charlie、敲冰块/Ice Climber、气球大战/Balloon Fight、越野机车/Excitebike、影子传说/The Legend of Kage、西游记/西游记、七宝奇谋、忍者龙剑传/Ninja Gaiden、火焰之纹章/Fire Emblem、勇者斗恶龙/Dragon Quest、最终幻想/Final Fantasy、星之卡比（非 FC 但可留）、高桥名人的冒险岛 2/3/4 等。方法：`static int score(String filename)` — 转小写，遍历条目若文件名包含任一词 → 返回该条分数；无命中返回 0。分数设计：前 20 名 100~90，之后递减，最低 10。写清楚来源注释"游戏名元数据，非版权内容"。

- [ ] **Step 3: `RomScanner.java`** —
  - `static List<GameEntry> scanTree(Context, Uri treeUri, int maxDepth)`：用 `DocumentsContract.buildChildDocumentsUriUsingTree`/DocumentFile 递归；过滤 `.nes`/`.zip` 后缀（小写）；对 .nes 直接 `readFully` 前 16 字节解析 iNES 头（magic "NES\x1A"、prg=头[4]、chr=头[5]、mapper=头[6]>>4 | 头[7]&0xF0、mirroring=头[6]&1）；名字 = 文件名去扩展（保留中文）。
  - zip 探测：`.zip` 条目不读内容（扫描阶段不解压），仅记 `zipped=true`（文件名去 .zip）。
  - 深度限制 3，超限跳过；单文件 > 4MB 跳过（NES ROM 不会这么大，防误扫）。
  - `static byte[] readFully(ContentResolver, Uri)` 供 RomLoader 复用。

- [ ] **Step 4: `RomStore.java`** —
  - SharedPreferences 存：`game_list_json`（Gson 不可用则手写简单 JSON 或 `ObjectOutputStream` 到私有文件——**选最简单的**：用 `SharedPreferences` + `org.json.JSONArray`（Android 自带）序列化 GameEntry 列表）、`tree_uri`。
  - `saveTreeUri(Uri)` / `getTreeUri()` / `saveGames(List<GameEntry>)` / `loadGames()`（无则空表）。
  - 用 `JSONObject/JSONArray` 手写序列化（项目零依赖原则）。

- [ ] **Step 5: `RomLoader.java`** —
  - `static byte[] load(Context, GameEntry, ContentResolver)`：`source=assets` → assets.open(路径)；`source=saf` + zipped → 用 ZipInputStream 打开 SAF Uri 输入流，找第一个扩展名为 .nes 的 entry（或名为"*.nes"），解出字节返回；非 zip → readFully。
  - zip 解出后**不**缓存（每次点开解一次，2400 个也才几百 KB，简化；注释说明）。
  - 返回 null 表示失败。

- [ ] **Step 6: 编译验证**：`.\gradlew.bat assembleDebug` BUILD SUCCESSFUL（先放一个空的 GameLibraryActivity 占位，Task G2 填充，否则清单引用报错——或者本任务先不碰清单，G2 一起做）。

## Task G2: UI 层（GameLibraryActivity + MainActivity 集成 + Manifest）

**Files:**
- Create: `GameLibraryActivity.java`
- Modify: `app/src/main/AndroidManifest.xml`, `MainActivity.java`, `res/values/strings.xml`（若需）

- [ ] **Step 1: `GameLibraryActivity.java`** —
  - 布局（代码构建，避免 XML 文件过多）：顶部 EditText（搜索）+ Spinner/两个按钮（排序：热度/名称/大小）+ ListView。
  - 数据：`RomStore.loadGames()` + 内置 From Below 条目（hardcode：name="From Below (内置)", uri="file:///android_asset/roms/from_below.nes", source=assets）。
  - 搜索：EditText TextWatcher → 过滤（名称 contains，忽略大小写）。
  - 排序：热度（popularity desc, name asc）/ 名称（name 拼音序近似：直接 String.compareTo，中文按 Unicode，可接受）/ 大小（size desc）。
  - 首次无数据：显示"点击选择 ROM 目录"按钮 → `ACTION_OPEN_DOCUMENT_TREE` → onActivityResult 拿 treeUri → `takePersistableUriPermission` → RomScanner.scanTree（后台线程 + ProgressDialog 或简单 Toast"扫描中…"）→ RomStore.save → 刷新列表。
  - 点击条目 → 后台线程 RomLoader.load → 成功则 `Intent` 回传 byte[]？**不行，byte[] 不能过 Intent**。改：把加载结果写临时文件 `filesDir/current_rom.nes`，Intent 带 extra `"rom_file"` 路径 → MainActivity 读文件 → NesCore.loadRom。或者更简单：MainActivity 持有一个 `static volatile byte[] sPendingRom`（进程内传递，简单可靠，注释说明）。**选后者**（单 Activity 进程内，最简）。
  - 失败 → Toast。
  - 底部合规提示 TextView："仅加载你合法拥有的 ROM"。
  - 列表项显示：名称 + 副行（mapper/PRG/大小 + "内置"徽标）。

- [ ] **Step 2: `MainActivity.java` 集成** —
  - onCreate 顶栏加"游戏库"按钮（复用 ℹ️ 的 FrameLayout 方案，加一个 📚 在左上）。
  - 点击 → `startActivity(GameLibraryActivity)`。
  - onCreate/onResume：检查 `NesCore.sPendingRom`（静态）非空 → 用它替换 assets ROM 加载（读文件→loadRom→clear）。
  - 提取 `loadRom(byte[] rom)` 私有方法（原 assets 逻辑复用）。

- [ ] **Step 3: Manifest** — 注册 `GameLibraryActivity`（exported=false）。strings.xml 可加游戏库文案（或用字面量）。

## Task G3: 验证与验收

- [ ] **Step 1: 构建**：`.\gradlew.bat assembleDebug` BUILD SUCCESSFUL。
- [ ] **Step 2: 模拟器验收**（MIT_Phone_API35）：
  - `adb push core/tests/fixtures/from_below.nes /sdcard/ROMs/`（造两个合法测试文件：from_below.nes + 把 from_below.nes 压成 zip 放 ROMs/zips/ 测 zip 路径——用 PowerShell `Compress-Archive` 造 zip）。
  - 启动 App → 游戏库 → 选 /sdcard/ROMs → 扫描出 2 条（1 nes + 1 zip）→ 搜索过滤生效 → 排序切换不崩 → 点击 .nes 条目 → 回到主界面开始玩（logcat 有 ROM loaded rc=0）→ 再点 zip 条目 → 同样能玩（zip 解压路径）。
  - 记录结果（logcat 摘要 + 截图可选）。
- [ ] **Step 3: 提交**：`git add -A && git commit -m "feat: game library (SAF scan, search, popularity/name/size sort, zip support)"`

---

## 自检要点

- **合规**：App 不内置/不下载任何商业 ROM；库内提示文案在；扫描的是用户自己设备上的文件。
- **零新依赖**：只用 Android SDK（org.json、DocumentFile、ZipInputStream）——不加 Gson/RecyclerView 等。
- **主线程**：扫描/解压在后台线程，UI 更新回主线程。
- **不碰 core/**：纯壳层功能。
