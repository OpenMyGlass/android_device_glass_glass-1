/* SPDX-License-Identifier: Apache-2.0 */
/* The fake mixer of tests/mock_tinyalsa.c. */
#ifndef GLASS_MOCK_TINYALSA_H
#define GLASS_MOCK_TINYALSA_H

#include <stddef.h>

#include "../audio/glass_tinyalsa.h"

#define MOCK_MAX_VALUES 2

struct mixer_ctl {
    const char *name;
    unsigned int num_values;
    int min, max;                /* integer controls */
    const char *const *enums;    /* NULL-terminated; NULL for an integer control */
    int values[MOCK_MAX_VALUES]; /* an enum's item index is values[0] */
    int sets;                    /* successful set calls */
};

struct mixer {
    struct mixer_ctl *ctls;
    size_t count;
};

#endif
