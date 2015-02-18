/*
 * Copyright (c) 2015 Andrew Kelley
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
    RuckSackErrorStreamOpen,
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
    float anchor_x;
    float anchor_y;

    /* the following fields are assigned after a call to
     * rucksack_bundle_add_texture and also populated when reading a texture
     * from a bundle */
    int width;
    int height;

    int x;
    int y;

    /* whether this image is rotated 90 degrees
     * you may set this value to force an image to be rotated which may be
     * useful for debugging. */
    char r90;
};

/* A RuckSackTexture contains multiple images. Also known as a spritesheet.
 * The size of this struct is not part of the public ABI.
 * Use rucksack_texture_create to make one. */
struct RuckSackTexture {
    /* when writing, set this value. when reading it is set automatically. */
    char *key;
    /* key is an array of bytes, not a null-delimited string. however,
     * key_size defaults to -1 which tells rucksack to run strlen on key. */
    int key_size;

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

void rucksack_version(int *major, int *minor, int *patch);
int rucksack_bundle_version(void);

const char *rucksack_err_str(int err);

/* open for reading and writing */
int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **bundle);
int rucksack_bundle_open_precise(const char *bundle_path, struct RuckSackBundle **bundle,
        long headers_size);
/* open read-only */
int rucksack_bundle_open_read(const char *bundle_path, struct RuckSackBundle **bundle);
int rucksack_bundle_close(struct RuckSackBundle *bundle);

int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        int key_size, const char *file_name);
int rucksack_bundle_add_stream(struct RuckSackBundle *bundle, const char *key,
        int key_size, long size_guess, struct RuckSackOutStream **stream);
int rucksack_bundle_add_stream_precise(struct RuckSackBundle *bundle, const char *key,
        int key_size, long size, struct RuckSackOutStream **stream, long mtime);

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        long count);
void rucksack_stream_close(struct RuckSackOutStream *stream);

int rucksack_bundle_delete_file(struct RuckSackBundle *bundle, const char *key,
        int key_size);

/* mark this texture so that rucksack_bundle_delete_untouched will not delete it */
void rucksack_texture_touch(struct RuckSackTexture *texture);


long rucksack_bundle_file_count(struct RuckSackBundle *bundle);
void rucksack_bundle_get_files(struct RuckSackBundle *bundle,
        struct RuckSackFileEntry **entries);

struct RuckSackFileEntry *rucksack_bundle_find_file(
        struct RuckSackBundle *bundle, const char *key, int key_size);
long rucksack_file_size(struct RuckSackFileEntry *entry);
const char *rucksack_file_name(struct RuckSackFileEntry *entry);
int rucksack_file_name_size(struct RuckSackFileEntry *entry);
long rucksack_file_mtime(struct RuckSackFileEntry *entry);
int rucksack_file_read(struct RuckSackFileEntry *entry, unsigned char *buffer);

/* mark this file so that rucksack_bundle_delete_untouched will not delete it */
void rucksack_file_touch(struct RuckSackFileEntry *entry);

/* answer is placed in is_texture; possible error returned */
int rucksack_file_is_texture(struct RuckSackFileEntry *entry, int *is_texture);
/* call rucksack_texture_close when done */
int rucksack_file_open_texture(struct RuckSackFileEntry *entry, struct RuckSackTexture **texture);
/* call this to close textures you have opened with rucksack_file_open_texture */
void rucksack_texture_close(struct RuckSackTexture *texture);

/* get the size of the image data for this texture */
long rucksack_texture_size(struct RuckSackTexture *texture);
/* get the image data for this texture */
int rucksack_texture_read(struct RuckSackTexture *texture, unsigned char *buffer);

/* image metadata */
long rucksack_texture_image_count(struct RuckSackTexture *texture);
void rucksack_texture_get_images(struct RuckSackTexture *texture,
        struct RuckSackImage **images);

/* usually not needed. used by the `strip` command */
long rucksack_bundle_get_headers_byte_count(struct RuckSackBundle *bundle);

/* delete all file entries you have not written to while the bundle was open */
void rucksack_bundle_delete_untouched(struct RuckSackBundle *bundle);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RUCKSACK_H_INCLUDED */
