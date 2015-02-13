/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */
#ifndef RUCKSACK_MKDIRP_H_INCLUDED
#define RUCKSACK_MKDIRP_H_INCLUDED


#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static int make_dir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static int rucksack_mkdirp(const char *path) {
    struct stat st;
    int err = stat(path, &st);
    if (!err && S_ISDIR(st.st_mode))
        return 0;

    err = make_dir(path);
    if (!err)
        return 0;
    if (errno != ENOENT)
        return errno;

    char buf[4096];
    path_dirname(path, buf);
    err = rucksack_mkdirp(buf);
    if (err)
        return err;

    return rucksack_mkdirp(path);
}


#endif
