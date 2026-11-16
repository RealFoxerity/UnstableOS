#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;
typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef int16_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;
typedef uint8_t uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

typedef signed long intptr_t;
typedef unsigned long uintptr_t;

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

#undef INT8_MAX
#define INT8_MAX 0x7f
#undef INT8_MIN
#define INT8_MIN (-INT8_MAX - 1)


#undef UINT8_MAX
#define UINT8_MAX 0xff
#undef INT16_MAX
#define INT16_MAX 0x7fff
#undef INT16_MIN
#define INT16_MIN (-1-INT16_MAX)
#undef UINT16_MAX
#define UINT16_MAX 0xffff
#undef INT32_MAX
#define INT32_MAX 0x7fffffff
#undef INT32_MIN
#define INT32_MIN (-1-INT32_MAX)
#undef UINT32_MAX
#define UINT32_MAX 0xffffffffU
#undef INT64_MAX
#define INT64_MAX 0x7fffffffffffffffLL
#undef INT64_MIN
#define INT64_MIN (-1-INT64_MAX)
#undef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffULL

#undef INT_LEAST8_MAX
#define INT_LEAST8_MAX INT8_MAX
#undef INT_LEAST8_MIN
#define INT_LEAST8_MIN INT8_MIN
#undef UINT_LEAST8_MAX
#define UINT_LEAST8_MAX UINT8_MAX
#undef INT_LEAST16_MAX
#define INT_LEAST16_MAX INT16_MAX
#undef INT_LEAST16_MIN
#define INT_LEAST16_MIN INT16_MIN
#undef UINT_LEAST16_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#undef INT_LEAST32_MAX
#define INT_LEAST32_MAX INT32_MAX
#undef INT_LEAST32_MIN
#define INT_LEAST32_MIN INT32_MIN
#undef UINT_LEAST32_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#undef INT_LEAST64_MAX
#define INT_LEAST64_MAX INT64_MAX
#undef INT_LEAST64_MIN
#define INT_LEAST64_MIN INT64_MIN
#undef UINT_LEAST64_MAX
#define UINT_LEAST64_MAX UINT64_MAX

#undef INT_FAST8_MAX
#define INT_FAST8_MAX INT8_MAX
#undef INT_FAST8_MIN
#define INT_FAST8_MIN INT8_MIN
#undef UINT_FAST8_MAX
#define UINT_FAST8_MAX UINT8_MAX
#undef INT_FAST16_MAX
#define INT_FAST16_MAX INT16_MAX
#undef INT_FAST16_MIN
#define INT_FAST16_MIN INT16_MIN
#undef UINT_FAST16_MAX
#define UINT_FAST16_MAX UINT16_MAX
#undef INT_FAST32_MAX
#define INT_FAST32_MAX INT32_MAX
#undef INT_FAST32_MIN
#define INT_FAST32_MIN INT32_MIN
#undef UINT_FAST32_MAX
#define UINT_FAST32_MAX UINT32_MAX
#undef INT_FAST64_MAX
#define INT_FAST64_MAX INT64_MAX
#undef INT_FAST64_MIN
#define INT_FAST64_MIN INT64_MIN
#undef UINT_FAST64_MAX
#define UINT_FAST64_MAX UINT64_MAX

#undef INTPTR_MAX
#define INTPTR_MAX INT32_MAX
#undef INTPTR_MIN
#define INTPTR_MIN INT32_MIN
#undef UINTPTR_MAX
#define UINTPTR_MAX UINT32_MAX

#undef INTMAX_MAX
#define INTMAX_MAX INT64_MAX
#undef INTMAX_MIN
#define INTMAX_MIN INT64_MIN
#undef UINTMAX_MAX
#define UINTMAX_MAX UINT64_MAX

#undef PTRDIFF_MAX
#define PTRDIFF_MAX INT32_MAX
#undef PTRDIFF_MIN
#define PTRDIFF_MIN INT32_MIN

#undef SIG_ATOMIC_MAX
#define SIG_ATOMIC_MAX INT32_MAX
#undef SIG_ATOMIC_MIN
#define SIG_ATOMIC_MIN INT32_MIN

#undef SIZE_MAX
#define SIZE_MAX UINT32_MAX

#undef WCHAR_MAX
#define WCHAR_MAX INT32_MAX
#undef WCHAR_MIN
#define WCHAR_MIN INT32_MIN

#undef WINT_MAX
#define WINT_MAX UINT32_MAX
#undef WINT_MIN
#define WINT_MIN 0

#undef INT8_C
#define INT8_C(c)  c
#undef INT16_C
#define INT16_C(c) c
#undef INT32_C
#define INT32_C(c) c ## L
#undef INT64_C
#define INT64_C(c) c ## LL
#undef UINT8_C
#define UINT8_C(c)  c
#undef UINT16_C
#define UINT16_C(c) c
#undef UINT32_C
#define UINT32_C(c) c ## UL
#undef UINT64_C
#define UINT64_C(c) c ## ULL
#undef INTMAX_C
#define INTMAX_C(c) INT64_C(c)
#undef UINTMAX_C
#define UINTMAX_C(c) UINT64_C(c)

#undef INT8_WIDTH
#define INT8_WIDTH 8
#undef UINT8_WIDTH
#define UINT8_WIDTH 8
#undef INT16_WIDTH
#define INT16_WIDTH 16
#undef UINT16_WIDTH
#define UINT16_WIDTH 16
#undef INT32_WIDTH
#define INT32_WIDTH 32
#undef UINT32_WIDTH
#define UINT32_WIDTH 32
#undef INT64_WIDTH
#define INT64_WIDTH 64
#undef UINT64_WIDTH
#define UINT64_WIDTH 64

#undef INT_LEAST8_WIDTH
#define INT_LEAST8_WIDTH INT8_WIDTH
#undef UINT_LEAST8_WIDTH
#define UINT_LEAST8_WIDTH UINT8_WIDTH
#undef INT_LEAST16_WIDTH
#define INT_LEAST16_WIDTH INT16_WIDTH
#undef UINT_LEAST16_WIDTH
#define UINT_LEAST16_WIDTH UINT16_WIDTH
#undef INT_LEAST32_WIDTH
#define INT_LEAST32_WIDTH INT32_WIDTH
#undef UINT_LEAST32_WIDTH
#define UINT_LEAST32_WIDTH UINT32_WIDTH
#undef INT_LEAST64_WIDTH
#define INT_LEAST64_WIDTH INT64_WIDTH
#undef UINT_LEAST64_WIDTH
#define UINT_LEAST64_WIDTH UINT64_WIDTH

#undef INT_FAST8_WIDTH
#define INT_FAST8_WIDTH INT8_WIDTH
#undef UINT_FAST8_WIDTH
#define UINT_FAST8_WIDTH UINT8_WIDTH
#undef INT_FAST16_WIDTH
#define INT_FAST16_WIDTH INT16_WIDTH
#undef UINT_FAST16_WIDTH
#define UINT_FAST16_WIDTH UINT16_WIDTH
#undef INT_FAST32_WIDTH
#define INT_FAST32_WIDTH INT32_WIDTH
#undef UINT_FAST32_WIDTH
#define UINT_FAST32_WIDTH UINT32_WIDTH
#undef INT_FAST64_WIDTH
#define INT_FAST64_WIDTH INT64_WIDTH
#undef UINT_FAST64_WIDTH
#define UINT_FAST64_WIDTH UINT64_WIDTH

#undef INTPTR_WIDTH
#define INTPTR_WIDTH INT32_WIDTH
#undef UINTPTR_WIDTH
#define UINTPTR_WIDTH UINT32_WIDTH

#undef INTMAX_WIDTH
#define INTMAX_WIDTH INT64_WIDTH
#undef UINTMAX_WIDTH
#define UINTMAX_WIDTH UINT64_WIDTH

#undef PTRDIFF_WIDTH
#define PTRDIFF_WIDTH INT32_WIDTH

#undef SIG_ATOMIC_WIDTH
#define SIG_ATOMIC_WIDTH INT32_WIDTH

#undef SIZE_WIDTH
#define SIZE_WIDTH UINT32_WIDTH

#undef WCHAR_WIDTH
#define WCHAR_WIDTH INT32_WIDTH

#undef WINT_WIDTH
#define WINT_WIDTH UINT32_WIDTH

#endif