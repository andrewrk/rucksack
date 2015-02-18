/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "spritesheet.h"
#include "shared.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

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
    img->externals.r90 = userimg->r90;

    image->width = FreeImage_GetWidth(bmp);
    image->height = FreeImage_GetHeight(bmp);

    image->anchor = userimg->anchor;
    switch (userimg->anchor) {
        case RuckSackAnchorExplicit:
            image->anchor_x = userimg->anchor_x;
            image->anchor_y = userimg->anchor_y;
            break;
        case RuckSackAnchorCenter:
            image->anchor_x = image->width / 2.0f;
            image->anchor_y = image->height / 2.0f;
            break;
        case RuckSackAnchorLeft:
            image->anchor_x = 0.0f;
            image->anchor_y = image->height / 2.0f;
            break;
        case RuckSackAnchorRight:
            image->anchor_x = image->width;
            image->anchor_y = image->height / 2.0f;
            break;
        case RuckSackAnchorTop:
            image->anchor_x = image->width / 2.0f;
            image->anchor_y = 0.0f;
            break;
        case RuckSackAnchorBottom:
            image->anchor_x = image->width / 2.0f;
            image->anchor_y = image->height;
            break;
        case RuckSackAnchorTopLeft:
            image->anchor_x = 0.0f;
            image->anchor_y = 0.0f;
            break;
        case RuckSackAnchorTopRight:
            image->anchor_x = image->width;
            image->anchor_y = 0.0f;
            break;
        case RuckSackAnchorBottomLeft:
            image->anchor_x = 0.0f;
            image->anchor_y = image->height;
            break;
        case RuckSackAnchorBottomRight:
            image->anchor_x = image->width;
            image->anchor_y = image->height;
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

static void write_float32be(unsigned char *buf, float x) {
    write_uint32be(buf, x * FIXED_POINT_N);
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
            if (!image->r90) {
                int w_len = free_r->w - image->width;
                int h_len = free_r->h - image->height;
                int short_side = (w_len < h_len) ? w_len : h_len;
                int can_fit = w_len > 0 && h_len > 0;
                if (can_fit && short_side < best_short_side) {
                    best_short_side = short_side;
                    best_rect = free_r;
                    best_short_side_is_r90 = 0;
                }
            }

            // calculate short side fit with rotating 90 degrees
            if (texture->allow_r90 || image->r90) {
                int w_len = free_r->w - image->height;
                int h_len = free_r->h - image->width;
                int short_side = (w_len < h_len) ? w_len : h_len;
                int can_fit = w_len > 0 && h_len > 0;
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
        write_float32be(&buf[8], image->anchor_x);
        write_float32be(&buf[12], image->anchor_y);
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

struct RuckSackTexture *rucksack_texture_create(void) {
    struct RuckSackTexturePrivate *p = calloc(1, sizeof(struct RuckSackTexturePrivate));
    if (!p)
        return NULL;
    FreeImage_Initialise(0);
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
    FreeImage_DeInitialise();
}

