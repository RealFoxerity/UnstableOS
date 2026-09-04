#ifndef _BASIC_H
#define _BASIC_H

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

extern const char * ld_path;
extern int exec_fd;

long syscall(unsigned long syscall_number, ...);
void dbg_print(const char * s);
void * alloc(unsigned long n);
__attribute__((noreturn)) void abort();

#define assert(cond) {\
    if (!(cond)) {\
        dbg_print("rtld: Assertion `"#cond"` failed [" __FILE__ ":" STR(__LINE__) "]\n");\
        abort();\
    }\
}
#define assert_msg(cond, msg) {\
    if (!(cond)) {\
        dbg_print("rtld: Error: "msg"\n");\
        abort();\
    }\
}
#endif