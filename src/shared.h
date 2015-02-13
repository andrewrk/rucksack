/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef RUCKSACK_SHARED_H_INCLUDED
#define RUCKSACK_SHARED_H_INCLUDED

// everything in this file is shared by both rucksack.c and spritesheet.c

#include <stdint.h>
#include <FreeImage.h>

#define MAX(x, y) ((x) > (y) ? (x) : (y))

static const int UUID_SIZE = 16;
static const char *TEXTURE_UUID = "\x0e\xb1\x4c\x84\x47\x4c\xb3\xad\xa6\xbd\x93\xe4\xbe\xa5\x46\xba";
static const int TEXTURE_HEADER_LEN = 38;
static const int IMAGE_HEADER_LEN = 37; // not taking into account key bytes
static const float FIXED_POINT_N = 16384.0f;

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct RuckSackTexturePrivate {
    struct RuckSackTexture externals;

    struct RuckSackImagePrivate *images;
    int images_count;
    int images_size;

    struct Rect *free_positions;
    int free_pos_count;
    int free_pos_size;
    int garbage_count;

    int width;
    int height;

    // for reading
    struct RuckSackFileEntry *entry;
    long pixel_data_offset;
    long pixel_data_size;
};

struct RuckSackFileEntry {
    struct RuckSackBundlePrivate *b;
    long offset;
    long size;
    long allocated_size;
    int key_size;
    long mtime;
    char *key;
    int is_open; // flag for when an out stream is writing to this entry
    int touched; // flag, set when the entry is written to
};

struct RuckSackOutStream {
    struct RuckSackBundlePrivate *b;
    struct RuckSackFileEntry *e;
};

struct RuckSackImagePrivate {
    struct RuckSackImage externals;

    FIBITMAP *bmp;
};

static void write_uint32be(unsigned char *buf, uint32_t x) {
    buf[3] = x & 0xff;

    x >>= 8;
    buf[2] = x & 0xff;

    x >>= 8;
    buf[1] = x & 0xff;

    x >>= 8;
    buf[0] = x & 0xff;
}

#endif
