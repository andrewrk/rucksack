/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "stringlist.h"
#include "util.h"

#include <string.h>
#include <stdlib.h>

struct RuckSackStringList * rucksack_stringlist_create(void) {
    return calloc(1, sizeof(struct RuckSackStringList));
}

void rucksack_stringlist_destroy(struct RuckSackStringList *list) {
    for (int i = 0; i < list->len; i += 1) {
        struct RuckSackString *str = &list->strs[i];
        free(str->str);
    }
    free(list->strs);
    free(list);
}

enum RuckSackError rucksack_stringlist_append(struct RuckSackStringList *list,
        const char *str, int str_len)
{
    char *s = dupe_string(str, &str_len);

    if (!s)
        return RuckSackErrorNoMem;

    if (list->len >= list->cap) {
        list->cap = 2 * list->cap + 64;
        struct RuckSackString *new_ptr = realloc(list->strs,
                list->cap * sizeof(struct RuckSackString));
        if (!new_ptr)
            return RuckSackErrorNoMem;
        list->strs = new_ptr;
    }
    struct RuckSackString *rs_str = &list->strs[list->len];
    rs_str->str = s;
    rs_str->len = str_len;
    list->len += 1;
    return RuckSackErrorNone;
}
