#include "rucksack.h"

#include <FreeImage.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>


#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;
    FILE *f;
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
        struct RuckSackImagePrivate *img = &p->images[p->images_count];
        free(img->key);
        FreeImage_Unload(img->bmp);
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

    i->key = mystrdup(key);

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
    FILE *out_f = fopen("testout.png", "wb");
    fwrite(data, 1, data_size, out_f);
    fclose(out_f);
    FreeImage_CloseMemory(out_stream);

    return 0;
}
