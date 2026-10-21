/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The part of the tinyalsa mixer API that audio_logic.c uses. On Android this is
 * external/tinyalsa's <tinyalsa/asoundlib.h>. The host tests (GLASS_HOST_TEST) have no
 * AOSP headers, so they get the same prototypes here and link tests/mock_tinyalsa.c.
 */
#ifndef GLASS_TINYALSA_H
#define GLASS_TINYALSA_H

#ifdef GLASS_HOST_TEST
struct mixer;
struct mixer_ctl;

struct mixer_ctl *mixer_get_ctl_by_name(struct mixer *mixer, const char *name);
unsigned int mixer_ctl_get_num_values(struct mixer_ctl *ctl);
int mixer_ctl_set_value(struct mixer_ctl *ctl, unsigned int id, int value);
int mixer_ctl_set_enum_by_string(struct mixer_ctl *ctl, const char *string);
#else
#include <tinyalsa/asoundlib.h>
#endif

#endif
