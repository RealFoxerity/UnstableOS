###  Known bugs
---
- broken RTC 12 hr mode time reading on certain platforms
- scanf() consumes \n
- there is no check if pid exists or not, multiple issues with pid wraparound
- multiple issues with instance fields wraparound in numerous structures
- race can overflow a semaphore's value (unsigned long) back to 0
### Known issues/quirks
---
- very unwieldy way of handling userspace thread creation
- Very slow scanf() implementation
- no locking in page frame allocator making it inherently thread-unsafe
- almost all cases of out of memory are currently handled by kernel panic
- `fork()` (intentionally) doesn't copy any other stack than the calling thread's (which can lead to lost argc/argv/environ)
- userspace `readdir()` is not thread-safe (POSIX doesn't specify whether it has to be)
- orphaned processes are reparented to their "grandparent" instead of to the init
- pipes only check if the fd has instance count >1 instead of checking for readers/writers
- consequently, EPIPE and SIGPIPE is sent regardless if readers/writers exist (if instance count < 2)
- I don't think every kernel process operation is thread safe, too lazy to check

### Known missing features
---
- `scanf()` and `printf()` family of functions don't implement floats
- `execve()` family of functions doesn't accept NULL argv or envp
- `execve()` family of functions doesn't currently implement auxv
- everything in the TODO obviously
- missing `sigaltstack()` and everything along with it
- missing `SA_RESTART`, almost all `si_code` values for `siginfo_t`
### Known console issues (compared to a VT102 excluding DEC escapes)
---
- not setting bg for the rest of a line when encountering `\r\n`
- not correctly deleting a `\t`
- no "computer editing" or insertions
- no programmable tab stops (constant 8)
- no double widths/heights
- no underlines
- no "bolds" (but we do support 16 colors like xterm)
- no blinks (though i am working on that)

### Untested features that might work?
---
- TTY foreground groups and sessions