/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A fake tinyalsa mixer for the host tests of audio/audio_logic.c: the mixer calls it
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

/* Like tinyalsa: no range check, the value is stored as given. */
int mixer_ctl_set_value(struct mixer_ctl *ctl, unsigned int id, int value)
{
    if (ctl->enums || id >= ctl->num_values)
        return -EINVAL;
    ctl->values[id] = value;
    ctl->sets++;
    return 0;
}

/* Like tinyalsa: -EINVAL for a control that is not an integer one. */
int mixer_ctl_get_range_min(struct mixer_ctl *ctl)
{
    return ctl->enums ? -EINVAL : ctl->min;
}

int mixer_ctl_get_range_max(struct mixer_ctl *ctl)
{
    return ctl->enums ? -EINVAL : ctl->max;
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
