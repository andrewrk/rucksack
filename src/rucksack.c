#include "rucksack.h"

#include <FreeImage.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#define MAX(x, y) ((x) > (y) ? (x) : (y))

const char *BUNDLE_UUID = "\x60\x70\xc8\x99\x82\xa1\x41\x84\x89\x51\x08\xc9\x1c\xc9\xb6\x20";

struct RuckSackHeaderEntry {
    uint64_t offset;
    uint64_t size;
    uint64_t allocated_size;
    uint32_t key_size;
    char *key;
};

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;
    FILE *f;

    uint32_t first_header_offset;
    uint32_t header_entry_count;
    uint32_t allocated_header_bytes;
    struct RuckSackHeaderEntry *entries;
    uint32_t header_entry_mem_size;
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
    struct RuckSackHeaderEntry *e;
    size_t pos;
};

void rucksack_init(void) {
    FreeImage_Initialise(0);
}

void rucksack_finish(void) {
    FreeImage_DeInitialise();
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
    size_t amt_read = fread(buf, 1, 28, f);

    if (amt_read != 28)
        return RuckSackErrorInvalidFormat;

    if (memcmp(BUNDLE_UUID, buf, 16) != 0)
        return RuckSackErrorInvalidFormat;

    b->first_header_offset = read_uint32be(&buf[16]);
    b->header_entry_count = read_uint32be(&buf[20]);
    b->header_entry_mem_size = b->header_entry_count * 1.3 + 64;
    b->allocated_header_bytes = read_uint32be(&buf[24]);
    b->entries = calloc(b->header_entry_mem_size, sizeof(struct RuckSackHeaderEntry));

    if (!b->entries)
        return RuckSackErrorNoMem;

    uint32_t header_offset = b->first_header_offset;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        if (fseek(f, header_offset, SEEK_SET))
            return RuckSackErrorFileAccess;
        amt_read = fread(buf, 1, 32, f);
        if (amt_read != 32)
            return RuckSackErrorInvalidFormat;
        uint32_t entry_size = read_uint32be(&buf[0]);
        header_offset += entry_size;
        struct RuckSackHeaderEntry *entry = &b->entries[i];
        entry->offset = read_uint64be(&buf[4]);
        entry->size = read_uint64be(&buf[12]);
        entry->allocated_size = read_uint64be(&buf[20]);
        entry->key_size = read_uint32be(&buf[28]);
        entry->key = malloc(entry->key_size);
        if (!entry->key)
            return RuckSackErrorNoMem;
        amt_read = fread(entry->key, 1, entry->key_size, f);
        if (amt_read != entry->key_size)
            return RuckSackErrorInvalidFormat;
    }

    return RuckSackErrorNone;
}

static int move_file_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackHeaderEntry *entry, uint64_t min_offset)
{
    // TODO
    return RuckSackErrorNone;
}

static int write_header(struct RuckSackBundlePrivate *b) {
    FILE *f = b->f;
    if (fseek(f, 0, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[32];
    memcpy(buf, BUNDLE_UUID, 16);
    write_uint32be(&buf[16], b->first_header_offset);
    write_uint32be(&buf[20], b->header_entry_count);
    write_uint32be(&buf[24], b->allocated_header_bytes);
    size_t amt_written = fwrite(buf, 1, 28, f);
    if (amt_written != 28)
        return RuckSackErrorFileAccess;

    // calculate how many bytes we need for the header entries
    uint32_t needed_entry_bytes = 0;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackHeaderEntry *entry = &b->entries[i];
        needed_entry_bytes += 32 + entry->key_size;
    }

    if (needed_entry_bytes < b->allocated_header_bytes) {
        uint32_t wanted_entry_bytes = MAX(needed_entry_bytes * 2, 16384);
        uint32_t wanted_offset_end = b->first_header_offset + wanted_entry_bytes;
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackHeaderEntry *entry = &b->entries[i];
            if (entry->offset < wanted_offset_end) {
                int err = move_file_entry(b, entry, wanted_offset_end);
                if (err)
                    return err;
            }
        }
    }

    if (fseek(f, b->first_header_offset, SEEK_SET))
        return RuckSackErrorFileAccess;

    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackHeaderEntry *entry = &b->entries[i];
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
    b->first_header_offset = 28;
}

struct RuckSackBundle *rucksack_bundle_open(const char *bundle_path) {
    struct RuckSackBundlePrivate *b = calloc(1, sizeof(struct RuckSackBundlePrivate));
    if (!b) return NULL;

    b->f = fopen(bundle_path, "rb+");
    if (b->f) {
        int err = read_header(b);
        if (err) {
            free(b);
            return NULL;
        }
    } else {
        b->f = fopen(bundle_path, "wb+");
        if (b->f) {
            init_new_bundle(b);
            int err = write_header(b);
            if (err) {
                free(b);
                return NULL;
            }
        } else {
            free(b);
            fprintf(stderr, "Unable to open %s for read/write/update\n",
                    bundle_path);
            return NULL;
        }
    }

    return &b->externals;
}

void rucksack_bundle_close(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;

    if (b->entries) {
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackHeaderEntry *entry = &b->entries[i];
            if (entry->key)
                free(entry->key);
        }
        free(b->entries);
    }

    fclose(b->f);
    free(b);
}

struct RuckSackPage *rucksack_page_create(void) {
    struct RuckSackPagePrivate *p = calloc(1, sizeof(struct RuckSackPagePrivate));
    assert(p);
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
    FIBITMAP *bmp = FreeImage_Load(fmt, userimg->path, 0);

    if (!bmp) {
        fprintf(stderr, "unable to open %s for reading\n", userimg->path);
        return -1;
    }

    if (!FreeImage_HasPixels(bmp)) {
        fprintf(stderr, "picture file %s has no pixels\n", userimg->path);
        return -1;
    }

    if (p->images_count >= p->images_size) {
        p->images_size += 512;
        struct RuckSackImagePrivate *new_ptr = realloc(p->images,
                p->images_size * sizeof(struct RuckSackImagePrivate));
        assert(new_ptr);
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
            assert(0);
    }

    i->key = strdup(key);

    return 0;
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
        assert(new_ptr);
        p->free_positions = new_ptr;
    }
    struct Rect *ptr = &p->free_positions[p->free_pos_count];
    p->free_pos_count += 1;

    fprintf(stderr, "add_free_rect. garbage count: %d, free_pos_count: %d\n",
            p->garbage_count, p->free_pos_count);
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

    fprintf(stderr, "remove_free_rect. garbage count: %d, free_pos_count: %d\n",
            p->garbage_count, p->free_pos_count);
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

        if (!best_rect) {
            fprintf(stderr, "unable to fit all images into page\n");
            return -1;
        }

        // place image at top left of this rect
        struct Rect img_rect;
        img_rect.x = best_rect->x;
        img_rect.y = best_rect->y;
        img_rect.w = best_short_side_is_r90 ? image->height : image->width;
        img_rect.h = best_short_side_is_r90 ? image->width : image->height;

        img->x = img_rect.x;
        img->y = img_rect.y;
        img->r90 = best_short_side_is_r90;
        fprintf(stderr, "placed image at %d, %d %s\n", img->x, img->y,
                img->r90 ? "sideways" : "");

        // keep track of page boundaries
        p->width = MAX(img->x + img_rect.w, p->width);
        p->height = MAX(img->y + img_rect.h, p->height);

        // insert the two new rectangles into our set
        struct Rect *horiz = add_free_rect(p);
        horiz->x = best_rect->x;
        horiz->y = best_rect->y + image->height;
        horiz->w = best_rect->w;
        horiz->h = best_rect->h - image->height;

        struct Rect *vert  = add_free_rect(p);
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
                    *new_free_rect = outer;
                }

                // check right side
                outer.x = img->x + img_rect.w;
                outer.y = free_r->y;
                outer.w = free_r->x + free_r->w - outer.x;
                outer.h = free_r->h;
                if (outer.w > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    *new_free_rect = outer;
                }

                // check top side
                outer.x = free_r->x;
                outer.y = free_r->y;
                outer.w = free_r->w;
                outer.h = img->y - free_r->y;
                if (outer.h > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
                    *new_free_rect = outer;
                }

                // check bottom side
                outer.x = free_r->x;
                outer.y = img->y + img_rect.h;
                outer.w = free_r->w;
                outer.h = free_r->y + free_r->h - outer.y;
                if (outer.h > 0) {
                    struct Rect *new_free_rect = add_free_rect(p);
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

    return 0;
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
    if (err != 0) {
        fprintf(stderr, "unable to find a maxrect packing for all images in the page\n");
        return err;
    }

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

    return 0;
}

int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        const char *file_name)
{
    FILE *f = fopen(file_name, "rb");

    if (!f) {
        fprintf(stderr, "unable to open %s\n", file_name);
        return RuckSackErrorFileAccess;
    }

    struct stat st;
    int err = fstat(fileno(f), &st);

    if (err != 0) {
        perror("unable to stat");
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

    size_t amt_read;
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

static int resize_file_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackHeaderEntry *e, size_t size)
{
    // TODO
    return RuckSackErrorNone;
}

static int allocate_file_entry(struct RuckSackBundlePrivate *b, const char *key,
        size_t size, struct RuckSackHeaderEntry **entry)
{
    // return info for existing entry
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackHeaderEntry *e = &b->entries[i];
        if (strcmp(key, e->key) == 0) {
            if (e->allocated_size < size) {
                int err = resize_file_entry(b, e, size);
                if (err) {
                    *entry = NULL;
                    return err;
                }
            }
            *entry = e;
            return RuckSackErrorNone;
        }
    }

    // create a new entry
    if (b->header_entry_count >= b->header_entry_mem_size) {
        b->header_entry_mem_size = b->header_entry_mem_size * 1.3 + 64;
        struct RuckSackHeaderEntry *new_ptr = realloc(b->entries,
                b->header_entry_mem_size * sizeof(struct RuckSackHeaderEntry));
        if (!new_ptr)
            return RuckSackErrorNoMem;
        size_t clear_amt = b->header_entry_mem_size - b->header_entry_count;
        memset(new_ptr + b->header_entry_count, 0, clear_amt);
        b->entries = new_ptr;
    }
    struct RuckSackHeaderEntry *e = &b->entries[b->header_entry_count];
    b->header_entry_count += 1;
    e->key_size = strlen(key);
    e->key = strdup(key);

    // figure out offset and allocated_size
    // TODO

    return RuckSackErrorNone;
}

int rucksack_bundle_add_stream(struct RuckSackBundle *bundle,
        const char *key, size_t size_guess, struct RuckSackOutStream **out_stream)
{
    struct RuckSackOutStream *stream = calloc(1, sizeof(struct RuckSackOutStream));

    if (!stream) {
        *out_stream = NULL;
        return RuckSackErrorNoMem;
    }

    size_guess = (size_guess > 0) ? size_guess : 8192;
    stream->b = (struct RuckSackBundlePrivate *) bundle;
    int err = allocate_file_entry(stream->b, key, size_guess, &stream->e);
    if (err) {
        free(stream);
        *out_stream = NULL;
        return err;
    }

    *out_stream = stream;
    return RuckSackErrorNone;
}

void rucksack_stream_close(struct RuckSackOutStream *stream) {
    free(stream);
}

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        size_t count)
{
    size_t end = stream->pos + count;
    if (end > stream->e->allocated_size) {
        // It didn't fit. Move this stream to a new one with extra padding
        size_t new_size = 1.5 * end + 8192;
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