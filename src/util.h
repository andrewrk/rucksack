/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef RUCKSACK_UTIL_H_INCLUDED
#define RUCKSACK_UTIL_H_INCLUDED

#include <string.h>
#include <stdlib.h>

// str_len should be -1 for unknown or a valid length
// if str_len is -1 it will be populated with the correct length
static char *dupe_string(const char *str, int *str_len) {
    if (*str_len == -1)
        *str_len = strlen(str);
    char *out = malloc(*str_len + 1);
    if (out) {
        memcpy(out, str, *str_len);
        out[*str_len] = 0;
    }
    return out;
}

#endif /* RUCKSACK_UTIL_H_INCLUDED */
