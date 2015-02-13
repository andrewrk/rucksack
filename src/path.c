/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "path.h"

enum PathNormalizeState {
    PathNormalizeStateStart,
    PathNormalizeStateIgnoreSlash,
    PathNormalizeStateSlashWouldBeCur,
    PathNormalizeStateNormal,
    PathNormalizeStateSlashWouldBeParent,
};

static char strbuf[4096];
static char strbuf2[4096];
static char strbuf3[4096];
static char strbuf4[4096];
static char strbuf5[4096];
static char strbuf6[4096];

void path_normalize(const char *in_path, char *out_path) {
    char *out_ptr = out_path;
    char *parent_start = out_path;
    enum PathNormalizeState state = PathNormalizeStateStart;
    for (const char *in_ptr = in_path; *in_ptr; in_ptr += 1) {
        switch (state) {
            case PathNormalizeStateStart:
                switch (*in_ptr) {
                    case '/':
                        state = PathNormalizeStateIgnoreSlash;
                        *out_ptr = *in_ptr;
                        out_ptr += 1;
                        break;
                    case '.':
                        state = PathNormalizeStateSlashWouldBeCur;
                        break;
                    default:
                        state = PathNormalizeStateNormal;
                        *out_ptr = *in_ptr;
                        out_ptr += 1;
                }
                break;
            case PathNormalizeStateIgnoreSlash:
                switch (*in_ptr) {
                    case '/':
                        break;
                    case '.':
                        state = PathNormalizeStateSlashWouldBeCur;
                        break;
                    default:
                        state = PathNormalizeStateNormal;
                        *out_ptr = *in_ptr;
                        out_ptr += 1;
                }
                break;
            case PathNormalizeStateSlashWouldBeCur:
                switch (*in_ptr) {
                    case '/':
                        state = PathNormalizeStateIgnoreSlash;
                        break;
                    case '.':
                        state = PathNormalizeStateSlashWouldBeParent;
                        break;
                    default:
                        state = PathNormalizeStateNormal;
                        out_ptr[0] = '.';
                        out_ptr[1] = *in_ptr;
                        out_ptr += 2;
                }
                break;
            case PathNormalizeStateNormal:
                *out_ptr = *in_ptr;
                out_ptr += 1;
                if (*in_ptr == '/')
                    state = PathNormalizeStateIgnoreSlash;
                break;
            case PathNormalizeStateSlashWouldBeParent:
                if (*in_ptr != '/') {
                    state = PathNormalizeStateNormal;
                    out_ptr[0] = '.';
                    out_ptr[1] = '.';
                    out_ptr[2] = *in_ptr;
                    out_ptr += 3;
                    break;
                }
                state = PathNormalizeStateIgnoreSlash;
                if (&out_ptr[-1] == out_path && out_path[0] == '/') {
                    // can't ../ up past root dir
                    break;
                }
                out_ptr -= 2;
                char walked_at_all = 0;
                for (;;) {
                    if (out_ptr < parent_start) {
                        if (!walked_at_all) {
                            // actually we need to *add* more ..'s
                            parent_start[0] = '.';
                            parent_start[1] = '.';
                            parent_start[2] = '/';
                            parent_start += 3;
                        }
                        out_ptr = parent_start;
                        break;
                    }
                    if (*out_ptr == '/') {
                        out_ptr += 1;
                        break;
                    }
                    walked_at_all = 1;
                    out_ptr -= 1;
                }
                break;
        }
    }
    if (state == PathNormalizeStateSlashWouldBeParent) {
        if (&out_ptr[-1] == out_path && out_path[0] == '/') {
            // can't ../ up past root dir
        } else {
            out_ptr -= 2;
            char walked_at_all = 0;
            for (;;) {
                if (out_ptr < parent_start) {
                    if (!walked_at_all) {
                        // actually we need to *add* more ..'s
                        parent_start[0] = '.';
                        parent_start[1] = '.';
                        parent_start[2] = '/';
                        parent_start += 3;
                    }
                    out_ptr = parent_start;
                    break;
                }
                if (*out_ptr == '/') {
                    out_ptr += 1;
                    break;
                }
                walked_at_all = 1;
                out_ptr -= 1;
            }
        }
    }
    if (out_ptr == out_path) {
        out_path[0] = '.';
        out_path[1] = 0;
        return;
    }
    // remove trailing slash
    if (out_ptr[-1] == '/' && &out_ptr[-1] != out_path)
        out_ptr[-1] = 0;
    else
        out_ptr[0] = 0;
}

void path_join(const char *in_path1, const char *in_path2, char *out_path) {
    if (*in_path1) {
        snprintf(strbuf, sizeof(strbuf), "%s/%s", in_path1, in_path2);
        path_normalize(strbuf, out_path);
        return;
    }
    strcpy(out_path, in_path2);
}

void path_resolve(const char *from_path, const char *to_path, char *out_path) {
    if (to_path[0] == '/') {
        path_normalize(to_path, out_path);
        return;
    }
    const char *path2 = to_path;
    if (from_path) {
        path_join(from_path, to_path, strbuf6);
        if (strbuf6[0] == '/') {
            path_normalize(strbuf6, out_path);
            return;
        }
        path2 = strbuf6;
    }
    if (!getcwd(strbuf2, sizeof(strbuf2))) {
        path_normalize(path2, out_path);
        return;
    }
    path_join(strbuf2, path2, strbuf3);
    path_normalize(strbuf3, out_path);
}

void path_relative(const char *from, const char *to, char *out_path) {
    char *from_resolved = strbuf5;
    char *to_resolved = strbuf4;
    path_resolve(NULL, from, from_resolved);
    path_resolve(NULL, to, to_resolved);

    // find the divergent point
    while (*from_resolved == *to_resolved && *from_resolved) {
        from_resolved += 1;
        to_resolved += 1;
    }
    // iterate over what's left of from and add ../'s
    char *out_ptr = out_path;
    char walked_at_all = 0;
    while (*from_resolved) {
        if (*from_resolved == '/' && walked_at_all) {
            out_ptr[0] = '.';
            out_ptr[1] = '.';
            out_ptr[2] = '/';
            out_ptr += 3;
        }
        from_resolved += 1;
        walked_at_all = 1;
    }
    if (walked_at_all) {
        out_ptr[0] = '.';
        out_ptr[1] = '.';
        out_ptr[2] = '/';
        out_ptr += 3;
    }
    // copy what's left of to
    if (*to_resolved == '/')
        to_resolved += 1;
    while (*to_resolved) {
        *out_ptr++ = *to_resolved++;
    }

    if (out_ptr == out_path) {
        *out_path = 0;
        return;
    }

    // remove trailing slash
    if (out_ptr[-1] == '/' && &out_ptr[-1] != out_path)
        out_ptr[-1] = 0;
    else
        out_ptr[0] = 0;
}

void path_dirname(const char *in_path, char *out_path) {
    const char *ptr = in_path;
    const char *last_slash = NULL;
    while (*ptr) {
        const char *next = ptr + 1;
        if (*ptr == '/' && *next)
            last_slash = ptr;
        ptr = next;
    }
    if (!last_slash || last_slash == in_path) {
        if (*in_path == '/') {
            out_path[0] = '/';
            out_path[1] = 0;
        } else {
            out_path[0] = 0;
        }
        return;
    }

    ptr = in_path;
    while (ptr != last_slash) {
        *out_path = *ptr;
        ptr += 1;
        out_path += 1;
    }
    *out_path = 0;
}
