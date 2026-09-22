# FlyNES 预装 homebrew NES 游戏 —— 法律可行性核查（原始证据草稿）

> 本文件是外部核查的原始证据记录。所有结论以本文件记录的 URL 为依据。

## 已核实（有 URL 原文）

| 游戏 | 证据 | URL |
|---|---|---|
| Super Tilt Bro | GitHub API `license.spdx_id = WTFPL`，LICENSE 文件为 WTFPL v2 全文 | https://api.github.com/repos/sgadrat/super-tilt-bro , https://github.com/sgadrat/super-tilt-bro/blob/master/LICENSE |
| Nova the Squirrel | 仓库 MIT 元数据显示 GPL-3.0；README 描述「All code ... GPL v3 or later. Assets (graphics, levels, etc.) ... Attribution-NonCommercial-...」 | https://github.com/NovaSquirrel/NovaTheSquirrel |
| From Below | GitHub API `mhughson/mbh-firstnes` license = NONE，仓库无 LICENSE 文件；itch 页面仅提供免费 ROM 下载，无任何许可声明 | https://api.github.com/repos/mhughson/mbh-firstnes , https://mhughson.itch.io/from-below |
| Witch n' Wiz | `mbh-A53-witchnwiz` 仓库 MIT，但描述为「Mapper 28 (Action 53) version」；主游戏仓库私有 | https://github.com/mhughson?tab=repositories |
| Micro Mages | 官网页脚「© 2011-2024 Morphcat Games. All Rights Reserved.」；仅有付费渠道（Broke Studio/Steam/itch/PS） | https://morphcat.de/micromages/ |
| Streemerz | 页面只有 Download ROM 与 © 声明，无许可条款 | https://www.fauxgame.com/ |
| Action 53 Vol.1 | itch 页面列出 20 个投稿人与各自游戏，无任何统一许可；下载为免费 zip | https://action53.itch.io/vol1 |
| Shiru NES 游戏 | 软件页提供 rom + source zip，但页面无许可声明 | http://shiru.untergrund.net/software.shtml |
| Pin Eight (Tepples) | 站点总声明 CC BY-SA 2.5 / GFDL 1.2（站点内容级，非每 ROM 明确） | https://pineight.com/nes/ |
| Super Bat Puncher Demo | itch 价格 $0.00 | https://morphcatgames.itch.io/sbpdemo |
| Nebs 'n Debs | itch 价格 $9.99（商业） | https://dullahan-software.itch.io/nebs-n-debs |

## 追加核实（第二批，全部为直接抓取的一手页面/API）

| 项目 | 核实结果 | URL |
|---|---|---|
| Super Tilt Bro 许可原文 | itch 页面正文写明「Super Tilt Bro. is licensed under WTFPL Version 2」；ROM 为 name-your-own-price 免费下载，512KB `Super_Tilt_Bro_(E).nes` | https://sgadrat.itch.io/super-tilt-bro |
| Super Tilt Bro 作者关于素材的公开答复 | 作者在 itch 评论区明确：Supporter Pack 中「Most items are not explicitely freely licenced, making them legally all rights reserved」；OST 为「Freely licenced tracks, mostly CC-BY」。**游戏本体与 Supporter Pack 是两批不同权利状态** | https://sgadrat.itch.io/super-tilt-bro （评论区，作者 sgadrat 回复） |
| Twin Dragons 许可 | 仓库 README「## Licensing」段：美术 CC-BY 3.0（原作者 surt）；除 famitone5 外全部代码 zlib；其余音乐 CC BY 4.0。GitHub API license = Zlib；LICENSE 为 zlib 标准全文含 "including commercial applications ... redistribute it" | https://raw.githubusercontent.com/Garydos/twin-dragons-nes/master/README.md ，https://github.com/Garydos/twin-dragons-nes/blob/master/LICENSE |
| Nova the Squirrel 素材许可原文 | README「License」段：代码 GPLv3+；「Assets (graphics, levels, etc.) are available under Attribution-NonCommercial-ShareAlike 4.0 International therefore **the game may not be sold without permission** with these intact」 | https://raw.githubusercontent.com/NovaSquirrel/NovaTheSquirrel/master/README.md |
| Alter Ego 非自由 | nesdev 论坛帖原文：「Except Alter Ego, which last I checked was under a nonfree license because it's a remake of someone else's game who provided a license for the remake under ...」 | https://forums.nesdev.org/viewtopic.php?t=17168 |
| From Below 无许可 | 仓库无 LICENSE 文件，README 无许可段；itch 页面无任何许可声明，只有免费 ROM 下载与 Discord | https://github.com/mhughson/mbh-firstnes ，https://mhughson.itch.io/from-below |
| Witch n' Wiz 商业 | 主游戏仓库为私有（只有 issue tracker 仓库 `witchnwiz-issues`）；有 Action 53 mapper 版仓库 `mbh-A53-witchnwiz`（MIT） | https://github.com/mhughson?tab=repositories |
| Micro Mages 全权保留 | 页面页脚「© 2011-2024 Morphcat Games. All Rights Reserved.」；渠道为 Broke Studio / Steam / itch 付费 / PS | https://morphcat.de/micromages/ |
| homebrew-db（权威机器可读许可库） | 每项含 `gameLicense` / `assetsLicense` 字段；23 个 NES 条目中 9 个有明确 gameLicense | https://github.com/nesdev-org/homebrew-db |
| Pin Eight 系列许可（一手 LICENSE 全文） | croom=GPL-3.0；thwaite=GPL-3.0；240p-test-mini=GPL-2.0；rhde-nes=FSF All Permissive（LICENSE.txt 原文「Copying and distribution of this file, with or without modification, are permitted in any medium without royalty...」）；zap-ruder=同类宽松（Copyright 2011 Damian Yerrick） | https://github.com/pinobatch/croom-nes ，https://github.com/pinobatch/thwaite-nes ，https://github.com/pinobatch/rhde-nes ，https://github.com/pinobatch/zap-ruder |
| Action 53 逐作授权不同 | itch Vol.1 页面逐一列名 20 个投稿人及其作品，页面无任何统一/逐项许可条款 | https://action53.itch.io/vol1 |
| 商业渠道与联系方式 | Broke Studio contact@brokestudio.fr；Morphcat morphcatgames@gmail.com；KHAN Games khangames.com；Retrotainment retrotainmentgames.com | https://www.brokestudio.fr/ ，https://morphcat.de/ ，https://www.khangames.com/ ，https://www.retrotainmentgames.com/ |

## 待核实 / 未核实
- Super Tilt Bro **游戏内音乐**的逐曲许可（仓库 music 目录与菜单 credits 的授权清单未逐条核对）
- Shiru 是否在任何地方声明 CC0/公共领域（**未找到任何许可声明**）
- Blade Buster / Super Bat Puncher / 8-Bit Xmas / Kira Kira Star Night / 8BIT MUSIC POWER 的官方页与授权（未取得一手页面）
- Full Quiet / Haunted 系列 / GPK NES / NEScape! / The Mad Wizard / Mystic Searches / Nomolos / Rollie / Project Blue / Wolfling / Curse of Illmoore Bay 的授权与渠道（未逐个核对）
- Action 53 Vol.2/3/4 是否提供逐作品许可清单（未核对）
- 本次核查后期 web_search 工具额度用尽（Firecrawl 免费层限流，约 24h 后恢复），上述未核实项需补查
