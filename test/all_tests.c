#include <rucksack.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static const char *RS_ERROR_STR[] = {
    "",
    "out of memory",
    "problem accessing file",
    "invalid bundle format",
    "invalid anchor enum value",
    "cannot fit all images into page",
    "image has no pixels",
    "unrecognized image format",
    "key not found",
};

static void ok(int err) {
    if (!err) return;
    fprintf(stderr, "Error: %s\n", RS_ERROR_STR[err]);
    assert(0);
}

static void test_open_close(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);
    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));
    ok(rucksack_bundle_close(bundle));
}

static void test_write_read(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);
    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    ok(rucksack_bundle_add_file(bundle, "blah", "../test/blah.txt"));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "blah");
    assert(entry);

    size_t size = rucksack_file_size(entry);
    assert(size == 10);

    char buf[11];
    ok(rucksack_bundle_file_read(bundle, entry, (unsigned char *)buf));
    buf[10] = 0;
    assert(strcmp(buf, "aoeu\n1234\n") == 0);

    ok(rucksack_bundle_close(bundle));
}

struct Test {
    const char *name;
    void (*fn)(void);
};

static struct Test tests[] = {
    {"opening and closing", test_open_close},
    {"writing and reading", test_write_read},
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
