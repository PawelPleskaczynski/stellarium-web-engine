/* Stellarium Web Engine - Copyright (c) 2018 - Noctua Software Ltd
 *
 * This program is licensed under the terms of the GNU AGPL v3, or
 * alternatively under a commercial licence.
 *
 * The terms of the AGPL v3 license can be found in the main directory of this
 * repository.
 */

/*
 * File: Skyculture
 *
 * Some basic functions to parse skyculture data files.
 * Still experimental, probably going to change.
 *
 * The actual skyculture struct is in src/modules/skycultures.c
 */

#ifndef SKYCULTURE_H
#define SKYCULTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "uthash.h"

/*
 * Type: constellation_infos_t
 * Information about a given constellation.
 */
typedef struct constellation_infos
{
    char id[8];
    char name[128];
    char name_translated[128];
    int  lines[64][2]; // star HIP number.
    int  nb_lines;
    double edges[64][2][2]; // Ra/dec B1875 boundaries polygon.
    int nb_edges;
} constellation_infos_t;

/*
 * Type: constellation_art_t
 * Information about a constellation image.
 */
typedef struct constellation_art
{
    char cst[8];    // Id of the constellation.
    char img[128];  // Name of the image file.
    bool uv_in_pixel; // Set to true if anchors uv are in pixel.
    struct {
        double  uv[2]; // Texture UV position.
        int     hip;   // Star HIP.
    } anchors[3];
} constellation_art_t;

/*
 * Type: skyculture_name
 * Structure to hold hash table of object name and oid.
 *
 * Used as result of skyculture names file parsing.
 */
typedef struct skyculture_name
{
    UT_hash_handle  hh;
    uint64_t        oid;
    char            name[128];
} skyculture_name_t;

/*
 * Function: skyculture_parse_constellations
 * Parse a skyculture constellation file.
 */
constellation_infos_t *skyculture_parse_constellations(
        const char *consts, int *nb);

/*
 * Function: skyculture_parse_edge
 * Parse a constellation edge file.
 *
 * Parameters:
 *   data   - Text data in the edge file format.
 *   infos  - Constellation info to update with the edge data.
 *
 * Return:
 *   The number of lines parsed, or -1 in case of error.
 */
int skyculture_parse_edges(const char *data, constellation_infos_t *infos);

/*
 * Function: skyculture_parse_stellarium_constellations
 * Parse a skyculture constellation file in stellarium format.
 */
constellation_infos_t *skyculture_parse_stellarium_constellations(
        const char *consts, int *nb);

/*
 * Function: skyculture_parse_stellarium_constellations_names
 * Parse a 'constellation_names.fab' file.
 *
 * Parameters:
 *   data   - Text data in the fab file format.
 *   infos  - Constellation info to update with the names.
 *
 * Return:
 *   The number of names parsed, or -1 in case of error.
 */
int skyculture_parse_stellarium_constellations_names(
        const char *data, constellation_infos_t *infos);

constellation_art_t *skyculture_parse_stellarium_constellations_art(
        const char *data, int *nb);

/*
 * Function: skyculture_parse_stellarium_star_names
 * Parse a skyculture star names file.
 *
 * Note that for the moment we just ignore the alternative names for the same
 * star.
 *
 * Return:
 *   A hash table of oid -> name.
 */
skyculture_name_t *skyculture_parse_stellarium_star_names(const char *data);

#endif // SKYCULTURE_H
