/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <laxjson.h>

#include "rucksack.h"
#include "path.h"

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
    StateExpectGlobArray,
    StateGlobObject,
    StateGlobObjectProp,
    StateGlobValueGlob,
    StateGlobValuePrefix,
    StateGlobValuePath,
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
    "StateExpectGlobArray",
    "StateGlobObject",
    "StateGlobObjectProp",
    "StateGlobValueGlob",
    "StateGlobValuePrefix",
    "StateGlobValuePath",
};

static enum State state;
static char parse_err_occurred = 0;
static char strbuf[2048];
static char strbuf2[2048];
static char strbuf3[2048];
static char strbuf4[2048];

static struct RuckSackPage *page = NULL;
static char *page_key = NULL;

static char *file_key = NULL;
static char *file_path = NULL;

struct RuckSackImage image;
static char *image_key = NULL;

static char *path_prefix = ".";

static char *glob_glob = NULL;
static char *glob_path = NULL;
static char *glob_prefix = NULL;

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

static int parse_error(const char *msg) {
    if (parse_err_occurred)
        return -1;

    fprintf(stderr, "line %d, col %d: %s\n", json->line, json->column, msg);
    parse_err_occurred = 1;
    return -1;
}

static char *resolve_path(const char *path) {
    char *out = malloc(2048);
    path_resolve(path_prefix, path, out);
    return out;
}


static int add_file_if_outdated(struct RuckSackBundle *bundle, char *key, char *path) {
    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, key);
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
    int err = rucksack_bundle_add_file(bundle, key, path);
    if (err) {
        snprintf(strbuf, sizeof(strbuf), "unable to add %s: %s", path, rucksack_err_str(err));
        return parse_error(strbuf);
    }
    return 0;
}

static int glob_insert_files(void) {
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
        err = add_file_if_outdated(bundle, strbuf4, path);
        if (err) return err;
    }

    globfree(&glob_result);

    return 0;
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
            page = rucksack_page_create();
            if (!page)
                return parse_error("out of memory");
            page_key = strdup(value);
            state = StateExpectTextureObject;
            break;
        case StateExpectImagesObject:
            return parse_error("expected images object, not string");
        case StateImageName:
            image.anchor = RuckSackAnchorCenter;
            image.name = NULL;
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
            image.name = resolve_path(value);
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
        case StateGlobObjectProp:
            if (strcmp(value, "glob") == 0) {
                state = StateGlobValueGlob;
            } else if (strcmp(value, "prefix") == 0) {
                state = StateGlobValuePrefix;
            } else if (strcmp(value, "path") == 0) {
                state = StateGlobValuePath;
            } else {
                snprintf(strbuf, sizeof(strbuf), "unknown glob property: %s", value);
                return parse_error(strbuf);
            }
            break;
        case StateGlobValueGlob:
            glob_glob = strdup(value);
            state = StateGlobObjectProp;
            break;
        case StateGlobValuePath:
            glob_path = strdup(value);
            state = StateGlobObjectProp;
            break;
        case StateGlobValuePrefix:
            glob_prefix = strdup(value);
            state = StateGlobObjectProp;
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
        case StateTexturePow2:
            switch (type) {
                case LaxJsonTypeTrue:
                    page->pow2 = 1;
                    break;
                case LaxJsonTypeFalse:
                    page->pow2 = 0;
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
                image.anchor = RuckSackAnchorExplicit;
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
            err = rucksack_page_add_image(page, image_key, &image);
            if (err) {
                snprintf(strbuf, sizeof(strbuf), "unable to add image to page: %s", rucksack_err_str(err));
                return parse_error(strbuf);
            }

            free(image.name);
            image.name = NULL;
            free(image_key);
            image_key = NULL;

            state = StateImageName;
            break;
        case StateFilePropName:
            err = add_file_if_outdated(bundle, file_key, file_path);
            if (err) return err;

            free(file_path);
            file_path = NULL;
            free(file_key);
            file_key = NULL;

            state = StateFileName;
            break;
        case StateImagePropAnchorObject:
            state = StateImagePropName;
            break;
        case StateImageName:
            state = StateTextureProp;
            break;
        case StateTextureProp:
            err = rucksack_bundle_add_page(bundle, page_key, page);
            if (err) {
                snprintf(strbuf, sizeof(strbuf), "unable to add page: %s", rucksack_err_str(err));
                return parse_error(strbuf);
            }

            rucksack_page_destroy(page);
            page = NULL;

            free(page_key);
            page_key = NULL;

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
        case StateTextureName:
        case StateGlobObject:
        case StateFileName:
            state = StateTopLevelProp;
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
        fprintf(stderr, "unable to open bundle %s\n", rucksack_err_str(rs_err));
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

    rs_err = rucksack_bundle_close(bundle);
    if (rs_err) {
        fprintf(stderr, "unable to close bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
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

    int rs_err = rucksack_bundle_open(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open bundle: %s\n", rucksack_err_str(rs_err));
        return 1;
    }

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, resource_name);
    if (!entry) {
        fprintf(stderr, "entry not found\n");
        return 1;
    }

    if (is_texture) {
        struct RuckSackTexture *texture;
        rs_err = rucksack_file_open_texture(entry, &texture);

        if (rs_err) {
            fprintf(stderr, "unable to open texture entry: %s\n", rucksack_err_str(rs_err));
            return 1;
        }

        long size = rucksack_texture_size(texture);

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

        rucksack_texture_close(texture);
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

    int rs_err = rucksack_bundle_open(bundle_filename, &bundle);
    if (rs_err) {
        fprintf(stderr, "unable to open bundle: %s\n", rucksack_err_str(rs_err));
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

int main(int argc, char *argv[]) {
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
