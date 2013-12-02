#include <rucksack.h>
#include <stdio.h>

static const char *RS_ERROR_STR[] = {
    "",
    "out of memory",
    "problem accessing file",
    "invalid bundle format",
    "invalid anchor enum value",
    "cannot fit all images into page",
    "image has no pixels",
    "unrecognized image format",
};

static void ok(int err) {
    if (!err) return;
    fprintf(stderr, "Error: %s\n", RS_ERROR_STR[err]);
    exit(1);
}

static void test_open_close(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);
    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));
    ok(rucksack_bundle_close(bundle));
}

struct Test {
    const char *name;
    void (*fn)(void);
};

static struct Test tests[] = {
    {"opening and closing", test_open_close},
    {NULL, NULL},
};

int main(int argc, char *argv[]) {
    struct Test *test = &tests[0];

    while (test->name) {
        fprintf(stderr, "testing %s...", test->name);
        test->fn();
        fprintf(stderr, "OK\n");
        test += 1;
    }

    return 0;
}
