#ifndef _UNSTABLEOS_HALT_H
#define _UNSTABLEOS_HALT_H

#define HALT_FORCE 1 // skip unmounting/syncing and just halt
#define HALT_RESET 2 // reset the system instead of halting

int halt(int how);

#endif