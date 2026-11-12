#include <wchar.h>
#include <errno.h>
#include <stdlib.h>

size_t wcrtomb(char *restrict s, wchar_t wc, mbstate_t *restrict ps) {
    if (!s)
        return 1;

    // the maximum value representable in a UTF-8 codepoint
    // defined up to U+10FFFF (SPUA-B)
    // actual maximum is U+1FFFFF
    if (wc < 0 || wc > 0x10FFFF) {
        ___set_errno(EILSEQ);
        return -1;
    }

    wint_t c = wc;
    int b = 0;
    // 1 bit more than representable in the continuation to not produce extra bytes
    while (c & ~0x7F) {
        b++;
        c >>= 6;
    }
    if (b == 0) {
        *s = c;
        return 1;
    }
    if (__builtin_clz(c) - 24 < b + 2)
        b++;
    c = wc;
    int i = b;
    while (c & ~0x7F) {
        s[i] = (c & 0x3F) | 0x80;
        i--;
        c >>= 6;
    }
    if (__builtin_clz(c) - 24 < b + 1) {
        s[i] = (c & 0x3F) | 0x80;
        c >>= 6;
    }
    int final = 0b11111111 & ~((1 << (7 - b)) - 1);
    final |= (int)c;
    s[0] = final;
    return b + 1;
}

size_t mbrtowc(wchar_t *restrict pwc, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!n)
        return (size_t)-2;
    static mbstate_t internal = 0;
    wchar_t __pwc;
    const unsigned char * _s = (void*)s;

    if (!ps)
        ps = &internal;
    if (!s) {
        if (*ps)
            goto ilseq;
        return 0;
    }
    if (!pwc)
        pwc = &__pwc;

    int i = 0;
    if (*ps == 0) {
        if (*_s < 128) {
            *pwc = (wchar_t)*_s;
            return 1;
        }
        unsigned int w = __builtin_clz((unsigned char)~*_s) - 24;
        if (w > 4 || w == 1)
            goto ilseq;
        w--;
        *ps = *_s & ((1 << (6 - w)) - 1);
        *ps |= w << 30;
        i++;
    }

    unsigned int remain = *ps >> 30;

    for (; i < n; i++) {
        if (remain && (_s[i] & 0xC0) != 0x80)
            goto ilseq;
        *ps <<= 6;
        *ps |= _s[i] & 0x3F;
        remain--;
        if (remain == 0) {
            *pwc = *ps;
            *ps = 0;
            if (*pwc == 0)
                return 0;
            return i + 1;
        }
    }
    *ps &= ~(3 << 30);
    *ps |= remain << 30;
    return -2;
    ilseq:
    ___set_errno(EILSEQ);
    *ps = 0;
    return -1;
}

size_t mbrlen(const char *restrict s, size_t n, mbstate_t *restrict ps) {
    mbstate_t internal = 0;
    return mbrtowc(NULL, s, n, ps != NULL ? ps : &internal);
}

// there's a lot of talk in the POSIX definition of this function regarding if dst is non-NULL
// however nothing about if it is NULL, so I decided to just perform the conversion and not save the result
size_t mbsnrtowcs(wchar_t *restrict dst, const char **restrict src, size_t nmc, size_t len, mbstate_t *restrict ps) {
    const char * s = *src;
    static mbstate_t internal = 0;
    if (!ps)
        ps = &internal;

    size_t i = 0;
    for (; (!dst || i < len) && s - *src < nmc; i++) {
        size_t to_read = nmc - (s - *src);
        to_read = to_read < MB_CUR_MAX ? to_read : MB_CUR_MAX;
        wchar_t wc;
        size_t delta = mbrtowc(&wc, s, to_read, ps);
        if (delta == -2)
            break;
        if (delta == -1)
            return -1;
        if (dst)
            *dst++ = wc;
        if (wc == 0) {
            *src = NULL;
            return i;
        }
        s += delta;
    }
    *src = s;
    return i;
}
size_t mbsrtowcs(wchar_t *restrict dst, const char **restrict src, size_t len, mbstate_t *restrict ps) {
    return mbsnrtowcs(dst, src, -1, len, ps);
}

#include <string.h>
size_t wcsnrtombs(char *restrict dst, const wchar_t **restrict src, size_t nwc, size_t len, mbstate_t *restrict ps) {
    const wchar_t * s = *src;
    static mbstate_t internal = 0;
    if (!ps)
        ps = &internal;

    size_t i = 0;
    for (; i < len && s - *src < nwc; s++) {
        size_t to_write = len - i;
        to_write = to_write < MB_CUR_MAX ? to_write : MB_CUR_MAX;
        char buf[MB_CUR_MAX];
        size_t written = wcrtomb(buf, *s, ps);
        if (written == -1)
            return -1;
        if (written > to_write) {
            *src = s;
            return i;
        }
        memcpy(dst, buf, written);
        if (*s == 0) {
            *src = NULL;
            return i;
        }
        dst += written;
        i += written;
    }
    *src = s;
    return i;
}
size_t wcsrtombs(char *restrict dst, const wchar_t **restrict src, size_t len, mbstate_t *restrict ps) {
    return wcsnrtombs(dst, src, -1, len, ps);
}