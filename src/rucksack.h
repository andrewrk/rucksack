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

/* when modifying this structure, remember to add the corresponding entry
 * in ERROR_STR in rucksack.c */
enum RuckSackError {
    RuckSackErrorNone,
    RuckSackErrorNoMem,
    RuckSackErrorFileAccess,
    RuckSackErrorInvalidFormat,
    RuckSackErrorWrongVersion,
    RuckSackErrorEmptyFile,
    RuckSackErrorInvalidAnchor,
    RuckSackErrorCannotFit,
    RuckSackErrorNoPixels,
    RuckSackErrorImageFormat,
    RuckSackErrorNotFound,
};

/* the size of this struct is not part of the public ABI. */
struct RuckSackBundle {
    /* the directory to do all path searches relative to */
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

/* the size of this struct is not part of the public ABI.
 * Create with rucksack_image_create */
struct RuckSackImage {
    /* when writing, set this value. when reading it is set automatically. */
    char *key;
    /* key is an array of bytes, not a null-delimited string. however,
     * key_size defaults to -1 which tells rucksack to run strlen on key. */
    int key_size;
    /* when writing, set this value. when reading, it is NULL. */
    char *path;
    /* defaults to RuckSackAnchorCenter */
    enum RuckSackAnchor anchor;
    /* set these if you set anchor to RuckSackAnchorExplicit */
    int anchor_x;
    int anchor_y;

    /* the following fields are assigned after a call to
     * rucksack_bundle_add_texture and also populated when reading a texture
     * from a bundle */
    int width;
    int height;

    int x;
    int y;

    /* whether this image is rotated 90 degrees */
    char r90;
};

/* A RuckSackTexture contains multiple images. Also known as a spritesheet.
 * The size of this struct is not part of the public ABI.
 * Use rucksack_texture_create to make one. */
struct RuckSackTexture {
    /* defaults to 1024x1024 */
    int max_width;
    int max_height;
    /* whether powers of 2 are required. Defaults to 1. */
    char pow2;
    /* normally rucksack is free to rotate images 90 degrees if it would
     * provide tighter texture packing. Set this field to 0 to prevent this. */
    char allow_r90;
};

struct RuckSackOutStream;

/* common API */
void rucksack_init(void);
void rucksack_finish(void);

void rucksack_version(int *major, int *minor, int *patch);
int rucksack_bundle_version(void);

const char *rucksack_err_str(int err);

int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **bundle);
int rucksack_bundle_close(struct RuckSackBundle *bundle);

/* write API */
struct RuckSackTexture *rucksack_texture_create(void);
void rucksack_texture_destroy(struct RuckSackTexture *texture);

struct RuckSackImage *rucksack_image_create(void);
void rucksack_image_destroy(struct RuckSackImage *image);

/* rucksack copies data from the image you pass here; you still own the memory. */
int rucksack_texture_add_image(struct RuckSackTexture *texture, struct RuckSackImage *image);

int rucksack_bundle_add_texture(struct RuckSackBundle *bundle, const char *key,
        struct RuckSackTexture *texture);
int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        const char *file_name);
int rucksack_bundle_add_stream(struct RuckSackBundle *bundle, const char *key,
        long size_guess, struct RuckSackOutStream **stream);

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
long rucksack_file_mtime(struct RuckSackFileEntry *entry);
int rucksack_file_read(struct RuckSackFileEntry *entry, unsigned char *buffer);

/* call rucksack_texture_destroy when done */
int rucksack_file_open_texture(struct RuckSackFileEntry *entry, struct RuckSackTexture **texture);

/* get the size of the image data for this texture */
long rucksack_texture_size(struct RuckSackTexture *texture);
/* get the image data for this texture */
int rucksack_texture_read(struct RuckSackTexture *texture, unsigned char *buffer);

/* image metadata */
long rucksack_texture_image_count(struct RuckSackTexture *texture);
void rucksack_texture_get_images(struct RuckSackTexture *texture,
        struct RuckSackImage **images);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RUCKSACK_H_INCLUDED */
