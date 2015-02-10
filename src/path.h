/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef RUCKSACK_PATH_H_INCLUDED
#define RUCKSACK_PATH_H_INCLUDED

// NOT THREAD SAFE!

void path_normalize(const char *in_path, char *out_path);
void path_join(const char *in_path1, const char *in_path2, char *out_path);

// from_path can be NULL
void path_resolve(const char *from_path, const char *to_path, char *out_path);

void path_relative(const char *from, const char *to, char *out_path);

void path_dirname(const char *in_path, char *out_path);

#endif /* RUCKSACK_PATH_H_INCLUDED */
