# rucksack

(work in progress) Open-source texture packer.

## Usage

```
rucksack assets.json

Options:
  --dir outputdir        defaults to current directory
  --format json          'json' or 'plain'
```

Parses assets.json and assembles textures and texture maps.

## Dependencies

 * [FreeImage](http://freeimage.sourceforge.net/)
 * [liblaxjson](https://github.com/superjoe30/liblaxjson)

## Installation

1. `mkdir build && cd build && cmake ..`
2. Verify that all dependencies say "OK".
3. `make && sudo make install`

## JSON Reference

### Example

```js
{
  files: {
    file1Name: {
      path: "path/to/file",
      compression: null,
    },
  },
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

  Offset | Contents
  -------+---------
       0 | 16 byte UUID - 60 70 c8 99 82 a1 41 84 89 51 08 c9 1c c9 b6 20
      16 | uint32be offset of first header entry from file start
      20 | uint32be offset of last header entry from file start
      24 | uint32be header entry size in bytes
      28 | uint32be MaxKeySize - number of bytes allocated for keys

### Header Entry Format

           Offset | Contents
  ----------------+---------
                0 | uint32be key size in bytes
                4 | key bytes
   MaxKeySize + 4 | uint64be offset of this entry's file contents
  MaxKeySize + 12 | uint64be actual size of this file's contents in bytes
  MaxKeySize + 20 | uint64be number of allocated bytes for this file

