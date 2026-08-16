# FlyNES: 挑选热度前 N 的【不同】游戏，重命名为 ASCII 安全名，供推送到手机
# 用法: powershell -ExecutionPolicy Bypass -File scripts/prepare-top100-for-phone.ps1 [-Source D:\FCgames] [-Count 100] [-OutDir D:\FCgames_phone]
# 说明: 只处理你自己设备上的文件（个人使用传输）。App 不内置任何商业 ROM。
# 为什么重命名: vivo 等手机 FUSE 存储会截断中文文件名；ASCII 名可完整落地，且 App 热度榜含英文关键词。

param(
    [string]$Source = "D:\FCgames",
    [int]$Count = 100,
    [string]$OutDir = "D:\FCgames_phone"
)

# --- 热度榜: 关键词 -> (分数, 英文slug)。slug 用于重命名（ASCII 安全）---
$Rank = @(
    @{kw=@("超级马里奥","超级玛丽","super mario","mario"); s=100; slug="SuperMario"},
    @{kw=@("魂斗罗","contra"); s=99; slug="Contra"},
    @{kw=@("坦克大战","battle city","tank"); s=98; slug="BattleCity"},
    @{kw=@("冒险岛","adventure island","高桥名人"); s=97; slug="AdventureIsland"},
    @{kw=@("忍者神龟","teenage mutant ninja turtles","tmnt"); s=96; slug="TMNT"},
    @{kw=@("恶魔城","castlevania"); s=95; slug="Castlevania"},
    @{kw=@("沙罗曼蛇","salamander"); s=94; slug="Salamander"},
    @{kw=@("赤色要塞","jackal","top gunner"); s=93; slug="Jackal"},
    @{kw=@("松鼠大战","chip 'n dale","chip n dale"); s=92; slug="ChipNDale"},
    @{kw=@("双截龙","double dragon"); s=91; slug="DoubleDragon"},
    @{kw=@("洛克人","mega man","rockman"); s=90; slug="MegaMan"},
    @{kw=@("热血物语","热血格斗","热血篮球","热血足球","热血硬派","热血新纪录","nekketsu"); s=89; slug="Nekketsu"},
    @{kw=@("塞尔达","zelda"); s=88; slug="Zelda"},
    @{kw=@("银河战士","metroid"); s=87; slug="Metroid"},
    @{kw=@("俄罗斯方块","tetris"); s=86; slug="Tetris"},
    @{kw=@("泡泡龙","bubble bobble"); s=85; slug="BubbleBobble"},
    @{kw=@("大金刚","donkey kong"); s=84; slug="DonkeyKong"},
    @{kw=@("吃豆人","pac-man","pacman"); s=83; slug="PacMan"},
    @{kw=@("绿色兵团","rush'n attack","rush n attack"); s=82; slug="RushNAttack"},
    @{kw=@("兵蜂","twinbee","twin bee"); s=81; slug="TwinBee"},
    @{kw=@("宇宙巡航机","gradius"); s=80; slug="Gradius"},
    @{kw=@("马戏团","circus charlie"); s=79; slug="CircusCharlie"},
    @{kw=@("敲冰块","ice climber"); s=78; slug="IceClimber"},
    @{kw=@("气球大战","balloon fight"); s=77; slug="BalloonFight"},
    @{kw=@("越野机车","excitebike"); s=76; slug="Excitebike"},
    @{kw=@("影子传说","legend of kage"); s=75; slug="LegendOfKage"},
    @{kw=@("忍者龙剑传","ninja gaiden"); s=74; slug="NinjaGaiden"},
    @{kw=@("勇者斗恶龙","dragon quest"); s=73; slug="DragonQuest"},
    @{kw=@("最终幻想","final fantasy"); s=72; slug="FinalFantasy"},
    @{kw=@("火焰之纹章","fire emblem"); s=71; slug="FireEmblem"},
    @{kw=@("西游记","monkey king"); s=70; slug="JourneyToWest"},
    @{kw=@("七宝奇谋","goonies"); s=69; slug="Goonies"},
    @{kw=@("赤影战士","kage"); s=68; slug="Kage"},
    @{kw=@("忍者猫","ninja cat"); s=67; slug="NinjaCat"},
    @{kw=@("兔宝宝","bunny"); s=66; slug="BugsBunny"},
    @{kw=@("米老鼠","mickey mouse"); s=65; slug="MickeyMouse"},
    @{kw=@("唐老鸭","donald duck"); s=64; slug="DonaldDuck"},
    @{kw=@("小美人鱼","little mermaid"); s=63; slug="LittleMermaid"},
    @{kw=@("小蜜蜂","galaga"); s=62; slug="Galaga"},
    @{kw=@("太空侵略者","space invaders"); s=61; slug="SpaceInvaders"},
    @{kw=@("敲砖块","arkanoid"); s=60; slug="Arkanoid"},
    @{kw=@("古巴战士","guerrilla war"); s=59; slug="GuerrillaWar"},
    @{kw=@("中东战争","p.o.w"); s=58; slug="POW"},
    @{kw=@("怒","ikari"); s=57; slug="IkariWarriors"},
    @{kw=@("脱狱","prisoner"); s=56; slug="Prisoner"},
    @{kw=@("成龙之龙","jackie chan"); s=55; slug="JackieChan"},
    @{kw=@("功夫","kung fu"); s=54; slug="KungFu"},
    @{kw=@("荒野大镖客","gun.smoke","gunsmoke"); s=53; slug="Gunsmoke"},
    @{kw=@("超级魂斗罗","super contra"); s=52; slug="SuperContra"},
    @{kw=@("忍者蛙","battletoads"); s=51; slug="Battletoads"},
    @{kw=@("快打旋风","final fight"); s=50; slug="FinalFight"},
    @{kw=@("街头霸王","street fighter"); s=49; slug="StreetFighter"},
    @{kw=@("拳皇","king of fighters"); s=48; slug="KOF"},
    @{kw=@("侍魂","samurai"); s=47; slug="SamuraiShodown"},
    @{kw=@("前线大作战","front line"); s=46; slug="FrontLine"},
    @{kw=@("机甲战龙","power blade"); s=45; slug="PowerBlade"},
    @{kw=@("上尉命令","captain commando"); s=44; slug="CaptainCommando"},
    @{kw=@("名将"); s=44; slug="CaptainCommando"},
    @{kw=@("恐龙快打","cadillacs"); s=43; slug="Cadillacs"},
    @{kw=@("圆桌骑士","knights of the round"); s=42; slug="KnightsOfRound"},
    @{kw=@("三国志","romance of the three kingdoms"); s=41; slug="Romance3Kingdoms"},
    @{kw=@("吞食天地","destiny of an emperor"); s=40; slug="DestinyEmperor"},
    @{kw=@("龙珠","dragon ball"); s=39; slug="DragonBall"},
    @{kw=@("圣斗士","saint seiya"); s=38; slug="SaintSeiya"},
    @{kw=@("北斗神拳","fist of the north star","hokuto"); s=37; slug="HokutoNoKen"},
    @{kw=@("幽游白书","yu yu hakusho"); s=36; slug="YuYuHakusho"},
    @{kw=@("阿拉丁","aladdin"); s=35; slug="Aladdin"},
    @{kw=@("狮子王","lion king"); s=34; slug="LionKing"},
    @{kw=@("美女与野兽","beauty and the beast"); s=33; slug="BeautyBeast"},
    @{kw=@("蝙蝠侠","batman"); s=32; slug="Batman"},
    @{kw=@("超人","superman"); s=31; slug="Superman"},
    @{kw=@("蜘蛛侠","spider-man","spiderman"); s=30; slug="SpiderMan"},
    @{kw=@("中华大仙","super chinese"); s=29; slug="SuperChinese"},
    @{kw=@("天才厨师","cook"); s=28; slug="Chef"},
    @{kw=@("运动会","track & field"); s=28; slug="TrackField"},
    @{kw=@("棒球","baseball"); s=27; slug="Baseball"},
    @{kw=@("网球","tennis"); s=26; slug="Tennis"},
    @{kw=@("足球","soccer"); s=25; slug="Soccer"},
    @{kw=@("篮球","basketball"); s=24; slug="Basketball"},
    @{kw=@("高尔夫","golf"); s=23; slug="Golf"},
    @{kw=@("弹球","pinball"); s=22; slug="Pinball"},
    @{kw=@("赛车","racing"); s=21; slug="Racing"},
    @{kw=@("老虎机","slot"); s=20; slug="Slot"},
    @{kw=@("麻将","mahjong"); s=19; slug="Mahjong"},
    @{kw=@("象棋","chess"); s=17; slug="Chess"},
    @{kw=@("拼图","puzzle"); s=13; slug="Puzzle"},
    @{kw=@("猜谜","quiz"); s=12; slug="Quiz"},
    @{kw=@("平台","platform"); s=10; slug="Platform"},
    @{kw=@("射击","shoot"); s=10; slug="Shooting"},
    @{kw=@("冒险","adventure"); s=10; slug="Adventure"}
)

# 特定游戏优先匹配（在通用词之前，避免"水上魂斗罗"被误归入 Contra 等）
$Specific = @(
    @{kw=@("赤影战士","kage"); slug="Kage"},
    @{kw=@("空中魂斗罗","星际魂斗罗","最终任务","raf世界"); slug="FinalMission"},
    @{kw=@("打坦克","battle city","battlecity"); slug="BattleCity"},
    @{kw=@("超级战魂"); slug="Contra7"},
    @{kw=@("龙牙","dragon ninja"); slug="DragonNinja"},
    @{kw=@("西部牛仔","cowboy"); slug="Cowboy"},
    @{kw=@("未来战士","terminator"); slug="Terminator"},
    @{kw=@("上尉密令","captain america"); slug="CaptainAmerica"}
)

function Get-Info([string]$name) {
    $n = $name.ToLowerInvariant()
    foreach ($e in $Specific) {
        foreach ($k in $e.kw) {
            if ($n.Contains($k.ToLowerInvariant())) {
                return @{ kw = @($k); s = 60; slug = $e.slug }  # 特定游戏给保底分，避免排最后
            }
        }
    }
    foreach ($e in $Rank) {
        foreach ($k in $e.kw) {
            if ($n.Contains($k.ToLowerInvariant())) { return $e }
        }
    }
    return $null
}

# 游戏键 = 匹配到的英文 slug（同游戏所有变体折叠为一条）；未匹配则用粗略规范化名
function Get-GameKey([string]$base, [hashtable]$info) {
    if ($info) { return $info.slug }
    $k = $base.ToLowerInvariant()
    $k = $k -replace '（[^）]*）','' -replace '\([^)]*\)',''
    $k = $k -replace '[^a-z0-9]',''
    if ($k.Length -lt 2) { $k = "game" }
    return $k
}

if (-not (Test-Path $Source)) { Write-Error "源目录不存在: $Source"; exit 1 }
$roms = Get-ChildItem $Source -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @('.zip','.nes') -and $_.Length -le 4MB }
Write-Host "扫描到 $($roms.Count) 个候选 ROM 文件"

# 打分 + 游戏键
$scored = foreach ($f in $roms) {
    $base = $f.Name -replace '\.(zip|nes)$',''
    $info = Get-Info $base
    [pscustomobject]@{
        Name = $f.Name; Base = $base; Path = $f.FullName
        SizeMB = [math]::Round($f.Length/1MB,1)
        Score = if ($info) { $info.s } else { 0 }
        Slug  = if ($info) { $info.slug } else { "Game" }
        Key   = Get-GameKey $base $info
    }
}

# 每个游戏键只保留分数最高、名字最短(最干净)的一条
$best = $scored | Group-Object Key | ForEach-Object {
    $_.Group | Sort-Object @{e={-($_.Score)}}, @{e={$_.Base.Length}} | Select-Object -First 1
}
$top = $best | Sort-Object @{Expression='Score'; Descending=$true}, Base | Select-Object -First $Count

# 重命名: 排名前缀 + 英文slug（ASCII 安全，vivo 等设备可完整落地）
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem $OutDir -ErrorAction SilentlyContinue | Remove-Item -Force
$rank = 0
$lines = @()
foreach ($t in $top) {
    $rank++
    $newname = "{0:D3}_{1}.zip" -f $rank, $t.Slug
    Copy-Item $t.Path -Destination (Join-Path $OutDir $newname) -Force
    $lines += "{0}|{1}|{2}MB" -f $newname, $t.Base, $t.SizeMB
}
$lines | Set-Content (Join-Path $OutDir "manifest.txt") -Encoding UTF8

Write-Host "已复制 $rank 个不同游戏到 $OutDir（ASCII 命名，可安全推送）"
Write-Host "--- Top 20 ---"
$top | Select-Object -First 20 @{n='rank';e={$lines.IndexOf($_.Name)+1}}, Score, Base | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "清单: $OutDir\manifest.txt"
Write-Host "下一步: adb push $OutDir /sdcard/ROMs/"
