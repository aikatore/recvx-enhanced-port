/* Native saves: savestore.h. */
#include "savestore.h"
#include "fileio.h"
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Header {
    char magic[8];          /* "RECVXSAV" */
    uint32_t version;       /* 1 */
    uint32_t data_size;     /* SAVE_DATA_BYTES */
    char game[12];          /* "SLUS-20184" */
    uint32_t reserved;
    int64_t written;
    int32_t play_time, save_ct, gm_mode;
    uint8_t ply_id, stg_no, rom_no, pad;
    uint32_t pad2;
    uint32_t sum;           /* byte sum of the header before this field */
} Header;
_Static_assert(sizeof(Header) == SAVE_HEADER_BYTES, "header size");

static SDL_SpinLock lock;   /* writers: the game thread's saves, the main thread's F1 import */

static uint32_t byte_sum(const void* p, int n)
{
    const unsigned char* b = p;
    uint32_t s = 0;
    while (n--) s += *b++;
    return s;
}

static int32_t rd32(const unsigned char* d, int off) { int32_t v; memcpy(&v, d + off, 4); return v; }

int savestore_card_valid(const unsigned char* d)
{
    return rd32(d, 0) != 0 && (uint32_t)rd32(d, SAVE_DATA_BYTES - 4) == byte_sum(d, SAVE_DATA_BYTES - 4);   /* version 0: an empty slot */
}

/* the save menu's room names (its messages 216..233, by RoomNameSwitch: stage * 100 + room) */
static const struct { int code; const char* name; } rooms[] = {
    { 1, "Prison: front" }, { 108, "Palace: S.Room" }, { 311, "T Facility: RR" }, { 500, "T.P.: Cockpit" }, { 604, "Ant. Base: CR" },
    { 709, "T Facility: CR" }, { 722, "T Facility: UG" }, { 904, "Ant. Base: CR" }, { 914, "Ant. Base: SR" }, { 4, "Prison: Obs. R" },
    { 921, "Ant. Base: L" }, { 600, "Starting Part 2" }, { 724, "Chris's Story" }, { 404, "Airport: Elev." }, { 804, "Airport: Elev." },
    { 304, "T Facility: DR" }, { 702, "T Facility: DR" }, { 207, "Residence: SR" },
};

int savestore_text(char* out, const unsigned char* d)
{
    const unsigned char* t = d + SAVE_TEXT_OFS + 4;
    int i;
    out[0] = 0;
    if (memcmp(d + SAVE_TEXT_OFS, "LOC=", 4)) return 0;
    for (i = 0; i < SAVE_TEXT_BYTES - 4 && t[i]; i++)
        if (t[i] < 0x20 || t[i] == 0x7F) return 0;
    if (i == SAVE_TEXT_BYTES - 4) return 0;   /* (no end) */
    memcpy(out, t, i + 1);
    return i > 0;
}

void savestore_label(char* out, int n, const unsigned char* d)
{
    static const char* chr[] = { "Claire", "Chris", "Steve", "Wesker" };
    int code = d[9] * 100 + d[10];
    size_t i;
    char text[SAVE_TEXT_BYTES];
    if (savestore_text(text, d)) { snprintf(out, n, "%s_%s", chr[d[8] & 3], text); return; }
    for (i = 0; i < sizeof(rooms) / sizeof(rooms[0]); i++)
        if (rooms[i].code == code) { snprintf(out, n, "%s_%s", chr[d[8] & 3], rooms[i].name); return; }
    snprintf(out, n, "%s_%d-%02d", chr[d[8] & 3], d[9], d[10]);
}

/* length of the valid UTF-8 sequence at s (0: none) */
static int utf8_len(const unsigned char* s)
{
    int n = *s >= 0xF0 && *s <= 0xF4 ? 4 : *s >= 0xE0 ? 3 : *s >= 0xC2 && *s < 0xE0 ? 2 : 0;
    for (int i = 1; i < n; i++) if ((s[i] & 0xC0) != 0x80) return 0;
    return n;
}

void savestore_filename(char* out, int n, int number, const char* label)
{
    char safe[65];
    int k = 0;
    const unsigned char* s = (const unsigned char*)label;
    while (*s) {
        int u = *s >= 0x80 ? utf8_len(s) : 1;
        if (u > 1) {   /* a whole character or nothing */
            if (k + u > 64) break;
            memcpy(safe + k, s, u); k += u; s += u;
            continue;
        }
        if (k + 1 > 64) break;
        if ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || strchr("_-+()", *s)) safe[k++] = (char)*s;
        else if (k && safe[k - 1] != '-') safe[k++] = '-';   /* spaces, '.', path and reserved characters, control bytes, broken UTF-8 */
        s++;
    }
    while (k && safe[k - 1] == '-') k--;
    safe[k] = 0;
    snprintf(out, n, "%03d_%s.sav", number, k ? safe : "save");
}

/* the save number of a file name "NNN_....sav" (or NNN.sav), 0 if it is none */
static int file_number(const char* name)
{
    size_t len = strlen(name);
    int v = 0, digits = 0;
    if (len < 5 || strcmp(name + len - 4, ".sav")) return 0;
    for (; *name >= '0' && *name <= '9' && digits < 9; name++, digits++) v = v * 10 + (*name - '0');
    return digits && (*name == '_' || *name == '.') ? v : 0;
}

typedef struct Scan { int want, max; char* file; int n, found; SaveEntry* list; int count, cap, over; const char* dir; unsigned char* used; } Scan;

static int read_file(const char* path, unsigned char* buf, int cap)
{
    FILE* f = pc_fopen(path, "rb");
    int n;
    if (!f) return -1;
    n = (int)fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static int parse(const unsigned char* b, int n, unsigned char* data, Header* h)
{
    if (n != SAVE_HEADER_BYTES + SAVE_DATA_BYTES) return 0;
    memcpy(h, b, sizeof(*h));
    if (memcmp(h->magic, "RECVXSAV", 8) || h->version != 1 || h->data_size != SAVE_DATA_BYTES || memcmp(h->game, "SLUS-20184", 11) ||
        h->sum != byte_sum(h, offsetof(Header, sum)))
        return 0;
    b += SAVE_HEADER_BYTES;
    if (!savestore_card_valid(b) || h->save_ct != rd32(b, 4) || h->ply_id != b[8] || h->stg_no != b[9] || h->rom_no != b[10]) return 0;
    if (data) memcpy(data, b, SAVE_DATA_BYTES);
    return 1;
}

int savestore_read(const char* path, unsigned char* data)
{
    unsigned char b[SAVE_HEADER_BYTES + SAVE_DATA_BYTES + 1];
    Header h;
    return parse(b, read_file(path, b, sizeof(b)), data, &h);
}

static SDL_EnumerationResult SDLCALL scan_entry(void* ud, const char* dir, const char* name)
{
    Scan* s = ud;
    int num = file_number(name);
    if (!num) return SDL_ENUM_CONTINUE;
    if (num > s->max) s->max = num;
    if (s->used && num <= SAVESTORE_MAX + 1) s->used[num] = 1;
    if (num == s->want && !s->found) {
        s->found = 1;
        if (s->file) SDL_strlcpy(s->file, name, s->n);
    }
    if (s->dir) {   /* listing: read and check it */
        unsigned char b[SAVE_HEADER_BYTES + SAVE_DATA_BYTES + 1];
        char path[1400];
        Header h;
        SaveEntry* e;
        snprintf(path, sizeof(path), "%s%s", dir, name);   /* SDL's dir ends in a separator */
        if (s->count == SAVESTORE_MAX) { s->over++; return SDL_ENUM_CONTINUE; }
        if (!parse(b, read_file(path, b, sizeof(b)), NULL, &h)) { SDL_Log("saves: %s is not a valid save: skipped", name); return SDL_ENUM_CONTINUE; }
        if (s->count == s->cap) {
            SaveEntry* l = realloc(s->list, sizeof(SaveEntry) * (s->cap = s->cap ? s->cap * 2 : 32));
            if (!l) return SDL_ENUM_FAILURE;
            s->list = l;
        }
        e = &s->list[s->count++];
        e->number = num;
        SDL_strlcpy(e->file, name, sizeof(e->file));
        e->written = h.written; e->play_time = h.play_time; e->save_ct = h.save_ct; e->gm_mode = h.gm_mode;
        e->ply_id = h.ply_id; e->stg_no = h.stg_no; e->rom_no = h.rom_no;
        savestore_text(e->text, b + SAVE_HEADER_BYTES);
    }
    return SDL_ENUM_CONTINUE;
}

static int by_number(const void* a, const void* b)
{
    const SaveEntry *x = a, *y = b;
    return x->number != y->number ? (x->number < y->number ? -1 : 1) : strcmp(x->file, y->file);
}

int savestore_list(const char* dir, SaveEntry** out)
{
    Scan s = { .dir = dir };
    SDL_EnumerateDirectory(dir, scan_entry, &s);
    if (s.over) SDL_Log("saves: more than %d save files in %s: %d not listed", SAVESTORE_MAX, dir, s.over);
    if (s.count > 1) qsort(s.list, s.count, sizeof(SaveEntry), by_number);
    *out = s.list;
    return s.count;
}

int savestore_find(const char* dir, int number, char* file, int n)
{
    Scan s = { .want = number, .file = file, .n = n };
    SDL_EnumerateDirectory(dir, scan_entry, &s);
    return s.found;
}

int savestore_next(const char* dir)
{
    unsigned char used[SAVESTORE_MAX + 2] = { 0 };
    Scan s = { .used = used };
    int i;
    SDL_EnumerateDirectory(dir, scan_entry, &s);
    if (s.max < SAVESTORE_NUMBER_MAX) return s.max + 1;
    for (i = 1; i <= SAVESTORE_MAX + 1; i++)   /* past the last number: the first free one */
        if (!used[i]) return i;
    return 0;
}

typedef struct Blob { const void* p; int n; } Blob;
static int put_blob(FILE* f, void* ud) { const Blob* b = ud; return fwrite(b->p, 1, b->n, f) == (size_t)b->n ? 0 : -1; }

static int write_locked(const char* dir, int number, const unsigned char* data, const char* replaces)
{
    unsigned char b[SAVE_HEADER_BYTES + SAVE_DATA_BYTES];
    Header h = { .magic = "RECVXSAV", .version = 1, .data_size = SAVE_DATA_BYTES, .game = "SLUS-20184" };
    char label[128], name[256], path[1400];
    Blob blob = { b, sizeof(b) };
    if (!savestore_card_valid(data)) return 0;
    if (number <= 0 && !(number = savestore_next(dir))) return 0;
    h.written = (int64_t)time(NULL);
    h.play_time = rd32(data, 0x824); h.save_ct = rd32(data, 4); h.gm_mode = rd32(data, 0x10);
    h.ply_id = data[8]; h.stg_no = data[9]; h.rom_no = data[10];
    h.sum = byte_sum(&h, offsetof(Header, sum));
    memcpy(b, &h, sizeof(h));
    memcpy(b + SAVE_HEADER_BYTES, data, SAVE_DATA_BYTES);
    savestore_label(label, sizeof(label), data);
    savestore_filename(name, sizeof(name), number, label);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (pc_save_file(path, "wb", put_blob, &blob)) return 0;
    if (replaces && *replaces && strcmp(replaces, name)) {   /* the save it overwrote had another label */
        snprintf(path, sizeof(path), "%s/%s", dir, replaces);
        SDL_RemovePath(path);
    }
    return number;
}

int savestore_write(const char* dir, int number, const unsigned char* data, const char* replaces)
{
    int r;
    SDL_LockSpinlock(&lock);
    r = write_locked(dir, number, data, replaces);
    SDL_UnlockSpinlock(&lock);
    return r;
}

static int copy_file(const char* from, const char* to, int max)
{
    unsigned char* b = malloc(max);
    int n = b ? read_file(from, b, max) : -1, ok = 0;
    Blob blob = { b, n };
    if (n > 0 && n < max) ok = pc_save_file(to, "wb", put_blob, &blob) == 0;
    free(b);
    return ok;
}

int savestore_import_card(const char* save_dir, const char* dir)
{
    unsigned char d[SAVE_DATA_BYTES + 1], have[SAVE_DATA_BYTES];
    char path[1400], to[1400], file[256];
    SaveEntry* list;
    int i, j, n, copied = 0;
    SDL_LockSpinlock(&lock);
    n = savestore_list(dir, &list);
    for (i = 0; i < 30; i++) {   /* card 1's slots 0..14, card 2's 15..29 (lobby.h) */
        int num, dup = 0;
        snprintf(path, sizeof(path), "%s/card%d/BASLUS-20184/SAVEDATA-%02d", save_dir, i / 15, i % 15);
        if (read_file(path, d, sizeof(d)) != SAVE_DATA_BYTES || !savestore_card_valid(d)) continue;
        for (j = 0; j < n && !dup; j++) {
            snprintf(to, sizeof(to), "%s/%s", dir, list[j].file);
            dup = savestore_read(to, have) && !memcmp(have, d, SAVE_DATA_BYTES);
        }
        if (dup) continue;
        num = write_locked(dir, savestore_find(dir, i + 1, file, sizeof(file)) ? 0 : i + 1, d, NULL);
        if (!num) { SDL_Log("saves: cannot copy memory card save %d", i + 1); continue; }
        copied++;
        snprintf(path, sizeof(path), "%s/coop-partners-%02d.bin", save_dir, i);
        snprintf(to, sizeof(to), "%s/" SAVE_PARTNERS_FMT, dir, num);
        copy_file(path, to, 1 << 20);
    }
    snprintf(to, sizeof(to), "%s/system.dat", dir);
    if (!SDL_GetPathInfo(to, NULL)) {
        snprintf(path, sizeof(path), "%s/card0/BASLUS-20184/BASLUS-20184", save_dir);
        copy_file(path, to, SAVE_CONFIG_BYTES + 1);
    }
    SDL_UnlockSpinlock(&lock);
    free(list);
    return copied;
}
