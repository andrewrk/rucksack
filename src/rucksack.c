#include "rucksack.h"

#include <FreeImage.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>


#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;
    FILE *f;
};

struct RuckSackImagePrivate {
    struct RuckSackImage externals;
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

void rucksack_init(void) {
    FreeImage_Initialise(0);
}

void rucksack_finish(void) {
    FreeImage_DeInitialise();
}


struct RuckSackBundle *rucksack_bundle_open(const char *bundle_path) {
    FILE *f = fopen(bundle_path, "rb+");
    if (!f) {
        f = fopen(bundle_path, "wb+");
        if (!f) {
            fprintf(stderr, "Unable to open %s for read/write/update\n",
                    bundle_path);
            return NULL;
        }
    }

    struct RuckSackBundlePrivate *b = calloc(1, sizeof(struct RuckSackBundlePrivate));
    assert(b);

    b->f = f;

    return &b->externals;
}

void rucksack_bundle_close(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;

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
        struct RuckSackImagePrivate *i = &p->images[p->images_count];
        struct RuckSackImage *img = &i->externals;
        free(img->key);
    }
    free(p->images);
    free(p->free_positions);
    free(p);
}

static char *mystrdup (const char *s) {
    char *d = malloc (strlen (s) + 1);
    if (!d) return NULL;
    strcpy(d, s);
    return d;
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

    img->key = mystrdup(key);

    return 0;
}

static int compare_images(const void *a, const void *b) {
    const struct RuckSackImage *img_a = a;
    const struct RuckSackImage *img_b = b;

    int max_dim_a;
    int other_dim_a;
    if (img_a->width > img_a->height) {
        max_dim_a = img_a->width;
        other_dim_a = img_a->height;
    } else {
        max_dim_a = img_a->height;   
        other_dim_a = img_a->width;
    }

    int max_dim_b;
    int other_dim_b;
    if (img_b->width > img_b->height) {
        max_dim_b = img_b->width;
        other_dim_b = img_b->height;
    } else {
        max_dim_b = img_b->height;   
        other_dim_b = img_b->width;
    }

    int delta = max_dim_a - max_dim_b;
    return (delta == 0) ? (other_dim_a - other_dim_b) : delta;
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
    p->width = INT_MIN;
    p->height = INT_MAX;

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

        // place image at bottom left of this rect
        int img_w = best_short_side_is_r90 ? image->height : image->width;
        int img_h = best_short_side_is_r90 ? image->width : image->height;

        img->x = best_rect->x;
        img->y = best_rect->y + best_rect->h - img_h;
        img->r90 = best_short_side_is_r90;

        // keep track of page boundaries
        p->width = MAX(img->x + img_w, p->width);
        p->height = MAX(img->y + img_h, p->height);

        // insert the two new rectangles into our set
        struct Rect *horiz = add_free_rect(p);
        horiz->x = best_rect->x;
        horiz->y = best_rect->y;
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

            struct Rect outer;
            char intersected = 0;

            // check left side
            outer.x = free_r->x;
            outer.y = free_r->y;
            outer.w = img->x - free_r->x;
            outer.h = free_r->h;
            if (outer.w > 0) {
                intersected = 1;
                struct Rect *new_free_rect = add_free_rect(p);
                *new_free_rect = outer;
            }

            // check right side
            outer.x = img->x + img_w;
            outer.y = free_r->y;
            outer.w = free_r->x + free_r->w - outer.x;
            outer.h = free_r->h;
            if (outer.w > 0) {
                intersected = 1;
                struct Rect *new_free_rect = add_free_rect(p);
                *new_free_rect = outer;
            }

            // check top side
            outer.x = free_r->x;
            outer.y = free_r->y;
            outer.w = free_r->w;
            outer.h = img->y - free_r->y;
            if (outer.h > 0) {
                intersected = 1;
                struct Rect *new_free_rect = add_free_rect(p);
                *new_free_rect = outer;
            }

            // check bottom side
            outer.x = free_r->x;
            outer.y = img->y + img_h;
            outer.w = free_r->w;
            outer.h = free_r->y + free_r->h - outer.y;
            if (outer.h > 0) {
                intersected = 1;
                struct Rect *new_free_rect = add_free_rect(p);
                *new_free_rect = outer;
            }

            if (intersected)
                remove_free_rect(p, free_r);
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
                    free_r1->w < free_r2->w - x_diff &&
                    free_r1->h < free_r2->h - y_diff)
                {
                    remove_free_rect(p, free_r1);
                    continue;
                }

                // check if r2 is a subrect of r1
                x_diff = free_r2->x - free_r1->x;
                y_diff = free_r2->y - free_r1->y;
                if (x_diff >= 0 && y_diff >= 0 &&
                    free_r2->w < free_r1->w - x_diff &&
                    free_r2->h < free_r1->h - y_diff)
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
        for (int y = 0; y < image->height; y += 1) {
            for (int x = 0; x < image->width; x += 1) {
                int offset = img->r90 ?
                    (out_pitch * x + (image->height - y - 1) * 32) :
                    (out_pitch * y + x * 32);
                memcpy(out_bits + offset, img_bits + x * 4, 4);
            }
            img_bits += img_pitch;
        }
    }

    // write it to a memory buffer
    FIMEMORY *out_stream = FreeImage_OpenMemory(NULL, 0);
    FreeImage_SaveToMemory(FIF_PNG, out_bmp, out_stream, 0);

    // write that to a debug file
    BYTE *data;
    DWORD data_size;
    FreeImage_AcquireMemory(out_stream, &data, &data_size);
    FILE *out_f = fopen("testout.png", "wb");
    fwrite(data, 1, data_size, out_f);
    fclose(out_f);
    FreeImage_CloseMemory(out_stream);

    return 0;
}
