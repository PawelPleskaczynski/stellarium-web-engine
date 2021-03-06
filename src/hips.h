/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

#ifndef HIPS_H
#define HIPS_H

#include "painter.h"

/*
 * File: hips.h
 */

/*
 * Type: hips_t
 * Opaque structure representing a hips survey.
 */
typedef struct hips hips_t;

enum {
    HIPS_PLANET                 = 1 << 0,
    HIPS_FORCE_USE_ALLSKY       = 1 << 1,
    HIPS_LOAD_IN_THREAD         = 1 << 2,
    HIPS_CACHED_ONLY            = 1 << 3,
};

/*
 * Type: hips_settings
 * Structure passed to hips_create for custom type surveys.
 *
 * Attributes:
 *   create_tile - function used to convert source data into a tile.
 *                 The returned pointer is handled by the hips survey, and
 *                 can be anything.  This is called every time the survey
 *                 load a tile that is not in the cache.  See note [1]
 *   delete_tile - function used to delete the data returned by create_tile.
 *   user        - pointer passed to create_tile.
 *
 * Note 1:
 *   The create_tile function needs to return a cost value (in bytes) for the
 *   cache, and if we know that some children tiles don't need to be loaded, we
 *   can set the transparency value, as a four bits bitmask, one bit per child.
 */
typedef struct hips_settings {
    const void *(*create_tile)(void *user, int order, int pix, void *data,
                               int size, int *cost, int *transparency);
    int (*delete_tile)(void *tile);
    void *user;
} hips_settings_t;

/*
 * Function: hips_create
 * Create a new hips survey.
 *
 * Parameters:
 *   url          - URL to the root of the survey.
 *   release_date - If known, release date in utc.  Otherwise 0.
 */
hips_t *hips_create(const char *url, double release_date,
                    const hips_settings_t *settings);

/*
 * Function: hips_get_tile
 * Get a given tile of a hips survey.
 *
 * This only makes sense for custom type surveys (for images we can directly
 * use <hips_get_tile_texture>).
 *
 * Parameters:
 *   hips   - a hips survey.
 *   order  - order of the tile.
 *   pix    - pix of the tile.
 *   flags  - unused for the moment.
 *   code   - get the return code of the tile loading.
 *
 * Return:
 *   The value previously returned by the settings create_tile function.
 *   If the tile data is not available yet set code to 0 and return NULL.
 *   In case of error, set code to the error code and return NULL.
 */
const void *hips_get_tile(hips_t *hips, int order, int pix, int flags,
                          int *code);

/*
 * Function: hips_is_ready
 * Check if a hips survey is ready to use
 *
 * The return true if:
 * - the property file has been parsed.
 * - the allsky image has been loaded (if there is one).
 */
bool hips_is_ready(hips_t *hips);

/*
 * Function: hips_traverse
 * Depth first traversal of healpix grid.
 *
 * The callback should return:
 *   1 to keep going deeper into the tile.
 *   0 to stop iterating inside this tile.
 *   <0 to immediately return (with the same value).
 *
 * Return:
 *   0 if the traverse finished.
 *   -1 if we reach the traverse limite.
 *   -v if the callback returned a negative value -v.
 */
int hips_traverse(void *user, int callback(int order, int pix, void *user));

/*
 * Function: hips_get_tile_texture
 * Get the texture for a given hips tile.
 *
 * This should return the most appropriate texture, no matter if the actual
 * tile exists.  It tries to use a parent texture, or the allsky as
 * fallback.
 *
 * The algorithm is more or less:
 *   - If the tile is loaded, return its texture.
 *   - If not, or if the order is higher than the survey max order,
 *     try to use a parent tile as a fallback.
 *   - If no parent is loaded, but we have an allsky image, use it.
 *   - If all else failed, return NULL.  In that case the UV and projection
 *     are still set, so that the client can still render a fallback texture.
 *
 * Parameters:
 *   order   - Order of the tile we are looking for.
 *   pix     - Pixel index of the tile we are looking for.
 *   flags   - <HIPS_FLAGS> union.
 *   transf  - If the returned texture is larger than the healpix pixel,
 *             this matrix is multiplied by the transformation to apply
 *             to the original UV coordinates to get the part of the texture.
 *   fade    - Recommended fade alpha.
 *   loading_complete - set to true if the tile is totally loaded.
 *
 * Return:
 *   The texture_t, or NULL if none is found.
 */
texture_t *hips_get_tile_texture(
        hips_t *hips, int order, int pix, int flags,
        double transf[3][3], double *fade, bool *loading_complete);

/*
 * Function: hips_parse_hipslist
 * Parse a hipslist file.
 *
 * Return:
 *    >= 0 if list successfully parsed, number of parsed entries.
 *    < 0 if error.
 */
int hips_parse_hipslist(
        const char *data, void *user,
        int callback(void *user, const char *url, double release_date));

/*
 * Function: hips_set_frame
 * Set the frame of a hips survey (if it is not specified in the properties).
 *
 * Parameters:
 *   hips  - A hips survey.
 *   frame - FRAME_ICRF or FRAME_OBSERVED.
 */
void hips_set_frame(hips_t *hips, int frame);

/*
 * Function: hips_set_label
 * Set the label for a hips survey
 *
 * Parameters:
 *   hips  - A hips survey.
 *   label - The new label to use. It will override existing labels
 *           such as the ones taken from the properties file.
 */
void hips_set_label(hips_t *hips, const char* label);

/*
 * Function: hips_get_render_order
 * Return the max order at which a survey will be rendered.
 *
 * Parameters:
 *   hips    - A hips survey.
 *   painter - The painter used to render.
 *   angle   - Visible angle the survey has in the sky.
 *             (2 * PI for full sky surveys).
 */
int hips_get_render_order(const hips_t *hips, const painter_t *painter,
                          double angle);

/*
 * Function: hips_render
 * Render a hips survey.
 *
 * Parameters:
 *   hips    - A hips survey.
 *   painter - The painter used to render.
 *   angle   - Visible angle the survey has in the sky.
 *             (2 * PI for full sky surveys).
 *   split_order - The requested order of the final quad divisions.
 *                 The actual split order could be higher if the rendering
 *                 order is too high for this value.
 */
int hips_render(hips_t *hips, const painter_t *painter, double angle,
                int split_order);

/*
 * Function: hips_render_traverse
 * Similar to hips_render, but instead of actually rendering the tiles
 *  we call a callback function.  This can be used when we need better
 *  control on the rendering.
 */
int hips_render_traverse(hips_t *hips, const painter_t *painter,
                         double angle, int split_order, void *user,
                         int callback(hips_t *hips, const painter_t *painter,
                                      int order, int pix, int split,
                                      int flags, void *user));

/*
 * Function: hips_parse_date
 * Parse a date in the format supported for HiPS property files
 *
 * Parameters:
 *   str    - A date string (like 2019-01-02T15:27Z)
 *
 * Returns:
 *   The time in MJD, or 0 in case of error.
 */
double hips_parse_date(const char *str);

#endif // HIPS_H
