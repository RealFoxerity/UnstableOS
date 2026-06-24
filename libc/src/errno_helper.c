#include <errno.h>

// the kernel doesn't have TLS,
// so accessing errno and similar would be invalid addressing
char is_klibc = 0;
__thread int errno;

void ___set_errno(int error) {
    if (is_klibc) return;
    errno = error;
}
int ___get_errno() {
    if (is_klibc) return 0;
    return errno;
}