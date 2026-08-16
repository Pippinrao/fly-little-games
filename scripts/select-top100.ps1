# FlyNES: 从 FC ROM 合集挑选热度前 N 的游戏（复制到手机用）
# 用法: powershell -ExecutionPolicy Bypass -File scripts/select-top100.ps1 [-Source D:\FCgames] [-Count 100] [-OutDir D:\FCgames_top100]
# 说明: 只处理你自己设备上的文件（个人使用传输）。App 不内置任何商业 ROM。
param(
    [string]$Source = "D:\FCgames",
    [int]$Count = 100,
    [string]$OutDir = "D:\FCgames_top100"
)

# --- 热度榜（关键词 -> 分数；与 app/src/main/java/com/flynes/emu/Popularity.java 同源，子串匹配）---
$Rank = @(
    @{kw=@("超级马里奥","超级玛丽","super mario","mario"); s=100},
    @{kw=@("魂斗罗","contra"); s=99},
    @{kw=@("坦克大战","battle city","tank"); s=98},
    @{kw=@("冒险岛","adventure island","高桥名人"); s=97},
    @{kw=@("忍者神龟","teenage mutant ninja turtles","tmnt"); s=96},
    @{kw=@("恶魔城","castlevania"); s=95},
    @{kw=@("沙罗曼蛇","salamander"); s=94},
    @{kw=@("赤色要塞","jackal","top gunner"); s=93},
    @{kw=@("松鼠大战","chip 'n dale","chip n dale"); s=92},
    @{kw=@("双截龙","double dragon"); s=91},
    @{kw=@("洛克人","mega man","rockman"); s=90},
    @{kw=@("热血物语","热血格斗","热血篮球","热血足球","热血硬派","热血新纪录","nekketsu"); s=89},
    @{kw=@("塞尔达","zelda"); s=88},
    @{kw=@("银河战士","metroid"); s=87},
    @{kw=@("俄罗斯方块","tetris"); s=86},
    @{kw=@("泡泡龙","bubble bobble"); s=85},
    @{kw=@("大金刚","donkey kong"); s=84},
    @{kw=@("吃豆人","pac-man","pacman"); s=83},
    @{kw=@("绿色兵团","rush'n attack","rush n attack"); s=82},
    @{kw=@("兵蜂","twinbee","twin bee"); s=81},
    @{kw=@("宇宙巡航机","gradius"); s=80},
    @{kw=@("马戏团","circus charlie"); s=79},
    @{kw=@("敲冰块","ice climber"); s=78},
    @{kw=@("气球大战","balloon fight"); s=77},
    @{kw=@("越野机车","excitebike"); s=76},
    @{kw=@("影子传说","legend of kage"); s=75},
    @{kw=@("忍者龙剑传","ninja gaiden"); s=74},
    @{kw=@("勇者斗恶龙","dragon quest"); s=73},
    @{kw=@("最终幻想","final fantasy"); s=72},
    @{kw=@("火焰之纹章","fire emblem"); s=71},
    @{kw=@("西游记","monkey king"); s=70},
    @{kw=@("七宝奇谋","goonies"); s=69},
    @{kw=@("赤影战士","kage"); s=68},
    @{kw=@("忍者猫","ninja cat"); s=67},
    @{kw=@("兔宝宝","bunny"); s=66},
    @{kw=@("米老鼠","mickey mouse"); s=65},
    @{kw=@("唐老鸭","donald duck"); s=64},
    @{kw=@("小美人鱼","little mermaid"); s=63},
    @{kw=@("小蜜蜂","galaga"); s=62},
    @{kw=@("太空侵略者","space invaders"); s=61},
    @{kw=@("敲砖块","arkanoid"); s=60},
    @{kw=@("打砖块"); s=60},
    @{kw=@("古巴战士","guerrilla war"); s=59},
    @{kw=@("中东战争","p.o.w"); s=58},
    @{kw=@("怒","ikari"); s=57},
    @{kw=@("脱狱","prisoner"); s=56},
    @{kw=@("成龙之龙","jackie chan"); s=55},
    @{kw=@("功夫","kung fu"); s=54},
    @{kw=@("成龙"); s=55},
    @{kw=@("荒野大镖客","gun.smoke","gunsmoke"); s=53},
    @{kw=@("魂斗罗2","super contra"); s=52},
    @{kw=@("忍者蛙","battletoads"); s=51},
    @{kw=@("快打旋风","final fight"); s=50},
    @{kw=@("街头霸王","street fighter"); s=49},
    @{kw=@("拳皇","king of fighters"); s=48},
    @{kw=@("侍魂","samurai"); s=47},
    @{kw=@("前线大作战","front line"); s=46},
    @{kw=@("超级魂斗罗"); s=52},
    @{kw=@("赤影战士"); s=68},
    @{kw=@("机甲战龙","power blade"); s=45},
    @{kw=@("上尉命令","captain commando"); s=44},
    @{kw=@("名将"); s=44},
    @{kw=@("恐龙快打","cadillacs"); s=43},
    @{kw=@("圆桌骑士","knights of the round"); s=42},
    @{kw=@("三国志","romance of the three kingdoms"); s=41},
    @{kw=@("吞食天地","destiny of an emperor"); s=40},
    @{kw=@("龙珠","dragon ball"); s=39},
    @{kw=@("圣斗士","saint seiya"); s=38},
    @{kw=@("北斗神拳","fist of the north star","hokuto"); s=37},
    @{kw=@("幽游白书","yu yu hakusho"); s=36},
    @{kw=@("阿拉丁","aladdin"); s=35},
    @{kw=@("狮子王","lion king"); s=34},
    @{kw=@("美女与野兽","beauty and the beast"); s=33},
    @{kw=@("蝙蝠侠","batman"); s=32},
    @{kw=@("超人","superman"); s=31},
    @{kw=@("蜘蛛侠","spider-man","spiderman"); s=30},
    @{kw=@("机器人战争"); s=29},
    @{kw=@("中华大仙","super chinese"); s=29},
    @{kw=@("天才厨师","cook"); s=28},
    @{kw=@("运动会","track & field"); s=28},
    @{kw=@("棒球","baseball"); s=27},
    @{kw=@("网球","tennis"); s=26},
    @{kw=@("足球","soccer"); s=25},
    @{kw=@("篮球","basketball"); s=24},
    @{kw=@("高尔夫","golf"); s=23},
    @{kw=@("弹球","pinball"); s=22},
    @{kw=@("赛车","racing"); s=21},
    @{kw=@("老虎机","slot"); s=20},
    @{kw=@("麻将","mahjong"); s=19},
    @{kw=@("围棋","go "); s=18},
    @{kw=@("象棋","chess"); s=17},
    @{kw=@("军棋"); s=16},
    @{kw=@("学习机","study"); s=15},
    @{kw=@("算数","math"); s=14},
    @{kw=@("拼图","puzzle"); s=13},
    @{kw=@("猜谜","quiz"); s=12},
    @{kw=@("射击"); s=11},
    @{kw=@("platform","action"); s=10}
)

function Get-Score([string]$name) {
    $n = $name.ToLowerInvariant()
    foreach ($e in $Rank) {
        foreach ($k in $e.kw) {
            if ($n.Contains($k.ToLowerInvariant())) { return $e.s }
        }
    }
    return 0
}

# --- 扫描 ---
if (-not (Test-Path $Source)) { Write-Error "源目录不存在: $Source"; exit 1 }
$roms = Get-ChildItem $Source -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @('.zip','.nes') -and $_.Length -le 4MB }

Write-Host "扫描到 $($roms.Count) 个候选 ROM 文件"

$scored = foreach ($f in $roms) {
    $base = $f.Name -replace '\.(zip|nes)$',''
    [pscustomobject]@{ Name = $f.Name; Base = $base; Path = $f.FullName; SizeMB = [math]::Round($f.Length/1MB,1); Score = Get-Score $base }
}

$top = $scored | Sort-Object @{Expression='Score'; Descending=$true}, Base | Select-Object -First $Count

# --- 复制到输出目录 ---
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$copied = 0
foreach ($t in $top) {
    Copy-Item $t.Path -Destination (Join-Path $OutDir $t.Name) -Force
    $copied++
}
$manifest = Join-Path $OutDir "manifest.csv"
$top | ForEach-Object { "{0},{1},{2}MB,score={3}" -f $_.Name,$_.Base,$_.SizeMB,$_.Score } | Set-Content $manifest -Encoding UTF8

Write-Host "已复制 $copied 个游戏到 $OutDir"
Write-Host "--- Top 10 ---"
$top | Select-Object -First 10 Base, Score, SizeMB | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "清单: $manifest"
Write-Host "下一步: 手机连接后 `adb push $OutDir /sdcard/ROMs/`（或手动拷贝该目录到手机存储），然后在 App 游戏库里选择该目录。"
