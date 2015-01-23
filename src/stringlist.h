/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "rucksack.h"

struct RuckSackString {
    char *str;
    int len;
};

struct RuckSackStringList {
    struct RuckSackString *strs;
    int cap;
    int len;
};

struct RuckSackStringList * rucksack_stringlist_create(void);
void rucksack_stringlist_destroy(struct RuckSackStringList *list);
enum RuckSackError rucksack_stringlist_append(struct RuckSackStringList *list,
        const char *str, int str_len);
