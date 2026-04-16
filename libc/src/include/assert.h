#ifndef _ASSERT_H
#define _ASSERT_H

#define __STR_INNER(x) #x
#define STR(x) __STR_INNER(x)

#define assert(cond) {\
    if (!(cond)) {\
        printf("Assertion `"#cond"` failed in %s()! [" __FILE__ ":" STR(__LINE__) "]\n", __func__);\
        abort();\
    }\
}
#endif