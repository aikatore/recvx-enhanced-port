/* Native saves (docs/ARCHITECTURE.md "Save system"): with the save system set to native files,
   the game's own save and load screens (ps2_SaveScreen.c, ps2_LoadScreen.c) list the save files
   of pc/src/host/savestore.c instead of the memory card's 15 slots and skip the card screens.
   The screens keep their states, drawing and messages; only their rows come from here, and the
   card reads and writes they queue on their MEMORYCARDSTATE (ReadMemoryCard / WriteMemoryCard,
   which run the game's own save logic first) are done here on the files instead of by
   ExecuteMemoryCard. The system file (CONFIGFILE: options, clear flags, best times) is
   saves/system.dat. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/bup_00.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/message.h"
#include "../../decomp/include/ps2/veronica/prog/vibman.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_MemoryCard..h"
#include "../../decomp/include/ps2/veronica/prog/ps2_McSaveFile.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_SystemLoadScreen.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_SystemSaveScreen.h"
#include "../src/host/savestore.h"
#include "../src/host/lang.h"
#include "../src/host/fileio.h"
#include "../src/host/lobby.h"
#include "../src/host/coop.h"
#include "../src/host/menu_mouse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int native;          /* the open save/load screen's save system, taken when it (re)starts */
static int listing_save;    /* it is the save screen (pc_save_list) */
static SaveEntry* saves;    /* the files of its rows; a row past them is a new save */
static int saves_n, rows_n;
static SELECTFILEINFO* rows;

int pc_saves_native(int state)
{
    if (state == 0) native = save_native();
    return native;
}

/* the rows in save number order, a new save last when saving; the cursor on the new save
   (saving) or on the save written last (loading) */
void pc_save_list(SELECTFILEWINDOW* win, MEMORYCARDSTATE* mc, int saving)
{
    const char* dir = save_native_dir();
    int i, n, cur = 0, top;
    listing_save = saving;
    free(saves);
    saves_n = savestore_list(dir, &saves);
    n = saves_n + (saving || !saves_n);   /* loading with no saves: one empty row ("no save data") */
    rows = realloc(rows, sizeof(*rows) * (rows_n = n));
    memset(rows, 0, sizeof(*rows) * n);
    for (i = 0; i < saves_n; i++) {
        const SaveEntry* e = &saves[i];
        rows[i].FileNumber = e->number;
        rows[i].save_ct = e->save_ct;
        rows[i].gm_mode = e->gm_mode;
        rows[i].ply_id = e->ply_id;
        rows[i].saveroom = RoomNameSwitch(e->rom_no + e->stg_no * 100);   /* as mcGetPortSelectDirInfo */
        if (e->written >= saves[cur].written) cur = i;
    }
    if (n > saves_n) rows[saves_n].FileNumber = savestore_next(dir);
    if (saving) cur = n - 1;
    win->pRecordTop = rows;
    win->lRecordMax = n;
    win->slDispRecordNumber = n < 5 ? n : 5;
    top = cur - (win->slDispRecordNumber - 1);   /* the cursor's row at the bottom of the window */
    win->slDispTopNumber = top < 0 ? 0 : top;
    win->sMesCur = cur;
    win->sCursol = cur - win->slDispTopNumber;
    SetMemoryCardFileNumber(mc, cur);
}

/* mcDisplayFileSelectWindow's header (MEMORY CARD and the slot, messages 463 and 464 + port, at
   y 32): with native saves "Load Game" / "Save Game" instead, as a message in the game's font
   (codes: lang.h pc_glyph_next, 0xFF01 a space) drawn by bhDispMessage the same way, centred above the list */
int pc_save_header(void)
{
    const char* s = tr(listing_save ? "Save Game" : "Load Game");
    unsigned short mes[48];
    void* keep = sys->mes_tp;
    int i = 0, c;
    if (!native) return 0;
    while (i < 47 && (c = pc_glyph_next(&s)) >= 0) mes[i++] = c ? c : 0xFF01;
    mes[i] = 0xFFFF;
    sys->mes_tp = mes;
    bhDispMessage(320.0f - bhMesLen(mes) / 2, 32.0f, 0, 2, 0, 0, 0);
    sys->mes_tp = keep;
    return 1;
}

/* the cursor one row up or down, wrapping around as the card's list does */
void pc_save_move(SELECTFILEWINDOW* win, short* cursor, int dir)
{
    int n = win->lRecordMax, c = *cursor + dir;
    c = c < 0 ? n - 1 : c >= n ? 0 : c;
    if (c < win->slDispTopNumber) win->slDispTopNumber = c;
    else if (c >= win->slDispTopNumber + win->slDispRecordNumber) win->slDispTopNumber = c - win->slDispRecordNumber + 1;
    *cursor = win->sMesCur = c;
    win->sCursol = c - win->slDispTopNumber;
    CallSystemSe(0, 2);
}

/* instead of ExecuteMemoryCard: the read or write the screen queued, on the chosen row's file;
   the card's end states (9 read, 10 written) or its error */
int pc_save_card(MEMORYCARDSTATE* mc)
{
    int i = mc->lSelectFileNumber;
    char path[1400];
    if (mc->ulState == 1) {   /* ReadMemoryCard (mcReadStartSaveFile) */
        unsigned char d[SAVE_DATA_BYTES];
        snprintf(path, sizeof(path), "%s/%s", save_native_dir(), i >= 0 && i < saves_n ? saves[i].file : "");
        if (i >= 0 && i < saves_n && mc->ulFileSize == SAVE_DATA_BYTES && savestore_read(path, d)) {
            memcpy(mc->vpAddr, d, SAVE_DATA_BYTES);
            mc->ulState = 9;
            host_log("saves: loaded %s\n", saves[i].file);
        } else {
            mc->ulState = 0;
            mc->ulError = 1;
            host_log("saves: cannot read save row %d\n", i + 1);
        }
    } else if (mc->ulState == 2) {   /* WriteMemoryCard (mcWriteStartSaveFile) */
        int old = i >= 0 && i < saves_n, num = 0;
        if (i >= 0 && i < rows_n && mc->ulFileSize == SAVE_DATA_BYTES)
            num = savestore_write(save_native_dir(), old ? saves[i].number : 0, mc->vpAddr, old ? saves[i].file : NULL);
        if (num) {
            mc->ulState = 10;
            rows[i].FileNumber = num;
            coop_save_written(num - 1);   /* the host's partner inventories go with the save (lobby.h slots) */
            host_log("saves: wrote save %03d%s\n", num, old ? " (overwritten)" : "");
        } else {
            mc->ulState = 0;
            mc->ulError = 2;
            host_log("saves: cannot write save row %d\n", i + 1);
        }
    }
    return 100;   /* no card change */
}

/* ---------------------------------------------------------------- the mouse
   ExecuteSaveScreen / ExecuteLoadScreen, before the state runs, with either save system: hovering
   a row of the list, a YES/NO answer or a memory card moves the screen's own cursor there (with the
   d-pad's sound), a click there confirms it, a click on a message confirms it; a right click is
   cancel, the wheel and a click on the list's arrows are up/down. All as this frame's pad bits
   (sys->pad_ps / pad_on as bhSetPad left them) or the cursor the d-pad moves, so keyboard and
   gamepad are untouched. Hit areas: where DispTexture / DispLoadTexture draw (game 2D px). */
static MouseTake smt;
static int smouse_seen, swheel, sgap;

/* menu_pad: a save or load screen ran since it last looked (the screen takes the mouse) */
int pc_save_screen(void) { int s = smouse_seen; smouse_seen = 0; return s; }

#define IN(x0, x1, y0, y1) (smt.x >= (x0) && smt.x < (x1) && smt.y >= (y0) && smt.y < (y1))

void pc_save_mouse(int saving, unsigned int state, int cg, short* cur_x, short* cur_y, short* sel, SELECTFILEWINDOW* win, MEMORYCARDSTATE* mc)
{
    static const short yn_save[7] = { 0, 279, 359, 359, 0, 205, 227 };   /* DispTexture's YES/NO arrow by cCgFlag */
    float yn = saving ? (cg >= 0 && cg < 7 ? yn_save[cg] : 0) : (cg == 2 || cg == 3 ? 358 : 0);   /* DispLoadTexture's (3: the BioRand question) */
    int hit = -1, cur;
    unsigned int bits = 0;
    smouse_seen = 1;
    g_menu_mouse.active = 1;   /* the OS cursor shows */
    if (state == 0) {   /* (re)started: what the mouse did before is not for this screen */
        memset(&smt, 0, sizeof(smt));
        menu_mouse_take(&smt);
        swheel = sgap = 0;
        return;
    }
    menu_mouse_take(&smt);
    swheel += smt.wheel;
    if (state == 20) {   /* the list: rows 40 px apart from the cursor's y 103 (mcDispWindowCurSol), arrows at x 320 above and below */
        if (IN(16, 616, 100, 100 + 40 * win->slDispRecordNumber)) hit = (int)(smt.y - 100) / 40;
        else if (smt.left && IN(296, 360, 64, 100)) swheel++;
        else if (smt.left && IN(296, 360, 302, 340)) swheel--;
        if (hit >= 0 && (smt.moved || smt.left) && hit != win->sCursol) {
            *cur_y = win->sMesCur = win->slDispTopNumber + hit;
            win->sCursol = hit;
            CallSystemSe(0, 2);
        }
    } else if (state == 10) {   /* the memory cards (CheckDispMemoryCard: cursor at 70 + 268 x, 129 + 258 y), EXIT below */
        int p0 = GetMemoryCardSelectPortState(mc, 0) == 2, p1 = GetMemoryCardSelectPortState(mc, 1) == 2;
        if (p0 && IN(70, 338, 120, 360)) hit = 0;
        else if (p1 && IN(338, 610, 120, 360)) hit = 1;
        else if (IN(70, 400, 380, 420)) hit = 2;
        cur = *cur_y ? 2 : p0 && p1 ? *cur_x : p1;
        if (hit >= 0 && (smt.moved || smt.left) && hit != cur) {
            *cur_y = hit == 2;
            if (hit < 2 && p0 && p1) *cur_x = *sel = hit;
            CallSystemSe(0, 2);
        }
    } else if (yn > 0) {   /* YES above NO, 22 px apart, right of the arrow */
        if (IN(240, 400, yn - 3, yn + 41)) hit = smt.y >= yn + 19;
        if (hit >= 0 && (smt.moved || smt.left) && hit != *sel) { *sel = hit; CallSystemSe(0, 2); }
    } else hit = 0;   /* a message */
    if (smt.left && hit >= 0) bits = 0x800;
    else if (smt.right) bits = 0x1000;
    else if (sgap) sgap = 0;   /* one up/down press every other frame, each a new press */
    else if (swheel) { bits = swheel > 0 ? 0x1 : 0x2; swheel += swheel > 0 ? -1 : 1; sgap = 1; }
    sys->pad_ps |= bits;
    sys->pad_on |= bits;
}

/* ---------------------------------------------------------------- the system file */
static int read_config(CONFIGFILE* c)
{
    char path[1200];
    FILE* f;
    int n;
    snprintf(path, sizeof(path), "%s/system.dat", save_native_dir());
    if (!(f = pc_fopen(path, "rb"))) return 0;
    n = (int)fread(c, 1, sizeof(*c), f);
    fclose(f);
    return n == SAVE_CONFIG_BYTES && c->Check_Sam == (unsigned int)MemoryCardGetSum((unsigned char*)c, offsetof(CONFIGFILE, Check_Sam));
}

/* the boot's system file (ExecuteStateSysLoadScreenSysLoad): options, clear flags and best times */
int pc_sysload_native(SYSLOAD_SCREEN* p)
{
    if (!save_native()) return 0;
    if (read_config(p->pConfigFile)) {
        memcpy(sys, p->pConfigFile, offsetof(CONFIGFILE, Check_Sam));
        SetUseVibrationUnit(p->pConfigFile->vibration);
        p->usExitFlag = 2;
    } else p->usExitFlag = 1;   /* none yet: the defaults, as without a system file on the card */
    SetStateSysLoadScreenTitleExit(p);
    return 1;
}

typedef struct { const void* p; int n; } Blob;
static int put_blob(FILE* f, void* ud) { const Blob* b = ud; return fwrite(b->p, 1, b->n, f) == (size_t)b->n ? 0 : -1; }

/* writing it (options, ranking): merged with the file's clear flags and best times as
   ExecuteStateWriteRankingData does, then written as ExecuteStateSysSave does */
int pc_syssave_native(SYSSAVE_SCREEN* p)
{
    CONFIGFILE old, merged;
    char path[1200];
    Blob b = { p->pConfigFile, SAVE_CONFIG_BYTES };
    if (!save_native()) return 0;
    if (read_config(&old)) {
        if (p->usSaveMode != 0) {
            mcCreateConfigInit(&merged);
            memcpy(&merged, &old, offsetof(CONFIGFILE, Check_Sam));
            SysSaveHikaku(&merged);
        } else memcpy(sys, &old, offsetof(CONFIGFILE, Check_Sam));
    }
    mcWriteStartConfigFile(p->pMcState, p->pConfigFile);   /* the file from sys, with its checksum */
    RecoveryMemoryCardError(p->pMcState);                  /* the card write it queued is not run */
    snprintf(path, sizeof(path), "%s/system.dat", save_native_dir());
    p->usExitFlag = pc_save_file(path, "wb", put_blob, &b) == 0 ? 2 : 1;
    if (p->usExitFlag != 2) host_log("saves: cannot write %s\n", path);
    SetStateSysSaveTitleExit(p);
    return 1;
}

