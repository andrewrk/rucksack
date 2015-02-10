/*
 * Copyright (c) 2013 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#undef NDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "path.h"

char strbuf[2048];

static void test_relative(const char *input1, const char *input2, const char *expected) {
    path_relative(input1, input2, strbuf);
    assert(strcmp(strbuf, expected) == 0);
}

static void test_join(const char *input1, const char *input2, const char *expected) {
    path_join(input1, input2, strbuf);
    assert(strcmp(strbuf, expected) == 0);
}

static void test_dirname(const char *input, const char *expected) {
    path_dirname(input, strbuf);
    assert(strcmp(strbuf, expected) == 0);
}

static void test_normalize(const char *input, const char *expected) {
    path_normalize(input, strbuf);
    assert(strcmp(strbuf, expected) == 0);
}

static void test_path_normalize(void) {
    test_normalize("/a/b/c", "/a/b/c");
    test_normalize("/foo/bar//baz/asdf/quux/..", "/foo/bar/baz/asdf");
    test_normalize("/", "/");
    test_normalize("", ".");
    test_normalize("//", "/");
    test_normalize("//...//..././", "/.../...");
    test_normalize(".", ".");
    test_normalize("./", ".");
    test_normalize("./..", "..");
    test_normalize("./a/..", ".");
    test_normalize("a/..", ".");
    test_normalize("..", "..");
    test_normalize("../", "..");
    test_normalize("a/b../c./", "a/b../c.");
    test_normalize("/a/b/../../../../", "/");
    test_normalize("a/b/../../../../", "../..");

    test_normalize("./fixtures///b/../b/c.js", "fixtures/b/c.js");
    test_normalize("/foo/../../../bar", "/bar");
    test_normalize("a//b//../b", "a/b");
    test_normalize("a//b//./c", "a/b/c");
    test_normalize("a//b//.", "a/b");
}

static void test_path_relative(void) {
    test_relative("/data/orandea/test/aaa", "/data/orandea/impl/bbb", "../../impl/bbb");
    test_relative("a/b", "a/b/c.txt", "c.txt");
    test_relative("", "", "");
    test_relative("", "aoeu", "aoeu");
    test_relative("aoeu", "", "..");
    test_relative("/", "/", "");

    test_relative("/var/lib", "/var", "..");
    test_relative("/var/lib", "/bin", "../../bin");
    test_relative("/var/lib", "/var/lib", "");
    test_relative("/var/lib", "/var/apache", "../apache");
    test_relative("/var/", "/var/lib", "lib");
    test_relative("/", "/var/lib", "var/lib");
}

static void test_path_join(void) {
    test_join("/a/b", "c/d", "/a/b/c/d");
    test_join("/a/b/", "c/d", "/a/b/c/d");
    test_join("/a/b/", "/c/d", "/a/b/c/d");
    test_join("", "foo", "foo");
    test_join("", "", "");
}

static void test_path_dirname(void) {
    test_dirname("/a/b/c", "/a/b");
    test_dirname("/a/b/c/", "/a/b");
    test_dirname("/", "/");
    test_dirname("", "");
    test_dirname("a/b/derp.mp3", "a/b");
}

struct Test {
    const char *name;
    void (*fn)(void);
};

static struct Test tests[] = {
    {"path_normalize", test_path_normalize},
    {"path_relative", test_path_relative},
    {"path_join", test_path_join},
    {"path_dirname", test_path_dirname},
    {NULL, NULL},
};

static void exec_test(struct Test *test) {
    fprintf(stderr, "testing %s...", test->name);
    test->fn();
    fprintf(stderr, "OK\n");
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        int index = atoi(argv[1]);
        exec_test(&tests[index]);
        return 0;
    }

    struct Test *test = &tests[0];

    while (test->name) {
        exec_test(test);
        test += 1;
    }

    return 0;
}
