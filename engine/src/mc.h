#ifndef MC_H
#define MC_H

#include <tamtypes.h>

/* The save on the memory card (mc.c) */
int mc_init(void);                     /* MCMAN / MCSERV, libmc; -1 if they would not start */
int mc_load(u8 *dst, int size);        /* the save read (blocking, at boot): its bytes, 0 none */
void mc_save(const u8 *src, int size); /* a save started (written in steps by mc_poll) */
void mc_poll(void);                    /* each frame */
int mc_busy(void);                     /* a save running */

#endif
