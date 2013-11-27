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
