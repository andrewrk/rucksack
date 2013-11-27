#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <laxjson.h>

#include "rucksack.h"

static char buffer[16384];

static int usage(char *arg0) {
    fprintf(stderr, "Usage: %s assets.json\n"
            "\n"
            "Options:\n"
            "  [--dir outputdir]  defaults to current directory\n"
            "  [--format json]    'json' or 'plain'\n"
            , arg0);
    return 1;
}

static const char *err_to_str(enum LaxJsonError err) {
    switch(err) {
        case LaxJsonErrorNone:
            return "";
        case LaxJsonErrorUnexpectedChar:
            return "unexpected char";
        case LaxJsonErrorExpectedEof:
            return "expected EOF";
        case LaxJsonErrorExceededMaxStack:
            return "exceeded max stack";
        case LaxJsonErrorNoMem:
            return "no mem";
        case LaxJsonErrorExceededMaxValueSize:
            return "exceeded max value size";
        case LaxJsonErrorInvalidHexDigit:
            return "invalid hex digit";
        case LaxJsonErrorInvalidUnicodePoint:
            return "invalid unicode point";
        case LaxJsonErrorExpectedColon:
            return "expected colon";
        case LaxJsonErrorUnexpectedEof:
            return "unexpected EOF";
        default:
            return "unknown error";
    }
}

static void on_string(struct LaxJsonContext *json, enum LaxJsonType type,
        const char *value, int length)
{

}

static void on_number(struct LaxJsonContext *json, double x) {

}

static void on_primitive(struct LaxJsonContext *json, enum LaxJsonType type) {

}

static void on_begin(struct LaxJsonContext *json, enum LaxJsonType type) {

}

static void on_end(struct LaxJsonContext *json, enum LaxJsonType type) {

}

int main(int argc, char *argv[]) {
    char *input_filename = NULL;
    char *out_dir = ".";
    char *out_format = "json";

    for (int i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            arg += 2;
            if (i + 1 >= argc) {
                return usage(argv[0]);
            } else if (strcmp(arg, "dir") == 0) {
                out_dir = argv[++i];
            } else if (strcmp(arg, "format") == 0) {
                out_format = argv[++i];
            } else {
                return usage(argv[0]);
            }
        } else if (!input_filename) {
            input_filename = arg;
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

    struct LaxJsonContext *json = lax_json_create();
    json->string = on_string;
    json->number = on_number;
    json->primitive = on_primitive;
    json->begin = on_begin;
    json->end = on_end;

    size_t amt_read;
    while ((amt_read = fread(buffer, 1, sizeof(buffer), in_f))) {
        enum LaxJsonError err = lax_json_feed(json, amt_read, buffer);
        if (err) {
            fprintf(stderr, "line %d column %d parse error: %s\n",
                    json->line, json->column, err_to_str(err));
            return 1;
        }
    }
    enum LaxJsonError err = lax_json_eof(json);
    if (err) {
        fprintf(stderr, "line %d column %d parse error: %s\n",
                json->line, json->column, err_to_str(err));
        return 1;
    }

    printf("out format: %s\n", out_format);
    printf("out dir: %s\n", out_dir);

    rucksack_init();
    atexit(rucksack_finish);

    struct RuckSackBundle *bundle = rucksack_bundle_open("test.bundle");
    struct RuckSackPage *page = rucksack_page_create();

    struct RuckSackImage img;
    img.path = "img/arrow.png";
    img.anchor = RuckSackAnchorLeft;
    rucksack_page_add_image(page, "arrow", &img);
    img.path = "img/radar-circle.png";
    img.anchor =  RuckSackAnchorCenter;
    rucksack_page_add_image(page, "radarCircle", &img);

    rucksack_bundle_add_page(bundle, "cockpit", page);
    rucksack_page_destroy(page);

    rucksack_bundle_close(bundle);

    return 0;
}
