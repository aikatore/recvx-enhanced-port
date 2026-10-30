/* The letters of the game fonts (lang.h; docs/LANGUAGES.md "Letters"): which glyph code shows a character in each
   language's font, for our own texts (pc_glyph_next) and the overlay (pc_glyphs_utf8). Pure: no game state
   (pc/tests/test_lang.c checks our translations against it).
     English  the US font: ASCII - 32 (0..94), ® 95, a few signs
     French, German, Spanish  the European font: ASCII, accents at 95..149
     Russian  the fan translation's font: ASCII, the Cyrillic letters that look Latin are the Latin ones, the others at
              95..150 (lang.c reads its widths from the patched executable)
     Japanese the Japanese font's 1296 cells (4 textures of 324) at LANG_JA_BASE + cell: kana, the kanji the game
              uses, full-width Latin, digits and signs; the codes below LANG_JA_BASE (the US font's, which the US
              engine writes) are drawn with the look-alike cells */
#include "lang.h"
#include <stdlib.h>
#include <string.h>

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

/* the US font's codes below LANG_JA_BASE drawn with the Japanese font: the look-alike cell (-1 none) */
static const struct { unsigned char code; short cell; } ja_signs[] = {
    { 3, 306 }, { 4, 307 }, { 6, 308 }, { 10, 309 }, { 93, 310 }, { 30, 311 }, { 28, 52 }, { 29, 53 }, { 32, 240 },
    { 60, 242 }, { 62, 230 }, { 91, 232 }, { 92, 233 }, { 94, 239 }, { 2, 294 }, { 7, 280 },
};
static struct { unsigned u; short cell; } * ja_index;   /* the cells' characters, sorted (heap) */
static short ja_alias[LANG_JA_BASE];
static int ja_n;

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

static void ja_init(void)
{
    const char* s = ja_cells;
    if (ja_index || !(ja_index = malloc(JA_CELLS * sizeof(*ja_index)))) return;
    for (int c = 0; c < JA_CELLS && *s; c++) {
        unsigned u = utf8_next(&s);
        int dup = 0;
        for (int k = 0; k < ja_n && !dup; k++) dup = ja_index[k].u == u;   /* (a few kanji twice: the first) */
        if (u != 1 && !dup) { ja_index[ja_n].u = u; ja_index[ja_n++].cell = (short)c; }
    }
    qsort(ja_index, ja_n, sizeof(*ja_index), cmp_u);
    for (int c = 0; c < LANG_JA_BASE; c++) {
        unsigned u = c < 95 ? (unsigned)c + 32 : 0;
        int cell = -1;
        for (size_t k = 0; k < sizeof(ja_signs) / sizeof(ja_signs[0]); k++) if (ja_signs[k].code == c) cell = ja_signs[k].cell;
        for (int k = 0; cell < 0 && u > 32 && k < ja_n; k++)   /* the character, else its full-width form */
            if (ja_index[k].u == u || ja_index[k].u == u + 0xFEE0) cell = ja_index[k].cell;
        ja_alias[c] = (short)cell;
    }
}
static int ja_find(unsigned u)
{
    int lo = 0, hi;
    ja_init();
    hi = ja_n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (ja_index[mid].u == u) return ja_index[mid].cell;
        if (ja_index[mid].u < u) lo = mid + 1; else hi = mid;
    }
    return -1;
}

int lang_cell(int l, int code)
{
    if (l != LANG_JA) return code;
    ja_init();
    if (code >= LANG_JA_BASE) return code - LANG_JA_BASE < JA_CELLS ? code - LANG_JA_BASE : -1;
    return code >= 0 ? ja_alias[code] : -1;
}

/* the code of character c in language l's font; -1 none (*plain: the letter to show instead, 0 none) */
static int code_of(int l, unsigned c, unsigned* plain)
{
    *plain = 0;
    if (c == ' ' || c == 0x3000 || (l == LANG_JA && (c == 0x3001 || c == ','))) return 0;   /* (Japanese: 、 and , as a space, as the game's text) */
    if (l == LANG_JA) {
        int cell;
        if (c == 0x3002) c = 0xFF0E;   /* 。: the game's ． */
        if (c > 32 && c < 127) return lang_cell(l, (int)c - 32) >= 0 ? (int)c - 32 : -1;
        if ((cell = ja_find(c)) >= 0) return LANG_JA_BASE + cell;
        if (c >= 0xFF01 && c <= 0xFF5E && lang_cell(l, (int)c - 0xFEE0 - 32) >= 0) return (int)c - 0xFEE0 - 32;
        for (int i = 0; i < N_ACCENTS; i++) if (accents[i].u == c) *plain = (unsigned char)accents[i].plain;
        return -1;
    }
    if (c > 32 && c < 127) return (int)c - 32;
    if (l == LANG_RU) {
        if (c >= 0x410 && c < 0x430) return ru_upper[c - 0x410];
        if (c >= 0x430 && c < 0x450) return ru_lower[c - 0x430];
        for (size_t i = 0; i < sizeof(ru_signs) / sizeof(ru_signs[0]); i++) if (ru_signs[i].u == c) return ru_signs[i].code;
    }
    for (int i = 0; i < N_ACCENTS; i++) {
        if (accents[i].u != c) continue;
        if ((l == LANG_FR || l == LANG_DE || l == LANG_ES) && accents[i].code) return accents[i].code;
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
    c = utf8_next(s);
    if ((k = code_of(l, c, &plain)) >= 0) return k;
    if (plain && (k = code_of(l, plain, &plain)) >= 0) return k;
    return code_of(l, '?', &plain) >= 0 ? code_of(l, '?', &plain) : 0;
}

void lang_missing(int l, const char* s, char* out, int n)
{
    int k = 0;
    out[0] = 0;
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
    if (l == LANG_JA && code >= LANG_JA_BASE) {
        const char* s = ja_cells;
        unsigned u = '?';
        for (int c = 0; c <= code - LANG_JA_BASE && *s; c++) u = utf8_next(&s);
        return u == 1 ? '?' : u;
    }
    if (code < 95) return code ? (unsigned)code + 32 : ' ';
    if (l == LANG_RU) {
        for (int i = 0; i < 32; i++) { if (ru_upper[i] == code) return 0x410 + i; if (ru_lower[i] == code) return 0x430 + i; }
        for (size_t i = 0; i < sizeof(ru_signs) / sizeof(ru_signs[0]); i++) if (ru_signs[i].code == code) return ru_signs[i].u;
        return '?';
    }
    if (l == LANG_EN || l == LANG_JA) return code == 95 ? 0xAE : '?';   /* (the US codes) */
    for (int i = 0; i < N_ACCENTS; i++) if (accents[i].code == code) return accents[i].u;
    return '?';
}
