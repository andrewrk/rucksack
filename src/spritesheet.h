/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of rucksack, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */


#ifndef RUCKSACK_SPRITESHEET_H_INCLUDED
#define RUCKSACK_SPRITESHEET_H_INCLUDED

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#include "rucksack.h"


struct RuckSackImage *rucksack_image_create(void);
void rucksack_image_destroy(struct RuckSackImage *image);

/* rucksack copies data from the image you pass here; you still own the memory. */
int rucksack_texture_add_image(struct RuckSackTexture *texture, struct RuckSackImage *image);

int rucksack_bundle_add_texture(struct RuckSackBundle *bundle, struct RuckSackTexture *texture);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RUCKSACK_SPRITESHEET_H_INCLUDED */
