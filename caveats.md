###  Known bugs
---
- broken RTC 12 hr mode time reading on certain platforms
- there is no check if pid exists or not, multiple issues with pid wraparound
- multiple issues with instance fields wraparound in numerous structures
- race can overflow a semaphore's value (unsigned long) back to 0
- race can _maybe_ cause an incorrect EOWNERDEAD and inconsistent mutex in pthread_mutex_trylock()
### Known issues/quirks
---
- some lowlevel internal cases of out of memory are currently handled by kernel panic
- I don't think every kernel process operation is thread safe, too lazy to check
- `fcntl()` advisory locks are internally converted from negative `l_len`s leading to `F_GETLK`/`F_OFD_GETLK` returning different structures
- `PARMRK` termios option may lead to wrong \t deletion on `ICANON` (\xFF\x00<c> is treated as 3 characters)
### Known missing features
---
- `scanf()` family of functions don't implement floats
- `printf()` family of functions don't support the `'` flag and the `%a`/`%A` format specifiers
- everything in the TODO obviously
- missing `sigaltstack()` and everything along with it
- missing almost all `si_code` values for `siginfo_t`
- no support for PCI Configuration Space #2 (for i486 and early Pentiums)
- no delays in termios
### Known console issues (compared to a VT102 excluding DEC escapes)
---
- no "computer editing" or insertions
- no programmable tab stops (constant 8)
- no double widths/heights
- no "bolds" (but we do support 16 colors like xterm)
- no blinks (though I am working on that)