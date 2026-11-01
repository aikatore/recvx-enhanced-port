/* pc/src/host/savestore.c: native save files (names, numbers past 999, UTF-8, atomic writes,
   header checks, card bytes in and out, the memory card import). Works in a scratch folder
   with a UTF-8 name in the current directory. */
#include "../src/host/savestore.h"
#include "../src/host/fileio.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char* root = "test_savestore_\xC3\x9C" "ber";   /* "Über" */

/* a card save as the game writes it: SAVEFILE with its byte sum */
static void card_save(unsigned char* d, int ply, int stg, int rom, int count)
{
    unsigned int sum = 0;
    memset(d, 0, SAVE_DATA_BYTES);
    d[0] = 10;   /* version */
    memcpy(d + 4, &count, 4);
    d[8] = (unsigned char)ply; d[9] = (unsigned char)stg; d[10] = (unsigned char)rom;
    for (int i = 0x1F4; i < 0x7F4; i++) d[i] = (unsigned char)(i * 7 + count);   /* items */
    for (int i = 0; i < SAVE_DATA_BYTES - 4; i++) sum += d[i];
    memcpy(d + SAVE_DATA_BYTES - 4, &sum, 4);
}

static int put(FILE* f, void* ud) { const unsigned char* b = ud; return fwrite(b + 4, 1, *(const int*)b, f) == (size_t)*(const int*)b ? 0 : -1; }
static void write_raw(const char* path, const void* p, int n)
{
    unsigned char* b = malloc(n + 4);
    memcpy(b, &n, 4); memcpy(b + 4, p, n);
    CHECK(pc_save_file(path, "wb", put, b) == 0);
    free(b);
}
static int file_size(const char* path) { SDL_PathInfo i; return SDL_GetPathInfo(path, &i) ? (int)i.size : -1; }

static SDL_EnumerationResult SDLCALL count_tmp(void* ud, const char* dir, const char* name)
{
    (void)dir;
    if (strstr(name, ".tmp")) (*(int*)ud)++;
    return SDL_ENUM_CONTINUE;
}

static void names(void)
{
    char b[256], l[128];
    unsigned char d[SAVE_DATA_BYTES];
    savestore_filename(b, sizeof(b), 1, "Claire_Prison: front");
    CHECK(!strcmp(b, "001_Claire_Prison-front.sav"));
    savestore_filename(b, sizeof(b), 1234, "Chris_Chris's Story");
    CHECK(!strcmp(b, "1234_Chris_Chris-s-Story.sav"));
    savestore_filename(b, sizeof(b), 7, "a/b\\c:d*e?\"f<g>|h. ");   /* path and reserved characters, trailing dot/space */
    CHECK(!strcmp(b, "007_a-b-c-d-e-f-g-h.sav"));
    savestore_filename(b, sizeof(b), 2, "Claire_Gef\xC3\xA4ngnis \xE6\x97\xA5\xE6\x9C\xAC");   /* UTF-8 kept */
    CHECK(!strcmp(b, "002_Claire_Gef\xC3\xA4ngnis-\xE6\x97\xA5\xE6\x9C\xAC.sav"));
    savestore_filename(b, sizeof(b), 3, "x\xC3y\xFFz");   /* broken UTF-8 */
    CHECK(!strcmp(b, "003_x-y-z.sav"));
    savestore_filename(b, sizeof(b), 4, "");
    CHECK(!strcmp(b, "004_save.sav"));
    {   /* long: at most 64 bytes of label, no character cut in half */
        char lab[200] = "";
        for (int i = 0; i < 40; i++) strcat(lab, "\xC3\xA4");   /* 80 bytes */
        savestore_filename(b, sizeof(b), 5, lab);
        CHECK(strlen(b) == 4 + 64 + 4 && !strcmp(b + strlen(b) - 6, "\xC3\xA4.sav"));
    }
    card_save(d, 0, 0, 1, 1);
    savestore_label(l, sizeof(l), d);
    CHECK(!strcmp(l, "Claire_Prison: front"));
    card_save(d, 1, 3, 7, 1);
    savestore_label(l, sizeof(l), d);
    CHECK(!strcmp(l, "Chris_3-07"));   /* not a save room: stage-room */
}

int main(void)
{
    char dir[512], save_dir[512], path[1024], file[256];
    unsigned char d[SAVE_DATA_BYTES], e[SAVE_DATA_BYTES], back[SAVE_DATA_BYTES];
    SaveEntry* l;
    int n, tmp = 0;

    names();
    snprintf(save_dir, sizeof(save_dir), "%s", root);
    snprintf(dir, sizeof(dir), "%s/saves", root);
    SDL_RemovePath(dir);   /* leftovers of a failed run */
    SDL_CreateDirectory(dir);

    /* a new save: number 1, the card bytes behind a 64-byte header, read back unchanged (native -> card) */
    card_save(d, 0, 0, 1, 3);
    CHECK(savestore_write(dir, 0, d, NULL) == 1);
    snprintf(path, sizeof(path), "%s/001_Claire_Prison-front.sav", dir);
    CHECK(file_size(path) == SAVE_HEADER_BYTES + SAVE_DATA_BYTES);
    CHECK(savestore_read(path, back) && !memcmp(back, d, SAVE_DATA_BYTES));
    n = savestore_list(dir, &l);
    CHECK(n == 1 && l[0].number == 1 && l[0].save_ct == 3 && l[0].ply_id == 0 && l[0].stg_no == 0 && l[0].rom_no == 1 && l[0].written > 0);
    free(l);
    CHECK(!savestore_card_valid((unsigned char[SAVE_DATA_BYTES]){ 0 }));   /* an empty card slot */
    CHECK(savestore_write(dir, 0, (unsigned char[SAVE_DATA_BYTES]){ 0 }, NULL) == 0);

    /* numbers past 999: 999, then new ones are 1000 and 1001, listed in number order */
    card_save(e, 1, 4, 4, 9);
    CHECK(savestore_write(dir, 999, e, NULL) == 999);
    CHECK(savestore_write(dir, 0, e, NULL) == 1000);
    CHECK(savestore_write(dir, 0, d, NULL) == 1001);
    CHECK(savestore_next(dir) == 1002);
    n = savestore_list(dir, &l);
    CHECK(n == 4 && l[0].number == 1 && l[1].number == 999 && l[2].number == 1000 && l[3].number == 1001);
    CHECK(n == 4 && !strcmp(l[2].file, "1000_Chris_Airport-Elev.sav"));
    free(l);

    /* overwriting a save in another room: same number, new name, the old file gone */
    CHECK(savestore_write(dir, 1, e, "001_Claire_Prison-front.sav") == 1);
    CHECK(file_size(path) < 0);
    CHECK(savestore_find(dir, 1, file, sizeof(file)) && !strcmp(file, "001_Chris_Airport-Elev.sav"));
    n = savestore_list(dir, &l);
    CHECK(n == 4);
    free(l);
    SDL_EnumerateDirectory(dir, count_tmp, &tmp);
    CHECK(tmp == 0);   /* atomic writes leave no temporary files */
    snprintf(path, sizeof(path), "%s/no_such_folder", dir);
    CHECK(savestore_write(path, 0, d, NULL) == 0);

    /* header and data checks: broken files are not listed (and not overwritten by new numbers) */
    {
        unsigned char b[SAVE_HEADER_BYTES + SAVE_DATA_BYTES];
        FILE* f;
        snprintf(path, sizeof(path), "%s/001_Chris_Airport-Elev.sav", dir);
        f = pc_fopen(path, "rb");
        CHECK(f && fread(b, 1, sizeof(b), f) == sizeof(b));
        if (f) fclose(f);
        b[0] ^= 1;   /* magic */
        snprintf(path, sizeof(path), "%s/1500_bad_magic.sav", dir); write_raw(path, b, sizeof(b));
        b[0] ^= 1; b[16] = 'X';   /* game id */
        snprintf(path, sizeof(path), "%s/1501_bad_game.sav", dir); write_raw(path, b, sizeof(b));
        b[16] = 'S'; b[44] ^= 1;   /* header field (save count): header sum */
        snprintf(path, sizeof(path), "%s/1502_bad_header.sav", dir); write_raw(path, b, sizeof(b));
        b[44] ^= 1; b[SAVE_HEADER_BYTES + 0x300] ^= 0x40;   /* an item word: the game's checksum */
        snprintf(path, sizeof(path), "%s/1503_bad_data.sav", dir); write_raw(path, b, sizeof(b));
        b[SAVE_HEADER_BYTES + 0x300] ^= 0x40;
        snprintf(path, sizeof(path), "%s/1504_short.sav", dir); write_raw(path, b, sizeof(b) - 1);
        snprintf(path, sizeof(path), "%s/1505_good.sav", dir); write_raw(path, b, sizeof(b));   /* any label works */
        snprintf(path, sizeof(path), "%s/notes.txt", dir); write_raw(path, "x", 1);
        CHECK(!savestore_read(path, back));
        n = savestore_list(dir, &l);
        CHECK(n == 5 && l[4].number == 1505);
        free(l);
        CHECK(savestore_write(dir, 0, d, NULL) == 1506);
    }

    /* sparse numbers: the last 9-digit number (a broken file) is listed as nothing; new saves take the first free number */
    {
        snprintf(path, sizeof(path), "%s/999999999_test.sav", dir); write_raw(path, "x", 1);
        snprintf(path, sizeof(path), "%s/1234567890_ten_digits.sav", dir); write_raw(path, "x", 1);
        n = savestore_list(dir, &l);
        CHECK(n == 6 && l[5].number == 1506);
        free(l);
        CHECK(savestore_next(dir) == 2);
        CHECK(savestore_write(dir, 0, d, NULL) == 2);
        CHECK(savestore_write(dir, 999999999, d, "999999999_test.sav") == 999999999);
        n = savestore_list(dir, &l);
        CHECK(n == 8 && l[7].number == 999999999 && !strcmp(l[7].file, "999999999_Claire_Prison-front.sav"));
        free(l);
        CHECK(savestore_next(dir) == 3);
    }

    /* memory card import: valid slots keep their number if free, empty ones are skipped, the
       partners file and the system file come along, the card keeps everything; again: nothing */
    {
        char card[600];
        unsigned char cfg[SAVE_CONFIG_BYTES] = { 1, 2, 3 };
        snprintf(save_dir, sizeof(save_dir), "%s/user", root);
        snprintf(card, sizeof(card), "%s/card0/BASLUS-20184", save_dir);
        SDL_CreateDirectory(card);
        snprintf(dir, sizeof(dir), "%s/saves", save_dir);
        SDL_CreateDirectory(dir);
        card_save(d, 0, 0, 1, 5);
        snprintf(path, sizeof(path), "%s/SAVEDATA-00", card); write_raw(path, d, SAVE_DATA_BYTES);
        snprintf(path, sizeof(path), "%s/SAVEDATA-01", card); write_raw(path, (unsigned char[SAVE_DATA_BYTES]){ 0 }, SAVE_DATA_BYTES);
        card_save(e, 2, 2, 7, 6);
        snprintf(path, sizeof(path), "%s/SAVEDATA-02", card); write_raw(path, e, SAVE_DATA_BYTES);
        snprintf(path, sizeof(path), "%s/BASLUS-20184", card); write_raw(path, cfg, sizeof(cfg));
        snprintf(path, sizeof(path), "%s/coop-partners-02.bin", save_dir); write_raw(path, "partners", 8);
        {   /* card 2's slot 2 is slot 17: number 18, its partners file coop-partners-17.bin */
            unsigned char c2[SAVE_DATA_BYTES];
            card_save(c2, 3, 1, 2, 7);
            snprintf(path, sizeof(path), "%s/card1/BASLUS-20184", save_dir); SDL_CreateDirectory(path);
            snprintf(path, sizeof(path), "%s/card1/BASLUS-20184/SAVEDATA-02", save_dir); write_raw(path, c2, SAVE_DATA_BYTES);
            snprintf(path, sizeof(path), "%s/coop-partners-17.bin", save_dir); write_raw(path, "card2", 5);
        }
        card_save(back, 1, 0, 4, 1);
        CHECK(savestore_write(dir, 1, back, NULL) == 1);   /* number 1 taken: slot 1's save goes after it */
        CHECK(savestore_import_card(save_dir, dir) == 3);
        CHECK(savestore_find(dir, 18, file, sizeof(file)));
        snprintf(path, sizeof(path), "%s/018.coop-partners.bin", dir);
        CHECK(file_size(path) == 5);
        CHECK(savestore_find(dir, 3, file, sizeof(file)) && !strcmp(file, "003_Steve_Residence-SR.sav"));
        CHECK(savestore_find(dir, 2, file, sizeof(file)) && !strcmp(file, "002_Claire_Prison-front.sav"));
        snprintf(path, sizeof(path), "%s/%s", dir, file);
        CHECK(savestore_read(path, back) && !memcmp(back, d, SAVE_DATA_BYTES));   /* card -> native: the same bytes */
        snprintf(path, sizeof(path), "%s/003.coop-partners.bin", dir);
        CHECK(file_size(path) == 8);
        snprintf(path, sizeof(path), "%s/system.dat", dir);
        CHECK(file_size(path) == SAVE_CONFIG_BYTES);
        snprintf(path, sizeof(path), "%s/SAVEDATA-00", card);
        CHECK(file_size(path) == SAVE_DATA_BYTES);
        CHECK(savestore_import_card(save_dir, dir) == 0);
        n = savestore_list(dir, &l);
        CHECK(n == 4);
        free(l);
    }

    {   /* SDL_RemovePath removes empty folders only: files first, then the folders inside out */
        const char* dirs[] = { "saves", "user/saves", "user/card0/BASLUS-20184", "user/card1/BASLUS-20184", "user", "user/card0", "user/card1", "" };
        for (int k = 0; k < 8; k++) {
            int cnt = 0;
            char** fl;
            snprintf(dir, sizeof(dir), "%s/%s", root, dirs[k]);
            fl = SDL_GlobDirectory(dir, NULL, 0, &cnt);
            for (int i = 0; fl && i < cnt; i++) { snprintf(path, sizeof(path), "%s/%s", dir, fl[i]); SDL_RemovePath(path); }
            SDL_free(fl);
        }
        snprintf(path, sizeof(path), "%s/user/card0", root); SDL_RemovePath(path);
        snprintf(path, sizeof(path), "%s/user/card1", root); SDL_RemovePath(path);
        snprintf(path, sizeof(path), "%s/user", root); SDL_RemovePath(path);
        SDL_RemovePath(root);
        CHECK(file_size(root) < 0);
    }
    printf("savestore: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
