/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#undef NDEBUG

#include <rucksack.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static void ok(int err) {
    if (!err) return;
    fprintf(stderr, "Error: %s\n", rucksack_err_str(err));
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
    ok(rucksack_file_read(entry, (unsigned char *)buf));
    buf[10] = 0;
    assert(strcmp(buf, "aoeu\n1234\n") == 0);

    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));

    entry = rucksack_bundle_find_file(bundle, "blah");
    assert(entry);

    size = rucksack_file_size(entry);
    assert(size == 10);

    memset(buf, 0, 11);
    ok(rucksack_file_read(entry, (unsigned char *)buf));
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

    img.name = "../test/file0.png";
    ok(rucksack_page_add_image(page, "image0", &img));

    img.name = "../test/file1.png";
    ok(rucksack_page_add_image(page, "image1", &img));

    img.name = "../test/file2.png";
    ok(rucksack_page_add_image(page, "image2", &img));

    img.name = "../test/file3.png";
    ok(rucksack_page_add_image(page, "image3", &img));

    ok(rucksack_bundle_add_page(bundle, "texture_foo", page));

    rucksack_page_destroy(page);

    ok(rucksack_bundle_close(bundle));

    // now try to read it
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "texture_foo");
    assert(entry);

    struct RuckSackTexture *texture;
    ok(rucksack_file_open_texture(entry, &texture));

    long image_count = rucksack_texture_image_count(texture);
    assert(image_count == 4);

    struct RuckSackImage *images = malloc(sizeof(struct RuckSackImage) * image_count);
    rucksack_texture_get_images(texture, &images);
    char got_them[4];
    for (int i = 0; i < image_count; i += 1) {
        struct RuckSackImage *image = &images[i];
        if (strcmp(image->name, "image0") == 0) {
            got_them[0] = 1;
            assert(image->width == 8);
            assert(image->height == 8);
        } else if (strcmp(image->name, "image1") == 0) {
            got_them[1] = 1;
            assert(image->width == 16);
            assert(image->height == 16);
        } else if (strcmp(image->name, "image2") == 0) {
            got_them[2] = 1;
            assert(image->width == 16);
            assert(image->height == 16);
        } else if (strcmp(image->name, "image3") == 0) {
            got_them[3] = 1;
            assert(image->width == 8);
            assert(image->height == 8);
        }
    }
    assert(got_them[0]);
    assert(got_them[1]);
    assert(got_them[2]);
    assert(got_them[3]);

    rucksack_texture_close(texture);

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

        img.name = "../test/radar-circle.png";
        ok(rucksack_page_add_image(page, "radarCircle", &img));

        img.name = "../test/arrow.png";
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
    ok(rucksack_file_read(entry, buffer));
    free(buffer);

    ok(rucksack_bundle_close(bundle));
}

static void test_three_files(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    ok(rucksack_bundle_add_file(bundle, "blah", "../test/blah.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", "../test/globby/globby1.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby2.txt", "../test/globby/globby2.txt"));

    ok(rucksack_bundle_close(bundle));
}

static void test_16kb_file(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));
    ok(rucksack_bundle_add_file(bundle, "monkey.obj", "../test/monkey.obj"));
    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "monkey.obj");
    assert(entry);

    long size = rucksack_file_size(entry);
    assert(size == 23875);

    unsigned char *buffer = malloc(size);
    ok(rucksack_file_read(entry, buffer));

    assert(buffer[0] == '#');
    assert(buffer[size - 2] == '1');

    free(buffer);

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
    {"add a file larger than 16KB", test_16kb_file},
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
