###  Known bugs
---
- broken RTC 12 hr mode time reading on certain platforms
- there is no check if pid exists or not, multiple issues with pid wraparound
- multiple issues with instance fields wraparound in numerous structures
- race can overflow a semaphore's value (unsigned long) back to 0
- race can _maybe_ cause an incorrect EOWNERDEAD and inconsistent mutex in pthread_mutex_trylock()
### Known issues/quirks
---
- almost all cases of out of memory are currently handled by kernel panic
- `fork()` (intentionally) doesn't copy any other stack than the calling thread's (which can lead to lost argc/argv/environ)
- userspace `readdir()` is not thread-safe (POSIX doesn't specify whether it has to be)
- I don't think every kernel process operation is thread safe, too lazy to check

### Known missing features
---
- `scanf()` and `printf()` family of functions don't implement floats
- `execve()` family of functions doesn't accept NULL argv or envp
- `execve()` family of functions doesn't currently implement auxv
- everything in the TODO obviously
- missing `sigaltstack()` and everything along with it
- missing `SA_RESTART`, almost all `si_code` values for `siginfo_t`
- no support for PCI Configuration Space #2 (for i486 and early Pentiums)
- no /dev/fb0 support on pure VGA (due to banking)
- no break condition support on TTY and RS-232
- no baud/speed settings, delays, and control flags in termios
### Known console issues (compared to a VT102 excluding DEC escapes)
---
- no "computer editing" or insertions
- no programmable tab stops (constant 8)
- no double widths/heights
- no underlines
- no "bolds" (but we do support 16 colors like xterm)
- no blinks (though I am working on that)