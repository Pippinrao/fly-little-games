package com.flynes.emu;

/**
 * 游戏名元数据（非版权内容），用于受欢迎程度排序。
 *
 * 内置的经典 NES/FC 游戏热度榜：每个条目是一组中英文/别名关键词 + 分数。
 * 条目按分数从高到低排列，{@link #score(String)} 对文件名做小写包含匹配，
 * 第一个命中的条目（即分数最高的命中）决定该文件的热度分；未命中返回 0。
 * 分数设计：最热门的约 20 款游戏 100~90 分，之后逐档递减，最低 10 分。
 */
public class Popularity {

    /** One ranked entry: any keyword contained in the filename scores `score`. */
    private static final class Rank {
        final String[] keywords;
        final int score;

        Rank(int score, String... keywords) {
            this.score = score;
            this.keywords = keywords;
        }
    }

    // Ordered by descending score: the first entry whose keyword matches wins.
    private static final Rank[] RANKS = {

            // ===== 100 分：国民级 =====
            new Rank(100, "超级马里奥", "超级玛丽", "超级玛利", "超级马利",
                    "super mario", "supermario", "super_mario", "mario"), // 超级马里奥
            new Rank(100, "魂斗罗", "contra"), // 魂斗罗
            new Rank(100, "坦克大战", "battle city", "battlecity", "tank"), // 坦克大战
            new Rank(100, "俄罗斯方块", "tetris"), // 俄罗斯方块
            new Rank(100, "冒险岛", "adventure island", "adventureisland",
                    "高桥名人", "takahashi"), // 冒险岛（高桥名人）
            new Rank(100, "忍者神龟", "忍者龟", "teenage mutant ninja turtles",
                    "teenage mutant ninja turtle", "tmnt", "ninja turtles", "turtles"), // 忍者神龟

            // ===== 99 分 =====
            new Rank(99, "恶魔城", "castlevania", "akumajou", "akumajo"), // 恶魔城
            new Rank(99, "沙罗曼蛇", "salamander", "life force", "lifeforce"), // 沙罗曼蛇
            new Rank(99, "赤色要塞", "jackal", "top gunner", "final command"), // 赤色要塞
            new Rank(99, "松鼠大战", "松鼠大作战", "chip 'n dale", "chip'n dale",
                    "chip and dale", "rescue rangers"), // 松鼠大战
            new Rank(99, "双截龙", "double dragon"), // 双截龙
            new Rank(99, "洛克人", "mega man", "megaman", "rockman", "rock man"), // 洛克人

            // ===== 98 分 =====
            new Rank(98, "热血", "nekketsu", "nekketu", "river city", "downtown"), // 热血系列
            new Rank(98, "塞尔达", "zelda"), // 塞尔达传说
            new Rank(98, "银河战士", "metroid"), // 银河战士
            new Rank(98, "勇者斗恶龙", "dragon quest", "dragonquest",
                    "dragon warrior", "dragonwarrior"), // 勇者斗恶龙
            new Rank(98, "最终幻想", "final fantasy", "finalfantasy", "太空战士"), // 最终幻想
            new Rank(98, "火焰之纹章", "火焰纹章", "火纹", "fire emblem", "圣火徽章"), // 火焰之纹章

            // ===== 97 分 =====
            new Rank(97, "泡泡龙", "bubble bobble", "bubblebobble"), // 泡泡龙
            new Rank(97, "大金刚", "donkey kong", "donkeykong"), // 大金刚
            new Rank(97, "吃豆人", "pac-man", "pacman", "pac man"), // 吃豆人
            new Rank(97, "绿色兵团", "rush'n attack", "rush n attack", "rushn attack",
                    "rush n' attack", "green beret"), // 绿色兵团
            new Rank(97, "兵蜂", "twinbee", "twin bee"), // 兵蜂

            // ===== 96 分 =====
            new Rank(96, "宇宙巡航机", "gradius"), // 宇宙巡航机
            new Rank(96, "马戏团", "circus charlie", "circus"), // 马戏团
            new Rank(96, "敲冰块", "ice climber"), // 敲冰块
            new Rank(96, "气球大战", "balloon fight"), // 气球大战
            new Rank(96, "越野机车", "excitebike", "越野摩托"), // 越野机车

            // ===== 95 分 =====
            new Rank(95, "影子传说", "legend of kage", "the legend of kage",
                    "kage no densetsu", "kage"), // 影子传说
            new Rank(95, "忍者龙剑传", "忍者龙剑", "ninja gaiden", "忍龙"), // 忍者龙剑传
            new Rank(95, "西游记", "西游记世界", "saiyuki", "son son", "sonson"), // 西游记
            new Rank(95, "七宝奇谋", "goonies"), // 七宝奇谋
            new Rank(95, "赤影战士", "shadow of the ninja", "blue shadow"), // 赤影战士

            // ===== 94 分 =====
            new Rank(94, "忍者猫", "ninja cat", "samurai pizza cats", "忍者喵"), // 忍者猫
            new Rank(94, "兔宝宝", "兔八哥", "兔巴哥", "bugs bunny", "tiny toon",
                    "looney tunes"), // 兔宝宝
            new Rank(94, "米老鼠", "米奇", "mickey mouse", "mickey"), // 米老鼠
            new Rank(94, "唐老鸭", "donald duck", "ducktales", "duck tales", "怪鸭历险记"), // 唐老鸭
            new Rank(94, "美人鱼", "小美人鱼", "little mermaid"), // 美人鱼

            // ===== 93 分 =====
            new Rank(93, "小蜜蜂", "大蜜蜂", "galaga", "galaxian"), // 小蜜蜂
            new Rank(93, "太空侵略者", "space invaders"), // 太空侵略者
            new Rank(93, "敲砖块", "打砖块", "arkanoid", "breakout"), // 敲砖块
            new Rank(93, "淘金者", "lode runner", "loderunner"), // 淘金者
            new Rank(93, "1942", "1943"), // 1942

            // ===== 92 分 =====
            new Rank(92, "炸弹人", "bomberman", "轰炸超人"), // 炸弹人
            new Rank(92, "蝙蝠侠", "batman", "蝙蝠"), // 蝙蝠侠
            new Rank(92, "超时空要塞", "macross", "太空堡垒"), // 超时空要塞
            new Rank(92, "忍者蛙", "battletoads", "battle toads"), // 忍者蛙
            new Rank(92, "魔界村", "ghosts n goblins", "ghosts'n goblins",
                    "ghosts 'n goblins", "魔界"), // 魔界村

            // ===== 91 分 =====
            new Rank(91, "圣斗士", "saint seiya", "seiya"), // 圣斗士星矢
            new Rank(91, "七龙珠", "龙珠", "dragon ball", "dragonball"), // 七龙珠
            new Rank(91, "哆啦A梦", "哆啦a梦", "doraemon", "机器猫", "叮当"), // 哆啦A梦
            new Rank(91, "火箭车", "road fighter"), // 火箭车
            new Rank(91, "功夫", "kung fu", "kung-fu", "kungfu", "spartan x", "成龙踢馆"), // 功夫

            // ===== 90 分 =====
            new Rank(90, "吞食天地"), // 吞食天地
            new Rank(90, "三国志", "sangokushi", "romance of the three kingdoms",
                    "三国外传", "三国"), // 三国志
            new Rank(90, "水浒传", "suikoden"), // 水浒传
            new Rank(90, "封神榜", "封神"), // 封神榜
            new Rank(90, "重装机兵", "metal max"), // 重装机兵

            // ===== 89 分 =====
            new Rank(89, "霸王的大陆", "haou no daichi"), // 霸王的大陆
            new Rank(89, "信长之野望", "nobunaga", "信长"), // 信长之野望
            new Rank(89, "幽游白书", "yu yu hakusho", "yuyu"), // 幽游白书
            new Rank(89, "灌篮高手", "slam dunk"), // 灌篮高手
            new Rank(89, "足球小将", "captain tsubasa", "天使之翼"), // 足球小将

            // ===== 88 分 =====
            new Rank(88, "美少女战士", "sailor moon"), // 美少女战士
            new Rank(88, "高达", "gundam", "敢达"), // 机动战士高达
            new Rank(88, "机器人大战", "super robot wars", "机战"), // 超级机器人大战
            new Rank(88, "阿童木", "astroboy", "astro boy", "铁臂阿童木"), // 铁臂阿童木
            new Rank(88, "科乐美世界", "柯纳米世界", "konami world", "wai wai world", "waiwai"), // 科乐美世界

            // ===== 87 分 =====
            new Rank(87, "玛丽医生", "马里奥医生", "dr. mario", "drmario"), // 玛丽医生
            new Rank(87, "猫和老鼠", "tom and jerry", "tom & jerry", "汤姆和杰瑞"), // 猫和老鼠
            new Rank(87, "蓝精灵", "smurfs", "smurf"), // 蓝精灵
            new Rank(87, "大力水手", "popeye"), // 大力水手
            new Rank(87, "捉鬼敢死队", "ghostbusters", "抓鬼"), // 捉鬼敢死队

            // ===== 86 分 =====
            new Rank(86, "星球大战", "star wars", "empire strikes back", "return of the jedi"), // 星球大战
            new Rank(86, "蜘蛛侠", "蜘蛛人", "spider-man", "spiderman", "spider man"), // 蜘蛛侠
            new Rank(86, "超人", "superman"), // 超人
            new Rank(86, "特种部队", "g.i. joe", "gi joe"), // 特种部队
            new Rank(86, "夺宝奇兵", "indiana jones", "印第安纳琼斯"), // 夺宝奇兵

            // ===== 85 分 =====
            new Rank(85, "荒野大镖客", "荒野大嫖客", "gun.smoke", "gunsmoke"), // 荒野大镖客
            new Rank(85, "古巴战士", "guerrilla war", "guevara"), // 古巴战士
            new Rank(85, "怒", "ikari warriors", "ikari"), // 怒
            new Rank(85, "脱狱", "p.o.w.", "prisoners of war"), // 脱狱
            new Rank(85, "忍者君", "ninja kun", "ninja-kun"), // 忍者君

            // ===== 84 分 =====
            new Rank(84, "忍者茶茶丸", "茶茶丸", "jajamaru", "chachamaru"), // 忍者茶茶丸
            new Rank(84, "超惑星战记", "blaster master"), // 超惑星战记
            new Rank(84, "沙迦", "saga"), // 沙迦
            new Rank(84, "龙魂", "dragon spirit"), // 龙魂
            new Rank(84, "雷电", "raiden"), // 雷电

            // ===== 83 分 =====
            new Rank(83, "冰上曲棍球", "blades of steel", "冰球"), // 冰上曲棍球
            new Rank(83, "田径", "track & field", "track and field"), // 田径
            new Rank(83, "摔跤", "pro wrestling", "摔角"), // 摔跤
            new Rank(83, "拳击", "punch out", "punch-out", "punchout", "mike tyson"), // 拳击
            new Rank(83, "一休", "ikkyu", "聪明的一休"), // 一休

            // ===== 82 分 =====
            new Rank(82, "花仙子", "lunlun", "hana no ko lunlun"), // 花仙子
            new Rank(82, "忍者乱太郎", "nintama"), // 忍者乱太郎
            new Rank(82, "魔神英雄传", "mashin eiyuuden", "wataru"), // 魔神英雄传
            new Rank(82, "卡诺夫", "karnov"), // 卡诺夫
            new Rank(82, "迷宫组曲", "meikyuu", "meikyu"), // 迷宫组曲

            // ===== 81 分 =====
            new Rank(81, "希魔复活", "bionic commando", "生化尖兵"), // 希魔复活
            new Rank(81, "机器战警", "robocop", "铁甲威龙"), // 机器战警
            new Rank(81, "兰博", "rambo", "第一滴血"), // 兰博
            new Rank(81, "战场之狼", "commando", "mercs", "merc"), // 战场之狼
            new Rank(81, "快打旋风", "final fight"), // 快打旋风

            // ===== 80 分 =====
            new Rank(80, "街头霸王", "street fighter"), // 街头霸王
            new Rank(80, "虎胆龙威", "die hard"), // 虎胆龙威

            // ===== 79 ~ 58 分：常见佳作 =====
            new Rank(79, "星之卡比", "卡比", "kirby"), // 星之卡比（非 FC，但常见于合卡/改版）
            new Rank(78, "打鸭子", "打鸭", "duck hunt", "猎鸭"), // 打鸭子
            new Rank(77, "中华大仙", "chuka taisen", "cloud master", "china town"), // 中华大仙
            new Rank(76, "李小龙", "bruce lee"), // 李小龙
            new Rank(75, "魔法气泡", "puyo"), // 魔法气泡
            new Rank(74, "变形金刚", "transformers"), // 变形金刚
            new Rank(73, "奥特曼", "ultraman"), // 奥特曼
            new Rank(72, "彩虹岛", "rainbow islands"), // 彩虹岛
            new Rank(71, "摩登原始人", "flintstones", "聪明笨伯"), // 摩登原始人
            new Rank(70, "赛车", "rc pro-am", "rc pro am"), // RC Pro-Am
            new Rank(69, "滑雪", "slalom"), // 滑雪
            new Rank(68, "葫芦娃", "葫芦兄弟"), // 葫芦娃
            new Rank(67, "黑猫警长", "黑猫警长"), // 黑猫警长
            new Rank(66, "麻将", "mahjong"), // 麻将
            new Rank(65, "中国象棋", "象棋", "chinese chess"), // 中国象棋
            new Rank(64, "音速小子", "sonic", "索尼克"), // 音速小子
            new Rank(63, "高尔夫", "golf"), // 高尔夫
            new Rank(62, "棒球", "baseball"), // 棒球
            new Rank(61, "网球", "tennis"), // 网球
            new Rank(60, "排球", "volleyball"), // 排球
            new Rank(59, "保龄球", "bowling"), // 保龄球
            new Rank(58, "台球", "billiards"), // 台球

            // ===== 地板分数（最低 10 分）=====
            new Rank(40, "围棋", "weiqi", "igo"), // 围棋
            new Rank(30, "扑克", "poker"), // 扑克
            new Rank(20, "五子棋", "gomoku"), // 五子棋
            new Rank(10, "桥牌", "bridge"), // 桥牌
    };

    private Popularity() {
    }

    /**
     * Scores a filename 0..100. Lowercases the filename, then walks the ranked
     * list from highest score down: the first entry containing any of its
     * keywords wins. Returns 0 when nothing matches.
     */
    public static int score(String filename) {
        if (filename == null) return 0;
        String lower = filename.toLowerCase();
        for (Rank r : RANKS) {
            for (String kw : r.keywords) {
                if (lower.contains(kw)) return r.score;
            }
        }
        return 0;
    }

    /** Number of curated entries (useful for tests/tools). */
    public static int entryCount() {
        return RANKS.length;
    }
}
