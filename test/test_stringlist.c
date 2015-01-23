#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "stringlist.h"

static void test_create_destroy(void) {
    struct RuckSackStringList *list = rucksack_stringlist_create();

    rucksack_stringlist_destroy(list);
}

static void test_dupe_str(void) {
    struct RuckSackStringList *list = rucksack_stringlist_create();

    char foo[100] = "derp";

    rucksack_stringlist_append(list, foo, -1);

    foo[0] = 't';

    struct RuckSackString *str = &list->strs[0];
    assert(strcmp(str->str, "derp") == 0);

    rucksack_stringlist_destroy(list);
}

struct Test {
    const char *name;
    void (*fn)(void);
};

static struct Test tests[] = {
    {"create and destroy", test_create_destroy},
    {"duplicates strings", test_dupe_str},
    {NULL, NULL},
};

static void exec_test(struct Test *test) {
    fprintf(stderr, "testing %s...", test->name);
    test->fn();
    fprintf(stderr, "OK\n");
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        int index = atoi(argv[1]);
        exec_test(&tests[index]);
        return 0;
    }

    struct Test *test = &tests[0];

    while (test->name) {
        exec_test(test);
        test += 1;
    }

    return 0;
}
