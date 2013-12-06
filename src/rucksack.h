/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef RUCKSACK_H_INCLUDED
#define RUCKSACK_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

enum RuckSackError {
    RuckSackErrorNone,
    RuckSackErrorNoMem,
    RuckSackErrorFileAccess,
    RuckSackErrorInvalidFormat,
    RuckSackErrorInvalidAnchor,
    RuckSackErrorCannotFit,
    RuckSackErrorNoPixels,
    RuckSackErrorImageFormat,
    RuckSackErrorNotFound,
};

struct RuckSackBundle {
    // the directory to do all path searches relative to
    const char *cwd;

};

struct RuckSackFileEntry;

enum RuckSackAnchor {
    RuckSackAnchorCenter,
    RuckSackAnchorExplicit,

    RuckSackAnchorLeft,
    RuckSackAnchorRight,
    RuckSackAnchorTop,
    RuckSackAnchorBottom,

    RuckSackAnchorTopLeft,
    RuckSackAnchorTopRight,
    RuckSackAnchorBottomLeft,
    RuckSackAnchorBottomRight,
};

struct RuckSackImage {
    // write API: this is the path
    // read API: this is the key
    char *name;
    enum RuckSackAnchor anchor;
    int anchor_x;
    int anchor_y;

    // assigned after a call to rucksack_bundle_add_page and also populated
    // when reading a page from a bundle
    int width;
    int height;

    int x;
    int y;
    char r90;
};

// a page contains multiple images. also known as texture or spritesheet
struct RuckSackPage {
    int max_width;
    int max_height;
    // whether powers of 2 are required
    char pow2;
};

struct RuckSackOutStream;

struct RuckSackTexture;

/* common API */
void rucksack_init(void);
void rucksack_finish(void);

void rucksack_version(int *major, int *minor, int *patch);

const char *rucksack_err_str(int err);

int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **bundle);
int rucksack_bundle_close(struct RuckSackBundle *bundle);

/* write API */
struct RuckSackPage *rucksack_page_create(void);
void rucksack_page_destroy(struct RuckSackPage *page);

int rucksack_bundle_add_page(struct RuckSackBundle *bundle, const char *key,
        struct RuckSackPage *page);
int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        const char *file_name);

int rucksack_bundle_add_stream(struct RuckSackBundle *bundle, const char *key,
        long size_guess, struct RuckSackOutStream **stream);

int rucksack_page_add_image(struct RuckSackPage *page, const char *key,
        struct RuckSackImage *image);

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        long count);
void rucksack_stream_close(struct RuckSackOutStream *stream);

/* read API */
long rucksack_bundle_file_count(struct RuckSackBundle *bundle);
void rucksack_bundle_get_files(struct RuckSackBundle *bundle,
        struct RuckSackFileEntry **entries);

struct RuckSackFileEntry *rucksack_bundle_find_file(
        struct RuckSackBundle *bundle, const char *key);
long rucksack_file_size(struct RuckSackFileEntry *entry);
const char *rucksack_file_name(struct RuckSackFileEntry *entry);
int rucksack_file_read(struct RuckSackFileEntry *entry, unsigned char *buffer);

int rucksack_file_open_texture(struct RuckSackFileEntry *entry, struct RuckSackTexture **texture);
void rucksack_texture_close(struct RuckSackTexture *texture);
// get the size of the image data for this texture
long rucksack_texture_size(struct RuckSackTexture *texture);
// get the image data for this texture
int rucksack_texture_read(struct RuckSackTexture *texture, unsigned char *buffer);

// image metadata
long rucksack_texture_image_count(struct RuckSackTexture *texture);
void rucksack_texture_get_images(struct RuckSackTexture *texture,
        struct RuckSackImage **images);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RUCKSACK_H_INCLUDED */
