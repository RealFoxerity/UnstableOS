#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t nel, size_t width, int (*compar)(const void *, const void *)) {
    while (nel > 0) {
        const void * t = base + width * (nel / 2);
        int c = compar(key, t);
        if (c == 0)
            return (void*)t;
        if (c > 0) {
            base = t + width;
            nel -= nel / 2;
            nel -= 1;
        } else nel /= 2;
    }
    return NULL;
}