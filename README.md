# rucksack

Texture packer and resource bundler. Use the rucksack executable to build your
resources file, and then depend on librucksack in-game to get the resources out
at runtime. (or roll your own resource-loading code using the very simple file
format, detailed at the end of this readme.)

## Command Line Usage

```
rucksack v1.0.0

Usage: rucksack [command] [command-options]

Commands:
  help       get info on how to use a command
  bundle     parses an assets json file and keeps a bundle up to date
  cat        extracts a single file from the bundle and writes it to stdout
  ls         lists all resources in a bundle
```

## Library Usage

```C
#include <rucksack.h>

int main() {
    struct RuckSackBundle *bundle;
    rucksack_bundle_open(bundle_name, &bundle);

    struct RuckSackFileEntry *entry = rucksack_bundle_find_file(bundle, "blah");
    size_t size = rucksack_file_size(entry);
    unsigned char *buffer = malloc(size);
    rucksack_bundle_file_read(bundle, entry, buf);
    // buffer now contains the contents of the file indexed by "blah"
    free(buffer);
    rucksack_bundle_close(bundle);
}
```

## Dependencies

 * [FreeImage](http://freeimage.sourceforge.net/)
 * [liblaxjson](https://github.com/superjoe30/liblaxjson)

## Installation

### Pre-Built Packages

 * [Ubuntu PPA](https://launchpad.net/~andrewrk/+archive/rucksack)

   ```
   sudo apt-add-repository ppa:andrewrk/rucksack
   sudo apt-get update
   sudo apt-get install rucksack
   ```

### From Source

1. `mkdir build && cd build && cmake ..`
2. Verify that all dependencies say "OK".
3. `make && sudo make install`

## JSON Reference

### Example

```js
{
  // one-off files you want to directly save into the bundle
  files: {
    file1Name: {
      path: "path/to/file",
    },
  },
  // if you want to avoid manually specifying every file, you can glob
  globFiles: [
    {
      path: "path/to/dir",
      glob: "*",
      prefix: "abc_", // prepended to the key
    },
  ],
  // spritesheet generation
  textures: {
    texture1Name: {
      maxWidth: 1024,
      maxHeight: 1024,
      pow2: true,
      images: {
        image1Name: {
          // can be any image format supported by FreeImage
          path: "path/to/image.png",

          // can be any of these strings:
          // "top", "left", "bottom", "right"
          // "topleft", "topright", "bottomleft", "bottomright"
          // "center"
          // or it can be an object like this: {x: 13, y: 19}
          anchor: "center",
        },
      },
    },
  },
}
```

## RuckSack Bundle File Format

The main header identifies the file and tells you some metadata about the
rest of the header entries.

The basic data supported by rucksack is a file. In this case the offset that
a header entry points to is the raw file data. The other format supported
is a texture, also known as a spritesheet. This is described by Texture Format
below.

    Offset | Contents
    -------+---------
         0 | 16 byte UUID - 60 70 c8 99 82 a1 41 84 89 51 08 c9 1c c9 b6 20
        16 | uint32be offset of first header entry from file start
        20 | uint32be number of header entries

### Header Entry Format

    Offset | Contents
    -------+---------
         0 | uint32be size of this header entry in bytes
         4 | uint64be offset of this entry's file contents
        12 | uint64be actual size of the file contents in bytes
        20 | uint64be number of allocated bytes for this file
        28 | uint32be file mtime
        32 | uint32be key size in bytes
        36 | key bytes


### Texture Format

    Offset | Contents
    -------+---------
         0 | uint32be offset of the actual image data from 0 in this struct
         4 | uint32be number of images in this texture
         8 | uint32be offset of the first image entry from 0 in this struct

#### Image Entry Format

    Offset | Contents
    -------+---------
         0 | uint32be size of this image entry header in bytes
         4 | uint32be anchor x
         8 | uint32be anchor y
        12 | uint32be image x
        16 | uint32be image y
        20 | uint32be unrotated image width
        24 | uint32be unrotated image height
        28 | uint8 boolean whether the image is rotated clockwise 90 degrees
        32 | uint32be key size in bytes
        36 | key bytes
