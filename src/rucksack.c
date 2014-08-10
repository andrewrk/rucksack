/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "rucksack.h"
#include "config.h"

#include <FreeImage.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>


#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

static const int UUID_SIZE = 16;
static const char *BUNDLE_UUID = "\x60\x70\xc8\x99\x82\xa1\x41\x84\x89\x51\x08\xc9\x1c\xc9\xb6\x20";
static const char *TEXTURE_UUID = "\x0e\xb1\x4c\x84\x47\x4c\xb3\xad\xa6\xbd\x93\xe4\xbe\xa5\x46\xba";

static const int BUNDLE_VERSION = 1;
static const int MAIN_HEADER_LEN = 28;
static const int HEADER_ENTRY_LEN = 36; // not taking into account key bytes
static const int TEXTURE_HEADER_LEN = 38;
static const int IMAGE_HEADER_LEN = 37; // not taking into account key bytes

static const char *ERROR_STR[] = {
    "",
    "out of memory",
    "problem accessing file",
    "invalid bundle format",
    "bundle version mismatch",
    "bundle is an empty file",
    "invalid anchor enum value",
    "cannot fit all images into texture",
    "image has no pixels",
    "unrecognized image format",
    "key not found",
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
};

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;

    FILE *f;

    long int first_header_offset;
    struct RuckSackFileEntry *entries;
    long int header_entry_count; // actual count of entries
    long int header_entry_mem_count; // allocated memory entry count

    // keep some stuff cached for quick access
    struct RuckSackFileEntry *first_entry;
    struct RuckSackFileEntry *last_entry;
    long int headers_byte_count;
    long int first_file_offset;
};

struct RuckSackImagePrivate {
    struct RuckSackImage externals;

    FIBITMAP *bmp;
};

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

struct RuckSackOutStream {
    struct RuckSackBundlePrivate *b;
    struct RuckSackFileEntry *e;
};

static int memneql(const char *mem1, int mem1_size, const char *mem2, int mem2_size) {
    if (mem1_size != mem2_size)
        return 1;
    else
        return memcmp(mem1, mem2, mem1_size);
}

static long int alloc_size(long int actual_size) {
    return 2 * actual_size + 8192;
}

static long int alloc_count(long int actual_count) {
    return 2 * actual_count + 64;
}


static void write_uint32be(unsigned char *buf, uint32_t x) {
    buf[3] = x & 0xff;

    x >>= 8;
    buf[2] = x & 0xff;

    x >>= 8;
    buf[1] = x & 0xff;

    x >>= 8;
    buf[0] = x & 0xff;
}

static void write_uint64be(unsigned char *buf, uint64_t x) {
    buf[7] = x & 0xff;

    x >>= 8;
    buf[6] = x & 0xff;

    x >>= 8;
    buf[5] = x & 0xff;

    x >>= 8;
    buf[4] = x & 0xff;

    x >>= 8;
    buf[3] = x & 0xff;

    x >>= 8;
    buf[2] = x & 0xff;

    x >>= 8;
    buf[1] = x & 0xff;

    x >>= 8;
    buf[0] = x & 0xff;
}

static uint32_t read_uint32be(const unsigned char *buf) {
    uint32_t result = buf[0];

    result <<= 8;
    result |= buf[1];

    result <<= 8;
    result |= buf[2];

    result <<= 8;
    result |= buf[3];

    return result;
}

static uint64_t read_uint64be(const unsigned char *buf) {
    uint64_t result = buf[0];

    result <<= 8;
    result |= buf[1];

    result <<= 8;
    result |= buf[2];

    result <<= 8;
    result |= buf[3];

    result <<= 8;
    result |= buf[4];

    result <<= 8;
    result |= buf[5];

    result <<= 8;
    result |= buf[6];

    result <<= 8;
    result |= buf[7];

    return result;
}

static int read_header(struct RuckSackBundlePrivate *b) {
    // read all the header entries
    FILE *f = b->f;
    if (fseek(f, 0, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[MAX(HEADER_ENTRY_LEN, MAIN_HEADER_LEN)];
    long int amt_read = fread(buf, 1, MAIN_HEADER_LEN, f);

    if (amt_read == 0)
        return RuckSackErrorEmptyFile;

    if (amt_read != MAIN_HEADER_LEN)
        return RuckSackErrorInvalidFormat;

    if (memcmp(BUNDLE_UUID, buf, UUID_SIZE) != 0)
        return RuckSackErrorInvalidFormat;

    int bundle_version = read_uint32be(&buf[16]);
    if (bundle_version != BUNDLE_VERSION)
        return RuckSackErrorWrongVersion;

    b->first_header_offset = read_uint32be(&buf[20]);
    b->header_entry_count = read_uint32be(&buf[24]);
    b->header_entry_mem_count = alloc_count(b->header_entry_count);
    b->entries = calloc(b->header_entry_mem_count, sizeof(struct RuckSackFileEntry));

    if (!b->entries)
        return RuckSackErrorNoMem;

    // calculate how many bytes are used by all the headers
    b->headers_byte_count = 0; 

    long int header_offset = b->first_header_offset;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        if (fseek(f, header_offset, SEEK_SET))
            return RuckSackErrorFileAccess;
        amt_read = fread(buf, 1, HEADER_ENTRY_LEN, f);
        if (amt_read != HEADER_ENTRY_LEN)
            return RuckSackErrorInvalidFormat;
        long int entry_size = read_uint32be(&buf[0]);
        header_offset += entry_size;
        struct RuckSackFileEntry *entry = &b->entries[i];
        entry->offset = read_uint64be(&buf[4]);
        entry->size = read_uint64be(&buf[12]);
        entry->allocated_size = read_uint64be(&buf[20]);
        entry->mtime = read_uint32be(&buf[28]);
        entry->key_size = read_uint32be(&buf[32]);
        entry->key = malloc(entry->key_size + 1);
        if (!entry->key)
            return RuckSackErrorNoMem;
        amt_read = fread(entry->key, 1, entry->key_size, f);
        if (amt_read != entry->key_size)
            return RuckSackErrorInvalidFormat;
        entry->key[entry->key_size] = 0;
        entry->b = b;

        b->headers_byte_count += HEADER_ENTRY_LEN + entry->key_size;

        if (!b->last_entry || entry->offset > b->last_entry->offset)
            b->last_entry = entry;

        if (!b->first_entry || entry->offset < b->first_entry->offset) {
            b->first_entry = entry;
            b->first_file_offset = entry->offset;
        }
    }

    return RuckSackErrorNone;
}

static struct RuckSackFileEntry *get_prev_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry)
{
    struct RuckSackFileEntry *prev = NULL;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        if (((e->offset < entry->offset) && !prev) || (prev && e->offset > prev->offset))
            prev = e;
    }
    return prev;
}

static struct RuckSackFileEntry *get_next_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry)
{
    struct RuckSackFileEntry *next = NULL;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        if (((e->offset > entry->offset) && !next) || (next && e->offset < next->offset))
            next = e;
    }
    return next;
}

static int copy_data(struct RuckSackBundlePrivate *b, long int source,
        long int dest, long int size)
{
    if (source == dest)
        return RuckSackErrorNone;

    const long int max_buf_size = 1048576;
    long int buf_size = MIN(max_buf_size, size);
    char *buffer = malloc(buf_size);

    if (!buffer)
        return RuckSackErrorNoMem;

    while (size > 0) {
        long int amt_to_read = MIN(buf_size, size);
        if (fseek(b->f, source, SEEK_SET)) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fread(buffer, 1, amt_to_read, b->f) != amt_to_read) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fseek(b->f, dest, SEEK_SET)) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fwrite(buffer, 1, amt_to_read, b->f) != amt_to_read) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        size -= amt_to_read;
        source += amt_to_read;
        dest += amt_to_read;
    }

    free(buffer);
    return RuckSackErrorNone;
}

static void allocate_file(struct RuckSackBundlePrivate *b, long int size,
        struct RuckSackFileEntry *entry)
{
    entry->allocated_size = size;

    long int wanted_headers_alloc_bytes = alloc_size(b->headers_byte_count);
    long int wanted_headers_alloc_end = b->first_header_offset + wanted_headers_alloc_bytes;

    // can we put it between the header and the first entry?
    if (b->first_entry) {
        long int extra = b->first_entry->offset - wanted_headers_alloc_end;
        if (extra >= entry->allocated_size) {
            // we can fit it here
            entry->offset = b->first_entry->offset - entry->allocated_size;
            b->first_entry = entry;
            b->first_file_offset = entry->offset;
            return;
        }
    }

    // figure out offset and allocated_size
    // find a file that has too much allocated room and stick it there
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        // don't overwrite a stream!
        if (e->is_open) continue;

        // don't put it somewhere that is likely to 
        if (e->offset < wanted_headers_alloc_end) continue;

        long int needed_alloc_size = alloc_size(e->size);
        long int extra = e->allocated_size - needed_alloc_size;

        // not enough room.
        if (extra < entry->allocated_size) continue;

        long int new_offset = e->offset + needed_alloc_size;

        // don't put it too close to the headers
        if (new_offset < wanted_headers_alloc_end) continue;

        // we can fit it here!
        entry->offset = new_offset;
        entry->allocated_size = extra;
        e->allocated_size = needed_alloc_size;

        if (e == b->last_entry)
            b->last_entry = entry;

        return;
    }

    // ok stick it at the end
    if (b->last_entry) {
        if (!b->last_entry->is_open)
            b->last_entry->allocated_size = alloc_size(b->last_entry->size);
        entry->offset = MAX(b->last_entry->offset + b->last_entry->allocated_size,
                wanted_headers_alloc_end);
        b->last_entry = entry;
    } else {
        // this is the first entry in the bundle
        b->first_file_offset = alloc_size(HEADER_ENTRY_LEN + entry->key_size) + b->first_header_offset;
        entry->offset = b->first_file_offset;
        b->first_entry = entry;
        b->last_entry = entry;
    }
}


static int resize_file_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry, long int size)
{
    // extend the allocation of the thing before the old entry
    if (entry == b->last_entry) {
        // well that was easy
        entry->allocated_size = size;
        return RuckSackErrorNone;
    } else if (entry == b->first_entry) {
        b->first_entry = get_next_entry(b, entry);
        b->first_file_offset = b->first_entry->offset;
    } else {
        struct RuckSackFileEntry *prev = get_prev_entry(b, entry);
        prev->allocated_size += entry->allocated_size;
    }

    // pick a new place for the entry
    long int old_offset = entry->offset;
    allocate_file(b, size, entry);

    // copy the old data to the new location
    return copy_data(b, old_offset, entry->offset, entry->size);
}

static int write_header(struct RuckSackBundlePrivate *b) {
    FILE *f = b->f;
    if (fseek(f, 0, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[MAX(MAIN_HEADER_LEN, HEADER_ENTRY_LEN)];
    memcpy(buf, BUNDLE_UUID, UUID_SIZE);
    write_uint32be(&buf[16], BUNDLE_VERSION);
    write_uint32be(&buf[20], b->first_header_offset);
    write_uint32be(&buf[24], b->header_entry_count);
    long int amt_written = fwrite(buf, 1, MAIN_HEADER_LEN, f);
    if (amt_written != MAIN_HEADER_LEN)
        return RuckSackErrorFileAccess;

    long int allocated_header_bytes = b->first_file_offset - b->first_header_offset;
    if (b->headers_byte_count > allocated_header_bytes) {
        long int wanted_entry_bytes = alloc_size(b->headers_byte_count);
        long int wanted_offset_end = b->first_header_offset + wanted_entry_bytes;
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackFileEntry *entry = &b->entries[i];
            if (entry->offset < wanted_offset_end) {
                int err = resize_file_entry(b, entry, alloc_size(entry->size));
                if (err)
                    return err;
            }
        }
    }

    if (fseek(f, b->first_header_offset, SEEK_SET))
        return RuckSackErrorFileAccess;

    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *entry = &b->entries[i];
        write_uint32be(&buf[0], HEADER_ENTRY_LEN + entry->key_size);
        write_uint64be(&buf[4], entry->offset);
        write_uint64be(&buf[12], entry->size);
        write_uint64be(&buf[20], entry->allocated_size);
        write_uint32be(&buf[28], entry->mtime);
        write_uint32be(&buf[32], entry->key_size);
        amt_written = fwrite(buf, 1, HEADER_ENTRY_LEN, f);
        if (amt_written != HEADER_ENTRY_LEN)
            return RuckSackErrorFileAccess;
        amt_written = fwrite(entry->key, 1, entry->key_size, f);
        if (amt_written != entry->key_size)
            return RuckSackErrorFileAccess;
    }

    return RuckSackErrorNone;
}

static void init_new_bundle(struct RuckSackBundlePrivate *b) {
    b->first_header_offset = MAIN_HEADER_LEN;
    long int allocated_header_bytes = alloc_size(HEADER_ENTRY_LEN);
    b->first_file_offset = b->first_header_offset + allocated_header_bytes;
}

void rucksack_init(void) {
    FreeImage_Initialise(0);
}

void rucksack_finish(void) {
    FreeImage_DeInitialise();
}


int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **out_bundle) {
    struct RuckSackBundlePrivate *b = calloc(1, sizeof(struct RuckSackBundlePrivate));
    if (!b) {
        *out_bundle = NULL;
        return RuckSackErrorNoMem;
    }

    init_new_bundle(b);

    int open_for_writing = 0;
    b->f = fopen(bundle_path, "rb+");
    if (b->f) {
        int err = read_header(b);
        if (err == RuckSackErrorEmptyFile) {
            open_for_writing = 1;
        } else if (err) {
            free(b);
            *out_bundle = NULL;
            return err;
        }
    } else {
        open_for_writing = 1;
    }
    if (open_for_writing) {
        b->f = fopen(bundle_path, "wb+");
        if (!b->f) {
            free(b);
            *out_bundle = NULL;
            return RuckSackErrorFileAccess;
        }
    }

    *out_bundle = &b->externals;
    return RuckSackErrorNone;
}

int rucksack_bundle_close(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;

    int write_err = write_header(b);

    if (b->entries) {
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackFileEntry *entry = &b->entries[i];
            if (entry->key)
                free(entry->key);
        }
        free(b->entries);
    }

    int close_err = fclose(b->f);
    free(b);

    return write_err || close_err;
}

struct RuckSackTexture *rucksack_texture_create(void) {
    struct RuckSackTexturePrivate *p = calloc(1, sizeof(struct RuckSackTexturePrivate));
    if (!p)
        return NULL;
    struct RuckSackTexture *texture = &p->externals;
    texture->key_size = -1;
    texture->max_width = 1024;
    texture->max_height = 1024;
    texture->pow2 = 1;
    texture->allow_r90 = 1;
    return texture;
}

void rucksack_texture_destroy(struct RuckSackTexture *texture) {
    if (!texture)
        return;
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;

    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;
        free(image->key);
        FreeImage_Unload(img->bmp);
    }
    free(t->images);
    free(t->free_positions);
    free(t);
}

static char *dupe_byte_str(char *src, int len) {
    char *dest = malloc(len);
    if (dest)
        memcpy(dest, src, len);
    return dest;
}

int rucksack_texture_add_image(struct RuckSackTexture *texture, struct RuckSackImage *userimg)
{
    struct RuckSackTexturePrivate *p = (struct RuckSackTexturePrivate *) texture;

    FREE_IMAGE_FORMAT fmt = FreeImage_GetFileType(userimg->path, 0);

    if (fmt == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fmt))
        return RuckSackErrorImageFormat;

    FIBITMAP *bmp = FreeImage_Load(fmt, userimg->path, 0);

    if (!bmp)
        return RuckSackErrorFileAccess;

    if (!FreeImage_HasPixels(bmp))
        return RuckSackErrorNoPixels;

    if (p->images_count >= p->images_size) {
        p->images_size += 512;
        struct RuckSackImagePrivate *new_ptr = realloc(p->images,
                p->images_size * sizeof(struct RuckSackImagePrivate));
        if (!new_ptr)
            return RuckSackErrorNoMem;
        p->images = new_ptr;
    }
    struct RuckSackImagePrivate *img = &p->images[p->images_count];
    struct RuckSackImage *image = &img->externals;

    img->bmp = bmp;

    image->width = FreeImage_GetWidth(bmp);
    image->height = FreeImage_GetHeight(bmp);

    image->anchor = userimg->anchor;
    switch (userimg->anchor) {
        case RuckSackAnchorExplicit:
            // nothing to do
            break;
        case RuckSackAnchorCenter:
            image->anchor_x = image->width / 2;
            image->anchor_y = image->height / 2;
            break;
        case RuckSackAnchorLeft:
            image->anchor_x = 0;
            image->anchor_y = image->height / 2;
            break;
        case RuckSackAnchorRight:
            image->anchor_x = image->width - 1;
            image->anchor_y = image->height / 2;
            break;
        case RuckSackAnchorTop:
            image->anchor_x = image->width / 2;
            image->anchor_y = 0;
            break;
        case RuckSackAnchorBottom:
            image->anchor_x = image->width / 2;
            image->anchor_y = image->height - 1;
            break;
        case RuckSackAnchorTopLeft:
            image->anchor_x = 0;
            image->anchor_y = 0;
            break;
        case RuckSackAnchorTopRight:
            image->anchor_x = image->width - 1;
            image->anchor_y = 0;
            break;
        case RuckSackAnchorBottomLeft:
            image->anchor_x = 0;
            image->anchor_y = image->height - 1;
            break;
        case RuckSackAnchorBottomRight:
            image->anchor_x = image->width - 1;
            image->anchor_y = image->height - 1;
            break;
        default:
            return RuckSackErrorInvalidAnchor;
    }
    image->key_size = (userimg->key_size == -1) ? strlen(userimg->key) : userimg->key_size;
    image->key = dupe_byte_str(userimg->key, image->key_size);
    if (!image->key)
        return RuckSackErrorNoMem;

    // do this now that we know the image is valid
    p->images_count += 1;

    return RuckSackErrorNone;
}

static int compare_images(const void *a, const void *b) {
    const struct RuckSackImagePrivate *img_a = a;
    const struct RuckSackImagePrivate *img_b = b;

    const struct RuckSackImage *image_a = &img_a->externals;
    const struct RuckSackImage *image_b = &img_b->externals;

    int max_dim_a;
    int other_dim_a;
    if (image_a->width > image_a->height) {
        max_dim_a = image_a->width;
        other_dim_a = image_a->height;
    } else {
        max_dim_a = image_a->height;
        other_dim_a = image_a->width;
    }

    int max_dim_b;
    int other_dim_b;
    if (image_b->width > image_b->height) {
        max_dim_b = image_b->width;
        other_dim_b = image_b->height;
    } else {
        max_dim_b = image_b->height;
        other_dim_b = image_b->width;
    }

    int delta = max_dim_b - max_dim_a;
    return (delta == 0) ? (other_dim_b - other_dim_a) : delta;
}

static struct Rect *add_free_rect(struct RuckSackTexturePrivate *p) {
    // search for garbage
    if (p->garbage_count > 0) {
        for (int i = 0; i < p->free_pos_count; i += 1) {
            struct Rect *r = &p->free_positions[i];
            if (r->x == -1) {
                p->garbage_count -= 1;
                return r;
            }
        }
        assert(0);
    }
    if (p->free_pos_count >= p->free_pos_size) {
        p->free_pos_size += 512;
        struct Rect *new_ptr = realloc(p->free_positions,
                p->free_pos_size * sizeof(struct Rect));
        if (!new_ptr)
            return NULL;
        p->free_positions = new_ptr;
    }
    struct Rect *ptr = &p->free_positions[p->free_pos_count];
    p->free_pos_count += 1;

    return ptr;
}

static void remove_free_rect(struct RuckSackTexturePrivate *p, struct Rect *r) {
    // mark the object as absent
    r->x = -1;
    p->garbage_count += 1;

    // decrement the end pointer if we can
    int next_free_pos_count = p->free_pos_count - 1;
    while (next_free_pos_count >= 0 && p->free_positions[next_free_pos_count].x == -1) {
        p->free_pos_count = next_free_pos_count;
        next_free_pos_count -= 1;
        p->garbage_count -= 1;
    }
}

static char rects_intersect(struct Rect *r1, struct Rect *r2) {
    return (r1->x < r2->x + r2->w &&
            r2->x < r1->x + r1->w &&
            r1->y < r2->y + r2->h &&
            r2->y < r1->y + r1->h);
}

static int do_maxrect_bssf(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *p = (struct RuckSackTexturePrivate *) texture;

    // the Maximal Rectangles Algorithm, Best Short Side Fit
    // calculate the positions according to max width and height. later we'll crop.

    // sort using a nice heuristic
    qsort(p->images, p->images_count, sizeof(struct RuckSackImagePrivate), compare_images);

    struct Rect *r = add_free_rect(p);
    if (!r)
        return RuckSackErrorNoMem;
    r->x = 0;
    r->y = 0;
    r->w = texture->max_width;
    r->h = texture->max_height;

    // keep track of the actual texture size
    p->width = 0;
    p->height = 0;

    for (int i = 0; i < p->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &p->images[i];
        struct RuckSackImage *image = &img->externals;

        // pick a value that will definitely be larger than any other
        int best_short_side = INT_MAX;
        char best_short_side_is_r90;
        struct Rect *best_rect = NULL;

        // decide which free rectangle to pack into
        for (int free_i = 0; free_i < p->free_pos_count; free_i += 1) {
            struct Rect *free_r = &p->free_positions[free_i];
            if (free_r->x == -1) {
                // this free rectangle has been removed from the set. skip it
                continue;
            }

            // calculate short side fit without rotating
            int w_len = free_r->w - image->width;
            int h_len = free_r->h - image->height;
            int short_side = (w_len < h_len) ? w_len : h_len;
            int can_fit = w_len > 0 && h_len > 0;
            if (can_fit && short_side < best_short_side) {
                best_short_side = short_side;
                best_rect = free_r;
                best_short_side_is_r90 = 0;
            }

            // calculate short side fit with rotating 90 degrees
            if (texture->allow_r90) {
                w_len = free_r->w - image->height;
                h_len = free_r->h - image->width;
                short_side = (w_len < h_len) ? w_len : h_len;
                can_fit = w_len > 0 && h_len > 0;
                if (can_fit && short_side < best_short_side) {
                    best_short_side = short_side;
                    best_rect = free_r;
                    best_short_side_is_r90 = 1;
                }
            }
        }

        if (!best_rect)
            return RuckSackErrorCannotFit;

        // freeimage images are upside down. so, geometrically we are placing
        // the image at the top left of this rect. However due to freeimage's
        // inverted Y axis, the image will actually end up in the bottom left.
        struct Rect img_rect;
        img_rect.x = best_rect->x;
        img_rect.y = best_rect->y;
        img_rect.w = best_short_side_is_r90 ? image->height : image->width;
        img_rect.h = best_short_side_is_r90 ? image->width : image->height;

        image->x = img_rect.x;
        image->y = img_rect.y;
        image->r90 = best_short_side_is_r90;

        // keep track of texture boundaries
        p->width = MAX(image->x + img_rect.w, p->width);
        p->height = MAX(image->y + img_rect.h, p->height);

        // insert the two new rectangles into our set
        struct Rect *horiz = add_free_rect(p);
        if (!horiz)
            return RuckSackErrorNoMem;
        horiz->x = best_rect->x;
        horiz->y = best_rect->y + image->height;
        horiz->w = best_rect->w;
        horiz->h = best_rect->h - image->height;

        struct Rect *vert  = add_free_rect(p);
        if (!vert)
            return RuckSackErrorNoMem;
        vert->x = best_rect->x + image->width;
        vert->y = best_rect->y;
        vert->w = best_rect->w - image->width;
        vert->h = best_rect->h;

        // remove the no longer free rectangle we just used from our set
        remove_free_rect(p, best_rect);

        // now we loop over all the free rectangles in our set and break them
        // into smaller rectangles if the chosen position overlaps
        for (int free_i = 0; free_i < p->free_pos_count; free_i += 1) {
            struct Rect *free_r = &p->free_positions[free_i];
            if (free_r->x == -1) {
                // this free rectangle has been removed from the set. skip it
                continue;
            }

            if (rects_intersect(free_r, &img_rect)) {
                struct Rect outer;

                // check left side
                outer.x = free_r->x;
                outer.y = free_r->y;
                outer.w = image->x - free_r->x;
                outer.h = free_r->h;
                if (outer.w > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                // check right side
                outer.x = image->x + img_rect.w;
                outer.y = free_r->y;
                outer.w = free_r->x + free_r->w - outer.x;
                outer.h = free_r->h;
                if (outer.w > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                // check top side
                outer.x = free_r->x;
                outer.y = free_r->y;
                outer.w = free_r->w;
                outer.h = image->y - free_r->y;
                if (outer.h > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                // check bottom side
                outer.x = free_r->x;
                outer.y = image->y + img_rect.h;
                outer.w = free_r->w;
                outer.h = free_r->y + free_r->h - outer.y;
                if (outer.h > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                remove_free_rect(p, free_r);
            }
        }

        // now we loop over the free rectangle set again looking for and
        // removing degenerate rectagles - rectangles that are subrectangles
        // of another
        for (int free_i = 0; free_i < p->free_pos_count; free_i += 1) {
            struct Rect *free_r1 = &p->free_positions[free_i];
            if (free_r1->x == -1) {
                // this free rectangle has been removed from the set. skip it
                continue;
            }
            for (int free_j = free_i + 1; free_j < p->free_pos_count; free_j += 1) {
                struct Rect *free_r2 = &p->free_positions[free_j];
                if (free_r2->x == -1) {
                    // this free rectangle has been removed from the set. skip it
                    continue;
                }

                // check if r1 is a subrect of r2
                int x_diff = free_r1->x - free_r2->x;
                int y_diff = free_r1->y - free_r2->y;
                if (x_diff >= 0 && y_diff >= 0 &&
                    free_r1->w <= free_r2->w - x_diff &&
                    free_r1->h <= free_r2->h - y_diff)
                {
                    remove_free_rect(p, free_r1);
                    continue;
                }

                // check if r2 is a subrect of r1
                x_diff = free_r2->x - free_r1->x;
                y_diff = free_r2->y - free_r1->y;
                if (x_diff >= 0 && y_diff >= 0 &&
                    free_r2->w <= free_r1->w - x_diff &&
                    free_r2->h <= free_r1->h - y_diff)
                {
                    remove_free_rect(p, free_r2);
                    continue;
                }
            }
        }
    }

    return RuckSackErrorNone;
}

static int next_pow2(int x) {
    int power = 1;
    while (power < x)
        power *= 2;
    return power;
}

int rucksack_bundle_add_texture(struct RuckSackBundle *bundle, struct RuckSackTexture *texture)
{
    struct RuckSackTexturePrivate *p = (struct RuckSackTexturePrivate *) texture;

    // assigns x and y positions to all images
    int err = do_maxrect_bssf(texture);
    if (err)
        return err;

    // find the smallest power of 2 width/height
    if (texture->pow2) {
        p->width = next_pow2(p->width);
        p->height = next_pow2(p->height);
    }

    // create the output picture
    FIBITMAP *out_bmp = FreeImage_Allocate(p->width, p->height, 32, 0, 0, 0);
    BYTE *out_bits = FreeImage_GetBits(out_bmp);
    int out_pitch = FreeImage_GetPitch(out_bmp);

    // copy all the images to the final one
    for (int i = 0; i < p->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &p->images[i];
        struct RuckSackImage *image = &img->externals;

        // make sure the input picture is 32-bits
        if (FreeImage_GetBPP(img->bmp) != 32) {
            FIBITMAP *new_bmp = FreeImage_ConvertTo32Bits(img->bmp);
            FreeImage_Unload(img->bmp);
            img->bmp = new_bmp;
        }

        int img_pitch = FreeImage_GetPitch(img->bmp);
        BYTE *img_bits = FreeImage_GetBits(img->bmp);
        BYTE *out_bits_ptr = out_bits + out_pitch * image->y + 4 * image->x;
        if (image->r90) {
            for (int x = image->width - 1; x >= 0; x -= 1) {
                for (int y = 0; y < image->height; y += 1) {
                    int src_offset = img_pitch * y + 4 * x;
                    memcpy(out_bits_ptr + y * 4, img_bits + src_offset, 4);
                }
                out_bits_ptr += out_pitch;
            }
        } else {
            for (int y = 0; y < image->height; y += 1) {
                memcpy(out_bits_ptr, img_bits, image->width * 4);
                out_bits_ptr += out_pitch;
                img_bits += img_pitch;
            }
        }
    }

    // write it to a memory buffer
    FIMEMORY *out_stream = FreeImage_OpenMemory(NULL, 0);
    FreeImage_SaveToMemory(FIF_PNG, out_bmp, out_stream, 0);
    FreeImage_Unload(out_bmp);

    BYTE *data;
    DWORD data_size;
    FreeImage_AcquireMemory(out_stream, &data, &data_size);

    // calculate the total size needed by the texture and texture coordinates
    // and calculate the offsets needed
    long total_image_entries_size = 0;
    for (int i = 0; i < p->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &p->images[i];
        struct RuckSackImage *image = &img->externals;
        total_image_entries_size += IMAGE_HEADER_LEN + image->key_size;
    }
    long image_data_offset = TEXTURE_HEADER_LEN + total_image_entries_size;
    long total_size = image_data_offset + data_size;

    struct RuckSackOutStream *stream;
    err = rucksack_bundle_add_stream(bundle, texture->key, texture->key_size, total_size, &stream);
    if (err)
        return err;

    unsigned char buf[MAX(TEXTURE_HEADER_LEN, IMAGE_HEADER_LEN)];
    memcpy(&buf[0], TEXTURE_UUID, UUID_SIZE);
    write_uint32be(&buf[16], image_data_offset);
    write_uint32be(&buf[20], p->images_count);
    write_uint32be(&buf[24], TEXTURE_HEADER_LEN);
    write_uint32be(&buf[28], texture->max_width);
    write_uint32be(&buf[32], texture->max_height);
    buf[36] = texture->pow2;
    buf[37] = texture->allow_r90;

    err = rucksack_stream_write(stream, buf, TEXTURE_HEADER_LEN);
    if (err)
        return err;

    for (int i = 0; i < p->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &p->images[i];
        struct RuckSackImage *image = &img->externals;

        write_uint32be(&buf[0], IMAGE_HEADER_LEN + image->key_size);
        write_uint32be(&buf[4], image->anchor);
        write_uint32be(&buf[8], image->anchor_x);
        write_uint32be(&buf[12], image->anchor_y);
        write_uint32be(&buf[16], image->x);
        write_uint32be(&buf[20], image->y);
        write_uint32be(&buf[24], image->width);
        write_uint32be(&buf[28], image->height);
        buf[32] = image->r90;
        write_uint32be(&buf[33], image->key_size);

        err = rucksack_stream_write(stream, buf, IMAGE_HEADER_LEN);
        if (err)
            return err;

        err = rucksack_stream_write(stream, image->key, image->key_size);
        if (err)
            return err;
    }

    // make sure that the position that we told we were about to write the
    // image data to is correct.
    assert(image_data_offset == stream->e->size);

    err = rucksack_stream_write(stream, data, data_size);
    if (err)
        return err;

    rucksack_stream_close(stream);
    FreeImage_CloseMemory(out_stream);

    return RuckSackErrorNone;
}

int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        int key_size, const char *file_name)
{
    FILE *f = fopen(file_name, "rb");

    if (!f)
        return RuckSackErrorFileAccess;

    struct stat st;
    int err = fstat(fileno(f), &st);

    if (err != 0) {
        fclose(f);
        return RuckSackErrorFileAccess;
    }

    off_t size = st.st_size;

    struct RuckSackOutStream *stream;
    err = rucksack_bundle_add_stream(bundle, key, key_size, size, &stream);
    if (err) {
        fclose(f);
        return err;
    }

    const int buf_size = 16384;
    char *buffer = malloc(buf_size);

    if (!buffer) {
        fclose(f);
        rucksack_stream_close(stream);
        return RuckSackErrorNoMem;
    }

    long int amt_read;
    while ((amt_read = fread(buffer, 1, buf_size, f))) {
        int err = rucksack_stream_write(stream, buffer, amt_read);
        if (err) {
            fclose(f);
            free(buffer);
            rucksack_stream_close(stream);
            return err;
        }
    }

    free(buffer);
    rucksack_stream_close(stream);

    if (fclose(f))
        return RuckSackErrorFileAccess;

    return RuckSackErrorNone;
}

static int allocate_file_entry(struct RuckSackBundlePrivate *b, const char *key,
        long int size, struct RuckSackFileEntry **out_entry)
{
    // create a new entry
    if (b->header_entry_count >= b->header_entry_mem_count) {
        b->header_entry_mem_count = alloc_count(b->header_entry_mem_count);
        struct RuckSackFileEntry *new_ptr = realloc(b->entries,
                b->header_entry_mem_count * sizeof(struct RuckSackFileEntry));
        if (!new_ptr) {
            *out_entry = NULL;
            return RuckSackErrorNoMem;
        }
        long int clear_amt = b->header_entry_mem_count - b->header_entry_count;
        long int clear_size = clear_amt * sizeof(struct RuckSackFileEntry);
        memset(new_ptr + b->header_entry_count, 0, clear_size);
        b->entries = new_ptr;
    }
    struct RuckSackFileEntry *entry = &b->entries[b->header_entry_count];
    b->header_entry_count += 1;
    entry->key_size = strlen(key);
    entry->key = strdup(key);
    entry->b = b;
    b->headers_byte_count += 32 + entry->key_size;

    allocate_file(b, size, entry);

    *out_entry = entry;
    return RuckSackErrorNone;
}

static struct RuckSackFileEntry *find_file_entry(struct RuckSackBundlePrivate *b,
        const char *key, int key_size)
{
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];
        if (memneql(key, key_size, e->key, e->key_size) == 0)
            return e;
    }
    return NULL;
}

static int get_file_entry(struct RuckSackBundlePrivate *b, const char *key,
        int key_size, long int size, struct RuckSackFileEntry **out_entry)
{
    // return info for existing entry
    struct RuckSackFileEntry *e = find_file_entry(b, key, key_size);
    if (e) {
        if (e->allocated_size < size) {
            int err = resize_file_entry(b, e, size);
            if (err) {
                *out_entry = NULL;
                return err;
            }
        }
        *out_entry = e;
        return RuckSackErrorNone;
    }

    // none found, allocate new entry
    return allocate_file_entry(b, key, size, out_entry);
}

int rucksack_bundle_add_stream(struct RuckSackBundle *bundle,
        const char *key, int key_size, long int size_guess, struct RuckSackOutStream **out_stream)
{
    struct RuckSackOutStream *stream = calloc(1, sizeof(struct RuckSackOutStream));

    if (!stream) {
        *out_stream = NULL;
        return RuckSackErrorNoMem;
    }
    key_size = (key_size == -1) ? strlen(key) : key_size;

    stream->b = (struct RuckSackBundlePrivate *) bundle;
    int err = get_file_entry(stream->b, key, key_size, alloc_size(size_guess), &stream->e);
    if (err) {
        free(stream);
        *out_stream = NULL;
        return err;
    }
    stream->e->is_open = 1;
    stream->e->size = 0;
    stream->e->mtime = time(0);

    *out_stream = stream;
    return RuckSackErrorNone;
}

void rucksack_stream_close(struct RuckSackOutStream *stream) {
    stream->e->is_open = 0;
    free(stream);
}

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        long int count)
{
    long int pos = stream->e->size;
    long int end = pos + count;
    if (end > stream->e->allocated_size) {
        // It didn't fit. Move this stream to a new one with extra padding
        long int new_size = alloc_size(end);
        int err = resize_file_entry(stream->b, stream->e, new_size);
        if (err)
            return err;
    }

    FILE *f = stream->b->f;

    if (fseek(f, stream->e->offset + pos, SEEK_SET))
        return RuckSackErrorFileAccess;

    if (fwrite(ptr, 1, count, stream->b->f) != count)
        return RuckSackErrorFileAccess;

    stream->e->size = pos + count;

    return RuckSackErrorNone;
}

struct RuckSackFileEntry *rucksack_bundle_find_file(struct RuckSackBundle *bundle,
        const char *key, int key_size)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    key_size = (key_size == -1) ? strlen(key) : key_size;
    return find_file_entry(b, key, key_size);
}

long int rucksack_file_size(struct RuckSackFileEntry *entry) {
    return entry->size;
}

const char *rucksack_file_name(struct RuckSackFileEntry *entry) {
    return entry->key;
}

int rucksack_file_read(struct RuckSackFileEntry *e, unsigned char *buffer)
{
    struct RuckSackBundlePrivate *b = e->b;
    if (fseek(b->f, e->offset, SEEK_SET))
        return RuckSackErrorFileAccess;
    long int amt_read = fread(buffer, 1, e->size, b->f);
    if (amt_read != e->size)
        return RuckSackErrorFileAccess;
    return RuckSackErrorNone;
}

void rucksack_version(int *major, int *minor, int *patch) {
    if (major) *major = RUCKSACK_VERSION_MAJOR;
    if (minor) *minor = RUCKSACK_VERSION_MINOR;
    if (patch) *patch = RUCKSACK_VERSION_PATCH;
}

long int rucksack_bundle_file_count(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    return b->header_entry_count;
}

void rucksack_bundle_get_files(struct RuckSackBundle *bundle,
        struct RuckSackFileEntry **entries)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        entries[i] = &b->entries[i];
    }
}

const char *rucksack_err_str(int err) {
    return ERROR_STR[err];
}

int rucksack_file_open_texture(struct RuckSackFileEntry *entry,
        struct RuckSackTexture **out_texture)
{
    *out_texture = NULL;

    struct RuckSackTexturePrivate *t = calloc(1, sizeof(struct RuckSackTexturePrivate));
    struct RuckSackTexture *texture = &t->externals;
    if (!t)
        return RuckSackErrorNoMem;
    t->entry = entry;

    struct RuckSackBundlePrivate *b = entry->b;
    if (fseek(b->f, entry->offset, SEEK_SET)) {
        rucksack_texture_destroy(texture);
        return RuckSackErrorFileAccess;
    }

    unsigned char buf[MAX(TEXTURE_HEADER_LEN, IMAGE_HEADER_LEN)];
    long amt_read = fread(buf, 1, TEXTURE_HEADER_LEN, b->f);
    if (amt_read != TEXTURE_HEADER_LEN) {
        rucksack_texture_destroy(texture);
        return RuckSackErrorFileAccess;
    }

    if (memcmp(TEXTURE_UUID, buf, UUID_SIZE) != 0)
        return RuckSackErrorInvalidFormat;

    t->pixel_data_offset = read_uint32be(&buf[16]);
    t->pixel_data_size = entry->size - t->pixel_data_offset;
    t->images_count = read_uint32be(&buf[20]);
    long offset_to_first_img = read_uint32be(&buf[24]);

    texture->max_width = read_uint32be(&buf[28]);
    texture->max_height = read_uint32be(&buf[32]);
    texture->pow2 = buf[36];
    texture->allow_r90 = buf[37];

    t->images = calloc(t->images_count, sizeof(struct RuckSackImagePrivate));

    if (!t->images) {
        rucksack_texture_destroy(texture);
        return RuckSackErrorNoMem;
    }

    long next_offset = entry->offset + offset_to_first_img;
    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;

        if (fseek(b->f, next_offset, SEEK_SET)) {
            rucksack_texture_destroy(texture);
            return RuckSackErrorFileAccess;
        }

        long amt_read = fread(buf, 1, IMAGE_HEADER_LEN, b->f);
        if (amt_read != IMAGE_HEADER_LEN) {
            rucksack_texture_destroy(texture);
            return RuckSackErrorFileAccess;
        }

        long this_size = read_uint32be(&buf[0]);
        next_offset += this_size;

        image->anchor = read_uint32be(&buf[4]);
        image->anchor_x = read_uint32be(&buf[8]);
        image->anchor_y = read_uint32be(&buf[12]);
        image->x = read_uint32be(&buf[16]);
        image->y = read_uint32be(&buf[20]);
        image->width = read_uint32be(&buf[24]);
        image->height = read_uint32be(&buf[28]);
        image->r90 = buf[32];

        image->key_size = read_uint32be(&buf[33]);
        image->key = malloc(image->key_size + 1);
        if (!image->key) {
            rucksack_texture_destroy(texture);
            return RuckSackErrorNoMem;
        }
        amt_read = fread(image->key, 1, image->key_size, b->f);
        if (amt_read != image->key_size) {
            rucksack_texture_destroy(texture);
            return RuckSackErrorFileAccess;
        }
        image->key[image->key_size] = 0;
    }

    texture->key = entry->key;
    texture->key_size = entry->key_size;

    *out_texture = texture;
    return RuckSackErrorNone;
}

long rucksack_texture_size(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    return t->pixel_data_size;
}

int rucksack_texture_read(struct RuckSackTexture *texture, unsigned char *buffer) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    struct RuckSackFileEntry *entry = t->entry;
    FILE *f = entry->b->f;
    if (fseek(f, entry->offset + t->pixel_data_offset, SEEK_SET))
        return RuckSackErrorFileAccess;
    long int amt_read = fread(buffer, 1, t->pixel_data_size, f);
    if (amt_read != t->pixel_data_size)
        return RuckSackErrorFileAccess;
    return RuckSackErrorNone;
}

long rucksack_texture_image_count(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    return t->images_count;
}

void rucksack_texture_get_images(struct RuckSackTexture *texture,
        struct RuckSackImage **images)
{
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;
        images[i] = image;
    }
}

long rucksack_file_mtime(struct RuckSackFileEntry *entry) {
    return entry->mtime;
}

int rucksack_bundle_version(void) {
    return BUNDLE_VERSION;
}

struct RuckSackImage *rucksack_image_create(void) {
    struct RuckSackImagePrivate *img = calloc(1, sizeof(struct RuckSackImagePrivate));
    if (!img)
        return NULL;
    struct RuckSackImage *image = &img->externals;
    image->key_size = -1; // run strlen on key to find out key_size
    image->anchor = RuckSackAnchorCenter;
    return image;
}

void rucksack_image_destroy(struct RuckSackImage *image) {
    if (!image)
        return;
    struct RuckSackImagePrivate *img = (struct RuckSackImagePrivate *) image;
    free(img);
}

int rucksack_file_is_texture(struct RuckSackFileEntry *e, int *is_texture) {
    struct RuckSackBundlePrivate *b = e->b;
    if (e->size < UUID_SIZE) {
        *is_texture = 0;
        return RuckSackErrorNone;
    }
    if (fseek(b->f, e->offset, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[UUID_SIZE];
    long int amt_read = fread(buf, 1, UUID_SIZE, b->f);
    if (amt_read != UUID_SIZE)
        return RuckSackErrorFileAccess;

    *is_texture = memcmp(TEXTURE_UUID, buf, UUID_SIZE) == 0;

    return RuckSackErrorNone;
}
