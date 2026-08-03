#include <string.h>
#include <stdlib.h>

int getsubopt(char **restrict optionp, char * const *restrict keylistp, char **restrict valuep) {
    char * this = *optionp;
    *optionp = strchrnul(*optionp, ',');
    *valuep = NULL;
    if (**optionp) *(*optionp)++ = '\0';

    char * eq = strchrnul(this, '=');
    for (int i = 0; keylistp[i]; i++) {
        if (strncmp(this, keylistp[i], eq - this) != 0)
            continue;
        if (keylistp[i][eq - this] != '\0')
            continue;
        if (*eq) *valuep = eq + 1;
        return i;
    }
    return -1;
}