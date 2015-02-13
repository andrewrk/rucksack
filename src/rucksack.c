/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "config.h"
#include "rucksack.h"
#include "shared.h"
#include "util.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>


#define MIN(x, y) ((x) < (y) ? (x) : (y))

static const char *BUNDLE_UUID = "\x60\x70\xc8\x99\x82\xa1\x41\x84\x89\x51\x08\xc9\x1c\xc9\xb6\x20";

static const int BUNDLE_VERSION = 1;
static const int MAIN_HEADER_LEN = 28;
static const int HEADER_ENTRY_LEN = 36; // not taking into account key bytes

static const char *ERROR_STR[] = {
    "",
    "out of memory",
    "problem accessing file",
    "invalid bundle format",
    "bundle version mismatch",
    "bundle is an empty file",
    "invalid anchor enum value",
    "cannot fit all images into texture",
    "image has no pixels",
    "unrecognized image format",
    "key not found",
    "cannot delete while stream open",
};

struct RuckSackBundlePrivate {
    struct RuckSackBundle externals;

    FILE *f;

    long int first_header_offset;
    struct RuckSackFileEntry *entries;
    long int header_entry_count; // actual count of entries
    long int header_entry_mem_count; // allocated memory entry count

    // keep some stuff cached for quick access
    struct RuckSackFileEntry *first_entry;
    struct RuckSackFileEntry *last_entry;
    long int headers_byte_count;
    long int first_file_offset;

    char read_only;
};

static int memneql(const char *mem1, int mem1_size, const char *mem2, int mem2_size) {
    if (mem1_size != mem2_size)
        return 1;
    else
        return memcmp(mem1, mem2, mem1_size);
}

static long alloc_size(long actual_size) {
    return 2 * actual_size + 8192;
}

static long int alloc_size_precise(char precise, long actual_size) {
    return precise ? actual_size : alloc_size(actual_size);
}

static long int alloc_count(long int actual_count) {
    return 2 * actual_count + 64;
}

static void write_uint64be(unsigned char *buf, uint64_t x) {
    buf[7] = x & 0xff;

    x >>= 8;
    buf[6] = x & 0xff;

    x >>= 8;
    buf[5] = x & 0xff;

    x >>= 8;
    buf[4] = x & 0xff;

    x >>= 8;
    buf[3] = x & 0xff;

    x >>= 8;
    buf[2] = x & 0xff;

    x >>= 8;
    buf[1] = x & 0xff;

    x >>= 8;
    buf[0] = x & 0xff;
}

static uint32_t read_uint32be(const unsigned char *buf) {
    uint32_t result = buf[0];

    result <<= 8;
    result |= buf[1];

    result <<= 8;
    result |= buf[2];

    result <<= 8;
    result |= buf[3];

    return result;
}

static uint64_t read_uint64be(const unsigned char *buf) {
    uint64_t result = buf[0];

    result <<= 8;
    result |= buf[1];

    result <<= 8;
    result |= buf[2];

    result <<= 8;
    result |= buf[3];

    result <<= 8;
    result |= buf[4];

    result <<= 8;
    result |= buf[5];

    result <<= 8;
    result |= buf[6];

    result <<= 8;
    result |= buf[7];

    return result;
}

static float read_float32be(const unsigned char *buf) {
    return read_uint32be(buf) / FIXED_POINT_N;
}

static int read_header(struct RuckSackBundlePrivate *b) {
    // read all the header entries
    FILE *f = b->f;
    if (fseek(f, 0, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[MAX(HEADER_ENTRY_LEN, MAIN_HEADER_LEN)];
    long int amt_read = fread(buf, 1, MAIN_HEADER_LEN, f);

    if (amt_read == 0)
        return RuckSackErrorEmptyFile;

    if (amt_read != MAIN_HEADER_LEN)
        return RuckSackErrorInvalidFormat;

    if (memcmp(BUNDLE_UUID, buf, UUID_SIZE) != 0)
        return RuckSackErrorInvalidFormat;

    int bundle_version = read_uint32be(&buf[16]);
    if (bundle_version != BUNDLE_VERSION)
        return RuckSackErrorWrongVersion;

    b->first_header_offset = read_uint32be(&buf[20]);
    b->header_entry_count = read_uint32be(&buf[24]);
    b->header_entry_mem_count = alloc_count(b->header_entry_count);
    b->entries = calloc(b->header_entry_mem_count, sizeof(struct RuckSackFileEntry));

    if (!b->entries)
        return RuckSackErrorNoMem;

    // calculate how many bytes are used by all the headers
    b->headers_byte_count = 0; 

    long int header_offset = b->first_header_offset;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        if (fseek(f, header_offset, SEEK_SET))
            return RuckSackErrorFileAccess;
        amt_read = fread(buf, 1, HEADER_ENTRY_LEN, f);
        if (amt_read != HEADER_ENTRY_LEN)
            return RuckSackErrorInvalidFormat;
        long int entry_size = read_uint32be(&buf[0]);
        header_offset += entry_size;
        struct RuckSackFileEntry *entry = &b->entries[i];
        entry->offset = read_uint64be(&buf[4]);
        entry->size = read_uint64be(&buf[12]);
        entry->allocated_size = read_uint64be(&buf[20]);
        entry->mtime = read_uint32be(&buf[28]);
        entry->key_size = read_uint32be(&buf[32]);
        entry->key = malloc(entry->key_size + 1);
        if (!entry->key)
            return RuckSackErrorNoMem;
        amt_read = fread(entry->key, 1, entry->key_size, f);
        if (amt_read != entry->key_size)
            return RuckSackErrorInvalidFormat;
        entry->key[entry->key_size] = 0;
        entry->b = b;

        b->headers_byte_count += HEADER_ENTRY_LEN + entry->key_size;

        if (!b->last_entry || entry->offset > b->last_entry->offset)
            b->last_entry = entry;

        if (!b->first_entry || entry->offset < b->first_entry->offset) {
            b->first_entry = entry;
            b->first_file_offset = entry->offset;
        }
    }

    return RuckSackErrorNone;
}

static struct RuckSackFileEntry *get_prev_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry)
{
    struct RuckSackFileEntry *prev = NULL;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        if (e->offset < entry->offset && (!prev || e->offset > prev->offset))
            prev = e;
    }
    return prev;
}

static struct RuckSackFileEntry *get_next_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry)
{
    struct RuckSackFileEntry *next = NULL;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        if (e->offset > entry->offset && (!next || e->offset < next->offset))
            next = e;
    }
    return next;
}

static int copy_data(struct RuckSackBundlePrivate *b, long int source,
        long int dest, long int size)
{
    if (source == dest)
        return RuckSackErrorNone;

    const long int max_buf_size = 1048576;
    long int buf_size = MIN(max_buf_size, size);
    char *buffer = malloc(buf_size);

    if (!buffer)
        return RuckSackErrorNoMem;

    while (size > 0) {
        long int amt_to_read = MIN(buf_size, size);
        if (fseek(b->f, source, SEEK_SET)) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fread(buffer, 1, amt_to_read, b->f) != amt_to_read) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fseek(b->f, dest, SEEK_SET)) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        if (fwrite(buffer, 1, amt_to_read, b->f) != amt_to_read) {
            free(buffer);
            return RuckSackErrorFileAccess;
        }
        size -= amt_to_read;
        source += amt_to_read;
        dest += amt_to_read;
    }

    free(buffer);
    return RuckSackErrorNone;
}

static void allocate_file(struct RuckSackBundlePrivate *b, long int size,
        struct RuckSackFileEntry *entry, char precise)
{
    entry->allocated_size = size;

    long int wanted_headers_alloc_bytes = alloc_size_precise(precise, b->headers_byte_count);
    long int wanted_headers_alloc_end = precise ? b->first_file_offset :
        (b->first_header_offset + wanted_headers_alloc_bytes);

    // can we put it between the header and the first entry?
    if (b->first_entry) {
        long int extra = b->first_entry->offset - wanted_headers_alloc_end;
        if (extra >= entry->allocated_size) {
            // we can fit it here
            entry->offset = b->first_entry->offset - entry->allocated_size;
            b->first_entry = entry;
            b->first_file_offset = entry->offset;
            return;
        }
    }

    // figure out offset and allocated_size
    // find a file that has too much allocated room and stick it there
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];

        // don't overwrite a stream!
        if (e->is_open || e == entry) continue;

        // don't put it somewhere that is likely to 
        if (e->offset < wanted_headers_alloc_end) continue;

        long int needed_alloc_size = alloc_size_precise(precise, e->size);
        long int extra = e->allocated_size - needed_alloc_size;

        // not enough room.
        if (extra < entry->allocated_size) continue;

        long int new_offset = e->offset + needed_alloc_size;

        // don't put it too close to the headers
        if (new_offset < wanted_headers_alloc_end) continue;

        // we can fit it here!
        entry->offset = new_offset;
        entry->allocated_size = extra;
        e->allocated_size = needed_alloc_size;

        if (e == b->last_entry)
            b->last_entry = entry;

        return;
    }

    // ok stick it at the end
    if (b->last_entry) {
        if (!b->last_entry->is_open)
            b->last_entry->allocated_size = alloc_size_precise(precise, b->last_entry->size);
        entry->offset = MAX(b->last_entry->offset + b->last_entry->allocated_size,
                wanted_headers_alloc_end);
        b->last_entry = entry;
    } else {
        // this is the first entry in the bundle
        long this_entry_header_len = HEADER_ENTRY_LEN + entry->key_size;
        long min_offset = b->first_header_offset +
            (precise ? this_entry_header_len : alloc_size(this_entry_header_len * 10));
        b->first_file_offset = (b->first_file_offset < min_offset) ?
            min_offset : b->first_file_offset;
        entry->offset = b->first_file_offset;
        b->first_entry = entry;
        b->last_entry = entry;
    }
}


static int resize_file_entry(struct RuckSackBundlePrivate *b,
        struct RuckSackFileEntry *entry, long int size, char precise)
{
    if (entry == b->last_entry) {
        // well that was easy
        entry->allocated_size = size;
        return RuckSackErrorNone;
    } else if (entry == b->first_entry) {
        b->first_entry = get_next_entry(b, entry);
        b->first_file_offset = b->first_entry->offset;
    } else {
        struct RuckSackFileEntry *prev = get_prev_entry(b, entry);
        prev->allocated_size += entry->allocated_size;
    }

    // pick a new place for the entry
    long int old_offset = entry->offset;
    allocate_file(b, size, entry, precise);

    // copy the old data to the new location
    return copy_data(b, old_offset, entry->offset, entry->size);
}

static int write_header(struct RuckSackBundlePrivate *b) {
    FILE *f = b->f;
    if (fseek(f, 0, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[MAX(MAIN_HEADER_LEN, HEADER_ENTRY_LEN)];
    memcpy(buf, BUNDLE_UUID, UUID_SIZE);
    write_uint32be(&buf[16], BUNDLE_VERSION);
    write_uint32be(&buf[20], b->first_header_offset);
    write_uint32be(&buf[24], b->header_entry_count);
    long int amt_written = fwrite(buf, 1, MAIN_HEADER_LEN, f);
    if (amt_written != MAIN_HEADER_LEN)
        return RuckSackErrorFileAccess;

    long int allocated_header_bytes = b->first_file_offset - b->first_header_offset;
    if (b->headers_byte_count > allocated_header_bytes) {
        long int wanted_entry_bytes = alloc_size(b->headers_byte_count);
        long int wanted_offset_end = b->first_header_offset + wanted_entry_bytes;
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackFileEntry *entry = &b->entries[i];
            if (entry->offset < wanted_offset_end) {
                int err = resize_file_entry(b, entry, alloc_size(entry->size), 0);
                if (err)
                    return err;
            }
        }
    }

    if (fseek(f, b->first_header_offset, SEEK_SET))
        return RuckSackErrorFileAccess;

    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *entry = &b->entries[i];
        write_uint32be(&buf[0], HEADER_ENTRY_LEN + entry->key_size);
        write_uint64be(&buf[4], entry->offset);
        write_uint64be(&buf[12], entry->size);
        write_uint64be(&buf[20], entry->allocated_size);
        write_uint32be(&buf[28], entry->mtime);
        write_uint32be(&buf[32], entry->key_size);
        amt_written = fwrite(buf, 1, HEADER_ENTRY_LEN, f);
        if (amt_written != HEADER_ENTRY_LEN)
            return RuckSackErrorFileAccess;
        amt_written = fwrite(entry->key, 1, entry->key_size, f);
        if (amt_written != entry->key_size)
            return RuckSackErrorFileAccess;
    }

    return RuckSackErrorNone;
}

static void init_new_bundle(struct RuckSackBundlePrivate *b, long headers_size) {
    b->first_header_offset = MAIN_HEADER_LEN;
    long allocated_header_bytes = (headers_size == -1) ?
        alloc_size(HEADER_ENTRY_LEN * 10) : headers_size;
    b->first_file_offset = b->first_header_offset + allocated_header_bytes;
}

static int open_bundle(const char *bundle_path, struct RuckSackBundle **out_bundle,
        char read_only, long headers_size)
{
    struct RuckSackBundlePrivate *b = calloc(1, sizeof(struct RuckSackBundlePrivate));
    if (!b) {
        *out_bundle = NULL;
        return RuckSackErrorNoMem;
    }

    init_new_bundle(b, headers_size);
    b->read_only = read_only;

    const char *open_flags = read_only ? "rb" : "rb+";
    int open_for_writing = 0;
    b->f = fopen(bundle_path, open_flags);
    if (b->f) {
        int err = read_header(b);
        if (err == RuckSackErrorEmptyFile) {
            open_for_writing = 1;
        } else if (err) {
            free(b);
            *out_bundle = NULL;
            return err;
        }
    } else if (read_only) {
            free(b);
            *out_bundle = NULL;
            return RuckSackErrorFileAccess;
    } else {
        open_for_writing = 1;
    }
    if (open_for_writing) {
        if (read_only) {
            free(b);
            *out_bundle = NULL;
            return RuckSackErrorEmptyFile;
        }
        b->f = fopen(bundle_path, "wb+");
        if (!b->f) {
            free(b);
            *out_bundle = NULL;
            return RuckSackErrorFileAccess;
        }
    }

    *out_bundle = &b->externals;
    return RuckSackErrorNone;
}

int rucksack_bundle_open_read(const char *bundle_path, struct RuckSackBundle **out_bundle) {
    return open_bundle(bundle_path, out_bundle, 1, -1);
}

int rucksack_bundle_open(const char *bundle_path, struct RuckSackBundle **out_bundle) {
    return open_bundle(bundle_path, out_bundle, 0, -1);
}

int rucksack_bundle_open_precise(const char *bundle_path, struct RuckSackBundle **out_bundle,
        long headers_size)
{
    return open_bundle(bundle_path, out_bundle, 0, headers_size);
}

int rucksack_bundle_close(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;

    int write_err = RuckSackErrorNone;
    if (!b->read_only)
        write_err = write_header(b);

    if (b->entries) {
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackFileEntry *entry = &b->entries[i];
            if (entry->key)
                free(entry->key);
        }
        free(b->entries);
    }

    int close_err = fclose(b->f);
    free(b);

    if (write_err)
        return write_err;

    if (close_err)
        return RuckSackErrorFileAccess;

    return RuckSackErrorNone;
}

int rucksack_bundle_add_file(struct RuckSackBundle *bundle, const char *key,
        int key_size, const char *file_name)
{
    FILE *f = fopen(file_name, "rb");

    if (!f)
        return RuckSackErrorFileAccess;

    struct stat st;
    int err = fstat(fileno(f), &st);

    if (err != 0) {
        fclose(f);
        return RuckSackErrorFileAccess;
    }

    off_t size = st.st_size;

    struct RuckSackOutStream *stream;
    err = rucksack_bundle_add_stream(bundle, key, key_size, size, &stream);
    if (err) {
        fclose(f);
        return err;
    }

    const int buf_size = 16384;
    char *buffer = malloc(buf_size);

    if (!buffer) {
        fclose(f);
        rucksack_stream_close(stream);
        return RuckSackErrorNoMem;
    }

    long int amt_read;
    while ((amt_read = fread(buffer, 1, buf_size, f))) {
        int err = rucksack_stream_write(stream, buffer, amt_read);
        if (err) {
            fclose(f);
            free(buffer);
            rucksack_stream_close(stream);
            return err;
        }
    }

    free(buffer);
    rucksack_stream_close(stream);

    if (fclose(f))
        return RuckSackErrorFileAccess;

    return RuckSackErrorNone;
}

static int allocate_file_entry(struct RuckSackBundlePrivate *b, const char *key, int key_size,
        long int size, struct RuckSackFileEntry **out_entry, char precise)
{
    char *key_dupe = dupe_string(key, &key_size);
    if (!key_dupe) {
        *out_entry = NULL;
        return RuckSackErrorNoMem;
    }

    // create a new entry
    if (b->header_entry_count >= b->header_entry_mem_count) {
        b->header_entry_mem_count = alloc_count(b->header_entry_mem_count);
        struct RuckSackFileEntry *new_ptr = realloc(b->entries,
                b->header_entry_mem_count * sizeof(struct RuckSackFileEntry));
        if (!new_ptr) {
            *out_entry = NULL;
            return RuckSackErrorNoMem;
        }
        long int clear_amt = b->header_entry_mem_count - b->header_entry_count;
        long int clear_size = clear_amt * sizeof(struct RuckSackFileEntry);
        memset(new_ptr + b->header_entry_count, 0, clear_size);
        b->entries = new_ptr;
    }
    struct RuckSackFileEntry *entry = &b->entries[b->header_entry_count];
    b->header_entry_count += 1;
    entry->key = key_dupe;
    entry->key_size = key_size;
    entry->b = b;
    b->headers_byte_count += HEADER_ENTRY_LEN + entry->key_size;

    allocate_file(b, size, entry, precise);

    *out_entry = entry;
    return RuckSackErrorNone;
}

static struct RuckSackFileEntry *find_file_entry(struct RuckSackBundlePrivate *b,
        const char *key, int key_size)
{
    for (int i = 0; i < b->header_entry_count; i += 1) {
        struct RuckSackFileEntry *e = &b->entries[i];
        if (memneql(key, key_size, e->key, e->key_size) == 0)
            return e;
    }
    return NULL;
}

static int get_file_entry(struct RuckSackBundlePrivate *b, const char *key,
        int key_size, long int size, struct RuckSackFileEntry **out_entry, char precise)
{
    // return info for existing entry
    struct RuckSackFileEntry *e = find_file_entry(b, key, key_size);
    if (e) {
        if (e->allocated_size < size) {
            int err = resize_file_entry(b, e, size, precise);
            if (err) {
                *out_entry = NULL;
                return err;
            }
        }
        *out_entry = e;
        return RuckSackErrorNone;
    }

    // none found, allocate new entry
    return allocate_file_entry(b, key, key_size, size, out_entry, precise);
}

static int add_stream(struct RuckSackBundle *bundle, const char *key,
        int key_size, long size_guess, struct RuckSackOutStream **out_stream,
        char precise, long mtime)
{
    struct RuckSackOutStream *stream = calloc(1, sizeof(struct RuckSackOutStream));

    if (!stream) {
        *out_stream = NULL;
        return RuckSackErrorNoMem;
    }
    key_size = (key_size == -1) ? strlen(key) : key_size;

    stream->b = (struct RuckSackBundlePrivate *) bundle;
    long stream_size = alloc_size_precise(precise, size_guess);
    int err = get_file_entry(stream->b, key, key_size, stream_size, &stream->e, precise);
    if (err) {
        free(stream);
        *out_stream = NULL;
        return err;
    }
    stream->e->is_open = 1;
    stream->e->size = 0;
    stream->e->mtime = mtime;
    stream->e->touched = 1;

    *out_stream = stream;
    return RuckSackErrorNone;
}

int rucksack_bundle_add_stream_precise(struct RuckSackBundle *bundle,
        const char *key, int key_size, long size, struct RuckSackOutStream **out_stream,
        long mtime)
{
    return add_stream(bundle, key, key_size, size, out_stream, 1, mtime);
}

int rucksack_bundle_add_stream(struct RuckSackBundle *bundle,
        const char *key, int key_size, long size_guess, struct RuckSackOutStream **out_stream)
{
    return add_stream(bundle, key, key_size, size_guess, out_stream, 0, time(0));
}

void rucksack_stream_close(struct RuckSackOutStream *stream) {
    stream->e->is_open = 0;
    free(stream);
}

int rucksack_stream_write(struct RuckSackOutStream *stream, const void *ptr,
        long int count)
{
    long int pos = stream->e->size;
    long int end = pos + count;
    if (end > stream->e->allocated_size) {
        // It didn't fit. Move this stream to a new one with extra padding
        long int new_size = alloc_size(end);
        int err = resize_file_entry(stream->b, stream->e, new_size, 0);
        if (err)
            return err;
    }

    FILE *f = stream->b->f;

    if (fseek(f, stream->e->offset + pos, SEEK_SET))
        return RuckSackErrorFileAccess;

    if (fwrite(ptr, 1, count, stream->b->f) != count)
        return RuckSackErrorFileAccess;

    stream->e->size = pos + count;

    return RuckSackErrorNone;
}

struct RuckSackFileEntry *rucksack_bundle_find_file(struct RuckSackBundle *bundle,
        const char *key, int key_size)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    key_size = (key_size == -1) ? strlen(key) : key_size;
    return find_file_entry(b, key, key_size);
}

long int rucksack_file_size(struct RuckSackFileEntry *entry) {
    return entry->size;
}

const char *rucksack_file_name(struct RuckSackFileEntry *entry) {
    return entry->key;
}

int rucksack_file_name_size(struct RuckSackFileEntry *entry) {
    return entry->key_size;
}

int rucksack_file_read(struct RuckSackFileEntry *e, unsigned char *buffer)
{
    struct RuckSackBundlePrivate *b = e->b;
    if (fseek(b->f, e->offset, SEEK_SET))
        return RuckSackErrorFileAccess;
    long int amt_read = fread(buffer, 1, e->size, b->f);
    if (amt_read != e->size)
        return RuckSackErrorFileAccess;
    return RuckSackErrorNone;
}

void rucksack_version(int *major, int *minor, int *patch) {
    if (major) *major = RUCKSACK_VERSION_MAJOR;
    if (minor) *minor = RUCKSACK_VERSION_MINOR;
    if (patch) *patch = RUCKSACK_VERSION_PATCH;
}

long int rucksack_bundle_file_count(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    return b->header_entry_count;
}

void rucksack_bundle_get_files(struct RuckSackBundle *bundle,
        struct RuckSackFileEntry **entries)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    for (int i = 0; i < b->header_entry_count; i += 1) {
        entries[i] = &b->entries[i];
    }
}

const char *rucksack_err_str(int err) {
    return ERROR_STR[err];
}

int rucksack_file_open_texture(struct RuckSackFileEntry *entry,
        struct RuckSackTexture **out_texture)
{
    *out_texture = NULL;

    struct RuckSackTexturePrivate *t = calloc(1, sizeof(struct RuckSackTexturePrivate));
    struct RuckSackTexture *texture = &t->externals;
    if (!t)
        return RuckSackErrorNoMem;
    t->entry = entry;

    struct RuckSackBundlePrivate *b = entry->b;
    if (fseek(b->f, entry->offset, SEEK_SET)) {
        rucksack_texture_close(texture);
        return RuckSackErrorFileAccess;
    }

    unsigned char buf[MAX(TEXTURE_HEADER_LEN, IMAGE_HEADER_LEN)];
    long amt_read = fread(buf, 1, TEXTURE_HEADER_LEN, b->f);
    if (amt_read != TEXTURE_HEADER_LEN) {
        rucksack_texture_close(texture);
        return RuckSackErrorFileAccess;
    }

    if (memcmp(TEXTURE_UUID, buf, UUID_SIZE) != 0)
        return RuckSackErrorInvalidFormat;

    t->pixel_data_offset = read_uint32be(&buf[16]);
    t->pixel_data_size = entry->size - t->pixel_data_offset;
    t->images_count = read_uint32be(&buf[20]);
    long offset_to_first_img = read_uint32be(&buf[24]);

    texture->max_width = read_uint32be(&buf[28]);
    texture->max_height = read_uint32be(&buf[32]);
    texture->pow2 = buf[36];
    texture->allow_r90 = buf[37];

    t->images = calloc(t->images_count, sizeof(struct RuckSackImagePrivate));

    if (!t->images) {
        rucksack_texture_close(texture);
        return RuckSackErrorNoMem;
    }

    long next_offset = entry->offset + offset_to_first_img;
    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;

        if (fseek(b->f, next_offset, SEEK_SET)) {
            rucksack_texture_close(texture);
            return RuckSackErrorFileAccess;
        }

        long amt_read = fread(buf, 1, IMAGE_HEADER_LEN, b->f);
        if (amt_read != IMAGE_HEADER_LEN) {
            rucksack_texture_close(texture);
            return RuckSackErrorFileAccess;
        }

        long this_size = read_uint32be(&buf[0]);
        next_offset += this_size;

        image->anchor = read_uint32be(&buf[4]);
        image->anchor_x = read_float32be(&buf[8]);
        image->anchor_y = read_float32be(&buf[12]);
        image->x = read_uint32be(&buf[16]);
        image->y = read_uint32be(&buf[20]);
        image->width = read_uint32be(&buf[24]);
        image->height = read_uint32be(&buf[28]);
        image->r90 = buf[32];

        image->key_size = read_uint32be(&buf[33]);
        image->key = malloc(image->key_size + 1);
        if (!image->key) {
            rucksack_texture_close(texture);
            return RuckSackErrorNoMem;
        }
        amt_read = fread(image->key, 1, image->key_size, b->f);
        if (amt_read != image->key_size) {
            rucksack_texture_close(texture);
            return RuckSackErrorFileAccess;
        }
        image->key[image->key_size] = 0;
    }

    texture->key = entry->key;
    texture->key_size = entry->key_size;

    *out_texture = texture;
    return RuckSackErrorNone;
}

long rucksack_texture_size(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    return t->pixel_data_size;
}

int rucksack_texture_read(struct RuckSackTexture *texture, unsigned char *buffer) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    struct RuckSackFileEntry *entry = t->entry;
    FILE *f = entry->b->f;
    if (fseek(f, entry->offset + t->pixel_data_offset, SEEK_SET))
        return RuckSackErrorFileAccess;
    long int amt_read = fread(buffer, 1, t->pixel_data_size, f);
    if (amt_read != t->pixel_data_size)
        return RuckSackErrorFileAccess;
    return RuckSackErrorNone;
}

long rucksack_texture_image_count(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    return t->images_count;
}

void rucksack_texture_get_images(struct RuckSackTexture *texture,
        struct RuckSackImage **images)
{
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;
        images[i] = image;
    }
}

long rucksack_file_mtime(struct RuckSackFileEntry *entry) {
    return entry->mtime;
}

int rucksack_bundle_version(void) {
    return BUNDLE_VERSION;
}

int rucksack_file_is_texture(struct RuckSackFileEntry *e, int *is_texture) {
    struct RuckSackBundlePrivate *b = e->b;
    if (e->size < UUID_SIZE) {
        *is_texture = 0;
        return RuckSackErrorNone;
    }
    if (fseek(b->f, e->offset, SEEK_SET))
        return RuckSackErrorFileAccess;

    unsigned char buf[UUID_SIZE];
    long int amt_read = fread(buf, 1, UUID_SIZE, b->f);
    if (amt_read != UUID_SIZE)
        return RuckSackErrorFileAccess;

    *is_texture = memcmp(TEXTURE_UUID, buf, UUID_SIZE) == 0;

    return RuckSackErrorNone;
}

long rucksack_bundle_get_headers_byte_count(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *) bundle;
    return b->headers_byte_count;
}

static void delete_entry(struct RuckSackBundlePrivate *b, struct RuckSackFileEntry *e) {
    long allocated_size = e->allocated_size;
    b->headers_byte_count -= HEADER_ENTRY_LEN + e->key_size;
    b->header_entry_count -= 1;
    struct RuckSackFileEntry *prev = get_prev_entry(b, e);
    struct RuckSackFileEntry *next = get_next_entry(b, e);
    struct RuckSackFileEntry *next_last = get_prev_entry(b, b->last_entry);
    if (e->key)
        free(e->key);
    *e = *b->last_entry;
    b->last_entry = next_last;
    if (prev) {
        prev->allocated_size += allocated_size;
    } else if (next) {
        b->first_entry = next;
        b->first_file_offset = b->first_entry->offset;
    } else {
        init_new_bundle(b, -1);
    }
}

int rucksack_bundle_delete_file(struct RuckSackBundle *bundle,
        const char *key, int key_size)
{
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;
    if (key_size == -1)
        key_size = strlen(key);
    struct RuckSackFileEntry *e = find_file_entry(b, key, key_size);
    if (!e)
        return RuckSackErrorNotFound;
    if (e->is_open)
        return RuckSackErrorStreamOpen;

    delete_entry(b, e);
    return RuckSackErrorNone;
}

void rucksack_bundle_delete_untouched(struct RuckSackBundle *bundle) {
    struct RuckSackBundlePrivate *b = (struct RuckSackBundlePrivate *)bundle;
    for (;;) {
        int deleted_something = 0;
        for (int i = 0; i < b->header_entry_count; i += 1) {
            struct RuckSackFileEntry *e = &b->entries[i];
            if (!e->touched) {
                delete_entry(b, e);
                deleted_something = 1;
                break;
            }
        }
        if (!deleted_something)
            return;
    }
}

void rucksack_file_touch(struct RuckSackFileEntry *entry) {
    entry->touched = 1;
}

void rucksack_texture_touch(struct RuckSackTexture *texture) {
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;
    t->entry->touched = 1;
}

void rucksack_texture_close(struct RuckSackTexture *texture) {
    if (!texture)
        return;
    struct RuckSackTexturePrivate *t = (struct RuckSackTexturePrivate *) texture;

    for (int i = 0; i < t->images_count; i += 1) {
        struct RuckSackImagePrivate *img = &t->images[i];
        struct RuckSackImage *image = &img->externals;
        free(image->key);
    }
    free(t->images);
    free(t->free_positions);
    free(t);
}

