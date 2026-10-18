#ifndef MC_H
#define MC_H

#include <tamtypes.h>

/* The save on the memory card (mc.c). Its results: MC_OK, MC_NOFILE (a card with room, no save), MC_NOCARD (no card,
 * not a PS2 card, unformatted), MC_FULL (no save and too little room), MC_CORRUPT (a bad save), MC_FAILED (a write) */
enum { MC_OK, MC_NOFILE, MC_NOCARD, MC_FULL, MC_CORRUPT, MC_FAILED };
int mc_init(void);                     /* MCMAN / MCSERV, libmc; -1 if they would not start */
int mc_load(u8 *dst, int size);        /* the save read (blocking, at boot): MC_OK, MC_NOFILE, NOCARD, FULL, CORRUPT */
int mc_check(void);                    /* CheckEnableToCreate (blocking): MC_NOFILE (room), MC_NOCARD, MC_FULL */
void mc_remove(void);                  /* the corrupted save deleted (blocking) */
void mc_save(const u8 *src, int size); /* a save started (written in steps by mc_poll; nothing with no-save) */
void mc_poll(void);                    /* each frame */
int mc_busy(void);                     /* a save running */
int mc_result(void);                   /* the last save's: MC_OK, NOCARD, FULL, FAILED */
int mc_take_error(void);               /* a failed save's result once, else MC_OK */
void mc_set_boot(int res);             /* main.c: mc_load's result for the first scene's SaveDataHandleSequence */
int mc_take_boot(void);                /* that result once, else MC_OK */
void mc_set_nosave(void);              /* NANDErrorSequence isResultNoSave / FileCorrupted: play without saving */
int mc_nosave(void);
void mc_exit_to_browser(void);        /* requestGoWiiMenu: OSDSYS's browser */

#endif
