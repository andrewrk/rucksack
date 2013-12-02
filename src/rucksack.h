#ifndef RUCKSACK_H_INCLUDED
#define RUCKSACK_H_INCLUDED

#include <stdlib.h>

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
    char *path;
    enum RuckSackAnchor anchor;
    int anchor_x;
    int anchor_y;

    unsigned int width;
    unsigned int height;
    // triangle strip
    int uv_coords[10];
};

// a page contains multiple images. also known as texture or spritesheet
struct RuckSackPage {
    int max_width;
    int max_height;
    // whether powers of 2 are required
    char pow2;
};

struct RuckSackOutStream;

void rucksack_init(void);
void rucksack_finish(void);

int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **bundle);
int rucksack_bundle_close(struct RuckSackBundle *bundle);

struct RuckSackPage *rucksack_page_create(void);
void rucksack_page_destroy(struct RuckSackPage *page);

int rucksack_bundle_add_page(struct RuckSackBundle *bundle, const char *key,
        struct RuckSackPage *page);
int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        const char *file_name);

int rucksack_bundle_add_stream(struct RuckSackBundle *bundle, const char *key,
        size_t size_guess, struct RuckSackOutStream **stream);
int rucksack_bundle_del(struct RuckSackBundle *bundle, const char *key);

int rucksack_page_add_image(struct RuckSackPage *page, const char *key,
        struct RuckSackImage *image);

struct RuckSackFileEntry *rucksack_bundle_find_file(
        struct RuckSackBundle *bundle, const char *key);
size_t rucksack_file_size(struct RuckSackFileEntry *entry);
int rucksack_bundle_file_read(struct RuckSackBundle *bundle,
        struct RuckSackFileEntry *entry, unsigned char *buffer);

void rucksack_bundle_get_page(struct RuckSackBundle *bundle, const char *key,
        struct RuckSackPage *page);
void rucksack_page_get_image(struct RuckSackPage *page, const char *key,
        struct RuckSackImage *image);

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        size_t count);
void rucksack_stream_close(struct RuckSackOutStream *stream);

#endif /* RUCKSACK_H_INCLUDED */
