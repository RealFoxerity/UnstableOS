#ifndef _UNSTABLEOS_FUTEX_H
#define _UNSTABLEOS_FUTEX_H

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* syntax of sys_futex(...)
 * syscall(SYSCALL_FUTEX, uint32_t * uaddr, FUTEX_WAIT, uint32_t expected, pid_t owner, struct timespec * _Nullable timeout)
 * owner being set to other than 0 makes it a "robust" futex that's to be awoken when the owner dies
 * if timeout is not null, and is 0, can be used to check whether the owner still lives
 *
 * syscall(SYSCALL_FUTEX, uint32_t * uaddr, FUTEX_WAKE, uint32_t max_woken_threads)
 */
#endif
