/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <laxjson.h>

#include "rucksack.h"
#include "stringlist.h"
#include "path.h"
#include "util.h"

struct RuckSackBundle *bundle;
static char buffer[16384];
static struct LaxJsonContext *json;

enum State {
    StateStart,
    StateTopLevelProp,
    StateDone,
    StateTextures,
    StateTextureName,
    StateExpectImagesObject,
    StateExpectGlobImagesArray,
    StateImageName,
    StateImageObjectBegin,
    StateImagePropName,
    StateImagePropAnchor,
    StateImagePropAnchorObject,
    StateImagePropAnchorX,
    StateImagePropAnchorY,
    StateImagePropPath,
    StateExpectTextureObject,
    StateTextureProp,
    StateTextureMaxWidth,
    StateTextureMaxHeight,
    StateTexturePow2,
    StateTextureAllowRotate90,
    StateExpectFilesObject,
    StateFileName,
    StateFileObjectBegin,
    StateFilePropName,
    StateFilePropPath,
    StateExpectGlobArray,
    StateGlobObject,
    StateGlobObjectProp,
    StateGlobValueGlob,
    StateGlobValuePrefix,
    StateGlobValuePath,
    StateGlobImageObject,
    StateGlobImageObjectProp,
    StateGlobImageValueGlob,
    StateGlobImageValuePrefix,
    StateGlobImageValuePath,
};

static const char *STATE_STR[] = {
    "StateStart",
    "StateTopLevelProp",
    "StateDone",
    "StateTextures",
    "StateTextureName",
    "StateExpectImagesObject",
    "StateExpectGlobImagesArray",
    "StateImageName",
    "StateImageObjectBegin",
    "StateImagePropName",
    "StateImagePropAnchor",
    "StateImagePropAnchorObject",
    "StateImagePropAnchorX",
    "StateImagePropAnchorY",
    "StateImagePropPath",
    "StateExpectTextureObject",
    "StateTextureProp",
    "StateTextureMaxWidth",
    "StateTextureMaxHeight",
    "StateTexturePow2",
    "StateTextureAllowRotate90",
    "StateExpectFilesObject",
    "StateFileName",
    "StateFileObjectBegin",
    "StateFilePropName",
    "StateFilePropPath",
    "StateExpectGlobArray",
    "StateGlobObject",
    "StateGlobObjectProp",
    "StateGlobValueGlob",
    "StateGlobValuePrefix",
    "StateGlobValuePath",
    "StateGlobImageObject",
    "StateGlobImageObjectProp",
    "StateGlobImageValueGlob",
    "StateGlobImageValuePrefix",
    "StateGlobImageValuePath",
};

static enum State state;
static char parse_err_occurred = 0;
static char strbuf[4096];
static char strbuf2[4096];
static char strbuf3[4096];
static char strbuf4[4096];

// texture we will add to the bundle
static struct RuckSackTexture *texture = NULL;

// the texture that is already in the bundle
static struct RuckSackTexture *bundle_texture = NULL;
static struct RuckSackFileEntry *bundle_texture_entry = NULL;
static int dirty_texture_flag = 0;
static long bundle_mtime = 0;
static long bundle_texture_image_count = 0;
static struct RuckSackImage **bundle_texture_images = NULL;

static char *file_key = NULL;
static int file_key_size = 0;
static char *file_path = NULL;

struct RuckSackImage *image = NULL;

static char *path_prefix = ".";

static struct RuckSackStringList *deps_list = NULL;

static char *glob_glob = NULL;
static char *glob_path = NULL;
static char *glob_prefix = NULL;

// after collecting the anchor property, sets state to this one
static enum State anchor_next_state;

static char debug_mode = 0;
static char verbose = 0;

static const char *ERR_STR[] = {
    "",
    "unexpected char",
    "expected EOF",
    "exceeded max stack",
    "no mem",
    "exceeded max value size",
    "invalid hex digit",
    "invalid unicode point",
    "expected colon",
    "unexpected EOF",
    "aborted",
};

static const char *JSON_TYPE_STR[] = {
    "String",
    "Property",
    "Number",
    "Object",
    "Array",
    "True",
    "False",
    "Null",
};

static char *dupe_c_string(const char *str) {
    int len = -1;
    return dupe_string(str, &len);
}

static void append_dep(char *dep) {
    if (deps_list) {
        enum RuckSackError err = rucksack_stringlist_append(deps_list, dep, -1);
        if (err != RuckSackErrorNone) {
            fprintf(stderr, "error: %s\n", rucksack_err_str(err));
            exit(-1);
        }
    }
}

static int parse_error(const char *msg) {
    if (parse_err_occurred)
        return -1;

    fprintf(stderr, "line %d, col %d: %s\n", json->line, json->column, msg);
    parse_err_occurred = 1;
    return -1;
}

static char *resolve_path(const char *path) {
    char *out = malloc(2048);
    if (out) {
        char resolved_path[2048];
        path_resolve(path_prefix, path, resolved_path);
        path_relative(path_prefix, resolved_path, out);
    }
    return out;
}

static char *memstrclone(const char *mem, int length) {
    char *clone = malloc(length + 1);
    if (clone) {
        memcpy(clone, mem, length);
        clone[length] = 0;
    }
    return clone;
}

static int memneql(const char *mem1, int mem1_size, const char *mem2, int mem2_size) {
    if (mem1_size != mem2_size)
        return 1;
    else
        return memcmp(mem1, mem2, mem1_size);
}

static void check_if_image_dirty(void) {
    // if already marked as dirty, we have no work to do.
    if (dirty_texture_flag)
        return;

    struct stat st;
    stat(image->path, &st);
    long mtime = st.st_mtime;
    if (mtime > bundle_mtime) {
        dirty_texture_flag = 1;
        return;
    }

    for (int i = 0; i < bundle_texture_image_count; i += 1) {
        struct RuckSackImage *bundle_image = bundle_texture_images[i];
        if (memneql(bundle_image->key, bundle_image->key_size, image->key, image->key_size) == 0) {
            dirty_texture_flag = bundle_image->anchor != image->anchor ||
                (image->anchor == RuckSackAnchorExplicit &&
                (bundle_image->anchor_x != image->anchor_x ||
                bundle_image->anchor_y != image->anchor_y));
            return;
        }
    }

    // did not find the image in the texture.
    dirty_texture_flag = 1;
}

static int add_texture_if_outdated(struct RuckSackBundle *bundle, 
        struct RuckSackTexture *texture)
{
    if (bundle_texture_entry) {
        int up_to_date = !dirty_texture_flag &&
            bundle_texture->max_width == texture->max_width &&
            bundle_texture->max_height == texture->max_height &&
            bundle_texture->pow2 == texture->pow2 &&
            bundle_texture->allow_r90 == texture->allow_r90;
        rucksack_texture_destroy(bundle_texture);
        free(bundle_texture_images);
        bundle_texture_images = NULL;
        if (up_to_date) {
            if (verbose)
                fprintf(stderr, "Texture up to date: %s\n", texture->key);
            return 0;
        }
        if (verbose)
            fprintf(stderr, "Updating texture: %s\n", texture->key);
    } else if (verbose) {
        fprintf(stderr, "New texture: %s\n", texture->key);
    }
    int err = rucksack_bundle_add_texture(bundle, texture);
    if (err) {
        snprintf(strbuf, sizeof(strbuf), "unable to add texture: %s", rucksack_err_str(err));
        return parse_error(strbuf);
    }
    return 0;
}

static int add_file_if_outdated(struct RuckSackBundle *bundle,
        char *key, int key_size, char *path)
{
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, key, key_size);
    if (entry) {
        long bundle_mtime = rucksack_file_mtime(entry);
        struct stat st;
        stat(path, &st);
        long file_mtime = st.st_mtime;
        if (file_mtime <= bundle_mtime) {
            if (verbose)
                fprintf(stderr, "File up to date: %s\n", key);
            return 0;
        }
        if (verbose)
            fprintf(stderr, "Updating file: %s\n", key);
    } else if (verbose) {
        fprintf(stderr, "New file: %s\n", key);
    }
    append_dep(path);
    int err = rucksack_bundle_add_file(bundle, key, key_size, path);
    if (err) {
        snprintf(strbuf, sizeof(strbuf), "unable to add %s: %s", path, rucksack_err_str(err));
        return parse_error(strbuf);
    }
    return 0;
}

static int perform_glob(int (*match_callback)(char *key, int key_size, char *path)) {
    char *use_glob_str = glob_glob ? glob_glob : "*";
    char *use_glob_path = glob_path ? glob_path : "";
    char *use_glob_prefix = glob_prefix ? glob_prefix : "";

    glob_t glob_result;
    path_join(path_prefix, use_glob_path, strbuf3);
    path_join(strbuf3, use_glob_str, strbuf2);
    int err = glob(strbuf2, GLOB_NOSORT, NULL, &glob_result);

    switch (err) {
        case GLOB_NOSPACE:
            return parse_error("out of memory");
        case GLOB_ABORTED:
            return parse_error("read error while globbing");
        case GLOB_NOMATCH:
            return parse_error("no patterns matched");
    }

    append_dep(*use_glob_path == '0' ? "." : use_glob_path);

    for (unsigned int i = 0; i < glob_result.gl_pathc; i += 1) {
        struct stat s;
        char *path = glob_result.gl_pathv[i];
        if (stat(path, &s) != 0) {
            snprintf(strbuf4, sizeof(strbuf4), "unable to stat %s", path);
            return parse_error(strbuf4);
        }

        if (S_ISDIR(s.st_mode))
            continue;

        // compute a relative path so we can use it to build the key
        path_relative(strbuf3, path, strbuf2);

        snprintf(strbuf4, sizeof(strbuf4), "%s%s", use_glob_prefix, strbuf2);
        err = match_callback(strbuf4, strlen(strbuf4), path);
        if (err) return err;
    }

    globfree(&glob_result);

    return 0;
}

static int add_glob_match_to_bundle(char *key, int key_size, char *path) {
    return add_file_if_outdated(bundle, key, key_size, path);
}

static int glob_insert_files(void) {
    return perform_glob(add_glob_match_to_bundle);
}

static int add_glob_match_to_texture(char *key, int key_size, char *path) {
    image->path = path;
    image->key = key;
    image->key_size = key_size;
    append_dep(path);
    int err = rucksack_texture_add_image(texture, image);
    if (err) {
        snprintf(strbuf, sizeof(strbuf), "unable to add image to texture: %s", rucksack_err_str(err));
        return parse_error(strbuf);
    }
    check_if_image_dirty();
    image->path = NULL;
    image->key = NULL;
    return 0;
}

static int glob_insert_images(void) {
    return perform_glob(add_glob_match_to_texture);
}

static int on_string(struct LaxJsonContext *json, enum LaxJsonType type,
        const char *value, int length)
{
    if (debug_mode)
        fprintf(stderr, "state: %s, %s: %s\n", STATE_STR[state], JSON_TYPE_STR[type], value);
    switch (state) {
        case StateStart:
            return parse_error("top-level value must be an object, not string"); 
        case StateDone:
            return parse_error("unexpected content after EOF");
        case StateTopLevelProp:
            if (strcmp(value, "textures") == 0) {
                state = StateTextures;
            } else if (strcmp(value, "files") == 0) {
                state = StateExpectFilesObject;
            } else if (strcmp(value, "globFiles") == 0) {
                state = StateExpectGlobArray;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown top level property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateTextures:
            return parse_error("expected textures to be an object, not string");
        case StateTextureName:
            texture = rucksack_texture_create();
            if (!texture)
                return parse_error("out of memory");
            texture->key = memstrclone(value, length);
            texture->key_size = length;
            bundle_texture_entry = rucksack_bundle_find_file(bundle, texture->key, texture->key_size);
            dirty_texture_flag = 0;
            if (bundle_texture_entry) {
                rucksack_file_open_texture(bundle_texture_entry, &bundle_texture);
                if (bundle_texture) {
                    bundle_mtime = rucksack_file_mtime(bundle_texture_entry);
                    bundle_texture_image_count = rucksack_texture_image_count(bundle_texture);
                    bundle_texture_images = malloc(sizeof(struct RuckSackImage *) *
                            bundle_texture_image_count);
                    if (!bundle_texture_images)
                        return parse_error("out of memory");
                    rucksack_texture_get_images(bundle_texture, bundle_texture_images);
                } else {
                    dirty_texture_flag = 1;
                }
            }
            state = StateExpectTextureObject;
            break;
        case StateExpectImagesObject:
            return parse_error("expected images object, not string");
        case StateExpectGlobImagesArray:
            return parse_error("expected globImages array, not string");
        case StateImageName:
            image->anchor = RuckSackAnchorCenter;
            image->path = NULL;
            image->key = memstrclone(value, length);
            if (!image->key)
                return parse_error("out of memory");
            image->key_size = length;
            state = StateImageObjectBegin;
            break;
        case StateFileName:
            file_key = memstrclone(value, length);
            file_key_size = length;
            state = StateFileObjectBegin;
            break;
        case StateImageObjectBegin:
            return parse_error("expected image properties object, not string");
        case StateFilePropName:
            if (strcmp(value, "path") == 0) {
                state = StateFilePropPath;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown file property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateImagePropName:
            if (strcmp(value, "anchor") == 0) {
                state = StateImagePropAnchor;
                anchor_next_state = StateImagePropName;
            } else if (strcmp(value, "path") == 0) {
                state = StateImagePropPath;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown image property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateImagePropAnchor:
            if (strcmp(value, "top") == 0) {
                image->anchor = RuckSackAnchorTop;
            } else if (strcmp(value, "right") == 0) {
                image->anchor = RuckSackAnchorRight;
            } else if (strcmp(value, "bottom") == 0) {
                image->anchor = RuckSackAnchorBottom;
            } else if (strcmp(value, "left") == 0) {
                image->anchor = RuckSackAnchorLeft;
            } else if (strcmp(value, "topleft") == 0) {
                image->anchor = RuckSackAnchorTopLeft;
            } else if (strcmp(value, "topright") == 0) {
                image->anchor = RuckSackAnchorTopRight;
            } else if (strcmp(value, "bottomleft") == 0) {
                image->anchor = RuckSackAnchorBottomLeft;
            } else if (strcmp(value, "bottomright") == 0) {
                image->anchor = RuckSackAnchorBottomRight;
            } else if (strcmp(value, "center") == 0) {
                image->anchor = RuckSackAnchorCenter;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown anchor value: %s", value);
                return parse_error(strbuf);
            }
            state = anchor_next_state;
            break;
        case StateImagePropAnchorObject:
            if (strcmp(value, "x") == 0) {
                state = StateImagePropAnchorX;
            } else if (strcmp(value, "y") == 0) {
                state = StateImagePropAnchorY;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown anchor point property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateImagePropAnchorX:
        case StateImagePropAnchorY:
            return parse_error("expected number");
        case StateFilePropPath:
            file_path = resolve_path(value);
            if (!file_path)
                return parse_error("out of memory");
            state = StateFilePropName;
            break;
        case StateImagePropPath:
            image->path = resolve_path(value);
            if (!image->path)
                return parse_error("out of memory");
            state = StateImagePropName;
            break;
        case StateTextureProp:
            if (strcmp(value, "images") == 0) {
                state = StateExpectImagesObject;
            } else if (strcmp(value, "globImages") == 0) {
                state = StateExpectGlobImagesArray;
            } else if (strcmp(value, "maxWidth") == 0) {
                state = StateTextureMaxWidth;
            } else if (strcmp(value, "maxHeight") == 0) {
                state = StateTextureMaxHeight;
            } else if (strcmp(value, "pow2") == 0) {
                state = StateTexturePow2;
            } else if (strcmp(value, "allowRotate90") == 0) {
                state = StateTextureAllowRotate90;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown texture property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateGlobObjectProp:
            if (strcmp(value, "glob") == 0) {
                state = StateGlobValueGlob;
            } else if (strcmp(value, "prefix") == 0) {
                state = StateGlobValuePrefix;
            } else if (strcmp(value, "path") == 0) {
                state = StateGlobValuePath;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown globFiles property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateGlobImageObjectProp:
            if (strcmp(value, "glob") == 0) {
                state = StateGlobImageValueGlob;
            } else if (strcmp(value, "prefix") == 0) {
                state = StateGlobImageValuePrefix;
            } else if (strcmp(value, "path") == 0) {
                state = StateGlobImageValuePath;
            } else if (strcmp(value, "anchor") == 0) {
                state = StateImagePropAnchor;
                anchor_next_state = StateGlobImageObjectProp;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown globImages property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateGlobValueGlob:
            glob_glob = dupe_c_string(value);
            state = StateGlobObjectProp;
            break;
        case StateGlobValuePath:
            glob_path = dupe_c_string(value);
            state = StateGlobObjectProp;
            break;
        case StateGlobValuePrefix:
            glob_prefix = dupe_c_string(value);
            state = StateGlobObjectProp;
            break;
        case StateGlobImageValueGlob:
            glob_glob = dupe_c_string(value);
            state = StateGlobImageObjectProp;
            break;
        case StateGlobImageValuePath:
            glob_path = dupe_c_string(value);
            state = StateGlobImageObjectProp;
            break;
        case StateGlobImageValuePrefix:
            glob_prefix = dupe_c_string(value);
            state = StateGlobImageObjectProp;
            break;
        default:
            return parse_error("unexpected string");
    }

    return 0;
}

static int on_number(struct LaxJsonContext *json, double x) {
    if (debug_mode)
        fprintf(stderr, "state: %s, number: %f\n", STATE_STR[state], x);
    switch (state) {
        case StateStart:
            return parse_error("top-level value must be an object, not number"); 
        case StateDone:
            return parse_error("unexpected content after EOF");
        case StateTextures:
            return parse_error("expected textures to be an object, not number");
        case StateExpectImagesObject:
            return parse_error("expected image object, not number");
        case StateExpectGlobImagesArray:
            return parse_error("expected globImages array, not number");
        case StateImageObjectBegin:
            return parse_error("expected image properties object, not number");
        case StateImagePropAnchor:
            return parse_error("expected object or string, not number");
        case StateImagePropAnchorX:
            image->anchor_x = x;
            state = StateImagePropAnchorObject;
            break;
        case StateImagePropAnchorY:
            image->anchor_y = x;
            state = StateImagePropAnchorObject;
            break;
        case StateImagePropPath:
            return parse_error("expected string, not number");
        case StateTextureMaxWidth:
            if (x != (double)(int)x)
                return parse_error("expected integer");
            texture->max_width = (int)x;
            state = StateTextureProp;
            break;
        case StateTextureMaxHeight:
            if (x != (double)(int)x)
                return parse_error("expected integer");
            texture->max_height = (int)x;
            state = StateTextureProp;
            break;
        default:
            return parse_error("unexpected number");
    }

    return 0;
}

static int on_primitive(struct LaxJsonContext *json, enum LaxJsonType type) {
    if (debug_mode)
        fprintf(stderr, "state: %s, primitive: %s\n", STATE_STR[state], JSON_TYPE_STR[type]);
    switch (state) {
        case StateTexturePow2:
            switch (type) {
                case LaxJsonTypeTrue:
                    texture->pow2 = 1;
                    break;
                case LaxJsonTypeFalse:
                    texture->pow2 = 0;
                    break;
                default:
                    return parse_error("expected true or false");
            }
            state = StateTextureProp;
            break;
        case StateTextureAllowRotate90:
            switch (type) {
                case LaxJsonTypeTrue:
                    texture->allow_r90 = 1;
                    break;
                case LaxJsonTypeFalse:
                    texture->allow_r90 = 0;
                    break;
                default:
                    return parse_error("expected true or false");
            }
            state = StateTextureProp;
            break;
        default:
            return parse_error("unexpected primitive");
    }

    return 0;
}

static int on_begin(struct LaxJsonContext *json, enum LaxJsonType type) {
    if (debug_mode)
        fprintf(stderr, "state: %s, begin %s\n", STATE_STR[state], JSON_TYPE_STR[type]);
    if (type == LaxJsonTypeArray) {
        switch (state) {
            case StateExpectGlobArray:
                state = StateGlobObject;
                break;
            case StateExpectGlobImagesArray:
                state = StateGlobImageObject;
                break;
            default:
                return parse_error("unexpected array");
        }
    } else {
        assert(type == LaxJsonTypeObject);

        switch (state) {
            case StateStart:
                state = StateTopLevelProp;
                break;
            case StateTextures:
                state = StateTextureName;
                break;
            case StateExpectImagesObject:
                state = StateImageName;
                break;
            case StateExpectTextureObject:
                state = StateTextureProp;
                break;
            case StateImageObjectBegin:
                state = StateImagePropName;
                break;
            case StateFileObjectBegin:
                state = StateFilePropName;
                break;
            case StateImagePropAnchor:
                state = StateImagePropAnchorObject;
                image->anchor = RuckSackAnchorExplicit;
                break;
            case StateExpectFilesObject:
                state = StateFileName;
                break;
            case StateGlobObject:
                state = StateGlobObjectProp;
                glob_glob = NULL;
                glob_path = NULL;
                glob_prefix = NULL;
                break;
            case StateGlobImageObject:
                state = StateGlobImageObjectProp;
                glob_glob = NULL;
                glob_path = NULL;
                glob_prefix = NULL;
                // image is used to store anchor information
                image->anchor = RuckSackAnchorCenter;
                break;
            default:
                return parse_error("unexpected object");
        }
    }

    return 0;
}

static int on_end(struct LaxJsonContext *json, enum LaxJsonType type) {
    if (debug_mode)
        fprintf(stderr, "state: %s, end %s\n", STATE_STR[state], JSON_TYPE_STR[type]);
    int err;
    switch (state) {
        case StateStart:
            return parse_error("expected an object, got nothing");
        case StateTopLevelProp:
            // done parsing. nothing to do
            state = StateDone;
            break;
        case StateDone:
            return parse_error("unexpected content after EOF");
        case StateImagePropName:
            append_dep(image->path);
            err = rucksack_texture_add_image(texture, image);
            if (err) {
                snprintf(strbuf, sizeof(strbuf), "unable to add image to texture: %s", rucksack_err_str(err));
                return parse_error(strbuf);
            }
            check_if_image_dirty();

            free(image->path);
            image->path = NULL;
            free(image->key);
            image->key = NULL;

            state = StateImageName;
            break;
        case StateFilePropName:
            err = add_file_if_outdated(bundle, file_key, file_key_size, file_path);
            if (err) return err;

            free(file_path);
            file_path = NULL;
            free(file_key);
            file_key = NULL;

            state = StateFileName;
            break;
        case StateImagePropAnchorObject:
            state = anchor_next_state;
            break;
        case StateImageName:
            state = StateTextureProp;
            break;
        case StateTextureProp:
            err = add_texture_if_outdated(bundle, texture);
            if (err) return err;

            free(texture->key);
            rucksack_texture_destroy(texture);
            texture = NULL;

            state = StateTextureName;
            break;
        case StateGlobObjectProp:
            err = glob_insert_files();
            if (err) return err;
            free(glob_glob);
            free(glob_path);
            free(glob_prefix);
            state = StateGlobObject;
            break;
        case StateGlobImageObjectProp:
            err = glob_insert_images();
            if (err) return err;
            free(glob_glob);
            free(glob_path);
            free(glob_prefix);
            state = StateGlobImageObject;
            break;
        case StateTextureName:
        case StateGlobObject:
        case StateFileName:
            state = StateTopLevelProp;
            break;
        case StateGlobImageObject:
            state = StateTextureProp;
            break;
        default:
            return parse_error("unexpected end of object or array");
    }

    return 0;
}

static int bundle_usage(char *arg0) {
    fprintf(stderr, "Usage: %s bundle assetsfile bundlefile\n"
            "\n"
            "Options:\n"
            "  [--prefix path]  assets are loaded relative to this path. defaults to cwd\n"
            "  [--verbose]      print what is happening while it is happening\n"
            "  [--deps path]    generate a .d dependencies file\n"
            , arg0);
    return 1;
}

static int cat_usage(char *arg0) {
    fprintf(stderr, "Usage: %s cat bundlefile resourcename\n"
            "\n"
            "Options:\n"
            "  [--texture]  interpret as texture and output the image.\n"
            , arg0);
    return 1;
}

static int command_bundle(char *arg0, int argc, char *argv[]) {
    char *input_filename = NULL;
    char *bundle_filename = NULL;
    char *deps_filename = NULL;

    for (int i = 0; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            arg += 2;
            if (strcmp(arg, "verbose") == 0) {
                verbose = 1;
            } else if (i + 1 >= argc) {
                return bundle_usage(arg0);
            } else if (strcmp(arg, "prefix") == 0) {
                path_prefix = argv[++i];
            } else if (strcmp(arg, "deps") == 0) {
                deps_filename = argv[++i];
            } else {
                return bundle_usage(arg0);
            }
        } else if (!input_filename) {
            input_filename = arg;
        } else if (!bundle_filename) {
            bundle_filename = arg;
        } else {
            return bundle_usage(arg0);
        }
    }

    if (!input_filename)
        return bundle_usage(arg0);

    if (!bundle_filename)
        return bundle_usage(arg0);

    if (deps_filename) {
        deps_list = rucksack_stringlist_create();
        append_dep(input_filename);
    }

    FILE *in_f;
    if (strcmp(input_filename, "-") == 0) {
        in_f = stdin;
    } else {
        in_f = fopen(input_filename, "rb");
        if (!in_f) {
            fprintf(stderr, "Unable to open %s\n", input_filename);
            return 1;
        }
    }

    rucksack_init();
    atexit(rucksack_finish);

    int rs_err = rucksack_bundle_open(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    json = lax_json_create();
    json->string = on_string;
    json->number = on_number;
    json->primitive = on_primitive;
    json->begin = on_begin;
    json->end = on_end;

    size_t amt_read;
    while ((amt_read = fread(buffer, 1, sizeof(buffer), in_f))) {
        enum LaxJsonError err = lax_json_feed(json, amt_read, buffer);
        if (err) {
            parse_error(ERR_STR[err]);
            rucksack_bundle_close(bundle);
            return 1;
        }
    }
    enum LaxJsonError err = lax_json_eof(json);
    if (err) {
        parse_error(ERR_STR[err]);
        rucksack_bundle_close(bundle);
        return 1;
    }

    if (state != StateDone)
        parse_error("unexpected EOF");

    if (parse_err_occurred) {
        rucksack_bundle_close(bundle);
        return 1;
    }

    rs_err = rucksack_bundle_close(bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    if (deps_filename) {
        FILE *deps_f = fopen(deps_filename, "w");
        if (!deps_f) {
            fprintf(stderr, "Unable to open %s\n", deps_filename);
            return 1;
        }

        int column = fprintf(deps_f, "%s:", bundle_filename);
        for (int i = 0; i < deps_list->len; i += 1) {
            struct RuckSackString *dep = &deps_list->strs[i];
            int new_column = column + dep->len
                + 1  // space after the prev dep name
                + 1; // potential backslash on line break
            if (new_column > 80) {
                fprintf(deps_f, " \\\n ");
                column = 1;
            }
            column += fprintf(deps_f, " %s", dep->str);
        }
        for (int i = 0; i < deps_list->len; i += 1) {
            struct RuckSackString *dep = &deps_list->strs[i];
            fprintf(deps_f, "\n\n%s:", dep->str);
        }
        fprintf(deps_f, "\n");
        fclose(deps_f);
        rucksack_stringlist_destroy(deps_list);
    }

    return 0;
}

static int command_cat(char *arg0, int argc, char *argv[]) {
    char *bundle_filename = NULL;
    char *resource_name = NULL;

    char is_texture = 0;
    for (int i = 0; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            arg += 2;
            if (strcmp(arg, "texture") == 0) {
                is_texture = 1;
            } else {
                return cat_usage(arg0);
            }
        } else if (!bundle_filename) {
            bundle_filename = arg;
        } else if (!resource_name) {
            resource_name = arg;
        } else {
            return cat_usage(arg0);
        }
    }

    if (!bundle_filename)
        return cat_usage(arg0);

    if (!resource_name)
        return cat_usage(arg0);

    rucksack_init();
    atexit(rucksack_finish);

    int rs_err = rucksack_bundle_open_read(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open %s: %s\n", bundle_filename, rucksack_err_str(rs_err));
        return 1;
    }

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, resource_name, -1);
    if (!entry) {
        fprintf(stderr, "entry not found\n");
        return 1;
    }

    int entry_is_texture;
    rs_err = rucksack_file_is_texture(entry, &entry_is_texture);
    if (rs_err) {
        fprintf(stderr, "error reading bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }
    if (entry_is_texture) {
        struct RuckSackTexture *texture;
        rs_err = rucksack_file_open_texture(entry, &texture);

        if (rs_err) {
            fprintf(stderr, "unable to open texture entry: %s\n", rucksack_err_str(rs_err));
            return 1;
        }

        long size = rucksack_texture_size(texture);
        if (is_texture) {
            unsigned char *buffer = malloc(size);
            if (!buffer) {
                fprintf(stderr, "out of memory\n");
                return 1;
            }
            rs_err = rucksack_texture_read(texture, buffer);
            if (rs_err) {
                fprintf(stderr, "unable to read texture entry: %s\n", rucksack_err_str(rs_err));
                return 1;
            }

            if (fwrite(buffer, 1, size, stdout) != size) {
                fprintf(stderr, "error writing to stdout\n");
                return 1;
            }
        } else {
            printf("{\n");
            printf("  \"bytes\": %ld,\n", size);
            printf("  \"maxWidth\": %d,\n", texture->max_width);
            printf("  \"maxHeight\": %d,\n", texture->max_height);
            printf("  \"pow2\": %d,\n", texture->pow2);
            printf("  \"allowRotate90\": %d,\n", texture->allow_r90);
            printf("  \"images\": {\n");
            long image_count = rucksack_texture_image_count(texture);
            struct RuckSackImage **images = malloc(sizeof(struct RuckSackImage *) * image_count);
            rucksack_texture_get_images(texture, images);
            for (int i = 0; i < image_count; i += 1) {
                struct RuckSackImage *image = images[i];
                printf("    \"%s\": {\n", image->key);
                printf("      \"x\": %d,\n", image->x);
                printf("      \"y\": %d,\n", image->y);
                printf("      \"w\": %d,\n", image->width);
                printf("      \"h\": %d,\n", image->height);
                printf("      \"r90\": %d,\n", image->r90);
                printf("      \"anchor\": {\n");
                printf("        \"x\": %f,\n", image->anchor_x);
                printf("        \"y\": %f\n", image->anchor_y);
                printf("      }\n");
                printf("    }"); if (i < image_count - 1) printf(","); printf("\n");
            }
            printf("  }\n");
            printf("}\n");
            free(images);
        }
        rucksack_texture_destroy(texture);
    } else {
        long size = rucksack_file_size(entry);

        unsigned char *buffer = malloc(size);
        if (!buffer) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }

        rs_err = rucksack_file_read(entry, buffer);

        if (rs_err) {
            fprintf(stderr, "unable to read file entry: %s\n", rucksack_err_str(rs_err));
            return 1;
        }

        if (fwrite(buffer, 1, size, stdout) != size) {
            fprintf(stderr, "error writing to stdout\n");
            return 1;
        }

        free(buffer);
    }

    rs_err = rucksack_bundle_close(bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    return 0;
}

static int help_usage(char *arg0) {
    fprintf(stderr, "Usage: %s help command\n"
            , arg0);
    return 1;
}

static int list_usage(char *arg0) {
    fprintf(stderr, "Usage: %s list bundlefile\n"
            , arg0);
    return 1;
}

static int command_list(char *arg0, int argc, char *argv[]) {
    char *bundle_filename = NULL;

    for (int i = 0; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            return list_usage(arg0);
        } else if (!bundle_filename) {
            bundle_filename = arg;
        } else {
            return list_usage(arg0);
        }
    }

    if (!bundle_filename)
        return list_usage(arg0);

    rucksack_init();
    atexit(rucksack_finish);

    int rs_err = rucksack_bundle_open_read(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open %s: %s\n", bundle_filename, rucksack_err_str(rs_err));
        return 1;
    }

    size_t count = rucksack_bundle_file_count(bundle);

    struct RuckSackFileEntry **entries = malloc(count * sizeof(struct RuckSackFileEntry *));

    if (!entries) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    rucksack_bundle_get_files(bundle, entries);

    for (int i = 0; i < count; i += 1) {
        struct RuckSackFileEntry *e = entries[i];
        printf("%s\n", rucksack_file_name(e));
    }

    free(entries);

    rs_err = rucksack_bundle_close(bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    return 0;
}

static int strip_usage(char *arg0) {
    fprintf(stderr, "Usage: %s strip bundlefile\n", arg0);
    return 1;
}

static void get_tmp_name(char *out, int size) {
   static const char alphanumeric[64] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
   out[0] = '.';
   for(int i = 1; i < size - 1; i += 1) {
       int index = rand() % sizeof(alphanumeric);
       out[i] = alphanumeric[index];
   }
   out[size - 1] = 0;
}

static int command_strip(char *arg0, int argc, char *argv[]) {
    char *bundle_filename = NULL;

    for (int i = 0; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            return strip_usage(arg0);
        } else if (!bundle_filename) {
            bundle_filename = arg;
        } else {
            return strip_usage(arg0);
        }
    }

    if (!bundle_filename)
        return strip_usage(arg0);

    rucksack_init();
    atexit(rucksack_finish);

    char rand_str[18];
    get_tmp_name(rand_str, sizeof(rand_str));
    path_dirname(bundle_filename, strbuf);
    path_join(strbuf, rand_str, strbuf2);
    char *tmp_filename = strbuf2;

    int rs_err = rucksack_bundle_open_read(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open %s: %s\n", bundle_filename, rucksack_err_str(rs_err));
        return 1;
    }

    size_t count = rucksack_bundle_file_count(bundle);

    struct RuckSackFileEntry **entries = malloc(count * sizeof(struct RuckSackFileEntry *));

    if (!entries) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    rucksack_bundle_get_files(bundle, entries);

    long headers_size = rucksack_bundle_get_headers_byte_count(bundle);

    struct RuckSackBundle *out_bundle;
    rs_err = rucksack_bundle_open_precise(tmp_filename, &out_bundle, headers_size);
    if (rs_err) {
        fprintf(stderr, "unable to open %s: %s\n", tmp_filename, rucksack_err_str(rs_err));
        return 1;
    }
    // determine max file size
    long max_file_size = 0;
    for (int i = 0; i < count; i += 1) {
        struct RuckSackFileEntry *e = entries[i];
        long file_size = rucksack_file_size(e);
        if (file_size > max_file_size)
            max_file_size = file_size;
    }
    unsigned char *buffer = malloc(max_file_size);
    for (int i = 0; i < count; i += 1) {
        struct RuckSackFileEntry *e = entries[i];
        long file_size = rucksack_file_size(e);
        const char *file_name = rucksack_file_name(e);
        int file_name_size = rucksack_file_name_size(e);
        long file_mtime = rucksack_file_mtime(e);
        struct RuckSackOutStream *stream;
        rs_err = rucksack_bundle_add_stream_precise(out_bundle, file_name,
            file_name_size, file_size, &stream, file_mtime);
        if (rs_err) {
            fprintf(stderr, "unable to add stream: %s\n", rucksack_err_str(rs_err));
            remove(tmp_filename);
            return 1;
        }
        rs_err = rucksack_file_read(e, buffer);
        if (rs_err) {
            fprintf(stderr, "unable to read %s: %s\n", file_name, rucksack_err_str(rs_err));
            remove(tmp_filename);
            return 1;
        }
        rs_err = rucksack_stream_write(stream, buffer, file_size);
        if (rs_err) {
            fprintf(stderr, "unable to write %s: %s\n", file_name, rucksack_err_str(rs_err));
            remove(tmp_filename);
            return 1;
        }
        rucksack_stream_close(stream);
    }
    free(buffer);
    free(entries);

    rs_err = rucksack_bundle_close(out_bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    rs_err = rucksack_bundle_close(bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    if (rename(tmp_filename, bundle_filename)) {
        fprintf(stderr, "unable to rename %s to %s\n", tmp_filename, bundle_filename);
        remove(tmp_filename);
        return 1;
    }

    return 0;
}

struct Command {
    const char *name;
    int (*exec)(char *arg0, int agc, char *argv[]);
    int (*usage)(char *arg0);
    const char *desc;
};

static int command_help(char *arg0, int argc, char *argv[]);
static struct Command commands[] = {
    {"help", command_help, help_usage,
        "get info on how to use a command"},
    {"bundle", command_bundle, bundle_usage,
        "parses an assets json file and keeps a bundle up to date"},
    {"cat", command_cat, cat_usage,
        "extracts a single file from the bundle and writes it to stdout"},
    {"ls", command_list, list_usage,
        "lists all resources in a bundle"},
    {"strip", command_strip, strip_usage,
        "make an existing bundle as small as possible"},
    {NULL, NULL, NULL},
};

static int usage(char *arg0) {
    int major, minor, patch;
    rucksack_version(&major, &minor, &patch);
    fprintf(stderr, 
            "rucksack v%d.%d.%d\n"
            "\n"
            "Usage: %s [command] [command-options]\n"
            "\n"
            "Commands:\n"
            , major, minor, patch, arg0);

    struct Command *cmd = &commands[0];

    while (cmd->name) {
        fprintf(stderr, "  %-10s %s\n", cmd->name, cmd->desc);
        cmd += 1;
    }
    return 1;
}

static int command_help(char *arg0, int argc, char *argv[]) {
    if (argc != 1) return help_usage(arg0);

    char *cmd_name = argv[0];

    struct Command *cmd = &commands[0];

    while (cmd->name) {
        if (strcmp(cmd_name, cmd->name) == 0) {
            cmd->usage(arg0);
            return 0;
        }
        cmd += 1;
    }

    fprintf(stderr, "unrecognized command: %s\n", cmd_name);
    return 1;
}

static void cleanup(void) {
    rucksack_image_destroy(image);
}

int main(int argc, char *argv[]) {
    srand(time(NULL) + getpid());

    image = rucksack_image_create();
    atexit(cleanup);

    if (argc < 2) return usage(argv[0]);

    char *cmd_name = argv[1];

    struct Command *cmd = &commands[0];

    while (cmd->name) {
        if (strcmp(cmd_name, cmd->name) == 0)
            return cmd->exec(argv[0], argc - 2, argv + 2);
        cmd += 1;
    }

    return usage(argv[0]);
}
