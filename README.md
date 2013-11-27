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
