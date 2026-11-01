/* The letters of the game fonts (lang.h; docs/LANGUAGES.md "Letters"): which glyph code shows a character in each
   language's font, for our own texts (pc_glyph_next) and the overlay (pc_glyphs_utf8). Pure: no game state
   (pc/tests/test_lang.c checks our translations against it).
     English  the US font: ASCII - 32 (0..94), ® 95, a few signs
     French, German, Spanish  the European font: ASCII, accents at 95..149
     Russian  the fan translation's font: ASCII, the Cyrillic letters that look Latin are the Latin ones, the others at
              95..150 (lang.c reads its widths from the patched executable)
     Turkish  the European font with six cells redrawn: Ğ 102, İ 107, Ş 109, ş 124, ı 126, ğ 140 (in place of
              Ê Ï Ó ë í ñ)
     Brazilian Portuguese  the US font with õ ó ô â ã á à ê é í ú ç at 96..107, Ç 108 (no width: the patch writes
              the 24-wide blank 94 after it), ° 59; no capitals with accents (the patch writes them plain)
     Japanese the Japanese font's 1296 cells (4 textures of 324) at LANG_JA_BASE + cell: kana, the kanji the game
              uses, full-width Latin, digits and signs; the codes below LANG_JA_BASE (the US font's, which the US
              engine writes) are drawn with the look-alike cells
     Korean   the fan translation's font: the Japanese one with about 740 cells redrawn (Hangul, a few signs moved),
              the same codes; a US code without a look-alike cell is drawn from the US font (lang_glue.c)
     Italian  the European font (the fan translation's, in the French folder) */
#include "lang.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(LANG_OTHER(LANG_COUNT - 1, LANG_OTHER_SPACE) < 0xFE00, "LANG_OTHER codes reach the control codes");

/* the European font's letters (codes 95..149); the US font has none of them (plain: the letter without its accent) */
static const struct { unsigned short u, code; char plain; } accents[] = {
    { 0xC0, 95, 'A' }, { 0xC1, 96, 'A' }, { 0xC2, 97, 'A' }, { 0xC4, 98, 'A' }, { 0xC3, 0, 'A' }, { 0xC5, 0, 'A' },
    { 0xC8, 100, 'E' }, { 0xC9, 101, 'E' }, { 0xCA, 102, 'E' }, { 0xCB, 103, 'E' },
    { 0xCC, 104, 'I' }, { 0xCD, 105, 'I' }, { 0xCE, 106, 'I' }, { 0xCF, 107, 'I' },
    { 0xD2, 108, 'O' }, { 0xD3, 109, 'O' }, { 0xD4, 110, 'O' }, { 0xD6, 111, 'O' }, { 0xD5, 0, 'O' },
    { 0xD9, 112, 'U' }, { 0xDA, 113, 'U' }, { 0xDB, 114, 'U' }, { 0xDC, 115, 'U' },
    { 0xE0, 116, 'a' }, { 0xE1, 117, 'a' }, { 0xE2, 118, 'a' }, { 0xE4, 119, 'a' }, { 0xE3, 0, 'a' }, { 0xE5, 0, 'a' },
    { 0xDF, 120, 's' }, { 0xE8, 121, 'e' }, { 0xE9, 122, 'e' }, { 0xEA, 123, 'e' }, { 0xEB, 124, 'e' },
    { 0xEC, 125, 'i' }, { 0xED, 126, 'i' }, { 0xEE, 127, 'i' }, { 0xEF, 128, 'i' },
    { 0xF2, 129, 'o' }, { 0xF3, 130, 'o' }, { 0xF4, 131, 'o' }, { 0xF6, 132, 'o' }, { 0xF5, 0, 'o' },
    { 0xF9, 133, 'u' }, { 0xFA, 134, 'u' }, { 0xFB, 135, 'u' }, { 0xFC, 136, 'u' },
    { 0xC7, 137, 'C' }, { 0xE7, 138, 'c' }, { 0xD1, 139, 'N' }, { 0xF1, 140, 'n' }, { 0xAA, 141, 'a' },
    { 0xA1, 142, '!' }, { 0xBF, 143, '?' }, { 0xE6, 144, 'a' }, { 0x152, 145, 'O' }, { 0x153, 146, 'o' },
    { 0xAE, 147, 0 }, { 0x2122, 148, 0 }, { 0xB0, 149, 0 }, { 0xAB, 0, '"' }, { 0xBB, 0, '"' }, { 0x2019, 0, '\'' },
    { 0x2026, 0, '.' }, { 0x2013, 0, '-' }, { 0x2014, 0, '-' }, { 0xA0, 0, ' ' },
    { 0x11E, 0, 'G' }, { 0x11F, 0, 'g' }, { 0x130, 0, 'I' }, { 0x131, 0, 'i' }, { 0x15E, 0, 'S' }, { 0x15F, 0, 's' },
};
#define N_ACCENTS (int)(sizeof(accents) / sizeof(accents[0]))

/* the Russian font: А..я (U+0410..U+044F) and Ё ё; a Latin letter's code for the ones that look alike */
static const unsigned char ru_upper[32] = {   /* А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я */
    33, 102, 34, 103, 104, 37, 106, 107, 108, 109, 110, 111, 45, 40, 47, 112,
    48, 35, 52, 113, 114, 56, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124 };
static const unsigned char ru_lower[32] = {   /* а .. я */
    65, 125, 126, 127, 128, 69, 130, 131, 132, 133, 134, 135, 136, 137, 79, 138,
    80, 67, 139, 89, 140, 88, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150 };
static const struct { unsigned short u, code; } ru_signs[] = {
    { 0x401, 105 }, { 0x451, 129 }, { 0xAB, 100 }, { 0xBB, 101 }, { 0x2116, 98 }, { 0xAE, 95 }, { 0xB0, 96 }, { 0x2013, 97 }, { 0x2014, 97 },
};

/* the Turkish font: its letters in the European font's cells, and the European letters those cells lost */
static const struct { unsigned short u, code; } tr_letters[] = { { 0x11E, 102 }, { 0x130, 107 }, { 0x15E, 109 }, { 0x15F, 124 }, { 0x131, 126 }, { 0x11F, 140 } };
static const unsigned short tr_lost[] = { 0xCA, 0xCF, 0xD3, 0xEB, 0xED, 0xF1 };
/* the Brazilian Portuguese font (the US one's ASCII but [ ] ~, which are ° and blanks) */
static const struct { unsigned short u, code; } pt_letters[] = {
    { 0xF5, 96 }, { 0xF3, 97 }, { 0xF4, 98 }, { 0xE2, 99 }, { 0xE3, 100 }, { 0xE1, 101 }, { 0xE0, 102 }, { 0xEA, 103 },
    { 0xE9, 104 }, { 0xED, 105 }, { 0xFA, 106 }, { 0xE7, 107 }, { 0xC7, 108 }, { 0xB0, 59 }, { 0xAE, 95 } };
#define PT_SPACER 94   /* the blank after Ç (108: no width) */

/* the Arabic font (the fan translation's, in the European font's place): its cells per letter as the Unicode
   presentation forms order them (isolated, final, initial, medial; 0: the font has none), the first form's code point;
   no Latin letters (the US font draws them: codes below LANG_AR_BASE) */
static const struct { unsigned short u, pf; unsigned char cell[4]; } ar_letters[] = {
    { 0x621, 0xFE80, { 10 } },                 { 0x622, 0xFE81, { 0, 145 } },           { 0x623, 0xFE83, { 13, 144 } },
    { 0x624, 0xFE85, { 128, 129 } },           { 0x625, 0xFE87, { 8, 146 } },           { 0x626, 0xFE89, { 139, 140, 142, 141 } },
    { 0x627, 0xFE8D, { 11, 143 } },            { 0x628, 0xFE8F, { 147, 31, 148, 149 } }, { 0x629, 0xFE93, { 126, 127 } },
    { 0x62A, 0xFE95, { 32, 35, 33, 34 } },     { 0x62B, 0xFE99, { 36, 39, 37, 38 } },   { 0x62C, 0xFE9D, { 40, 43, 41, 42 } },
    { 0x62D, 0xFEA1, { 44, 47, 45, 46 } },     { 0x62E, 0xFEA5, { 48, 51, 49, 50 } },   { 0x62F, 0xFEA9, { 71, 53 } },
    { 0x630, 0xFEAB, { 54, 55 } },             { 0x631, 0xFEAD, { 56, 57 } },           { 0x632, 0xFEAF, { 58, 59 } },
    { 0x633, 0xFEB1, { 60, 7, 61, 62 } },      { 0x634, 0xFEB5, { 64, 67, 65, 66 } },   { 0x635, 0xFEB9, { 68, 52, 69, 70 } },
    { 0x636, 0xFEBD, { 72, 75, 73, 74 } },     { 0x637, 0xFEC1, { 76, 79, 77, 78 } },   { 0x638, 0xFEC5, { 80, 83, 81, 82 } },
    { 0x639, 0xFEC9, { 84, 87, 85, 86 } },     { 0x63A, 0xFECD, { 88, 91, 89, 90 } },   { 0x641, 0xFED1, { 92, 95, 93, 94 } },
    { 0x642, 0xFED5, { 96, 99, 97, 98 } },     { 0x643, 0xFED9, { 100, 103, 101, 102 } }, { 0x644, 0xFEDD, { 104, 107, 105, 106 } },
    { 0x645, 0xFEE1, { 108, 111, 109, 110 } }, { 0x646, 0xFEE5, { 112, 115, 113, 114 } }, { 0x647, 0xFEE9, { 116, 119, 117, 118 } },
    { 0x648, 0xFEED, { 120, 121 } },           { 0x649, 0xFEEF, { 130, 131 } },         { 0x64A, 0xFEF1, { 122, 125, 123, 124 } },
};
#define N_AR (int)(sizeof(ar_letters) / sizeof(ar_letters[0]))
/* lam + alef: one glyph (isolated, final) */
static const struct { unsigned short alef, pf; unsigned char cell[2]; } ar_lamalef[] = {
    { 0x622, 0xFEF5, { 138, 0 } }, { 0x623, 0xFEF7, { 134, 135 } }, { 0x625, 0xFEF9, { 136, 137 } }, { 0x627, 0xFEFB, { 132, 133 } } };
/* signs: the Arabic question mark, tatweel; ASCII in the Arabic font's own cells (the same as the US codes) */
static const struct { unsigned short u; unsigned char cell; } ar_signs[] = { { 0x61F, 9 }, { 0x640, 63 } };
/* the US codes drawn with the Arabic font: the signs the US font has there too (28 the choice cursor: ◀ here, the patch
   writes the choice after its word); digits and punctuation are the US font's (Latin runs: seed strings, sizes) */
static int ar_own(int code) { return code >= 28 && code <= 30; }

/* the Arabic font's cell of a character (presentation form, base letter: its isolated form), 0 none */
static int ar_cell(unsigned c)
{
    for (int i = 0; i < N_AR; i++) {
        if (c == ar_letters[i].u) return ar_letters[i].cell[0] ? ar_letters[i].cell[0] : ar_letters[i].cell[1];
        if (c >= ar_letters[i].pf && c < ar_letters[i].pf + (ar_letters[i].cell[2] ? 4u : 2u)) return ar_letters[i].cell[c - ar_letters[i].pf];
    }
    for (int i = 0; i < 4; i++) if (c >= ar_lamalef[i].pf && c < ar_lamalef[i].pf + 2u) return ar_lamalef[i].cell[c - ar_lamalef[i].pf];
    for (size_t i = 0; i < sizeof(ar_signs) / sizeof(ar_signs[0]); i++) if (ar_signs[i].u == c) return ar_signs[i].cell;
    return c >= 0x660 && c <= 0x669 ? 16 + (int)(c - 0x660) : 0;   /* (Arabic-Indic digits: the font's 0-9) */
}
/* the letter to show instead of a form the font lacks (the alef madda alone, final lam-alef madda: plain alef / lam) */
static unsigned ar_plain(unsigned c) { return c == 0xFE81 || c == 0x622 ? 0x627 : c == 0xFEF6 ? 0xFEFC : 0; }

/* the Japanese font's cells in order (\001: a sign without a character: S. T. A. R. W. B. O., the biohazard sign,
   crown, heart, spade, Umbrella, "AA") */
static const char ja_cells[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz▶▼"
        "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんがぎぐげござじず"
        "ぜぞだ霊づでどばびぶべぼぱぴぷ嬢嬉ぁぃぅぇぉゃゅょっアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフ"
        "ヘホマミムメモヤユヨラリルレロワ版ン局ガギグゲゴザジズゼゾダ讐恨デドバビブベボパピプペポァィゥェォャュョッ／"
        "0123456789－？！．・ーαΣ（）「」＋〜○％×勢垂西街正油京\001\001活\001希拾窓推\001病蝕払規項凍赴氷\001提願"
        "懐暇遊拡飲太房陥勤：’卿協利権優秀適亡輝那威揮因”幼詫困越賭没宮賎涯架塗\001\001\001\001\001\001\001\001含蓄混融盛躍耐系狙榴"
        "後編開始南海孤島製日便行大古照返査絵監一供小型給宿走男操舵室双眼舎見俺知合旧者受来二前生今助下線部分釜会記雑"
        "誰突込…友達事実加溜外切汝近姿潜望庇映像台護処声付中方水艦刑壁停泊内糊手力所貴様再不幸刃度魚雷発射可能撃準備"
        "欲乗組員良思資想定改積必要時代当然子械納用意角言複物画橋隊早教書指医人務持重作業遅別押奇妙音白療立真文字向飛"
        "出同木端臭模欠口赤面終了軍訓練鍾乳洞拷磨兜掲上鎧息荒歓迎剣我聖何械第暗広墓地探棟場公昔住違認渡無最初話石銀掛"
        "座毛顔弱々女胸景心配逃機多脱妹世礼残追刻屋連続肖取陶皿兄彼飾虫現燭隣歴戦史辿細料企議壷腹登労暴視普通完全賞袋"
        "殿裂績共豪抱家族死貰華丁落判絶悪運遺体置既楽戻親気馬鹿設高結果自失敗感相対好演象得応以奏学決政暖番難許炉術待"
        "棒間種類神素材十玩味強引勉卒成調更博招消化跡形入奴綺麗百色削除科典攻施司令頑少嫌漏響巨防空港鉄菌崩注壊状態例"
        "予目覚搬影盤回動制参背数絡蓋匂野漂光閉送転翼告放昇天井張穀証法頼変並電源点描写情報基棄打輸極進陸縦路誘導官専"
        "駐搭信使止的棒単薄廊先棚茶怪途明滅鳴主観爆破濁建火北鉢側呼移径燃炸炎増避階倉庫降原暮倒振駆頭荷洋館凶仮称伏隙"
        "反襲苦警滑銃久拳英故居織雇奪構娘直液吐離次着格負剤片掴憎砕新魂売驚横恐右吹跳冥土産念任若都似怒預考義殺戚退散"
        "理弟快確寄展温命焼首興冗車扉談紋章窪噴奥器脅伸表示鷲弾速痕保限界骨問貪起丸食盾疫熱雪貫恰障月聞長蟻飼趣溺愛才"
        "年研究責頃支夢語王兵換本姉毎私邸嚇柱皆級将眠特伝祖植父験試採功名低工冬校仲胞球守身委門君臨誌黙籠鳥断弄笑辺闘"
        "技吊安廃坑休穴憩交騙踏割獄歯尽劣性質掃勿末仕排管平余計盗頂懸抜去揃関宝忌輪集惑駄係遣猿測毒帰徐曲灯捜服解秘索"
        "装市裏察未読貼御借乾鎮捕填硫説酸矢干救緑鮮葉青緒船元血清薬雌簡銅存在隠況鈍寝載根案厳具危鍵丈夫晶椅縛鉢寸足緯"
        "戒陰郎捧執溶接雄継承詞花号申請紙美歳免印祝塚黄異程束週容留急羽昆過満武金属禁及堅為偶量栄届培養有常遂抵抗倍値"
        "録旦脳効価敵撒染期誕習非社統段従流各争邪魔傷復超整星河森育災蛾巣充替個虎磁筈顕微鏡経約鷹役固薔薇険他際蜂純万"
        "封創鎖側道塞与軒剖精巧姓緊位差勲陳治列厚膨勘就汚玉吸販詰濃識槽艇扱掘循環砲三収囚般逆敷等士錠洗牢獲侵品由迷勝"
        "院稼蒸兼暑折標網叩悲圧繰益些涙昨夜偵机件訪黒担狂耳叫捨国殊冷忘汗造掻席震蘇悔怖酷瞳図庭左髪乱痛衛塔歪式激痙両"
    ;
#define JA_CELLS 1296
/* the Korean fan translation's font (\001 also: the blank cell its text writes as a wide space); its Hangul are the
   syllables the patch's text uses */
static const char ko_cells[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz▶▼"
        "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをんがぎぐげござじず"
        "ぜぞだ컨컴でどばびぶべ흐ぱ몰ぷ몸몹ぁ앨못ぇ몽ゃ♡ょっアイウエオカキクケコサシスセ즉タチ즌テトナニ!ネノハ컷フ"
        "묘즐マ케ムメモ지ユ\"ラリルレロ켓(ン局ガ표グ무ゴザジ묵묶직ダ야문デドバビブベボパ푸プ),ァィーェォャュ묻ッ."
        "0123456789－？！．・ー:製풀품「」[]・％Σα○물진흑油질켜약뭐양「뭔」가어각간갇갈감억갑값\001갔강"
        "갖언같개거걱건걸검：’게겠겨격얻뭘견므결겹겼미경곁얼민계믿고곡곤곧골곳밀짐풍퓨프흔플\001\001공과관광괜밑괴교구국"
        "後바開始南박피島製코엄行大古照콜査絵쿠一供小쿤給업군男操굴室흘밖없見俺知合퀴흡집来크前生今助下엇部分궁会흥픽"
        "誰큭込…반達事実加권外切받필발짓밝귀흩像台었징에付中方水짜희壁停균内그手力所貴様핏不방배度극근発히可能撃準備"
        "힌乗째員백思하밸定뱃積必要時代当버子械쨌用意학言번物画橋쩌早쩍書힘벌人겁持重作業엘別押奇妙글쪽금立真文엠向飛"
        "出同범것법模급口赤여終了큰한\001긋기긴긷길벗쫓上베息까깐깜깨我꺄何꺅벨할꺼墓地함껏場역껑클합認연無最쯤항石銀掛"
        "열毛顔께々女胸景心配逃機多脱妹世礼残追刻屋連해肖핸陶염兄彼飾찍現찢였차戦착껴벼찬영꼭키꽂벽꽃꽤킨꿈通完全꿔찮"
        "뀌옆끄했豪예家族死킬華타落끊絶悪運변体置끌행戻親気별끗향옛오병自끝끼感탁対허보탄옥応以탈学決탐찰복難나참온待"
        "본間올類볼素材창봉味봐引난날成調봤낡남消化跡形入奴부분불色찾除붉붙攻채탓태브택블漏납옷험空港鉄와탱注壊状책났"
        "비目터헤빈盤回動制낮턴챙빛蓋내빠낸光閉送혀낼완처昇왔왕빡빨냈텀텅変並電源点描현情報基냐打輸極進빼빽테냥뺄혈왜"
        "너搭혔使止텐넉単외왼先요템뼈뿐쁜滅욕主토爆破사용火넌널側呼移넘척넣우네避階倉庫降녀산倒년살념荷삽상녕노녹놀욱"
        "反톡운警놈형울새농움높천톱놓웃워색샘놔철생着格원호뇽누첨눈통월혹눌첫恐右눠뉴느는늘念任샷능서웠석늦늬투섞退혼"
        "理니닌確선展청트설닐웨님車扉닛특홀窪다위器단닫튼示달弾유섬保닮육담답당起닿율성으홈세대더윽체은長蟻덕쳐던덤덮"
        "틀研究데델도독돌王동換本화돼私을됐음되응의眠特쳤이된父験셈인될名티工됩일셋두둔파身둘확읽둠셔잃둡임션둥뒀입초"
        "셨소安잇뒤속穴뒷총드손있잉득잊든性자작들末仕잔管송쇄최쇠듯잖抜추수関宝숙등잘순디환딘따잠毒축딱출灯활판解땅때"
        "装떠춤잡술황貼御충숨숫회획떤쉘떨떻떼쉬팔뗄또青팜똑元血춰薬뚜簡뚫쉽在슈況뛰뜨뜬뜻장厳具危鍵丈夫라슐재락란足스"
        "랄람슨쟁랍저취랑래랜램효슬습랩랫美랭량적전러럭異승束시容런急럴식측満층金属패럼럽신렀量후절실싫有常렇레렉렐팩"
        "려력련렷렸敵령치례로록친침롤段롭流심십젊점카칸롯팬롱싶싸싹접뢰퍼充替료칼싼루정젖제룩約젠役固룸류険他펄륙캐륭"
        "르젤져른道塞캔쌍쌓써쏘를緊졌조쏠쏴족쑤존름릅쓰훈릇리좀종린릴캘좋좌훌쓸씨아휴편캡죄죠주악림폐안립앉않品알죽압"
        "링준마줄막만았앞많애圧말맙망맞맡매맨机맷머먹먼멀커멈捨중殊줘액멋앤멍멎메줬멘怖멤며図면左쥐흉멸명모목式포몬폭"
    ;

/* the US font's codes below LANG_JA_BASE drawn with the Japanese font: the look-alike cell (-1 none) */
static const struct { unsigned char code; short cell; } ja_signs[] = {
    { 3, 306 }, { 4, 307 }, { 6, 308 }, { 10, 309 }, { 93, 310 }, { 30, 311 }, { 28, 52 }, { 29, 53 }, { 32, 240 },
    { 60, 242 }, { 62, 230 }, { 91, 232 }, { 92, 233 }, { 94, 239 }, { 2, 294 }, { 7, 280 },
};
/* ... with the Korean font (its signs moved; no biohazard, crown, heart, spade, Umbrella, "AA", ×, 〜) */
static const struct { unsigned char code; short cell; } ko_signs[] = {
    { 28, 52 }, { 29, 53 }, { 32, 244 }, { 62, 240 }, { 91, 243 }, { 92, 242 }, { 2, 171 }, { 7, 280 },
};
typedef struct { unsigned u; short cell; } CjkChar;
typedef struct Cjk {
    const char* cells;
    const struct { unsigned char code; short cell; } * signs;
    int nsigns, n;
    CjkChar* index;              /* the cells' characters, sorted (heap) */
    short alias[LANG_JA_BASE];
} Cjk;
static Cjk cjk[2] = { { ja_cells, (const void*)ja_signs, sizeof(ja_signs) / sizeof(ja_signs[0]) },
                      { ko_cells, (const void*)ko_signs, sizeof(ko_signs) / sizeof(ko_signs[0]) } };
static Cjk* cjk_of(int l) { return &cjk[l == LANG_KO]; }

static unsigned utf8_next(const char** s)
{
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = *p++;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (c >= 0x80 && !n) c = '?';
    else if (n) c &= 0x3F >> n;
    for (; n && (*p & 0xC0) == 0x80; n--) c = c << 6 | (*p++ & 0x3F);
    if (n) c = '?';
    *s = (const char*)p;
    return c;
}
static int cmp_u(const void* a, const void* b) { unsigned x = *(const unsigned*)a, y = *(const unsigned*)b; return x < y ? -1 : x > y; }

static Cjk* cjk_init(int l)
{
    Cjk* f = cjk_of(l);
    const char* s = f->cells;
    if (f->index || !(f->index = malloc(JA_CELLS * sizeof(*f->index)))) return f;
    for (int c = 0; c < JA_CELLS && *s; c++) {
        unsigned u = utf8_next(&s);
        int dup = 0;
        for (int k = 0; k < f->n && !dup; k++) dup = f->index[k].u == u;   /* (a few kanji twice: the first) */
        if (u != 1 && !dup) { f->index[f->n].u = u; f->index[f->n++].cell = (short)c; }
    }
    qsort(f->index, f->n, sizeof(*f->index), cmp_u);
    for (int c = 0; c < LANG_JA_BASE; c++) {
        unsigned u = c < 95 ? (unsigned)c + 32 : 0;
        int cell = -1;
        for (int k = 0; k < f->nsigns; k++) if (f->signs[k].code == c) cell = f->signs[k].cell;
        for (int k = 0; cell < 0 && u > 32 && k < f->n; k++)   /* the character, else its full-width form */
            if (f->index[k].u == u || f->index[k].u == u + 0xFEE0) cell = f->index[k].cell;
        f->alias[c] = (short)cell;
    }
    return f;
}
static int cjk_find(int l, unsigned u)
{
    Cjk* f = cjk_init(l);
    int lo = 0, hi = f->index ? f->n : 0;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (f->index[mid].u == u) return f->index[mid].cell;
        if (f->index[mid].u < u) lo = mid + 1; else hi = mid;
    }
    return -1;
}

int lang_cell(int l, int code)
{
    if (l == LANG_AR) return code >= LANG_AR_BASE ? (code - LANG_AR_BASE < 324 ? code - LANG_AR_BASE : -1) : ar_own(code) ? code : -1;   /* (-1: the US font's) */
    if (!lang_cjk(l)) return code;
    if (code >= LANG_JA_BASE) return code - LANG_JA_BASE < JA_CELLS ? code - LANG_JA_BASE : -1;
    return code >= 0 ? cjk_init(l)->alias[code] : -1;   /* (Korean: -1 is the US font's letter) */
}

/* the code of character c in language l's font; -1 none (*plain: the letter to show instead, 0 none) */
static int code_of(int l, unsigned c, unsigned* plain)
{
    int lost = 0;
    *plain = 0;
    if (c == ' ' || c == 0x3000 || (l == LANG_JA && (c == 0x3001 || c == ','))) return 0;   /* (Japanese: 、 and , as a space, as the game's text) */
    if (lang_cjk(l)) {
        int cell;
        if (c == 0x3002) c = 0xFF0E;   /* 。: the game's ． */
        if (c > 32 && c < 127) return lang_cell(l, (int)c - 32) >= 0 || l == LANG_KO ? (int)c - 32 : -1;   /* (Korean: the US font's) */
        if ((cell = cjk_find(l, c)) >= 0) return LANG_JA_BASE + cell;
        if (c >= 0xFF01 && c <= 0xFF5E && lang_cell(l, (int)c - 0xFEE0 - 32) >= 0) return (int)c - 0xFEE0 - 32;
        for (int i = 0; i < N_ACCENTS; i++) if (accents[i].u == c) *plain = (unsigned char)accents[i].plain;
        return -1;
    }
    if (l == LANG_PT) {
        for (size_t i = 0; i < sizeof(pt_letters) / sizeof(pt_letters[0]); i++) if (pt_letters[i].u == c) return pt_letters[i].code;
        if (c == '[' || c == ']' || c == '~') { *plain = c == '[' ? '(' : c == ']' ? ')' : '-'; return -1; }
    }
    if (c > 32 && c < 127) return (int)c - 32;
    if (l == LANG_AR) {
        int cell = ar_cell(c);
        if (cell) return LANG_AR_BASE + cell;
        if ((*plain = c == 0x60C ? ',' : ar_plain(c))) return -1;   /* (the Arabic comma: the Latin one) */
    }
    if (l == LANG_TR) {
        for (size_t i = 0; i < sizeof(tr_letters) / sizeof(tr_letters[0]); i++) if (tr_letters[i].u == c) return tr_letters[i].code;
        for (size_t i = 0; i < sizeof(tr_lost) / sizeof(tr_lost[0]); i++) lost |= tr_lost[i] == c;   /* (its cell is a Turkish letter: plain) */
    }
    if (l == LANG_RU) {
        if (c >= 0x410 && c < 0x430) return ru_upper[c - 0x410];
        if (c >= 0x430 && c < 0x450) return ru_lower[c - 0x430];
        for (size_t i = 0; i < sizeof(ru_signs) / sizeof(ru_signs[0]); i++) if (ru_signs[i].u == c) return ru_signs[i].code;
    }
    for (int i = 0; i < N_ACCENTS; i++) {
        if (accents[i].u != c) continue;
        if ((l == LANG_FR || l == LANG_DE || l == LANG_ES || l == LANG_IT || (l == LANG_TR && !lost)) && accents[i].code) return accents[i].code;
        if (c == 0xAE && l == LANG_EN) return 95;   /* (the US font's ®) */
        *plain = (unsigned char)accents[i].plain;
        return -1;
    }
    return -1;
}

int lang_glyph(int l, const char** s)
{
    unsigned c, plain;
    int k;
    if (!**s) return -1;
    /* ponytail: Brazilian Portuguese Ç is two codes (108, then the blank PT_SPACER): the first call stops on Ç's second
       byte, the next one gives the blank */
    if (l == LANG_PT && (unsigned char)**s == 0x87 && (unsigned char)(*s)[-1] == 0xC3) { (*s)++; return PT_SPACER; }
    c = utf8_next(s);
    if (l == LANG_PT && c == 0xC7) { (*s)--; return 108; }
    if ((k = code_of(l, c, &plain)) >= 0) return k;
    if (plain && (k = code_of(l, plain, &plain)) >= 0) return k;
    return code_of(l, '?', &plain) >= 0 ? code_of(l, '?', &plain) : 0;
}

void lang_missing(int l, const char* s, char* out, int n)
{
    static char sh[4096];   /* (pure, single threaded: the tests and the tools) */
    int k = 0;
    out[0] = 0;
    if (l == LANG_AR) s = lang_shape(s, sh, sizeof(sh));   /* (the forms the text shows) */
    while (*s) {
        const char* a = s;
        unsigned c = utf8_next(&s), plain;
        char one[8] = { 0 };
        if (c < 32 || code_of(l, c, &plain) >= 0) continue;
        memcpy(one, a, (size_t)(s - a) < 7 ? (size_t)(s - a) : 7);
        if (strstr(out, one) || k + (int)strlen(one) >= n) continue;   /* (each once) */
        strcpy(out + k, one);
        k += (int)strlen(one);
    }
}

unsigned lang_char(int l, int code)
{
    if (lang_cjk(l) && code >= LANG_JA_BASE) {
        const char* s = cjk_of(l)->cells;
        unsigned u = '?';
        for (int c = 0; c <= code - LANG_JA_BASE && *s; c++) u = utf8_next(&s);
        return u == 1 ? '?' : u;
    }
    if (l == LANG_PT) {   /* (the blanks: 61 thin, 94 after Ç; 109..112 the accents the patch sets over capitals) */
        for (size_t i = 0; i < sizeof(pt_letters) / sizeof(pt_letters[0]); i++) if (pt_letters[i].code == code) return pt_letters[i].u;
        if (code == 61 || code == PT_SPACER || (code > 108 && code <= 112)) return code == 61 ? ' ' : 0;
    }
    if (l == LANG_AR && code >= LANG_AR_BASE) {
        int cell = code - LANG_AR_BASE;
        for (int i = 0; i < N_AR; i++)
            for (int k = 0; k < 4; k++) if (ar_letters[i].cell[k] == cell) return ar_letters[i].pf + k;
        for (int i = 0; i < 4; i++) for (int k = 0; k < 2; k++) if (ar_lamalef[i].cell[k] == cell) return ar_lamalef[i].pf + k;
        for (size_t i = 0; i < sizeof(ar_signs) / sizeof(ar_signs[0]); i++) if (ar_signs[i].cell == cell) return ar_signs[i].u;
        return cell == 12 ? 0xFE87 : cell == 1 || (cell >= 14 && cell <= 26) ? (unsigned)cell + 32 : '?';
    }
    if (code < 95) return code ? (unsigned)code + 32 : ' ';
    if (l == LANG_RU) {
        for (int i = 0; i < 32; i++) { if (ru_upper[i] == code) return 0x410 + i; if (ru_lower[i] == code) return 0x430 + i; }
        for (size_t i = 0; i < sizeof(ru_signs) / sizeof(ru_signs[0]); i++) if (ru_signs[i].code == code) return ru_signs[i].u;
        return '?';
    }
    if (l == LANG_PT) return '?';
    if (l == LANG_TR) for (size_t i = 0; i < sizeof(tr_letters) / sizeof(tr_letters[0]); i++) if (tr_letters[i].code == code) return tr_letters[i].u;
    if (l == LANG_EN || lang_cjk(l) || l == LANG_AR) return code == 95 ? 0xAE : '?';   /* (the US codes) */
    for (int i = 0; i < N_ACCENTS; i++) if (accents[i].code == code) return accents[i].u;
    return '?';
}

/* ---------------------------------------------------------------- Arabic display order (lang.h lang_shape)
   ponytail: a small subset of the Unicode bidi algorithm and of Arabic shaping, enough for our menu texts: one
   right-to-left paragraph per line, strong classes only (Arabic R; Latin letters, digits, printf placeholders L; the
   rest neutral: L between two L, else R), no explicit embeddings, no number-separator rules, mirrored brackets ()[]{}<>
   only; diacritics dropped (the font has none); at most AR_LINE characters a line (the rest cut). Upgrade: FriBidi /
   HarfBuzz if the texts ever need more. Main thread (tr_init) and the tests: static buffers. */
#define AR_LINE 1024
static int ar_index(unsigned c) { for (int i = 0; i < N_AR; i++) if (ar_letters[i].u == c) return i; return -1; }
/* joining: 2 both sides (dual), 1 to the letter before only (right-joining), 0 none */
static int ar_join(unsigned c)
{
    int i = ar_index(c);
    if (c == 0x640) return 2;   /* (tatweel) */
    if (i < 0 || c == 0x621) return 0;
    return ar_letters[i].cell[2] ? 2 : 1;   /* (the font's forms: alef maksura only isolated and final) */
}
static int ar_strong(unsigned c) { return (c >= 0x600 && c <= 0x6FF) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }

static int put_utf8(char* o, int k, int n, unsigned c)
{
    char b[4];
    int m = 1;
    if (c < 0x80) b[0] = (char)c;
    else if (c < 0x800) { b[0] = (char)(0xC0 | c >> 6); b[1] = (char)(0x80 | (c & 0x3F)); m = 2; }
    else { b[0] = (char)(0xE0 | c >> 12); b[1] = (char)(0x80 | (c >> 6 & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); m = 3; }
    if (k + m >= n) return k;
    memcpy(o + k, b, (size_t)m);
    return k + m;
}

/* one line (len bytes of s) in display order into o from k (n bytes); returns the new end */
static int shape_line(const char* s, int len, char* o, int k, int n)
{
    static unsigned cp[AR_LINE];
    static char cls[AR_LINE];
    const char* e = s + len;
    int m = 0, any = 0;
    while (s < e && m < AR_LINE) {   /* the code points, without diacritics */
        unsigned c = utf8_next(&s);
        if ((c >= 0x64B && c <= 0x65F) || c == 0x670) continue;
        any |= ar_strong(c);
        cp[m++] = c;
    }
    if (!any) { for (int i = 0; i < m; i++) k = put_utf8(o, k, n, cp[i]); return k; }
    for (int i = 0, before = 0; i < m; i++) {   /* the forms (logical order: the letter before, `before` its joining, and after) */
        int j = ar_join(cp[i]), ix = ar_index(cp[i]), prev = before == 2 && j, next, f;
        before = j;
        if (ix < 0) continue;
        if (cp[i] == 0x644 && i + 1 < m) {   /* lam-alef: one glyph */
            int a = -1;
            for (int t = 0; t < 4; t++) if (ar_lamalef[t].alef == cp[i + 1]) a = t;
            if (a >= 0) { cp[i] = ar_lamalef[a].pf + (unsigned)prev; cp[++i] = 0; before = 1; continue; }
        }
        next = j == 2 && i + 1 < m && ar_join(cp[i + 1]);
        f = prev && next ? 3 : prev ? 1 : next ? 2 : 0;
        cp[i] = ar_letters[ix].pf + (unsigned)f;
    }
    {   /* (without the lam-alefs' alefs) */
        int w = 0;
        for (int i = 0; i < m; i++) if (cp[i]) cp[w++] = cp[i];
        m = w;
    }
    for (int i = 0; i < m;) {   /* classes: R Arabic, L Latin letters, digits, other letters and placeholders, N the rest */
        int len_ = 1;
        unsigned c = cp[i];
        if (c == '%' && i + 1 < m) {   /* %[flags][width][.precision][length]conversion: one L */
            int j = i + 1;
            while (j < m && cp[j] && cp[j] < 128 && strchr("-+ #0123456789.*", (int)cp[j])) j++;
            while (j < m && cp[j] && cp[j] < 128 && strchr("hlLqjzt", (int)cp[j])) j++;
            if (j < m && cp[j] < 128 && (((cp[j] | 32) >= 'a' && (cp[j] | 32) <= 'z') || cp[j] == '%')) len_ = j + 1 - i;
        }
        if (len_ > 1) cls[i] = 'L';
        else if (ar_strong(c)) cls[i] = 'R';
        else if (c >= 128 || ((c | 32) >= 'a' && (c | 32) <= 'z') || (c >= '0' && c <= '9')) cls[i] = 'L';
        else cls[i] = 'N';
        for (int t = 1; t < len_; t++) cls[i + t] = 'L';
        i += len_;
    }
    {   /* bracket pairs (bidi N0, simplified): both sides R if they enclose Arabic, L if they enclose only Latin after Latin */
        static const char br[] = "()[]{}";
        int st[16], d = 0;
        for (int i = 0; i < m; i++) {
            const char* p = cp[i] && cp[i] < 128 && cls[i] == 'N' ? strchr(br, (int)cp[i]) : NULL;
            int o, r = 0, l = 0, before = 'R';
            if (!p) continue;
            if (!((p - br) & 1)) { if (d < 16) st[d++] = i; continue; }
            while (d > 0 && cp[st[d - 1]] != (unsigned char)p[-1]) d--;   /* (its opener; unmatched ones dropped) */
            if (!d) continue;
            o = st[--d];
            for (int t = o + 1; t < i; t++) { r |= cls[t] == 'R'; l |= cls[t] == 'L'; }
            for (int t = o - 1; t >= 0; t--) if (cls[t] != 'N') { before = cls[t]; break; }
            if (r || l) cls[o] = cls[i] = r ? 'R' : before == 'L' ? 'L' : 'R';
        }
    }
    for (int i = 0; i < m;) {   /* neutrals: L between two L, else R (the paragraph's) */
        int j = i, l = 'R', r = 'R';
        if (cls[i] != 'N') { i++; continue; }
        while (j < m && cls[j] == 'N') j++;
        for (int t = i - 1; t >= 0; t--) if (cls[t] != 'N') { l = cls[t]; break; }
        if (j < m) r = cls[j];
        for (int t = i; t < j; t++) cls[t] = l == 'L' && r == 'L' ? 'L' : 'R';
        i = j;
    }
    for (int end = m; end > 0;) {   /* the runs from the last one: L as it is, R reversed (brackets mirrored) */
        int start = end;
        while (start > 0 && cls[start - 1] == cls[end - 1]) start--;
        if (cls[end - 1] == 'L') for (int i = start; i < end; i++) k = put_utf8(o, k, n, cp[i]);
        else
            for (int i = end - 1; i >= start; i--) {
                static const char mir[] = "()[]{}<>";
                const char* p = cp[i] && cp[i] < 128 ? strchr(mir, (int)cp[i]) : NULL;
                k = put_utf8(o, k, n, p ? (unsigned)(unsigned char)mir[(p - mir) ^ 1] : cp[i]);
            }
        end = start;
    }
    return k;
}

char* lang_shape(const char* s, char* out, int n)
{
    int k = 0;
    if (n <= 0) return out;
    while (*s) {
        const char* nl = strchr(s, '\n');
        int len = nl ? (int)(nl - s) : (int)strlen(s);
        k = shape_line(s, len, out, k, n);
        s += len;
        if (*s == '\n') { if (k + 1 < n) out[k++] = '\n'; s++; }
    }
    out[k] = 0;
    return out;
}

int lang_rtl(const char* s)
{
    for (; *s; s++) if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] >= 0xB9 && (unsigned char)s[1] <= 0xBB) return 1;   /* (U+FE40..U+FEFF) */
    return 0;
}

int lang_rtl_cut(const char* s, int len, float w, float (*width)(const char* s, int n, void* ud), void* ud)
{
    int last = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] == ' ' || (i && s[i - 1] != ' ')) continue;
        last = i;
        if (width(s + i, len - i, ud) <= w) return i;
    }
    return last;   /* (one word wider than w: alone) */
}
