/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#undef NDEBUG

#include "rucksack.h"
#include "spritesheet.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <FreeImage.h>

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

    ok(rucksack_bundle_add_file(bundle, "blah", -1, "../test/blah.txt"));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "blah", -1);
    assert(entry);

    size_t size = rucksack_file_size(entry);
    assert(size == 10);

    int is_texture;
    ok(rucksack_file_is_texture(entry, &is_texture));
    assert(is_texture == 0);

    char buf[11];
    ok(rucksack_file_read(entry, (unsigned char *)buf));
    buf[10] = 0;
    assert(strcmp(buf, "aoeu\n1234\n") == 0);

    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));

    entry = rucksack_bundle_find_file(bundle, "blah", -1);
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

    struct RuckSackTexture *texture = rucksack_texture_create();
    assert(texture);

    struct RuckSackImage *img = rucksack_image_create();
    assert(img);

    img->path = "../test/file0.png";
    img->key = "image0";
    img->anchor_x = 3.5f;
    img->anchor_y = 4.0f;
    img->anchor = RuckSackAnchorExplicit;
    ok(rucksack_texture_add_image(texture, img));

    img->path = "../test/file1.png";
    img->key = "image1";
    img->anchor = RuckSackAnchorCenter;
    ok(rucksack_texture_add_image(texture, img));

    img->path = "../test/file2.png";
    img->key = "image2";
    img->anchor = RuckSackAnchorRight;
    ok(rucksack_texture_add_image(texture, img));

    img->path = "../test/file3.png";
    img->key = "image3";
    img->anchor = RuckSackAnchorLeft;
    ok(rucksack_texture_add_image(texture, img));
    rucksack_image_destroy(img);

    texture->key = "texture_foo";
    ok(rucksack_bundle_add_texture(bundle, texture));

    rucksack_texture_destroy(texture);

    ok(rucksack_bundle_close(bundle));

    // now try to read it
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "texture_foo", -1);
    assert(entry);

    int is_texture;
    ok(rucksack_file_is_texture(entry, &is_texture));
    assert(is_texture == 1);

    ok(rucksack_file_open_texture(entry, &texture));

    long image_count = rucksack_texture_image_count(texture);
    assert(image_count == 4);

    struct RuckSackImage **images = malloc(sizeof(struct RuckSackImage *) * image_count);
    rucksack_texture_get_images(texture, images);
    char got_them[4];
    for (int i = 0; i < image_count; i += 1) {
        struct RuckSackImage *image = images[i];
        if (strcmp(image->key, "image0") == 0) {
            got_them[0] = 1;
            assert(image->width == 8);
            assert(image->height == 8);
            assert(image->anchor_x == 3.5f);
            assert(image->anchor_y == 4.0f);
            assert(image->anchor == RuckSackAnchorExplicit);
        } else if (strcmp(image->key, "image1") == 0) {
            got_them[1] = 1;
            assert(image->width == 16);
            assert(image->height == 16);
            assert(image->anchor_x == 8.0f);
            assert(image->anchor_y == 8.0f);
            assert(image->anchor == RuckSackAnchorCenter);
        } else if (strcmp(image->key, "image2") == 0) {
            got_them[2] = 1;
            assert(image->width == 16);
            assert(image->height == 16);
            assert(image->anchor_x == 16.0f);
            assert(image->anchor_y == 8.0f);
            assert(image->anchor == RuckSackAnchorRight);
        } else if (strcmp(image->key, "image3") == 0) {
            got_them[3] = 1;
            assert(image->width == 8);
            assert(image->height == 8);
            assert(image->anchor_x == 0.0f);
            assert(image->anchor_y == 4.0f);
            assert(image->anchor == RuckSackAnchorLeft);
        }
    }
    free(images);
    assert(got_them[0]);
    assert(got_them[1]);
    assert(got_them[2]);
    assert(got_them[3]);

    long texture_size = rucksack_texture_size(texture);
    unsigned char *buffer = malloc(texture_size);
    assert(buffer);
    ok(rucksack_texture_read(texture, buffer));

    FIMEMORY *fi_mem = FreeImage_OpenMemory(buffer, texture_size);
    assert(fi_mem);
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(fi_mem, 0);
    assert(fif == FIF_PNG);
    FIBITMAP *bmp = FreeImage_LoadFromMemory(fif, fi_mem, 0);
    assert(FreeImage_HasPixels(bmp));
    assert(FreeImage_GetWidth(bmp) == 16);
    assert(FreeImage_GetHeight(bmp) == 64);

    FreeImage_Unload(bmp);
    FreeImage_CloseMemory(fi_mem);
    free(buffer);

    rucksack_texture_destroy(texture);

    ok(rucksack_bundle_close(bundle));
}

static void test_bundling_twice(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    for (int i = 0; i < 2; i += 1) {
        struct RuckSackBundle *bundle;
        ok(rucksack_bundle_open(bundle_name, &bundle));

        struct RuckSackTexture *texture = rucksack_texture_create();
        assert(texture);

        struct RuckSackImage *img = rucksack_image_create();
        assert(img);

        img->path = "../test/radar-circle.png";
        img->key = "radarCircle";
        ok(rucksack_texture_add_image(texture, img));

        img->path = "../test/arrow.png";
        img->key = "arrow";
        ok(rucksack_texture_add_image(texture, img));
        rucksack_image_destroy(img);

        texture->key = "cockpit";
        ok(rucksack_bundle_add_texture(bundle, texture));

        rucksack_texture_destroy(texture);

        ok(rucksack_bundle_close(bundle));
    }

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "cockpit", -1);
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

    ok(rucksack_bundle_add_file(bundle, "blah", -1, "../test/blah.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", -1, "../test/globby/globby1.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby2.txt", -1, "../test/globby/globby2.txt"));

    rucksack_bundle_delete_untouched(bundle);

    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));

    long count = rucksack_bundle_file_count(bundle);
    assert(count == 3);

    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", -1, "../test/globby/globby1.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby2.txt", -1, "../test/globby/globby2.txt"));

    rucksack_bundle_delete_untouched(bundle);

    count = rucksack_bundle_file_count(bundle);
    assert(count == 2);

    ok(rucksack_bundle_close(bundle));
}

static void test_16kb_file(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));
    ok(rucksack_bundle_add_file(bundle, "monkey.obj", -1, "../test/monkey.obj"));
    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "monkey.obj", -1);
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

static void test_empty_bundle(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    // create empty file
    FILE *f = fopen(bundle_name, "w");
    assert(f);
    int ret = fclose(f);
    assert(ret == 0);

    // open and close bundle should be OK
    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));
    ok(rucksack_bundle_close(bundle));
}

static void test_non_default_texture_props(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    struct RuckSackTexture *texture = rucksack_texture_create();
    assert(texture);
    texture->max_width = 256;
    texture->max_height = 128;
    texture->pow2 = 0;
    texture->allow_r90 = 0;

    struct RuckSackImage *img = rucksack_image_create();
    assert(img);

    img->path = "../test/file0.png";
    img->key = "image0";
    ok(rucksack_texture_add_image(texture, img));
    texture->key = "texture_foo";
    ok(rucksack_bundle_add_texture(bundle, texture));
    rucksack_image_destroy(img);
    rucksack_texture_destroy(texture);

    ok(rucksack_bundle_close(bundle));

    // now make sure the properties persisted
    ok(rucksack_bundle_open(bundle_name, &bundle));
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "texture_foo", -1);
    assert(entry);

    ok(rucksack_file_open_texture(entry, &texture));

    long image_count = rucksack_texture_image_count(texture);
    assert(image_count == 1);

    assert(texture->max_width == 256);
    assert(texture->max_height == 128);
    assert(texture->pow2 == 0);
    assert(texture->allow_r90 == 0);

    rucksack_texture_destroy(texture);

    ok(rucksack_bundle_close(bundle));
}

static void test_open_read_only(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    ok(rucksack_bundle_add_file(bundle, "blah", -1, "../test/blah.txt"));
    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", -1, "../test/globby/globby1.txt"));

    ok(rucksack_bundle_close(bundle));

    chmod(bundle_name, 0440);

    ok(rucksack_bundle_open_read(bundle_name, &bundle));

    enum RuckSackError err = rucksack_bundle_add_file(bundle, "g_globby2.txt", -1, "../test/globby/globby2.txt");
    assert(err == RuckSackErrorFileAccess);

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "blah", -1);
    assert(entry);
    size_t size = rucksack_file_size(entry);
    assert(size == 10);
    char buf[11];
    ok(rucksack_file_read(entry, (unsigned char *)buf));
    buf[10] = 0;
    assert(strcmp(buf, "aoeu\n1234\n") == 0);

    ok(rucksack_bundle_close(bundle));

    chmod(bundle_name, 0660);
    remove(bundle_name);
}

static void test_delete_from_bundle(void) {
    const char *bundle_name = "test.bundle";
    remove(bundle_name);

    struct RuckSackBundle *bundle;
    ok(rucksack_bundle_open(bundle_name, &bundle));

    ok(rucksack_bundle_add_file(bundle, "blah", -1, "../test/blah.txt"));
    ok(rucksack_bundle_add_file(bundle, "monkey.obj", -1, "../test/monkey.obj"));
    ok(rucksack_bundle_add_file(bundle, "g_globby1.txt", -1, "../test/globby/globby1.txt"));

    ok(rucksack_bundle_delete_file(bundle, "monkey.obj", -1));
    assert(rucksack_bundle_delete_file(bundle, "monkey.obj", -1) == RuckSackErrorNotFound);

    ok(rucksack_bundle_add_file(bundle, "g_globby2.txt", -1, "../test/globby/globby2.txt"));

    ok(rucksack_bundle_close(bundle));

    ok(rucksack_bundle_open(bundle_name, &bundle));
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "g_globby2.txt", -1);
    assert(entry);

    long size = rucksack_file_size(entry);
    assert(size == 9);

    unsigned char *buffer = malloc(size);
    ok(rucksack_file_read(entry, buffer));

    assert(memcmp(buffer, "electric", 8) == 0);

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
    {"write to an empty bundle", test_empty_bundle},
    {"non-default texture properties", test_non_default_texture_props},
    {"open bundle read-only", test_open_read_only},
    {"delete from a bundle", test_delete_from_bundle},
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
