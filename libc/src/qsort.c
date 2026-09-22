#include <stdlib.h>
#include <stddef.h>
#include <errno.h>

static void qsort_swap(size_t w, void * a, void * b) {
    for (size_t i = 0; i < w; i++, a++, b++) {
        unsigned char t = *(unsigned char *)a;
        *(unsigned char *)a = *(unsigned char *)b;
        *(unsigned char *)b = t;
    }
}

static void * qsort_part(size_t w, void * a, void * b, int (*compar)(const void *, const void *, void *), void *arg) {
    void * i = a - w;
    void * j = b + w;

    while (1) {
        do {
            i += w;
        } while (compar(i, a, arg) < 0);
        do {
            j -= w;
        } while (compar(j, a, arg) > 0);
        if (i >= j)
            return j;
        qsort_swap(w, i, j);
    }
}

static void qsort_sort(size_t w, void * a, void * b, int (*compar)(const void *, const void *, void *), void *arg) {
    if (a >= b)
        return;
    void * p = qsort_part(w, a, b, compar, arg);
    qsort_sort(w, a, p, compar, arg);
    qsort_sort(w, p + w, b, compar, arg);
}

void qsort_r(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *, void *), void *arg) {
    if (nel == 0 || nel == 1)
        return;
    qsort_sort(width, base, base + (nel-1)*width, compar, arg);
}

void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *)) {
    // shouldn't be a problem recasting and setting the extra argument like this
    qsort_r(base, nel, width, (int(*)(const void *, const void *, void*))compar, NULL);
}