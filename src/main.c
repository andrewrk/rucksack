#include <FreeImage.h>
#include <stdio.h>
#include <string.h>

static int usage(char *arg0) {
    fprintf(stderr, "Usage: %s assets.json\n"
            "\n"
            "Options:\n"
            "  [--dir outputdir]  defaults to current directory\n"
            "  [--format json]    'json' or 'plain'\n"
            , arg0);
    return 1;
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

    FreeImage_Initialise(0);

    FreeImage_DeInitialise();
    return 0;
}
