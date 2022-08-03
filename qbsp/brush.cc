/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis
    Copyright (C) 1999-2005  Id Software, Inc.

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

#include <cstring>
#include <list>

#include <qbsp/brush.hh>
#include <qbsp/csg.hh>
#include <qbsp/map.hh>
#include <qbsp/qbsp.hh>

bool bspbrush_t_less::operator()(const bspbrush_t *a, const bspbrush_t *b) const
{
    return a->file_order < b->file_order;
}

const maptexinfo_t &side_t::get_texinfo() const
{
    return map.mtexinfos[this->texinfo];
}

const qbsp_plane_t &side_t::get_plane() const
{
    return map.get_plane(planenum);
}

const qbsp_plane_t &side_t::get_positive_plane() const
{
    return map.get_plane(planenum & ~1);
}

std::unique_ptr<bspbrush_t> bspbrush_t::copy_unique() const
{
    return std::make_unique<bspbrush_t>(*this);
}

/*
=================
Face_Plane
=================
*/
qplane3d Face_Plane(const face_t *face)
{
    return face->get_plane();
}

qplane3d Face_Plane(const side_t *face)
{
    return face->get_plane();
}

/*
=================
CheckFace

Note: this will not catch 0 area polygons
=================
*/
static void CheckFace(side_t *face, const mapface_t &sourceface)
{
    if (face->w.size() < 3) {
        if (face->w.size() == 2) {
            logging::print(
                "WARNING: line {}: too few points (2): ({}) ({})\n", sourceface.linenum, face->w[0], face->w[1]);
        } else if (face->w.size() == 1) {
            logging::print("WARNING: line {}: too few points (1): ({})\n", sourceface.linenum, face->w[0]);
        } else {
            logging::print("WARNING: line {}: too few points ({})\n", sourceface.linenum, face->w.size());
        }

        face->w.clear();
        return;
    }

    const qbsp_plane_t &plane = face->get_plane();
    qvec3d facenormal = plane.get_normal();

    for (size_t i = 0; i < face->w.size(); i++) {
        const qvec3d &p1 = face->w[i];
        const qvec3d &p2 = face->w[(i + 1) % face->w.size()];

        for (auto &v : p1) {
            if (fabs(v) > qbsp_options.worldextent.value()) {
                // this is fatal because a point should never lay outside the world
                FError("line {}: coordinate out of range ({})\n", sourceface.linenum, v);
            }
        }

        /* check the point is on the face plane */
        // fixme check: plane's normal is not inverted by planeside check above,
        // is this a bug? should `Face_Plane` be used instead?
        vec_t dist = plane.distance_to(p1);
        if (dist < -qbsp_options.epsilon.value() || dist > qbsp_options.epsilon.value()) {
            logging::print("WARNING: Line {}: Point ({:.3} {:.3} {:.3}) off plane by {:2.4}\n", sourceface.linenum,
                p1[0], p1[1], p1[2], dist);
        }

        /* check the edge isn't degenerate */
        qvec3d edgevec = p2 - p1;
        vec_t length = qv::length(edgevec);
        if (length < qbsp_options.epsilon.value()) {
            logging::print("WARNING: Line {}: Healing degenerate edge ({}) at ({:.3f} {:.3} {:.3})\n",
                sourceface.linenum, length, p1[0], p1[1], p1[2]);
            for (size_t j = i + 1; j < face->w.size(); j++)
                face->w[j - 1] = face->w[j];
            face->w.resize(face->w.size() - 1);
            CheckFace(face, sourceface);
            break;
        }

        qvec3d edgenormal = qv::normalize(qv::cross(facenormal, edgevec));
        vec_t edgedist = qv::dot(p1, edgenormal);
        edgedist += qbsp_options.epsilon.value();

        /* all other points must be on front side */
        for (size_t j = 0; j < face->w.size(); j++) {
            if (j == i)
                continue;
            dist = qv::dot(face->w[j], edgenormal);
            if (dist > edgedist) {
                logging::print("WARNING: line {}: Found a non-convex face (error size {}, point: {})\n",
                    sourceface.linenum, dist - edgedist, face->w[j]);
                face->w.clear();
                return;
            }
        }
    }
}

/*
=============================================================================

                        TURN BRUSHES INTO GROUPS OF FACES

=============================================================================
*/

/*
=================
FindTargetEntity
=================
*/
static const mapentity_t *FindTargetEntity(const std::string &target)
{
    for (const auto &entity : map.entities) {
        const std::string &name = entity.epairs.get("targetname");
        if (!string_iequals(target, name))
            return &entity;
    }

    return nullptr;
}

/*
=================
FixRotateOrigin
=================
*/
qvec3d FixRotateOrigin(mapentity_t *entity)
{
    const std::string &search = entity->epairs.get("target");
    const mapentity_t *target = nullptr;

    if (!search.empty()) {
        target = FindTargetEntity(search);
    }

    qvec3d offset;

    if (target) {
        target->epairs.get_vector("origin", offset);
    } else {
        logging::print("WARNING: No target for rotation entity \"{}\"", entity->epairs.get("classname"));
        offset = {};
    }

    entity->epairs.set("origin", qv::to_string(offset));
    return offset;
}

static bool MapBrush_IsHint(const mapbrush_t &brush)
{
    for (auto &f : brush.faces) {
        if (f.flags.is_hint)
            return true;
    }

    return false;
}

/*
=====================
FreeBrushes
=====================
*/
void FreeBrushes(mapentity_t *ent)
{
    ent->brushes.clear();
}

#if 0
        if (hullnum <= 0 && Brush_IsHint(*hullbrush)) {
            /* Don't generate hintskip faces */
            const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

            if (qbsp_options.target_game->texinfo_is_hintskip(texinfo.flags, map.miptexTextureName(texinfo.miptex)))
                continue;
        }
#endif

#if 0
/*
============
ExpandBrush
=============
*/
static void ExpandBrush(bspbrush_t &hullbrush, const aabb3d &hull_size)
{
    int x, s;
    qbsp_plane_t plane;
    int cBevEdge = 0;

    // create all the hull points
    for (auto &f : facelist)
        for (size_t i = 0; i < f.w.size(); i++) {
            AddHullPoint(hullbrush, f.w[i], hull_size);
            cBevEdge++;
        }

    // expand all of the planes
    for (auto &mapface : hullbrush->faces) {
        if (mapface.flags.no_expand)
            continue;
        qvec3d corner{};
        for (x = 0; x < 3; x++) {
            if (mapface.get_plane().get_normal()[x] > 0)
                corner[x] = hull_size[1][x];
            else if (mapface.get_plane().get_normal()[x] < 0)
                corner[x] = hull_size[0][x];
        }
        qplane3d plane = mapface.get_plane();
        plane.dist += qv::dot(corner, plane.normal);
        mapface.planenum = map.add_or_find_plane(plane);
    }

    // add any axis planes not contained in the brush to bevel off corners
    for (x = 0; x < 3; x++)
        for (s = -1; s <= 1; s += 2) {
            // add the plane
            qvec3d normal = {};
            normal[x] = (vec_t)s;
            plane.set_normal(normal);
            if (s == -1)
                plane.get_dist() = -hullbrush->bounds.mins()[x] + -hull_size[0][x];
            else
                plane.get_dist() = hullbrush->bounds.maxs()[x] + hull_size[1][x];
            AddBrushPlane(hullbrush, plane);
        }

    // add all of the edge bevels
    for (auto &f : facelist)
        for (size_t i = 0; i < f.w.size(); i++)
            AddHullEdge(hullbrush, f.w[i], f.w[(i + 1) % f.w.size()], hull_size);
}
#endif

//============================================================================

contentflags_t Brush_GetContents(const mapbrush_t *mapbrush)
{
    bool base_contents_set = false;
    contentflags_t base_contents = qbsp_options.target_game->create_empty_contents();

    // validate that all of the sides have valid contents
    for (auto &mapface : mapbrush->faces) {
        const maptexinfo_t &texinfo = map.mtexinfos.at(mapface.texinfo);

        contentflags_t contents =
            qbsp_options.target_game->face_get_contents(mapface.texname.data(), texinfo.flags, mapface.contents);

        if (contents.is_empty(qbsp_options.target_game)) {
            continue;
        }

        // use the first non-empty as the base contents value
        if (!base_contents_set) {
            base_contents_set = true;
            base_contents = contents;
        }

        if (!contents.types_equal(base_contents, qbsp_options.target_game)) {
            logging::print("mixed face contents ({} != {}) at line {}\n",
                base_contents.to_string(qbsp_options.target_game), contents.to_string(qbsp_options.target_game),
                mapface.linenum);
            break;
        }
    }

    // make sure we found a valid type
    Q_assert(base_contents.is_valid(qbsp_options.target_game, false));

    return base_contents;
}

/*
===============
LoadBrush

Converts a mapbrush to a bsp brush
===============
*/
std::optional<bspbrush_t> LoadBrush(const mapentity_t *src, const mapbrush_t *mapbrush, const contentflags_t &contents,
    const int hullnum)
{
    // create the brush
    bspbrush_t brush{};
    brush.contents = contents;
    brush.sides.reserve(mapbrush->faces.size());

    for (size_t i = 0; i < mapbrush->faces.size(); i++) {
        auto &src = mapbrush->faces[i];

        if (src.bevel) {
            continue;
        }

        auto &dst = brush.sides.emplace_back();

        dst.texinfo = hullnum > 0 ? 0 : src.texinfo;
        dst.planenum = src.planenum;
        dst.bevel = src.bevel;

        // TEMP
        dst.w = src.winding;

        CheckFace(&dst, src);
    }

    // todo: expand planes, recalculate bounds & windings
    brush.bounds = mapbrush->bounds;

#if 0
    if (hullnum > 0) {
        auto &hulls = qbsp_options.target_game->get_hull_sizes();
        Q_assert(hullnum < hulls.size());
        ExpandBrush(&hullbrush, *(hulls.begin() + hullnum), facelist);
        facelist = CreateBrushFaces(src, &hullbrush, hullnum);
    }
#endif

    brush.mapbrush = mapbrush;
    return brush;
}

//=============================================================================

static void Brush_LoadEntity(mapentity_t *dst, const mapentity_t *src, const int hullnum, content_stats_base_t &stats)
{
    // _omitbrushes 1 just discards all brushes in the entity.
    // could be useful for geometry guides, selective compilation, etc.
    if (src->epairs.get_int("_omitbrushes")) {
        return;
    }

    int i;
    int lmshift;
    bool all_detail, all_detail_fence, all_detail_illusionary;

    const std::string &classname = src->epairs.get("classname");

    /* If the source entity is func_detail, set the content flag */
    if (!qbsp_options.nodetail.value()) {
        all_detail = false;
        if (!Q_strcasecmp(classname, "func_detail")) {
            all_detail = true;
        }

        all_detail_fence = false;
        if (!Q_strcasecmp(classname, "func_detail_fence") || !Q_strcasecmp(classname, "func_detail_wall")) {
            all_detail_fence = true;
        }

        all_detail_illusionary = false;
        if (!Q_strcasecmp(classname, "func_detail_illusionary")) {
            all_detail_illusionary = true;
        }
    }

    /* entities with custom lmscales are important for the qbsp to know about */
    i = 16 * src->epairs.get_float("_lmscale");
    if (!i)
        i = 16; // if 0, pick a suitable default
    lmshift = 0;
    while (i > 1) {
        lmshift++; // only allow power-of-two scales
        i /= 2;
    }

    /* _mirrorinside key (for func_water etc.) */
    std::optional<bool> mirrorinside;

    if (src->epairs.has("_mirrorinside")) {
        mirrorinside = src->epairs.get_int("_mirrorinside") ? true : false;
    }

    /* _noclipfaces */
    std::optional<bool> clipsametype;

    if (src->epairs.has("_noclipfaces")) {
        clipsametype = src->epairs.get_int("_noclipfaces") ? false : true;
    }

    const bool func_illusionary_visblocker = (0 == Q_strcasecmp(classname, "func_illusionary_visblocker"));

    auto it = src->mapbrushes.begin();
    for (i = 0; i < src->mapbrushes.size(); i++, it++) {
        logging::percent(i, src->mapbrushes.size());
        auto &mapbrush = *it;
        contentflags_t contents = Brush_GetContents(&mapbrush);

        // per-brush settings
        bool detail = false;
        bool detail_illusionary = false;
        bool detail_fence = false;

        // inherit the per-entity settings
        detail |= all_detail;
        detail_illusionary |= all_detail_illusionary;
        detail_fence |= all_detail_fence;

        /* "origin" brushes always discarded */
        if (contents.is_origin(qbsp_options.target_game))
            continue;

        /* -omitdetail option omits all types of detail */
        if (qbsp_options.omitdetail.value() && detail)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailillusionary.value()) && detail_illusionary)
            continue;
        if ((qbsp_options.omitdetail.value() || qbsp_options.omitdetailfence.value()) && detail_fence)
            continue;

        /* turn solid brushes into detail, if we're in hull0 */
        if (hullnum <= 0 && contents.is_solid(qbsp_options.target_game)) {
            if (detail_illusionary) {
                contents = qbsp_options.target_game->create_detail_illusionary_contents(contents);
            } else if (detail_fence) {
                contents = qbsp_options.target_game->create_detail_fence_contents(contents);
            } else if (detail) {
                contents = qbsp_options.target_game->create_detail_solid_contents(contents);
            }
        }

        /* func_detail_illusionary don't exist in the collision hull
         * (or bspx export) except for Q2, who needs them in there */
        if (hullnum > 0 && detail_illusionary) {
            continue;
        }

        /*
         * "clip" brushes don't show up in the draw hull, but we still want to
         * include them in the model bounds so collision detection works
         * correctly.
         */
        if (hullnum != HULL_COLLISION && contents.is_clip(qbsp_options.target_game)) {
            if (hullnum == 0) {
                std::optional<bspbrush_t> brush = LoadBrush(src, &mapbrush, contents, hullnum);

                if (brush) {
                    dst->bounds += brush->bounds;
                }

                continue;
                // for hull1, 2, etc., convert clip to CONTENTS_SOLID
            } else {
                contents = qbsp_options.target_game->create_solid_contents();
            }
        }

        /* "hint" brushes don't affect the collision hulls */
        if (MapBrush_IsHint(mapbrush)) {
            if (hullnum > 0)
                continue;
            contents = qbsp_options.target_game->create_empty_contents();
        }

        /* entities in some games never use water merging */
        if (dst != map.world_entity() && !qbsp_options.target_game->allow_contented_bmodels) {
            contents = qbsp_options.target_game->create_solid_contents();

            /* Hack to turn bmodels with "_mirrorinside" into func_detail_fence in hull 0.
                this is to allow "_mirrorinside" to work on func_illusionary, func_wall, etc.
                Otherwise they would be CONTENTS_SOLID and the inside faces would be deleted.

                It's CONTENTS_DETAIL_FENCE because this gets mapped to CONTENTS_SOLID just
                before writing the bsp, and bmodels normally have CONTENTS_SOLID as their
                contents type.
                */
            if (hullnum <= 0 && mirrorinside.value_or(false)) {
                contents = qbsp_options.target_game->create_detail_fence_contents(contents);
            }
        }

        /* nonsolid brushes don't show up in clipping hulls */
        if (hullnum > 0 && !contents.is_solid(qbsp_options.target_game) && !contents.is_sky(qbsp_options.target_game))
            continue;

        /* sky brushes are solid in the collision hulls */
        if (hullnum > 0 && contents.is_sky(qbsp_options.target_game))
            contents = qbsp_options.target_game->create_solid_contents();

        // apply extended flags
        contents.set_mirrored(mirrorinside);
        contents.set_clips_same_type(clipsametype);
        contents.illusionary_visblocker = func_illusionary_visblocker;

        std::optional<bspbrush_t> brush = LoadBrush(src, &mapbrush, contents, hullnum);
        if (!brush)
            continue;

        brush->lmshift = lmshift;

        for (auto &face : brush->sides)
            face.lmshift = lmshift;

        if (classname == std::string_view("func_areaportal")) {
            brush->func_areaportal = const_cast<mapentity_t *>(src); // FIXME: get rid of consts on src in the callers?
        }

        qbsp_options.target_game->count_contents_in_stats(brush->contents, stats);
        dst->brushes.push_back(std::make_unique<bspbrush_t>(brush.value()));
        dst->bounds += brush->bounds;
    }

    logging::percent(src->mapbrushes.size(), src->mapbrushes.size(), src == map.world_entity());
}

/*
============
Brush_LoadEntity

hullnum HULL_COLLISION should contain ALL brushes. (used by BSPX_CreateBrushList())
hullnum 0 does not contain clip brushes.
============
*/
void Brush_LoadEntity(mapentity_t *entity, const int hullnum)
{
    logging::funcheader();

    auto stats = qbsp_options.target_game->create_content_stats();

    Brush_LoadEntity(entity, entity, hullnum, *stats);

    /*
     * If this is the world entity, find all func_group and func_detail
     * entities and add their brushes with the appropriate contents flag set.
     */
    if (entity == map.world_entity()) {
        /*
         * We no longer care about the order of adding func_detail and func_group,
         * Entity_SortBrushes will sort the brushes
         */
        for (int i = 1; i < map.entities.size(); i++) {
            mapentity_t *source = &map.entities.at(i);

            /* Load external .map and change the classname, if needed */
            ProcessExternalMapEntity(source);

            ProcessAreaPortal(source);

            if (IsWorldBrushEntity(source) || IsNonRemoveWorldBrushEntity(source)) {
                Brush_LoadEntity(entity, source, hullnum, *stats);
            }
        }
    }

    qbsp_options.target_game->print_content_stats(*stats, "brushes");
}

void bspbrush_t::update_bounds()
{
    this->bounds = {};
    for (const auto &face : sides) {
        this->bounds = this->bounds.unionWith(face.w.bounds());
    }

    this->sphere_origin = (bounds.mins() + bounds.maxs()) / 2.0;
    this->sphere_radius = qv::length((bounds.maxs() - bounds.mins()) / 2.0);
}
