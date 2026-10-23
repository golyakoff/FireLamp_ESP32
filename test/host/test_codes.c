// Host test of code_hash.c: effect codes and parameter codes of every effect are stored as
// hashes, so the hashes must differ. The codes come from the firmware sources (gen_codes.py).
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "code_hash.h"
#include "codes.h"

#define COUNT (sizeof(effect_codes) / sizeof(effect_codes[0]))

static bool code_valid(const char *code)
{
    size_t len = strlen(code);
    if (!len || len > 16) // EFFECT_CODE_MAX_LEN
        return false;
    for (size_t i = 0; i < len; i++)
        if (!((code[i] >= 'a' && code[i] <= 'z') || (code[i] >= '0' && code[i] <= '9') || code[i] == '_'))
            return false;
    return true;
}

int main(void)
{
    int failures = 0;
    size_t params = 0;

    // the known collision is resolved, and the values stored for `wheel` stay its own
    assert(code_hash("relax") != code_hash("wheel"));
    assert(code_hash("wheel") == 0x289b);

    for (size_t e = 0; e < COUNT; e++)
    {
        const char *code = effect_codes[e].code;
        if (!code_valid(code))
        {
            printf("codes: invalid effect code '%s'\n", code);
            failures++;
        }
        for (size_t e2 = e + 1; e2 < COUNT; e2++)
            if (code_hash(code) == code_hash(effect_codes[e2].code))
            {
                printf("codes: effect codes '%s' and '%s' have the same hash 0x%04x\n",
                       code, effect_codes[e2].code, code_hash(code));
                failures++;
            }

        const char *const *p = effect_codes[e].params;
        for (size_t i = 0; p[i]; i++, params++)
        {
            if (!code_valid(p[i]))
            {
                printf("codes: invalid parameter code '%s.%s'\n", code, p[i]);
                failures++;
            }
            for (size_t j = i + 1; p[j]; j++)
                if (code_hash(p[i]) == code_hash(p[j]))
                {
                    printf("codes: parameter codes '%s.%s' and '%s' have the same hash 0x%04x\n",
                           code, p[i], p[j], code_hash(p[i]));
                    failures++;
                }
        }
    }

    printf("codes: %zu effects, %zu parameters checked\n", COUNT, params);
    assert(failures == 0);
    printf("codes: all tests passed\n");
    return 0;
}
