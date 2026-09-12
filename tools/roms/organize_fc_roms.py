#!/usr/bin/env python3
"""Build a phone-ready, one-ROM-per-ZIP FC library with Chinese display names.

The source tree is read-only. ROM payload bytes are copied unchanged and a
manifest records the source archive/member and SHA-256 for every output.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import sys
import zipfile
from dataclasses import asdict, dataclass
from pathlib import Path


SUPPORTED_SUFFIXES = {".nes", ".fds", ".unf", ".unif"}
CJK_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")

# The collection already has Chinese archive names for most titles. These are
# the remaining playable archives whose package names are English-only.
ZH_TITLES = {
    "1942": "1942空战", "1943": "1943中途岛海战", "1944": "1944空战",
    "6in1": "六合一", "Addams_Family_-_Pugsley's_Scavenger_Hunt": "恶魔之子",
    "Addams_Family": "亚当斯一家", "AKIRA": "阿基拉", "ANPAN_MAN": "面包超人",
    "Archon": "执政官", "ARCTIC": "北极", "Arkista's_Ring": "阿尔基斯塔之戒",
    "ARTELIUS": "阿特利乌斯", "Asterix": "高卢英雄",
    "Barker_Bill's_Trick_Shooting": "巴克比尔特技射击", "Basels~1": "棒球游戏",
    "Basewars": "棒球战争", "Beetlejuice": "阴间大法师", "Bible_Buffet": "圣经大餐",
    "Bill_Elliott's_NASCA_Challenge": "比尔埃利奥特赛车挑战", "Cabal": "霹雳神兵",
    "Castelian": "卡斯特利安", "CHACK'N_POP": "查克弹跳", "CHAO’S_WORLD": "查奥世界",
    "Cheetahmen_2": "猎豹人2", "CHIISANA_OBAKE": "小小幽灵", "Cliffhanger": "巅峰战士",
    "Conan": "柯南", "Cool_World": "酷世界", "Crackout": "敲砖大战",
    "Dash_Galaxy_in_the_Alien_Asylum": "达什银河异星疯人院", "Day_Dreamin'_Davey": "白日梦戴维",
    "Ddribble": "篮球", "Deathbots": "死亡机器人", "Defenders_of_Dynatron_City": "迪纳特隆城守卫者",
    "Deja_Vu": "似曾相识", "Destination_Earthstar": "目标地球星", "DIE_HARD": "虎胆龙威",
    "Digger_-_The_Legend_of_the_Lost_City": "挖掘者失落之城", "Drac's_Night_Out": "德古拉夜游",
    "Dudes_With_Attitude": "态度小子", "Dudes_With_Attitude2": "态度小子2", "ELYSION": "极乐世界",
    "Exodus": "出埃及记", "Family_Feud": "家庭问答", "Fisher_Price_-_Firehouse_Rescue": "费雪消防站救援",
    "Fisher_Price_-_I_Can_Remember": "费雪记忆游戏", "Fisher_Price_-_Perfect_Fit": "费雪完美拼合",
    "FLAPPY": "飞跃方块", "Flight_of_the_Intruder": "入侵者战机", "Formula_One_Built_To_Win": "一级方程式胜利之路",
    "Foton_-_The_Ultimate_Game_on_Planet_Earth": "光子终极星球游戏", "FOTON": "光子",
    "Galaxy_5000": "银河5000", "George_Foreman's_KO_Boxing": "乔治福尔曼拳击", "Goal!!": "足球目标2",
    "Goal!": "足球目标", "Goal'92": "足球目标92", "Great_Waldo_Search": "寻找威利",
    "Greg_Norman's_Golf_Power": "格雷格诺曼高尔夫", "Gumshoe": "神枪手", "GUN_HED": "钢铁战机",
    "GUN_SIGHT": "枪炮瞄准", "Gyromite": "机器人陀螺", "HIRYUK3": "飞龙之拳3",
    "HOSTAGES": "人质", "HUSHIGINA_BLOBY": "不可思议的布洛比", "HYDLIDE_3": "海德莱德3",
    "Jack_Nicklaus'_Greatest_18_Holes_of_Championship_Golf": "杰克尼克劳斯冠军高尔夫18洞",
    "James_Bond_Jr": "小詹姆斯邦德", "Jeopardy!_25th_Anniversary_Edition)": "危险边缘25周年版",
    "Jeopardy!_Junior_Edition": "危险边缘少年版", "Jeopardy!": "危险边缘", "JJ": "JJ大作战",
    "Joshua": "约书亚", "Journey": "未来战争", "JOUST": "鸵鸟骑士", "JUST_BREED": "正义血统",
    "Kick_off": "开球", "Kickle_Cubicle": "迷宫岛", "Kid_Klown": "小丑小子",
    "King_Neptune's_Adventure": "海王星王冒险", "Klash_Ball": "冲击球", "KLAX": "克拉克斯方块",
    "LABYRINTH": "魔王迷宫", "LAST_ARMAGEDDON": "最后的末日", "Lemmings": "百战小旅鼠",
    "Linus_Spacehead's_Cosmic_Crusade": "莱纳斯宇宙远征", "LITLNEMO": "小贝贝梦游记",
    "LONERNGR": "孤独牛仔", "Loopz": "环形拼图", "LOT_LOT": "洛特洛特方块",
    "MANIAC_MANSION": "疯狂大楼", "Marble_Madness": "疯狂弹珠", "MASUZOE_YOUICHI": "舛添要一政治游戏",
    "MELVILLE'S_FLAME": "梅尔维尔之炎", "Menace_Beach": "危险海滩", "METAL_SLADER_GLORY": "金属之光",
    "METALSTR": "金属装甲", "Michael_Andretti's_World_Grand_Prix": "迈克尔安德烈蒂世界大奖赛",
    "Mindseeker": "超能力开发", "Miracle_Piano_Teaching_System": "奇迹钢琴教学系统",
    "MONSTER_MAKER": "怪兽制造者", "Monster_Party": "怪兽派对", "Monster_Truck_Rally": "怪兽卡车拉力赛",
    "Moon_Ranger": "月球游侠", "Mr_Gimmick": "奇妙先生", "Mule": "骡子",
    "Muppet_Adventure_-_Chaos_at_the_Carnival": "布偶嘉年华大冒险", "Mutant_Virus": "变异病毒",
    "Mystery_Quest": "神秘冒险", "Nakayoshi_To_Issho": "与好朋友同行",
    "NESA_Audio_Player_-_Little_Nemo_Sample": "音频播放器小尼莫示例",
    "NESA_Audio_Player_-_Times_of_Lore": "音频播放器传说时代示例",
    "Nigel_Mansell's_World_Championship_Challenge": "奈杰尔曼塞尔世界锦标赛",
    "OBOTCHAMA_KUN": "少爷君", "Orb_3D": "立体圆球", "P'radikus_Conflict": "普拉迪库斯冲突",
    "Papillion": "蝴蝶", "parasol_henbee": "雨伞小亨贝", "peepar_time": "皮帕时光",
    "Pesterminator": "害虫终结者", "Pictionary": "你画我猜", "PUNISHER": "惩罚者",
    "Q-bert": "Q伯特", "Qix": "天蚕变", "quinty": "昆蒂", "RADGRAV": "太空历险",
    "RAID_2020": "2020突袭", "RAMBO": "兰博", "RC_Pro-Am": "遥控赛车", "RECCA": "烈火战机",
    "Remote_Control": "遥控器", "Rogerrab": "罗杰兔", "Roundball_-_2-on-2_Challenge_(U)": "圆球二对二挑战",
    "SHAFFLE_FIGHT": "洗牌格斗", "SHANCARA": "香卡拉", "Shinobi": "忍者",
    "Short_Order_-_Eggsplode": "快餐与爆蛋", "SHUFFLEPUCK_CAFE": "沙狐球咖啡馆",
    "SILKWORM": "中东战争", "Slalom_(U)": "障碍滑雪",
    "Solar_Jetman_-_Hunt_for_the_Golden_Warpship": "太阳喷射人寻找黄金飞船",
    "Stanley_-_The_Search_For_Dr_Livingston": "史丹利寻找李文斯顿博士",
    "Star_Trek_-_25th_Anniversary": "星际迷航25周年", "Star_Trek_-_The_Next_Generation": "星际迷航下一代",
    "Stealth_ATF": "隐形战斗机", "Super_Mario_Bros_-_Tetris_-_Nintendo_World_Cup_(E)": "超级马力欧俄罗斯方块任天堂世界杯",
    "TALE_SPIN": "航空小英雄", "Tiles_of_Fate": "命运方块", "Toobin": "漂流大冒险",
    "Total_Recall": "全面回忆", "Totally_Rad": "魔法总动员", "Trog": "穴居人",
    "TSURU_PIKA": "光头鹤", "Uchuusen_-_Cosmo_Carrier": "宇宙船宇宙航母", "Ufouria": "乌福利亚大冒险",
    "WARP_MAN": "时空人", "Wayne's_World": "韦恩的世界", "Where_in_Time_is_Carmen_Sandiego": "神偷卡门时空之旅",
    "Widget": "小精灵维吉特", "X-men": "X战警", "Xenophobe": "异形恐惧", "ZOIDS_2": "机械兽2",
}

MULTI_MEMBER_TITLES = {
    "家庭围棋入门": ["家庭围棋入门", "家庭围棋入门四十二合一"],
}


@dataclass(frozen=True)
class ManifestRow:
    output: str
    title: str
    source: str
    source_member: str
    sha256: str
    size: int


def has_chinese(value: str) -> bool:
    return CJK_RE.search(value) is not None


def chinese_title(source_stem: str) -> str:
    title = ZH_TITLES.get(source_stem, source_stem).strip()
    if not has_chinese(title):
        raise ValueError(f"missing Chinese title: {source_stem}")
    return title


def safe_title(value: str) -> str:
    return re.sub(r'[<>:"/\\|?*]', "·", value).strip(" .")


def unique_title(wanted: str, used: dict[str, int]) -> str:
    count = used.get(wanted, 0) + 1
    used[wanted] = count
    return wanted if count == 1 else f"{wanted}（版本{count}）"


def write_zip(output: Path, member_name: str, payload: bytes) -> None:
    info = zipfile.ZipInfo(member_name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr(info, payload)


def organize(source: Path, destination: Path, *, clean: bool) -> tuple[list[ManifestRow], list[dict[str, object]]]:
    if clean and destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    rows: list[ManifestRow] = []
    skipped: list[dict[str, object]] = []
    used: dict[str, int] = {}

    for archive_path in sorted(source.glob("*.zip"), key=lambda path: path.name.casefold()):
        with zipfile.ZipFile(archive_path) as archive:
            members = [item for item in archive.infolist()
                       if not item.is_dir() and Path(item.filename).suffix.lower() in SUPPORTED_SUFFIXES]
            if not members:
                skipped.append({"source": archive_path.name, "reason": "no supported FC ROM member"})
                continue
            base_title = chinese_title(archive_path.stem)
            overrides = MULTI_MEMBER_TITLES.get(archive_path.stem, [])
            for index, member in enumerate(members):
                wanted = overrides[index] if index < len(overrides) else (
                    base_title if len(members) == 1 else f"{base_title}（第{index + 1}款）")
                title = unique_title(safe_title(wanted), used)
                suffix = Path(member.filename).suffix.lower()
                payload = archive.read(member)
                output_name = f"{title}.zip"
                write_zip(destination / output_name, f"{title}{suffix}", payload)
                rows.append(ManifestRow(output_name, title, archive_path.name, member.filename,
                                        hashlib.sha256(payload).hexdigest().upper(), len(payload)))

    for rom_path in sorted(source.iterdir(), key=lambda path: path.name.casefold()):
        if not rom_path.is_file() or rom_path.suffix.lower() not in SUPPORTED_SUFFIXES:
            continue
        title = unique_title(safe_title(chinese_title(rom_path.stem)), used)
        payload = rom_path.read_bytes()
        output_name = f"{title}.zip"
        write_zip(destination / output_name, f"{title}{rom_path.suffix.lower()}", payload)
        rows.append(ManifestRow(output_name, title, rom_path.name, rom_path.name,
                                hashlib.sha256(payload).hexdigest().upper(), len(payload)))

    with (destination / "flynes-sideload.txt").open("w", encoding="utf-8", newline="\n") as handle:
        for row in rows:
            handle.write(f"{row.output}\n")
    with (destination / "manifest.csv").open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(rows[0]).keys()) if rows else list(ManifestRow.__annotations__))
        writer.writeheader()
        writer.writerows(asdict(row) for row in rows)
    (destination / "skipped.json").write_text(json.dumps(skipped, ensure_ascii=False, indent=2), encoding="utf-8")
    return rows, skipped


def verify(destination: Path) -> int:
    manifest_path = destination / "manifest.csv"
    with manifest_path.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    errors: list[str] = []
    for row in rows:
        archive_path = destination / row["output"]
        if not archive_path.is_file():
            errors.append(f"missing output: {row['output']}")
            continue
        with zipfile.ZipFile(archive_path) as archive:
            members = [item for item in archive.infolist() if not item.is_dir()]
            if len(members) != 1:
                errors.append(f"expected one member: {row['output']}")
                continue
            member = members[0]
            if not has_chinese(Path(member.filename).stem):
                errors.append(f"non-Chinese member: {member.filename}")
            payload = archive.read(member)
            if hashlib.sha256(payload).hexdigest().upper() != row["sha256"]:
                errors.append(f"payload hash mismatch: {row['output']}")
    if errors:
        print("\n".join(errors[:50]), file=sys.stderr)
        return 1
    print(json.dumps({"result": "PASS", "roms": len(rows), "allDisplayNamesChinese": True}, ensure_ascii=False))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("source", type=Path)
    build.add_argument("destination", type=Path)
    build.add_argument("--clean", action="store_true")
    check = subparsers.add_parser("verify")
    check.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.command == "verify":
        return verify(args.destination)
    rows, skipped = organize(args.source, args.destination, clean=args.clean)
    print(json.dumps({"result": "PASS", "roms": len(rows), "skipped": len(skipped),
                      "destination": str(args.destination)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
