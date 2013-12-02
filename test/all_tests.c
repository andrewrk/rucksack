/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

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

    ok(rucksack_bundle_open(bundle_name, &bundle));

    entry = rucksack_bundle_find_file(bundle, "blah");
    assert(entry);

    size = rucksack_file_size(entry);
    assert(size == 10);

    memset(buf, 0, 11);
    ok(rucksack_bundle_file_read(bundle, entry, (unsigned char *)buf));
    assert(strcmp(buf, "aoeu\n1234\n") == 0);


    ok(rucksack_bundle_close(bundle));
}

static void test_texture_packing(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);
    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackPage *page = rucksack_page_create();
    assert(page);

    struct RuckSackImage img;
    img.anchor = RuckSackAnchorCenter;

    img.path = "../test/file0.png";
    ok(rucksack_page_add_image(page, "image0", &img));

    img.path = "../test/file1.png";
    ok(rucksack_page_add_image(page, "image1", &img));

    img.path = "../test/file2.png";
    ok(rucksack_page_add_image(page, "image2", &img));

    img.path = "../test/file3.png";
    ok(rucksack_page_add_image(page, "image3", &img));

    ok(rucksack_bundle_add_page(bundle, "texture_foo", page));

    rucksack_page_destroy(page);

    ok(rucksack_bundle_close(bundle));
}

static void test_bundling_twice(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    for (int i = 0; i < 2; i += 1) {
        struct RuckSackBundle *bundle;
        ok(rucksack_bundle_open(bundle_name, &bundle));

        struct RuckSackPage *page = rucksack_page_create();
        assert(page);

        struct RuckSackImage img;
        img.anchor = RuckSackAnchorCenter;

        img.path = "../test/radar-circle.png";
        ok(rucksack_page_add_image(page, "radarCircle", &img));

        img.path = "../test/arrow.png";
        ok(rucksack_page_add_image(page, "arrow", &img));

        ok(rucksack_bundle_add_page(bundle, "cockpit", page));

        rucksack_page_destroy(page);

        ok(rucksack_bundle_close(bundle));
    }

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "cockpit");
    assert(entry);

    size_t size = rucksack_file_size(entry);
    unsigned char *buffer = malloc(size);
    ok(rucksack_bundle_file_read(bundle, entry, buffer));
    free(buffer);

    ok(rucksack_bundle_close(bundle));
}

static void test_three_files(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    ok(rucksack_bundle_add_file(bundle, "blah", "../test/blah.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", "../test/globby1.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby2.txt", "../test/globby2.txt"));

    ok(rucksack_bundle_close(bundle));
}

struct Test {
    const char *name;
    void (*fn)(void);
};

static struct Test tests[] = {
    {"opening and closing", test_open_close},
    {"writing and reading", test_write_read},
    {"texture packing", test_texture_packing},
    {"bundling twice", test_bundling_twice},
    {"add 3 files", test_three_files},
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
