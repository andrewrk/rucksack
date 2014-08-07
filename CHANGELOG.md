### 2.0.0 (unreleased)

 * ability to open a rucksack bundle which is a 0 byte empty file.
 * added a setting which allows preventing rotating images 90 degrees.
 * ability to glob for images in a texture.
 * (API breaking) rename all `page` functions to `texture`.
 * (API breaking) remove `RuckSackImage` property `name` in favor of `key`,
   `key_size`, and `path`.
 * (format breaking) update bundle format
   - include bundle version number
   - texture header includes UUID to identify it as a texture
   - texture header includes the properties that were used to generate it
   - image header uses only 1 byte for the r90 boolean
 * fix not updating bundle when texture properties changed from manifest json

### 1.0.1

 * Update docs and add changelog

### 1.0.0

 * Initial release.
