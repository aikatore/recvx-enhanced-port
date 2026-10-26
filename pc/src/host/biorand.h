/* BioRand (https://github.com/biorand/classic, MIT): a randomizer that reads the player's disc image and
   writes a pack for this port into <mods>/biorand/ - room files RM_*.RDX, SYSTEM.AFS/10 and /11 (player
   models), ADV.AFS/2 (title background), log_pl0.txt (the spoiler log with the seed). The folder is
   BioRand's: it deletes and writes it whole. The mods loader (audio/adx.c mod_open_path, teleport.c)
   serves its files over <mods>/ only while BioRand is on (biorand_active); off, the folder is ignored.
   docs/USER_GUIDE.md "Randomizer (BioRand)", pc/glue/biorand_glue.c. */
#ifndef RECVX_BIORAND_H
#define RECVX_BIORAND_H
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

/* BioRand's seed number from its seed string "R840-0KR3-T4HH..." (RandoConfig.ToString: 'R', three digits, '-', the
   20-bit seed as 4 characters of its 32-letter alphabet, low 5 bits first, '-', the options); -1 when it is none */
static inline int pc_biorand_seed(const char* s)
{
    static const char abc[] = "0123456789ABCEFGHJKLMNPQRTUVWXYZ";
    int v = 0, i;
    for (i = 0; i < 10; i++)
        if (!s[i]) return -1;
    if (s[0] != 'R' || s[4] != '-' || s[9] != '-') return -1;
    for (i = 0; i < 4; i++) {
        const char* p = memchr(abc, s[5 + i] >= 'a' && s[5 + i] <= 'z' ? s[5 + i] - 32 : s[5 + i], 32);
        if (!p) return -1;
        v |= (int)(p - abc) << (5 * i);
    }
    return v;
}

void biorand_scan(void);              /* look at <mods>/biorand again (the start, the title menu) */
int  biorand_pack(void);              /* a generated pack is there */
int  biorand_seed(void);              /* its seed number (BioRand's, 0..1048575), -1 unknown */
unsigned int biorand_id(void);        /* the pack: a hash of its seed line (all options), log and room files; 0 none */
const char* biorand_seed_text(void);  /* "seed 123456", "" unknown */
int  biorand_active(void);            /* the loader serves the pack now: our setting, a co-op guest the host's choice */
int  biorand_shown(void);            /* the BioRand settings exist: only with --dev (g_dev_mode) */
const char* biorand_locked(void);     /* why the setting cannot change now (no pack, in game, a co-op guest), or NULL */
int  biorand_setting(void);           /* the setting (ini biorand), whether or not a pack is there */
int  biorand_toggle(void);            /* the title's BIORAND row: flips the setting; 0 when it is locked (biorand_locked) */
int  biorand_rifle_stack(void);       /* rifle ammo stacks: the setting, always while BioRand is on */
void* biorand_room0(int* size);       /* the pack's room 0/0 (RM_0000.RDX) expanded, malloc'd (free it); NULL none */
/* generating (docs/BIORAND_INGAME.md S5; the BIORAND screen, game thread) */
int  biorand_mods_free_mb(void);      /* the mods folder (created) has this much free space in MB; -1 unknown */
int  biorand_iso_known(void);         /* the game's disc image path is set */
int  biorand_generate(const char* seed_string);   /* the private settings.json, then the CLI's thread (biorand_cli_generate):
                                                     1 started, 0 a job runs, -1 settings.json not written */
void biorand_enable(void);            /* BioRand on (a pack was generated), saved */
void biorand_frame(void);             /* game thread, every frame: rifle ammo stacking (biorand_glue.c) */

/* game thread (biorand_glue.c): the save words, a co-op guest's start inventory */
void pc_biorand_stamp(void);                       /* the game state's save words: the current BioRand state */
void pc_biorand_start_kit(void);                   /* the pack's start inventory into ours (room 0/0's entry block) */
int  pc_biorand_save_differs(const void* save);    /* a save region (card bytes) made with another BioRand state */
void pc_biorand_save_accept(void* save);           /* loaded anyway: its words become the current state */
const char* pc_biorand_ask(int line);              /* the warning's lines 0..2 (after save_differs) */
/* "Randomize Played as" (biorand_glue.c, save word BR_APPEAR): the look this game was given, -1 none (the user's own) */
void pc_biorand_appear_new(void);                  /* a new game: picked from the pack's seed, or none */
int  pc_biorand_appear(void);                      /* in game: the picked "You appear as" chr, -1 none */
void pc_biorand_appear_drop(void);                 /* the user chose a look in game: this game's pick goes */
/* co-op (coop_glue.c): -1 not a guest in a session (our own setting counts), else the host's choice (0/1) */
int coop_biorand(void);

#ifdef __cplusplus
}
#endif
#endif
