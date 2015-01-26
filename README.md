# rucksack

Texture packer and resource bundler. Use the rucksack executable to build your
resources file, and then depend on librucksack in-game to get the resources out
at runtime. Alternately, roll your own resource-loading code using the file
format detailed at the end of this README.

rucksack defines a resource manifest file format which is loosely based on JSON
but accepts comments, extra punctuation, and unquoted strings. This manifest
file tells rucksack which files to bundle and how to store them. rucksack uses
a rectangle bin packing algorithm to efficiently store multiple images into a
single texture, otherwise known as a "sprite sheet".

## Command Line Usage

```
rucksack v2.1.1

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
 * [liblaxjson](https://github.com/andrewrk/liblaxjson)

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
// comments are OK :)
// single quotes, double quotes, and no quotes are OK
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
      // 1024x1024 is the default.
      maxWidth: 1024,
      maxHeight: 1024,

      // By default rucksack enforces that the width and height of a texture
      // will be a power of 2. Set pow2 to false to disable this.
      pow2: true,

      // rucksack efficiently packs textures by sometimes rotating images
      // to make them fit better, and you can compensate for this by using
      // different UV coords when the images are rotated. If you for some
      // reason want to disable this feature, you can set allowRotate90 to
      // false.
      allowRotate90: true,

      globImages: [
        {
          path: "path/to/dir",
          glob: "*",
          prefix: "abc_", // prepended to the key

          // can be any of these strings:
          // "top", "left", "bottom", "right"
          // "topleft", "topright", "bottomleft", "bottomright"
          // "center"
          // or it can be an object like this: {x: 13, y: 19}
          anchor: "center",
        },
      ],
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
        16 | uint32be file format version. bumped when incompatible changes made.
        20 | uint32be offset of first header entry from file start
        24 | uint32be number of header entries

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
         0 | 16 byte UUID 0e b1 4c 84 47 4c b3 ad a6 bd 93 e4 be a5 46 ba
        16 | uint32be offset of the actual image data from 0 in this struct
        20 | uint32be number of images in this texture
        24 | uint32be offset of the first image entry from 0 in this struct
        28 | uint32be max_width used when creating this texture
        32 | uint32be max_height used when creating this texture
        36 | uint8 pow2 value used when creating this texture
        37 | uint8 allow_r90 value used when creating this texture

#### Image Entry Format

    Offset | Contents
    -------+---------
         0 | uint32be size of this image entry header in bytes
         4 | uint32be anchor position enum value
         8 | uint32be anchor x multiplied by 16384
        12 | uint32be anchor y multiplied by 16384
        16 | uint32be image x
        20 | uint32be image y
        24 | uint32be unrotated image width
        28 | uint32be unrotated image height
        32 | uint8 boolean whether the image is rotated clockwise 90 degrees
        33 | uint32be key size in bytes
        37 | key bytes

## Projects Using rucksack

Feel free to make a pull request adding to this list.

 * [spacefight](https://github.com/andrewrk/spacefight) - 3D asteroids prototype
 * [grapple](https://github.com/andrewrk/grapple) - prototyping a 4 player arcade game based on a grapple gun and a physics engine
