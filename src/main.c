/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <laxjson.h>

#include "rucksack.h"

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
    StateExpectFilesObject,
    StateFileName,
    StateFileObjectBegin,
    StateFilePropName,
    StateFilePropPath,
};

static const char *STATE_STR[] = {
    "StateStart",
    "StateTopLevelProp",
    "StateDone",
    "StateTextures",
    "StateTextureName",
    "StateExpectImagesObject",
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
    "StateExpectFilesObject",
    "StateFileName",
    "StateFileObjectBegin",
    "StateFilePropName",
    "StateFilePropPath",
};

static enum State state;
static char parse_err_occurred = 0;
static char strbuf[500];

static struct RuckSackPage *page = NULL;
static char *page_key = NULL;

static char *file_key = NULL;
static char *file_path = NULL;

struct RuckSackImage image;
static char *image_key = NULL;

static char *path_prefix = ".";

static char debug_mode = 0;

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

static const char *RS_ERROR_STR[] = {
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

static int parse_error(const char *msg) {
    if (parse_err_occurred)
        return -1;

    fprintf(stderr, "line %d, col %d: %s\n", json->line, json->column, msg);
    parse_err_occurred = 1;
    return -1;
}

static char *resolve_path(const char *path) {
    if (path[0] == '/') {
        // absolute path - don't do anything to it
        return strdup(path);
    } else {
        char *out = malloc(512);
        snprintf(out, 512, "%s/%s", path_prefix, path);
        return out;
    }
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
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown top level property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateTextures:
            return parse_error("expected textures to be an object, not string");
        case StateTextureName:
            page = rucksack_page_create();
            page_key = strdup(value);
            state = StateExpectTextureObject;
            break;
        case StateExpectImagesObject:
            return parse_error("expected images object, not string");
        case StateImageName:
            image.anchor = RuckSackAnchorCenter;
            image.path = NULL;
            image_key = strdup(value);
            state = StateImageObjectBegin;
            break;
        case StateFileName:
            file_key = strdup(value);
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
            } else if (strcmp(value, "path") == 0) {
                state = StateImagePropPath;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown image property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateImagePropAnchor:
            if (strcmp(value, "top") == 0) {
                image.anchor = RuckSackAnchorTop;
            } else if (strcmp(value, "right") == 0) {
                image.anchor = RuckSackAnchorRight;
            } else if (strcmp(value, "bottom") == 0) {
                image.anchor = RuckSackAnchorBottom;
            } else if (strcmp(value, "left") == 0) {
                image.anchor = RuckSackAnchorLeft;
            } else if (strcmp(value, "topleft") == 0) {
                image.anchor = RuckSackAnchorTopLeft;
            } else if (strcmp(value, "topright") == 0) {
                image.anchor = RuckSackAnchorTopRight;
            } else if (strcmp(value, "bottomleft") == 0) {
                image.anchor = RuckSackAnchorBottomLeft;
            } else if (strcmp(value, "bottomright") == 0) {
                image.anchor = RuckSackAnchorBottomRight;
            } else if (strcmp(value, "center") == 0) {
                image.anchor = RuckSackAnchorCenter;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown anchor value: %s", value);
                return parse_error(strbuf);
            }
            state = StateImagePropName;
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
            state = StateFilePropName;
            break;
        case StateImagePropPath:
            image.path = resolve_path(value);
            state = StateImagePropName;
            break;
        case StateTextureProp:
            if (strcmp(value, "images") == 0) {
                state = StateExpectImagesObject;
            } else if (strcmp(value, "maxWidth") == 0) {
                state = StateTextureMaxWidth;
            } else if (strcmp(value, "maxHeight") == 0) {
                state = StateTextureMaxHeight;
            } else if (strcmp(value, "pow2") == 0) {
                state = StateTexturePow2;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown texture property: %s", value);
                return parse_error(strbuf);
            }
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
        case StateImageObjectBegin:
            return parse_error("expected image properties object, not number");
        case StateImagePropAnchor:
            return parse_error("expected object or string, not number");
        case StateImagePropAnchorX:
            image.anchor_x = x;
            state = StateImagePropAnchorObject;
            break;
        case StateImagePropAnchorY:
            image.anchor_x = x;
            state = StateImagePropAnchorObject;
            break;
        case StateImagePropPath:
            return parse_error("expected string, not number");
        case StateTextureMaxWidth:
            if (x != (double)(int)x)
                return parse_error("expected integer");
            page->max_width = (int)x;
            state = StateTextureProp;
            break;
        case StateTextureMaxHeight:
            if (x != (double)(int)x)
                return parse_error("expected integer");
            page->max_height = (int)x;
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
        case StateStart:
            return parse_error("top-level value must be an object, not primitive"); 
        case StateDone:
            return parse_error("unexpected content after EOF");
        case StateTextures:
            return parse_error("expected textures to be an object, not primitive");
        case StateExpectImagesObject:
            return parse_error("expected image object, not primitive");
        case StateImageObjectBegin:
            return parse_error("expected image properties object, not primitive");
        case StateImagePropAnchor:
            return parse_error("expected object or string, not primitive");
        case StateImagePropPath:
            return parse_error("expected string, not primitive");
        case StateTexturePow2:
            if (type == LaxJsonTypeTrue) {
                page->pow2 = 1;
            } else if (type == LaxJsonTypeFalse) {
                page->pow2 = 0;
            } else {
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
    switch (state) {
        case StateStart:
            if (type == LaxJsonTypeArray)
                return parse_error("top-level value must be an object, not array"); 
            state = StateTopLevelProp;
            break;
        case StateTopLevelProp:
            return parse_error("expected property name");
        case StateDone:
            return parse_error("unexpected content after EOF");
        case StateTextures:
            if (type == LaxJsonTypeArray) {
                return parse_error("expected textures to be an object, not array");
            }
            state = StateTextureName;
            break;
        case StateExpectImagesObject:
            if (type == LaxJsonTypeArray) {
                return parse_error("expected image object, not array");
            }
            state = StateImageName;
            break;
        case StateExpectTextureObject:
            if (type == LaxJsonTypeArray)
                return parse_error("expected texture object, not array");
            state = StateTextureProp;
            break;
        case StateImageObjectBegin:
            if (type == LaxJsonTypeArray) {
                return parse_error("expected image properties object, not array");
            }
            state = StateImagePropName;
            break;
        case StateFileObjectBegin:
            if (type == LaxJsonTypeArray) {
                return parse_error("expected file object, not array");
            }
            state = StateFilePropName;
            break;
        case StateImagePropAnchor:
            if (type == LaxJsonTypeArray) {
                return parse_error("expected object or string, not array");
            }
            state = StateImagePropAnchorObject;
            image.anchor = RuckSackAnchorExplicit;
            break;
        case StateImagePropAnchorX:
        case StateImagePropAnchorY:
            return parse_error("expected number");
        case StateImagePropPath:
            return parse_error("expected string");
        case StateExpectFilesObject:
            if (type == LaxJsonTypeArray)
                return parse_error("expected files object, not array");
            state = StateFileName;
            break;
        default:
            return parse_error("unexpected array or object");
    }

    return 0;
}

static int on_end(struct LaxJsonContext *json, enum LaxJsonType type) {
    if (debug_mode)
        fprintf(stderr, "state: %s, end %s\n", STATE_STR[state], JSON_TYPE_STR[type]);
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
            rucksack_page_add_image(page, image_key, &image);

            free(image.path);
            image.path = NULL;
            free(image_key);
            image_key = NULL;

            state = StateImageName;
            break;
        case StateFilePropName:
            rucksack_bundle_add_file(bundle, file_key, file_path);

            free(file_path);
            file_path = NULL;
            free(file_key);
            file_key = NULL;

            state = StateFileName;
            break;
        case StateImagePropAnchorObject:
            state = StateImagePropName;
            break;
        case StateTextureName:
            state = StateTopLevelProp;
            break;
        case StateImageName:
            state = StateTextureProp;
            break;
        case StateTextureProp:
            rucksack_bundle_add_page(bundle, page_key, page);
            rucksack_page_destroy(page);
            page = NULL;

            free(page_key);
            page_key = NULL;

            state = StateTextureName;
            break;
        default:
            return parse_error("unexpected end of object or array");
    }

    return 0;
}

static void print_rs_error(int err) {
    fprintf(stderr, "Error: %s\n", RS_ERROR_STR[err]);
}

static int usage(char *arg0) {
    fprintf(stderr, "Usage: %s assetsfile bundlefile\n"
            "\n"
            "Options:\n"
            "  [--prefix path]  assets are loaded relative to this path. defaults to cwd\n"
            , arg0);
    return 1;
}

int main(int argc, char *argv[]) {
    char *input_filename = NULL;
    char *bundle_filename = NULL;

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            arg += 2;
            if (i + 1 >= argc) {
                return usage(argv[0]);
            } else if (strcmp(arg, "prefix") == 0) {
                path_prefix = argv[++i];
            } else {
                return usage(argv[0]);
            }
        } else if (!input_filename) {
            input_filename = arg;
        } else if (!bundle_filename) {
            bundle_filename = arg;
        } else {
            return usage(argv[0]);
        }
    }

    if (!input_filename)
        return usage(argv[0]);


    FILE *in_f;
    if (strcmp(input_filename, "-") == 0) {
        in_f = stdin;
    } else {
        in_f = fopen(input_filename, "rb");
        if (!in_f) {
            fprintf(stderr, "Unable to open input file\n");
            return 1;
        }
    }

    rucksack_init();
    atexit(rucksack_finish);

    int rs_err = rucksack_bundle_open(bundle_filename, &bundle);
    if (rs_err) {
        print_rs_error(rs_err);
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
            return 1;
        }
    }
    enum LaxJsonError err = lax_json_eof(json);
    if (err) {
        parse_error(ERR_STR[err]);
        return 1;
    }

    if (state != StateDone)
        parse_error("unexpected EOF");

    if (parse_err_occurred)
        return 1;

    rucksack_bundle_close(bundle);

    return 0;
}
