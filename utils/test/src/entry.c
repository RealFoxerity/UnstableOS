#include "../../../libc/src/include/stdio.h"
#include "../../../libc/src/include/string.h"
#include "../../../libc/src/include/stdlib.h"
#include "../../../src/include/kernel.h"
#include "../../../libc/src/include/uthreads.h"


int test(struct uthread_args * self, void* _) {
    printf("test  ");

    for (int i = 0; i < 10000000; i++) {
        for (int i = 0; i < 10; i++);
    }
    printf("test #2");
    return 0;
}

int main() {
    printf("Trying yield()\n");
    syscall(SYSCALL_YIELD);

    uthread_t thread = uthread_create(test, NULL);
    if (thread.thread_lock < 0) {
        printf("Failed to allocate thread!\n");
        exit(1);
    }
    printf("Waiting on thread\n");
    printf("joined, exitcode %d\n", uthread_join(thread));
    while(1);
}