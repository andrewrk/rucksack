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


#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

static const char *BUNDLE_UUID = "\x60\x70\xc8\x99\x82\xa1\x41\x84\x89\x51\x08\xc9\x1c\xc9\xb6\x20";

static const char *ERROR_STR[] = {
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

struct RuckSackFileEntry {
    long int offset;
    long int size;
    long int allocated_size;
    long int key_size;
    char *key;
    char is_open; // flag for when an out stream is writing to this entry
};

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;
    FILE *f;

    long int first_header_offset;
    long int header_entry_count;
    struct RuckSackFileEntry *entries;
    long int header_entry_mem_size;

    // keep some stuff cached for quick access
    struct RuckSackFileEntry *first_entry;
    struct RuckSackFileEntry *last_entry;
    long int headers_byte_count;
    long int first_file_offset;
};

struct RuckSackImagePrivate {
    struct RuckSackImage externals;

    char *key;
    FIBITMAP *bmp;

    int x;
    int y;
    char r90;
};

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct RuckSackPagePrivate {
    struct RuckSackPage externals;

    struct RuckSackImagePrivate *images;
    int images_count;
    int images_size;

    struct Rect *free_positions;
    int free_pos_count;
    int free_pos_size;
    int garbage_count;

    int width;
    int height;
};

struct RuckSackOutStream {
    struct RuckSackBundlePrivate *b;
    struct RuckSackFileEntry *e;
    long int pos;
};

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

    unsigned char buf[32];
    long int amt_read = fread(buf, 1, 24, f);

    if (amt_read != 24)
        return RuckSackErrorInvalidFormat;

    if (memcmp(BUNDLE_UUID, buf, 16) != 0)
        return RuckSackErrorInvalidFormat;

    b->first_header_offset = read_uint32be(&buf[16]);
    b->header_entry_count = read_uint32be(&buf[20]);
    b->header_entry_mem_size = alloc_count(b->header_entry_count);
    b->entries = calloc(b->header_entry_mem_size, sizeof(struct RuckSackFileEntry));

    if (!b->entries)
        return RuckSackErrorNoMem;

    // calculate how many bytes are used by all the headers
    b->headers_byte_count = 0; 

    long int header_offset = b->first_header_offset;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        if (fseek(f, header_offset, SEEK_SET))
            return RuckSackErrorFileAccess;
        amt_read = fread(buf, 1, 32, f);
        if (amt_read != 32)
            return RuckSackErrorInvalidFormat;
        long int entry_size = read_uint32be(&buf[0]);
        header_offset += entry_size;
        struct RuckSackFileEntry *entry = &b->entries[i];
        entry->offset = read_uint64be(&buf[4]);
        entry->size = read_uint64be(&buf[12]);
        entry->allocated_size = read_uint64be(&buf[20]);
        entry->key_size = read_uint32be(&buf[28]);
        entry->key = malloc(entry->key_size + 1);
        if (!entry->key)
            return RuckSackErrorNoMem;
        amt_read = fread(entry->key, 1, entry->key_size, f);
        if (amt_read != entry->key_size)
            return RuckSackErrorInvalidFormat;
        entry->key[entry->key_size] = 0;

        b->headers_byte_count += 32 + entry->key_size;

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

        if (((e->offset < entry->offset) && !prev) || (e->offset > prev->offset))
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

        if (((e->offset > entry->offset) && !next) || (e->offset < next->offset))
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
        b->first_file_offset = alloc_size(32 + entry->key_size) + b->first_header_offset;
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

    unsigned char buf[32];
    memcpy(buf, BUNDLE_UUID, 16);
    write_uint32be(&buf[16], b->first_header_offset);
    write_uint32be(&buf[20], b->header_entry_count);
    long int amt_written = fwrite(buf, 1, 24, f);
    if (amt_written != 24)
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
        write_uint32be(&buf[0], 32 + entry->key_size);
        write_uint64be(&buf[4], entry->offset);
        write_uint64be(&buf[12], entry->size);
        write_uint64be(&buf[20], entry->allocated_size);
        write_uint32be(&buf[28], entry->key_size);
        amt_written = fwrite(buf, 1, 32, f);
        if (amt_written != 32)
            return RuckSackErrorFileAccess;
        amt_written = fwrite(entry->key, 1, entry->key_size, f);
        if (amt_written != entry->key_size)
            return RuckSackErrorFileAccess;
    }

    return RuckSackErrorNone;
}

static void init_new_bundle(struct RuckSackBundlePrivate *b) {
    b->first_header_offset = 24;
    long int allocated_header_bytes = alloc_size(32);
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

    b->f = fopen(bundle_path, "rb+");
    if (b->f) {
        int err = read_header(b);
        if (err) {
            free(b);
            *out_bundle = NULL;
            return err;
        }
    } else {
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

struct RuckSackPage *rucksack_page_create(void) {
    struct RuckSackPagePrivate *p = calloc(1, sizeof(struct RuckSackPagePrivate));
    if (!p) return NULL;
    struct RuckSackPage *page = &p->externals;
    page->max_width = 1024;
    page->max_height = 1024;
    page->pow2 = 1;
    return &p->externals;
}

void rucksack_page_destroy(struct RuckSackPage *page) {
    struct RuckSackPagePrivate *p = (struct RuckSackPagePrivate *) page;
    while (p->images_count) {
        p->images_count -= 1;
        struct RuckSackImagePrivate *img = &p->images[p->images_count];
        free(img->key);
        FreeImage_Unload(img->bmp);
    }
    free(p->images);
    free(p->free_positions);
    free(p);
}

int rucksack_page_add_image(struct RuckSackPage *page, const char *key,
        struct RuckSackImage *userimg)
{
    struct RuckSackPagePrivate *p = (struct RuckSackPagePrivate *) page;

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
    struct RuckSackImagePrivate *i = &p->images[p->images_count];
    struct RuckSackImage *img = &i->externals;
    p->images_count += 1;

    i->bmp = bmp;

    img->width = FreeImage_GetWidth(bmp);
    img->height = FreeImage_GetHeight(bmp);

    img->anchor = RuckSackAnchorExplicit;
    switch (userimg->anchor) {
        case RuckSackAnchorExplicit:
            // nothing to do
            break;
        case RuckSackAnchorCenter:
            img->anchor_x = img->width / 2;
            img->anchor_y = img->height / 2;
            break;
        case RuckSackAnchorLeft:
            img->anchor_x = 0;
            img->anchor_y = img->height / 2;
            break;
        case RuckSackAnchorRight:
            img->anchor_x = img->width - 1;
            img->anchor_y = img->height / 2;
            break;
        case RuckSackAnchorTop:
            img->anchor_x = img->width / 2;
            img->anchor_y = 0;
            break;
        case RuckSackAnchorBottom:
            img->anchor_x = img->width / 2;
            img->anchor_y = img->height - 1;
            break;
        case RuckSackAnchorTopLeft:
            img->anchor_x = 0;
            img->anchor_y = 0;
            break;
        case RuckSackAnchorTopRight:
            img->anchor_x = img->width - 1;
            img->anchor_y = 0;
            break;
        case RuckSackAnchorBottomLeft:
            img->anchor_x = 0;
            img->anchor_y = img->height - 1;
            break;
        case RuckSackAnchorBottomRight:
            img->anchor_x = img->width - 1;
            img->anchor_y = img->height - 1;
            break;
        default:
            return RuckSackErrorInvalidAnchor;
    }

    i->key = strdup(key);

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

static struct Rect *add_free_rect(struct RuckSackPagePrivate *p) {
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

static void remove_free_rect(struct RuckSackPagePrivate *p, struct Rect *r) {
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

static int do_maxrect_bssf(struct RuckSackPage *page) {
    struct RuckSackPagePrivate *p = (struct RuckSackPagePrivate *) page;

    // the Maximal Rectangles Algorithm, Best Short Side Fit
    // calculate the positions according to max width and height. later we'll crop.

    // sort using a nice heuristic
    qsort(p->images, p->images_count, sizeof(struct RuckSackImagePrivate), compare_images);

    struct Rect *r = add_free_rect(p);
    if (!r)
        return RuckSackErrorNoMem;
    r->x = 0;
    r->y = 0;
    r->w = page->max_width;
    r->h = page->max_height;

    // keep track of the actual page size
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

        if (!best_rect)
            return RuckSackErrorCannotFit;

        // place image at top left of this rect
        struct Rect img_rect;
        img_rect.x = best_rect->x;
        img_rect.y = best_rect->y;
        img_rect.w = best_short_side_is_r90 ? image->height : image->width;
        img_rect.h = best_short_side_is_r90 ? image->width : image->height;

        img->x = img_rect.x;
        img->y = img_rect.y;
        img->r90 = best_short_side_is_r90;

        // keep track of page boundaries
        p->width = MAX(img->x + img_rect.w, p->width);
        p->height = MAX(img->y + img_rect.h, p->height);

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
                outer.w = img->x - free_r->x;
                outer.h = free_r->h;
                if (outer.w > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                // check right side
                outer.x = img->x + img_rect.w;
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
                outer.h = img->y - free_r->y;
                if (outer.h > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    if (!new_free_rect)
                        return RuckSackErrorNoMem;
                    *new_free_rect = outer;
                }

                // check bottom side
                outer.x = free_r->x;
                outer.y = img->y + img_rect.h;
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

int rucksack_bundle_add_page(struct RuckSackBundle *bundle, const char *key,
        struct RuckSackPage *page)
{
    struct RuckSackPagePrivate *p = (struct RuckSackPagePrivate *) page;

    // assigns x and y positions to all images
    int err = do_maxrect_bssf(page);
    if (err)
        return err;

    // find the smallest power of 2 width/height
    if (page->pow2) {
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
        BYTE *out_bits_ptr = out_bits + out_pitch * img->y + 4 * img->x;
        if (img->r90) {
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

    // write that to a debug file
    BYTE *data;
    DWORD data_size;
    FreeImage_AcquireMemory(out_stream, &data, &data_size);

    struct RuckSackOutStream *stream;
    err = rucksack_bundle_add_stream(bundle, key, data_size, &stream);
    if (err)
        return err;

    err = rucksack_stream_write(stream, data, data_size);
    if (err)
        return err;

    rucksack_stream_close(stream);
    FreeImage_CloseMemory(out_stream);

    return RuckSackErrorNone;
}

int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        const char *file_name)
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
    err = rucksack_bundle_add_stream(bundle, key, size, &stream);
    if (err) {
        fclose(f);
        return err;
    }

    const int buf_size = 16384;
    char *buffer = malloc(buf_size);

    if (!buffer)
        return RuckSackErrorNoMem;

    long int amt_read;
    while ((amt_read = fread(buffer, 1, buf_size, f))) {
        int err = rucksack_stream_write(stream, buffer, amt_read);
        if (err) {
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
    if (b->header_entry_count >= b->header_entry_mem_size) {
        b->header_entry_mem_size = alloc_count(b->header_entry_mem_size);
        struct RuckSackFileEntry *new_ptr = realloc(b->entries,
                b->header_entry_mem_size * sizeof(struct RuckSackFileEntry));
        if (!new_ptr) {
            *out_entry = NULL;
            return RuckSackErrorNoMem;
        }
        long int clear_amt = b->header_entry_mem_size - b->header_entry_count;
        memset(new_ptr + b->header_entry_count, 0, clear_amt);
        b->entries = new_ptr;
    }
    struct RuckSackFileEntry *entry = &b->entries[b->header_entry_count];
    b->header_entry_count += 1;
    entry->key_size = strlen(key);
    entry->key = strdup(key);
    b->headers_byte_count += 32 + entry->key_size;

    allocate_file(b, size, entry);

    *out_entry = entry;
    return RuckSackErrorNone;
}

static struct RuckSackFileEntry *find_file_entry(struct RuckSackBundlePrivate *b,
        const char *key)
{
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];
        if (strcmp(key, e->key) == 0)
            return e;
    }
    return NULL;
}

static int get_file_entry(struct RuckSackBundlePrivate *b, const char *key,
        long int size, struct RuckSackFileEntry **out_entry)
{
    // return info for existing entry
    struct RuckSackFileEntry *e = find_file_entry(b, key);
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
        const char *key, long int size_guess, struct RuckSackOutStream **out_stream)
{
    struct RuckSackOutStream *stream = calloc(1, sizeof(struct RuckSackOutStream));

    if (!stream) {
        *out_stream = NULL;
        return RuckSackErrorNoMem;
    }

    stream->b = (struct RuckSackBundlePrivate *) bundle;
    int err = get_file_entry(stream->b, key, alloc_size(size_guess), &stream->e);
    if (err) {
        free(stream);
        *out_stream = NULL;
        return err;
    }
    stream->e->is_open = 1;
    stream->e->size = 0;

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
    long int end = stream->pos + count;
    if (end > stream->e->allocated_size) {
        // It didn't fit. Move this stream to a new one with extra padding
        long int new_size = alloc_size(end);
        int err = resize_file_entry(stream->b, stream->e, new_size);
        if (err)
            return err;
    }

    FILE *f = stream->b->f;

    if (fseek(f, stream->e->offset + stream->pos, SEEK_SET))
        return RuckSackErrorFileAccess;

    if (fwrite(ptr, 1, count, stream->b->f) != count)
        return RuckSackErrorFileAccess;

    stream->e->size += count;

    return RuckSackErrorNone;
}

struct RuckSackFileEntry *rucksack_bundle_find_file(struct RuckSackBundle *bundle, const char *key) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    return find_file_entry(b, key);
}

long int rucksack_file_size(struct RuckSackFileEntry *entry) {
    return entry->size;
}

const char *rucksack_file_name(struct RuckSackFileEntry *entry) {
    return entry->key;
}

int rucksack_bundle_file_read(struct RuckSackBundle *bundle, struct RuckSackFileEntry *e,
        unsigned char *buffer)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
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
