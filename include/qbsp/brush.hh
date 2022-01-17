/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#pragma once

#include <qbsp/winding.hh>
#include <common/aabb.hh>

/*
 * The brush list needs to be ordered (lowest to highest priority):
 * - detail_illusionary (which is saved as empty)
 * - liquid
 * - detail_fence
 * - detail (which is solid)
 * - sky
 * - solid
 */
enum brush_type_t
{
    BRUSH_SOLID,
    BRUSH_SKY,
    BRUSH_DETAIL,
    BRUSH_DETAIL_ILLUSIONARY,
    BRUSH_DETAIL_FENCE,
    BRUSH_LIQUID,
    BRUSH_TOTAL
};

static constexpr const char *brush_type_names[BRUSH_TOTAL] = {
    "solid",
    "sky",
    "detail",
    "detail illusionary",
    "detail fence",
    "liquid"
};

void FreeBrushFaces(face_t *faces);

struct brush_t
{
    brush_type_t type = BRUSH_TOTAL; /* brush type; defaulted to invalid value */
    aabb3d bounds;
    std::vector<face_t> faces;
    contentflags_t contents; /* BSP contents */
    short lmshift = 0; /* lightmap scaling (qu/lightmap pixel), passed to the light util */

    inline brush_t(contentflags_t contents, std::vector<face_t> &&faces, aabb3d bounds) :
        contents(contents),
        faces(faces),
        bounds(bounds)
    {
    }
};

class mapbrush_t;

qplane3d Face_Plane(const face_t *face);

enum class rotation_t
{
    none,
    hipnotic,
    origin_brush
};

std::optional<brush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
                                 const qvec3d &rotate_offset, const rotation_t rottype, const int hullnum);
void FreeBrushes(mapentity_t *ent);

int FindPlane(const qplane3d &plane, int *side);

void FreeBrush(brush_t *brush);
