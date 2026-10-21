/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A fake tinyalsa mixer for the host tests of audio/audio_logic.c: the four mixer calls it
 * makes, with the prototypes of audio/glass_tinyalsa.h, over a table of controls that the
 * test sets up. Each control records its values, and every successful set is counted.
 */
#include "mock_tinyalsa.h"

#include <errno.h>
#include <string.h>

struct mixer_ctl *mixer_get_ctl_by_name(struct mixer *mixer, const char *name)
{
    size_t i;

    for (i = 0; i < mixer->count; i++) {
        if (!strcmp(mixer->ctls[i].name, name))
            return &mixer->ctls[i];
    }
    return NULL;
}

unsigned int mixer_ctl_get_num_values(struct mixer_ctl *ctl)
{
    return ctl->num_values;
}

int mixer_ctl_set_value(struct mixer_ctl *ctl, unsigned int id, int value)
{
    if (ctl->enums || id >= ctl->num_values || value < ctl->min || value > ctl->max)
        return -EINVAL;
    ctl->values[id] = value;
    ctl->sets++;
    return 0;
}

int mixer_ctl_set_enum_by_string(struct mixer_ctl *ctl, const char *string)
{
    unsigned int i;

    if (!ctl->enums)
        return -EINVAL;
    for (i = 0; ctl->enums[i]; i++) {
        if (!strcmp(ctl->enums[i], string)) {
            ctl->values[0] = (int)i;
            ctl->sets++;
            return 0;
        }
    }
    return -EINVAL;
}
