/* Sound driver (TSNDDRV) HLE: bank storage + synthesizer interface. */
#pragma once
#include <stdint.h>
#include <SDL3/SDL_audio.h>

typedef struct { uint8_t* data; uint32_t size, cap; } SndBlob;
typedef struct {
    SndBlob se_hd[5];     /* SE bank headers (HD) */
    SndBlob midi_hd[4];   /* MIDI program bank headers (HD) */
    SndBlob bd[16];       /* bank bodies (BD, SPU2 ADPCM) by port */
    SndBlob sq[8];        /* MIDI sequences (SQ) by port */
    SndBlob xhd[31], xbd[31];   /* PC: extra enemy banks 1..31 (header, body), snd_xbank_load */
    SndBlob arms2_hd, arms2_bd;   /* co-op: the partner's weapon bank (snd_partner_arms) */
    SndBlob sys_hd, sys_bd;       /* PC: a save room's MIDI program bank 2 (snd_sysbank_load) */
} SndBanks;
extern SndBanks g_snd_banks;

enum { SND_BANK_SE, SND_BANK_MIDI, SND_BANK_BODY, SND_BANK_SQ };
void snd_synth_bank_changed(int kind, int index);
void snd_synth_command(const uint8_t* cmd, int len);
void snd_synth_post(const uint8_t* cmd, int len);   /* (lock held) a command for the mixer to run in time */
void snd_synth_flush(void);                         /* (lock held) run the queued commands now */
void snd_synth_init(SDL_AudioDeviceID dev);
void snd_synth_update(void);         /* no audio device: run the driver ticks up to the current vblank */
void snd_synth_set_volume(float v);  /* user effects volume */
void snd_synth_lock(void);
void snd_synth_unlock(void);
uint16_t snd_synth_se_active(int port);
uint16_t snd_synth_midi_active(void);   /* playing MIDI ports */
float snd_synth_voice_level(void);       /* the player's voice (SE port 4) since the last call: mean |sample|, 0..1 */
void snd_synth_xbanks_stop(void);       /* (lock held) stop what plays from the extra banks */
/* PC extra banks (docs/ENEMIES.md "Sounds of added types"): a room's bank 3 from the disc (key stage*1000 + room*10 +
   case), or a player voice bank CORE_00n bank 4 (key 0xFFF0 + n, docs/ACTORS.md "Voices"); 1..31 or 0 (none) */
int  snd_xbank_load(int key);
void snd_xbank_release(void);
/* the system sounds (MIDI bank 2: item box lid, typewriter keys and bell) whose programs a room's own bank 2 lacks
   (only save rooms have them) play from RM_1080's bank 2, loaded once by this (game thread; kept for the session) */
void snd_sysbank_load(void);
int  snd_xbank_prog_ms(int xb, int tq);   /* how long program tq of extra bank xb plays, ms; 0: none there, or a looping one */
/* co-op (coop_glue.c): the partner's weapon bank - the shot bank of an ARMS_nnn.SPQ - beside our own
   arms bank (SE bank 1): its programs play as arms programs SND_PARTNER_TQ + n. NULL / 0: none */
#define SND_PARTNER_TQ 0x40
void snd_partner_arms(const void* hd, uint32_t hd_size, const void* bd, uint32_t bd_size);
