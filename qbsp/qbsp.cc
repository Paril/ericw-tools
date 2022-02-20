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

#include <memory>
#include <cstring>
#include <algorithm>

#include <common/log.hh>
#include <common/aabb.hh>
#include <common/fs.hh>
#include <common/threads.hh>
#include <common/settings.hh>
#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>
#include <fmt/chrono.h>

#include "tbb/global_control.h"

constexpr const char *IntroString = "---- qbsp / ericw-tools " stringify(ERICWTOOLS_VERSION) " ----\n";

// command line flags
namespace settings
{
    static settings_group game_target_group { "Game/BSP Target", -1 };
    static settings_group map_development_group { "Map development", 1 };
    static settings_group common_format_group { "Common format options", 2 };
    static settings_group debugging_group { "Advanced/tool debugging", 500 };

    static lockable_bool hexen2 { "hexen2", false, &game_target_group, "target Hexen II's BSP format" };
    static lockable_bool hlbsp { "hlbsp", false, &game_target_group, "target Half Life's BSP format" };
    static lockable_bool q2bsp { "q2bsp", false, &game_target_group, "target Quake II's BSP format" };
    static lockable_bool qbism { "qbism", false, &game_target_group, "target Qbism's extended Quake II BSP format" };
    static lockable_bool bsp2 { "bsp2", false, &game_target_group, "target Quake's extended BSP2 format" };
    static lockable_bool bsp2rmq { "2psb", false, &game_target_group, "target Quake's extended 2PSB format (RMQ compatible)" };
    lockable_int32 subdivide { "subdivide", 240, &common_format_group, "change the subdivide threshold, in luxels. 0 will disable subdivision entirely" };
    static lockable_bool nofill { "nofill", false, &debugging_group, "don't perform outside filling" };
    static lockable_bool noclip { "noclip", false, &common_format_group, "don't write clip nodes (Q1-like BSP formats)" };
    lockable_bool noskip { "noskip", false, &debugging_group, "don't remove faces with the 'skip' texture" };
    lockable_bool nodetail { "nodetail", false, &debugging_group, "treat all detail brushes to structural" };
    lockable_bool onlyents { "onlyents", false, &map_development_group, "only updates .MAP entities" };
    lockable_bool splitsky { "splitsky", false, &debugging_group, "doesn't combine sky faces into one large face" };
    lockable_bool splitturb { strings { "litwater", "splitturb" }, false, &common_format_group, "doesn't combine water faces into one large face" };
    static lockable_redirect splitspecial { "splitspecial", { &splitsky, &splitturb }, &debugging_group, "doesn't combine sky and water faces into one large face (splitturb + splitsky)" };
    lockable_invertable_bool transwater { "transwater", true, &common_format_group, "compute portal information for transparent water" };
    lockable_bool transsky { "transsky", false, &map_development_group, "compute portal information for transparent sky" };
    lockable_bool notextures { "notex", false, &common_format_group, "write only placeholder textures to depend upon replacements, keep file sizes down, or to skirt copyrights" };
    lockable_enum<conversion_t> convertmapformat { "convert", conversion_t::none, {
        { "quake", conversion_t::quake },
        { "quake2", conversion_t::quake2 },
        { "valve", conversion_t::valve },
        { "bp", conversion_t::bp }
    }, &common_format_group, "convert a .MAP to a different .MAP format" };
    lockable_invertable_bool oldaxis { "oldaxis", true, &debugging_group, "uses alternate texture alignment which was default in tyrutils-ericw v0.15.1 and older" };
    lockable_bool forcegoodtree { "forcegoodtree", false, &debugging_group, "force use of expensive processing for SolidBSP stage" };
    lockable_scalar midsplitsurffraction { "midsplitsurffraction", 0.f, 0.f, 1.f, &debugging_group, "if 0 (default), use `maxnodesize` for deciding when to switch to midsplit bsp heuristic.\nif 0 < midsplitSurfFraction <= 1, switch to midsplit if the node contains more than this fraction of the model's\ntotal surfaces. Try 0.15 to 0.5. Works better than maxNodeSize for maps with a 3D skybox (e.g. +-128K unit maps)" };
    lockable_int32 maxnodesize { "maxnodesize", 1024, &debugging_group, "triggers simpler BSP Splitting when node exceeds size (default 1024, 0 to disable)" };
    lockable_bool oldrottex { "oldrottex", false, &debugging_group, "use old rotate_ brush texturing aligned at (0 0 0)" };
    lockable_scalar epsilon { "epsilon", 0.0001, 0.0, 1.0, &debugging_group, "customize epsilon value for point-on-plane checks" };
    lockable_bool contenthack { "contenthack", false, &debugging_group, "hack to fix leaks through solids. causes missing faces in some cases so disabled by default" };
    lockable_bool leaktest { "leaktest", false, &map_development_group, "make compilation fail if the map leaks" };
    lockable_bool includeskip { "includeskip", false, &common_format_group, "don't cull skip faces from the list of renderable surfaces (Q2RTX)" };
    lockable_scalar worldextent { "worldextent", 0.0, &debugging_group, "explicitly provide world extents; 0 will auto-detect" };
    lockable_int32 leakdist { "leakdist", 2, &debugging_group, "space between leakfile points" };
    lockable_bool forceprt1 { "forceprt1", false, &debugging_group, "force a PRT1 output file even if PRT2 is required for vis" };
    static lockable_bool notjunc { "notjunc", false, &debugging_group, "don't fix T-junctions" };
    static lockable_bool objexport { "objexport", false, &debugging_group, "export the map file as .OBJ models during various CSG phases" };
    static lockable_bool wrbrushes { strings { "wrbrushes", "bspx" }, false, &common_format_group, "includes a list of brushes for brush-based collision" };
    static lockable_redirect wrbrushesonly { strings { "wrbrushesonly", "bspxonly" }, { &wrbrushes, &noclip }, &common_format_group, "includes BSPX brushes and does not output clipping hulls (wrbrushes + noclip)" };
    lockable_bool omitdetail { "omitdetail", false, &map_development_group, "omit *all* detail brushes from the compile" };
    lockable_bool omitdetailwall { "omitdetailwall", false, &map_development_group, "func_detail_wall brushes are omitted from the compile" };
    lockable_bool omitdetailillusionary { "omitdetailillusionary", false, &map_development_group, "func_detail_illusionary brushes are omitted from the compile" };
    lockable_bool omitdetailfence { "omitdetailfence", false, &map_development_group, "func_detail_fence brushes are omitted from the compile" };
    lockable_bool expand { "expand", false, &common_format_group, "write hull 1 expanded brushes to expanded.map for debugging" };
    lockable_wadpathset wadpaths { strings { "wadpath", "xwadpath" }, &debugging_group, "add a path to the wad search paths; wads found in xwadpath's will not be embedded, otherwise they will be embedded (if not -notex)" };

    inline void register_settings()
    {
        globalSettings.addSettings({
            &hexen2, &hlbsp, &q2bsp, &qbism, &bsp2, &subdivide, &nofill, &noclip, &noskip, &nodetail, &onlyents,
            &splitsky, &splitturb, &splitspecial, &transwater, &transsky, &notextures, &convertmapformat, &oldaxis,
            &forcegoodtree, &midsplitsurffraction, &maxnodesize, &oldrottex, &epsilon, &contenthack, &leaktest, &includeskip,
            &worldextent, &leakdist, &forceprt1, &notjunc, &objexport, &wrbrushes, &wrbrushesonly, &omitdetail,
            &omitdetailwall, &omitdetailillusionary, &omitdetailfence, &expand, &wadpaths,
        });
    }
};

options_t options;

bool node_t::opaque() const
{
    return contents.is_sky(options.target_game) || contents.is_solid(options.target_game);
}

// a simple tree structure used for leaf brush
// compression.
struct leafbrush_entry_t
{
    uint32_t offset;
    std::map<uint32_t, leafbrush_entry_t> entries;
};

// per-entity
static struct
{
    uint32_t total_brushes, total_brush_sides;
    uint32_t total_leaf_brushes;
} brush_state;

// running total
static uint32_t brush_offset;

static void ExportBrushList_r(const mapentity_t *entity, node_t *node, const uint32_t &brush_offset)
{
    if (node->planenum == PLANENUM_LEAF) {
        if (node->contents.native) {
            uint32_t b_id = brush_offset;
            std::vector<uint32_t> brushes;

            for (auto &b : entity->brushes) {
                if (node->bounds.intersectWith(b.bounds)) {
                    brushes.push_back(b_id);
                }
                b_id++;
            }

            if (brushes.size()) {
                node->numleafbrushes = brushes.size();
                brush_state.total_leaf_brushes += node->numleafbrushes;
                node->firstleafbrush = map.bsp.dleafbrushes.size();
                map.bsp.dleafbrushes.insert(map.bsp.dleafbrushes.end(), brushes.begin(), brushes.end());
            }
        }

        return;
    }

    ExportBrushList_r(entity, node->children[0], brush_offset);
    ExportBrushList_r(entity, node->children[1], brush_offset);
}

/*
=================
AddBrushBevels

Adds any additional planes necessary to allow the brush to be expanded
against axial bounding boxes
=================
*/
static std::vector<std::tuple<size_t, const face_t *>> AddBrushBevels(const brush_t &b)
{
    // add already-present planes
    std::vector<std::tuple<size_t, const face_t *>> planes;

    for (auto &f : b.faces) {
        int32_t planenum = f.planenum;

        if (f.planeside) {
            planenum = FindPlane(-map.planes[f.planenum], nullptr);
        }

        int32_t outputplanenum = ExportMapPlane(planenum);
        planes.emplace_back(outputplanenum, &f);
    }

    //
    // add the axial planes
    //
    int32_t order = 0;
    for (int32_t axis = 0; axis < 3; axis++) {
        for (int32_t dir = -1; dir <= 1; dir += 2, order++) {
            size_t i;
            // see if the plane is allready present
            for (i = 0; i < planes.size(); i++) {
                if (map.bsp.dplanes[std::get<0>(planes[i])].normal[axis] == dir)
                    break;
            }

            if (i == planes.size()) {
                // add a new side
                qplane3d new_plane { };
                new_plane.normal[axis] = dir;
                if (dir == 1)
                    new_plane.dist = b.bounds.maxs()[axis];
                else
                    new_plane.dist = -b.bounds.mins()[axis];

                int32_t planenum = FindPlane(new_plane, nullptr);
                int32_t outputplanenum = ExportMapPlane(planenum);
                planes.emplace_back(outputplanenum, &b.faces.front());
            }

            // if the plane is not in it canonical order, swap it
            if (i != order)
                std::swap(planes[i], planes[order]);
        }
    }

    //
    // add the edge bevels
    //
    if (planes.size() == 6)
        return planes; // pure axial

    // test the non-axial plane edges
    size_t edges_to_test = planes.size();
    for (size_t i = 6; i < edges_to_test; i++) {
        auto &s = std::get<1>(planes[i]);
        if (!s)
            continue;
        auto &w = s->w;
        if (!w.size())
            continue;
        for (size_t j = 0; j < w.size(); j++) {
            size_t k = (j + 1) % w.size();
            qvec3d vec = w[j] - w[k];
            if (qv::normalizeInPlace(vec) < 0.5)
                continue;
            vec = qv::Snap(vec);
            for (k = 0; k < 3; k++)
                if (vec[k] == -1 || vec[k] == 1)
                    break; // axial
            if (k != 3)
                continue; // only test non-axial edges

            // try the six possible slanted axials from this edge
            for (int32_t axis = 0; axis < 3; axis++) {
                for (int32_t dir = -1; dir <= 1; dir += 2) {
                    qvec3d vec2 {};
                    // construct a plane
                    vec2[axis] = dir;
                    qplane3d current;
                    current.normal = qv::cross(vec, vec2);
                    if (qv::normalizeInPlace(current.normal) < 0.5)
                        continue;
                    current.dist = qv::dot(w[j], current.normal);

                    auto it = b.faces.begin();

                    // if all the points on all the sides are
                    // behind this plane, it is a proper edge bevel
                    for (; it != b.faces.end(); it++) {
                        auto &f = *it;
                        auto &plane = map.planes[f.planenum];
                        qplane3d temp = f.planeside ? -plane : plane;

                        // if this plane has allready been used, skip it
                        if (qv::epsilonEqual(current, temp))
                            break;

                        auto &w2 = f.w;
                        if (!w2.size())
                            continue;
                        size_t l;
                        for (l = 0; l < w2.size(); l++) {
                            vec_t d = current.distance_to(w2[l]);
                            if (d > 0.1)
                                break; // point in front
                        }
                        if (l != w2.size())
                            break;
                    }

                    if (it != b.faces.end())
                        continue; // wasn't part of the outer hull

                    // add this plane
                    int32_t planenum = FindPlane(current, nullptr);
                    int32_t outputplanenum = ExportMapPlane(planenum);
                    planes.emplace_back(outputplanenum, &b.faces.front());
                }
            }
        }
    }

    return planes;
}

static void ExportBrushList(const mapentity_t *entity, node_t *node, uint32_t &brush_offset)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    brush_state = {};

    for (auto &b : entity->brushes) {
        dbrush_t &brush = map.bsp.dbrushes.emplace_back(
            dbrush_t{static_cast<int32_t>(map.bsp.dbrushsides.size()), 0, b.contents.native});

        auto bevels = AddBrushBevels(b);

        for (auto &plane : bevels) {
            map.bsp.dbrushsides.push_back(
                {(uint32_t)std::get<0>(plane), (int32_t)ExportMapTexinfo(std::get<1>(plane)->texinfo)});
            brush.numsides++;
            brush_state.total_brush_sides++;
        }

        brush_state.total_brushes++;
    }

    ExportBrushList_r(entity, node, brush_offset);

    brush_offset += brush_state.total_brushes;

    LogPrint(LOG_STAT, "     {:8} total brushes\n", brush_state.total_brushes);
    LogPrint(LOG_STAT, "     {:8} total brush sides\n", brush_state.total_brush_sides);
    LogPrint(LOG_STAT, "     {:8} total leaf brushes\n", brush_state.total_leaf_brushes);
}

/*
=========================================================

FLOOD AREAS

=========================================================
*/

int32_t c_areas;

/*
===============
Portal_EntityFlood

The entity flood determines which areas are
"outside" on the map, which are then filled in.
Flowing from side s to side !s
===============
*/
static bool Portal_EntityFlood(const portal_t *p, int32_t s)
{
    auto contents0 = ClusterContents(p->nodes[0]);
    auto contents1 = ClusterContents(p->nodes[1]);

    // can never cross to a solid
    if (contents0.is_solid(options.target_game))
        return false;
    if (contents1.is_solid(options.target_game))
        return false;

    // can flood through everything else
    return true;
}

static void ApplyArea_r(node_t *node)
{
    node->area = c_areas;

    if (node->planenum != PLANENUM_LEAF) {
        ApplyArea_r(node->children[0]);
        ApplyArea_r(node->children[1]);
    }
}

/*
=============
FloodAreas_r
=============
*/
static void FloodAreas_r(node_t *node)
{
    if (node->planenum == PLANENUM_LEAF && node->contents.native == Q2_CONTENTS_AREAPORTAL) {
        // grab the func_areanode entity
        mapentity_t *entity = node->markfaces[0]->src_entity;

        // this node is part of an area portal;
        // if the current area has allready touched this
        // portal, we are done
        if (entity->portalareas[0] == c_areas || entity->portalareas[1] == c_areas)
            return;

        // note the current area as bounding the portal
        if (entity->portalareas[1]) {
            // FIXME: entity #
            LogPrint("WARNING: areaportal entity touches > 2 areas\n  Node Bounds: {} -> {}\n", node->bounds.mins(),
                node->bounds.maxs());
            return;
        }

        if (entity->portalareas[0])
            entity->portalareas[1] = c_areas;
        else
            entity->portalareas[0] = c_areas;

        return;
    }

    if (node->area)
        return; // already got it

    node->area = c_areas;

    // propagate area assignment to descendants if we're a cluster
    if (!(node->planenum == PLANENUM_LEAF)) {
        ApplyArea_r(node);
    }

    int32_t s;

    for (portal_t *p = node->portals; p; p = p->next[s]) {
        s = (p->nodes[1] == node);
#if 0
		if (p->nodes[!s]->occupied)
			continue;
#endif
        if (!Portal_EntityFlood(p, s))
            continue;

        FloodAreas_r(p->nodes[!s]);
    }
}

/*
=============
FindAreas_r

Just decend the tree, and for each node that hasn't had an
area set, flood fill out from there
=============
*/
static void FindAreas(node_t *node)
{
    auto leafs = FindOccupiedClusters(node);
    for (auto *leaf : leafs) {
        if (leaf->area)
            continue;

        // area portals are always only flooded into, never
        // out of
        if (leaf->contents.native == Q2_CONTENTS_AREAPORTAL)
            return;

        c_areas++;
        FloodAreas_r(leaf);
    }
}

/*
=============
SetAreaPortalAreas_r

Just decend the tree, and for each node that hasn't had an
area set, flood fill out from there
=============
*/
static void SetAreaPortalAreas_r(node_t *node)
{
    if (node->planenum != PLANENUM_LEAF) {
        SetAreaPortalAreas_r(node->children[0]);
        SetAreaPortalAreas_r(node->children[1]);
        return;
    }

    if (node->contents.native != Q2_CONTENTS_AREAPORTAL)
        return;

    if (node->area)
        return; // already set

    // grab the func_areanode entity
    mapentity_t *entity = node->markfaces[0]->src_entity;

    node->area = entity->portalareas[0];
    if (!entity->portalareas[1]) {
        // FIXME: entity #
        LogPrint("WARNING: areaportal entity doesn't touch two areas\n  Node Bounds: {} -> {}\n",
            qv::to_string(entity->bounds.mins()), qv::to_string(entity->bounds.maxs()));
        return;
    }
}

/*
=============
FloodAreas

Mark each leaf with an area, bounded by CONTENTS_AREAPORTAL
=============
*/
static void FloodAreas(mapentity_t *entity, node_t *headnode)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);
    FindAreas(headnode);
    SetAreaPortalAreas_r(headnode);
    LogPrint(LOG_STAT, "{:5} areas\n", c_areas);
}

/*
=============
EmitAreaPortals

=============
*/
static void EmitAreaPortals(node_t *headnode)
{
    LogPrint(LOG_PROGRESS, "---- {} ----\n", __func__);

    map.bsp.dareaportals.emplace_back();
    map.bsp.dareas.emplace_back();

    for (size_t i = 1; i <= c_areas; i++) {
        darea_t &area = map.bsp.dareas.emplace_back();
        area.firstareaportal = map.bsp.dareaportals.size();

        for (auto &e : map.entities) {

            if (!e.areaportalnum)
                continue;
            dareaportal_t &dp = map.bsp.dareaportals.emplace_back();

            if (e.portalareas[0] == i) {
                dp.portalnum = e.areaportalnum;
                dp.otherarea = e.portalareas[1];
            } else if (e.portalareas[1] == i) {
                dp.portalnum = e.areaportalnum;
                dp.otherarea = e.portalareas[0];
            }
        }

        area.numareaportals = map.bsp.dareaportals.size() - area.firstareaportal;
    }

    LogPrint(LOG_STAT, "{:5} numareas\n", map.bsp.dareas.size());
    LogPrint(LOG_STAT, "{:5} numareaportals\n", map.bsp.dareaportals.size());
}

winding_t BaseWindingForPlane(const qplane3d &p)
{
    return winding_t::from_plane(p, settings::worldextent.numberValue());
}

/*
===============
ProcessEntity
===============
*/
static void ProcessEntity(mapentity_t *entity, const int hullnum)
{
    int firstface;
    node_t *nodes;

    /* No map brushes means non-bmodel entity.
       We need to handle worldspawn containing no brushes, though. */
    if (!entity->nummapbrushes && entity != pWorldEnt())
        return;

    /*
     * func_group and func_detail entities get their brushes added to the
     * worldspawn
     */
    if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity))
        return;

    // Export a blank model struct, and reserve the index (only do this once, for all hulls)
    if (!entity->outputmodelnumber.has_value()) {
        entity->outputmodelnumber = map.bsp.dmodels.size();
        map.bsp.dmodels.emplace_back();
    }

    if (entity != pWorldEnt()) {
        if (entity == pWorldEnt() + 1)
            LogPrint(LOG_PROGRESS, "---- Internal Entities ----\n");

        std::string mod = fmt::format("*{}", entity->outputmodelnumber.value());

        if (options.fVerbose)
            PrintEntity(entity);

        if (hullnum <= 0)
            LogPrint(LOG_STAT, "     MODEL: {}\n", mod);
        SetKeyValue(entity, "model", mod.c_str());
    }

    /*
     * Init the entity
     */
    entity->brushes.clear();
    entity->bounds = {};

    /*
     * Convert the map brushes (planes) into BSP brushes (polygons)
     */
    LogPrint(LOG_PROGRESS, "---- Brush_LoadEntity ----\n");
    auto stats = Brush_LoadEntity(entity, hullnum);

    /* Print brush counts */
    if (stats.solid) {
        LogPrint(LOG_STAT, "     {:8} solid brushes\n", stats.solid);
    }
    if (stats.sky) {
        LogPrint(LOG_STAT, "     {:8} sky brushes\n", stats.sky);
    }
    if (stats.detail) {
        LogPrint(LOG_STAT, "     {:8} detail brushes\n", stats.detail);
    }
    if (stats.detail_illusionary) {
        LogPrint(LOG_STAT, "     {:8} detail illusionary brushes\n", stats.detail_illusionary);
    }
    if (stats.detail_fence) {
        LogPrint(LOG_STAT, "     {:8} detail fence brushes\n", stats.detail_fence);
    }
    if (stats.liquid) {
        LogPrint(LOG_STAT, "     {:8} liquid brushes\n", stats.liquid);
    }

    LogPrint(LOG_STAT, "     {:8} planes\n", map.numplanes());

    if (entity->brushes.empty() && hullnum) {
        PrintEntity(entity);
        FError("Entity with no valid brushes");
    }

    /*
     * Take the brush_t's and clip off all overlapping and contained faces,
     * leaving a perfect skin of the model with no hidden faces
     */
    std::vector<surface_t> surfs = CSGFaces(entity);

    if (settings::objexport.boolValue() && entity == pWorldEnt() && hullnum <= 0) {
        ExportObj_Surfaces("post_csg", surfs);
    }

    if (hullnum > 0) {
        nodes = SolidBSP(entity, surfs, true);
        if (entity == pWorldEnt() && !settings::nofill.boolValue()) {
            // assume non-world bmodels are simple
            PortalizeWorld(entity, nodes, hullnum);
            if (!settings::nofill.boolValue() && FillOutside(nodes, hullnum)) {
                // Free portals before regenerating new nodes
                FreeAllPortals(nodes);
                surfs = GatherNodeFaces(nodes);
                // make a really good tree
                nodes = SolidBSP(entity, surfs, false);

                DetailToSolid(nodes);
            }
        }
        ExportClipNodes(entity, nodes, hullnum);
    } else {
        /*
         * SolidBSP generates a node tree
         *
         * if not the world, make a good tree first the world is just
         * going to make a bad tree because the outside filling will
         * force a regeneration later.
         *
         * Forcing the good tree for the first pass on the world can
         * sometimes result in reduced marksurfaces at the expense of
         * longer processing time.
         */
        if (settings::forcegoodtree.boolValue())
            nodes = SolidBSP(entity, surfs, false);
        else
            nodes = SolidBSP(entity, surfs, entity == pWorldEnt());

        // build all the portals in the bsp tree
        // some portals are solid polygons, and some are paths to other leafs
        if (entity == pWorldEnt()) {
            // assume non-world bmodels are simple
            PortalizeWorld(entity, nodes, hullnum);
            if (!settings::nofill.boolValue() && FillOutside(nodes, hullnum)) {
                FreeAllPortals(nodes);

                // get the remaining faces together into surfaces again
                surfs = GatherNodeFaces(nodes);

                // merge polygons
                MergeAll(surfs);

                // make a really good tree
                nodes = SolidBSP(entity, surfs, false);

                // convert detail leafs to solid
                DetailToSolid(nodes);

                // make the real portals for vis tracing
                PortalizeWorld(entity, nodes, hullnum);

                if (!settings::notjunc.boolValue()) {
                    TJunc(entity, nodes);
                }
            }

            // Area portals
            if (options.target_game->id == GAME_QUAKE_II) {
                FloodAreas(entity, nodes);
                EmitAreaPortals(nodes);
            }

            FreeAllPortals(nodes);
        }

        // bmodels
        if (entity != pWorldEnt() && !settings::notjunc.boolValue()) {
            TJunc(entity, nodes);
        }

        // convert detail leafs to solid (in case we didn't make the call above)
        DetailToSolid(nodes);

        if (settings::objexport.boolValue() && entity == pWorldEnt()) {
            ExportObj_Nodes("pre_makefaceedges_plane_faces", nodes);
            ExportObj_Marksurfaces("pre_makefaceedges_marksurfaces", nodes);
        }

        firstface = MakeFaceEdges(entity, nodes);

        if (options.target_game->id == GAME_QUAKE_II) {
            ExportBrushList(entity, nodes, brush_offset);
        }

        ExportDrawNodes(entity, nodes, firstface);
    }

    FreeBrushes(entity);
    FreeNodes(nodes);
}

/*
=================
UpdateEntLump

=================
*/
static void UpdateEntLump(void)
{
    int modnum, i;
    char modname[10];
    mapentity_t *entity;

    LogPrint(LOG_STAT, "     Updating entities lump...\n");

    modnum = 1;
    for (i = 1; i < map.numentities(); i++) {
        entity = &map.entities.at(i);

        /* Special handling for misc_external_map.
           Duplicates some logic from ProcessExternalMapEntity. */
        bool is_misc_external_map = false;
        if (!Q_strcasecmp(ValueForKey(entity, "classname"), "misc_external_map")) {
            const char *new_classname = ValueForKey(entity, "_external_map_classname");

            SetKeyValue(entity, "classname", new_classname);
            SetKeyValue(entity, "origin", "0 0 0");

            /* Note: the classname could have switched to
             * a IsWorldBrushEntity entity (func_group, func_detail),
             * or a bmodel entity (func_wall
             */
            is_misc_external_map = true;
        }

        bool isBrushEnt = (entity->nummapbrushes > 0) || is_misc_external_map;
        if (!isBrushEnt)
            continue;

        if (IsWorldBrushEntity(entity) || IsNonRemoveWorldBrushEntity(entity))
            continue;

        snprintf(modname, sizeof(modname), "*%d", modnum);
        SetKeyValue(entity, "model", modname);
        modnum++;

        /* Do extra work for rotating entities if necessary */
        const char *classname = ValueForKey(entity, "classname");
        if (!strncmp(classname, "rotate_", 7))
            FixRotateOrigin(entity);
    }

    WriteEntitiesToString();
    UpdateBSPFileEntitiesLump();

    if (!options.fAllverbose) {
        options.fVerbose = false;
        log_mask &= ~((1 << LOG_STAT) | (1 << LOG_PROGRESS));
    }
}

/*
Actually writes out the final bspx BRUSHLIST lump
This lump replaces the clipnodes stuff for custom collision sizes.
*/
void BSPX_Brushes_Finalize(struct bspxbrushes_s *ctx)
{
    // Actually written in WriteBSPFile()
    map.exported_bspxbrushes = std::move(ctx->lumpdata);
}
void BSPX_Brushes_Init(struct bspxbrushes_s *ctx)
{
    ctx->lumpdata.clear();
}

/*
WriteBrushes
Generates a submodel's direct brush information to a separate file, so the engine doesn't need to depend upon specific
hull sizes
*/

static void BSPX_Brushes_AddModel(struct bspxbrushes_s *ctx, int modelnum, std::vector<brush_t> &brushes)
{
    bspxbrushes_permodel permodel {
        1,
        modelnum
    };

    for (auto &b : brushes) {
        permodel.numbrushes++;
        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;
            permodel.numfaces++;
        }
    }

    permodel.numbrushes = LittleLong(permodel.numbrushes);
    permodel.numfaces = LittleLong(permodel.numfaces);

    std::ostringstream str(std::ios_base::out | std::ios_base::binary);

    str << endianness<std::endian::little>;

    str <= permodel;

    for (auto &b : brushes) {
        bspxbrushes_perbrush perbrush {};

        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;
            perbrush.numfaces++;
        }

        perbrush.bounds = b.bounds;

        switch (b.contents.native) {
            // contents should match the engine.
            case CONTENTS_EMPTY: // really an error, but whatever
            case CONTENTS_SOLID: // these are okay
            case CONTENTS_WATER:
            case CONTENTS_SLIME:
            case CONTENTS_LAVA:
            case CONTENTS_SKY:
                if (b.contents.is_clip()) {
                    perbrush.contents = -8;
                } else {
                    perbrush.contents = b.contents.native;
                }
                break;
            //              case CONTENTS_LADDER:
            //                      perbrush.contents = -16;
            //                      break;
            default: {
                if (b.contents.is_clip()) {
                    perbrush.contents = -8;
                } else {
                    LogPrint("WARNING: Unknown contents: {}. Translating to solid.\n",
                        b.contents.to_string(options.target_game));
                    perbrush.contents = CONTENTS_SOLID;
                }
                break;
            }
        }

        str <= perbrush;

        for (auto &f : b.faces) {
            /*skip axial*/
            if (fabs(map.planes[f.planenum].normal[0]) == 1 || fabs(map.planes[f.planenum].normal[1]) == 1 ||
                fabs(map.planes[f.planenum].normal[2]) == 1)
                continue;

            bspxbrushes_perface perface;

            if (f.planeside) {
                perface = -map.planes[f.planenum];
            } else {
                perface = map.planes[f.planenum];
            }

            str <= std::tie(perface.normal, perface.dist);
        }
    }

    std::string data = str.str();
    ctx->lumpdata.insert(ctx->lumpdata.end(), (uint8_t *)data.data(), ((uint8_t *) data.data()) + data.size());
}

/* for generating BRUSHLIST bspx lump */
static void BSPX_CreateBrushList(void)
{
    mapentity_t *ent;
    int entnum;
    int modelnum;
    const char *mod;
    struct bspxbrushes_s ctx;

    if (!settings::wrbrushes.boolValue())
        return;

    BSPX_Brushes_Init(&ctx);

    for (entnum = 0; entnum < map.numentities(); entnum++) {
        ent = &map.entities.at(entnum);
        if (ent == pWorldEnt())
            modelnum = 0;
        else {
            mod = ValueForKey(ent, "model");
            if (*mod != '*')
                continue;
            modelnum = atoi(mod + 1);
        }

        ent->brushes.clear();

        Brush_LoadEntity(ent, HULL_COLLISION);

        if (ent->brushes.empty())
            continue; // non-bmodel entity

        BSPX_Brushes_AddModel(&ctx, modelnum, ent->brushes);
        FreeBrushes(ent);
    }

    BSPX_Brushes_Finalize(&ctx);
}

/*
=================
CreateSingleHull
=================
*/
static void CreateSingleHull(const int hullnum)
{
    int i;
    mapentity_t *entity;

    LogPrint("Processing hull {}...\n", hullnum);

    // for each entity in the map file that has geometry
    for (i = 0; i < map.numentities(); i++) {
        entity = &map.entities.at(i);
        ProcessEntity(entity, hullnum);
        if (!options.fAllverbose) {
            options.fVerbose = false; // don't print rest of entities
            log_mask &= ~((1 << LOG_STAT) | (1 << LOG_PROGRESS));
        }
    }
}

/*
=================
CreateHulls
=================
*/
static void CreateHulls(void)
{
    /* create the hulls sequentially */
    if (!options.fNoverbose) {
        options.fVerbose = true;
        log_mask |= (1 << LOG_STAT) | (1 << LOG_PROGRESS);
    }

    auto &hulls = options.target_game->get_hull_sizes();

    // game has no hulls, so we have to export brush lists and stuff.
    if (!hulls.size()) {
        CreateSingleHull(HULL_COLLISION);
    // only create hull 0 if fNoclip is set
    } else if (settings::noclip.boolValue()) {
        CreateSingleHull(0);
    // do all the hulls
    } else {
        for (size_t i = 0; i < hulls.size(); i++) {
            CreateSingleHull(i);
        }
    }
}

static bool wadlist_tried_loading = false;

void EnsureTexturesLoaded()
{
    if (wadlist_tried_loading)
        return;

    wadlist_tried_loading = true;

    const char *wadstring = ValueForKey(pWorldEnt(), "_wad");
    if (!wadstring[0])
        wadstring = ValueForKey(pWorldEnt(), "wad");
    if (!wadstring[0])
        LogPrint("WARNING: No wad or _wad key exists in the worldmodel\n");
    else
        WADList_Init(wadstring);

    if (!wadlist.size()) {
        if (wadstring[0])
            LogPrint("WARNING: No valid WAD filenames in worldmodel\n");

        /* Try the default wad name */
        std::filesystem::path defaultwad = options.szMapName;
        defaultwad.replace_extension("wad");

        WADList_Init(defaultwad.string().c_str());

        if (wadlist.size())
            LogPrint("Using default WAD: {}\n", defaultwad);
    }
}

/*
=================
ProcessFile
=================
*/
static void ProcessFile(void)
{
    // load brushes and entities
    LoadMapFile();

    if (settings::convertmapformat.enumValue() != conversion_t::none) {
        ConvertMapFile();
        return;
    }
    if (settings::onlyents.boolValue()) {
        UpdateEntLump();
        return;
    }

    // this can happen earlier if brush primitives are in use, because we need texture sizes then
    EnsureTexturesLoaded();

    // init the tables to be shared by all models
    BeginBSPFile();

    if (!options.fAllverbose) {
        options.fVerbose = false;
        log_mask &= ~((1 << LOG_STAT) | (1 << LOG_PROGRESS));
    }

    // calculate extents, if required
    if (!settings::worldextent.numberValue()) {
        CalculateWorldExtent();
    }

    // create hulls!
    CreateHulls();

    WriteEntitiesToString();
    WADList_Process();
    BSPX_CreateBrushList();
    FinishBSPFile();

    wadlist.clear();
}

namespace settings
{
    inline void set_target_version(const bspversion_t *version)
    {
        if (options.target_version) {
            FError("BSP version was set by multiple flags; currently {}, tried to change to {}\n", options.target_version->name, version->name);
        }
    }

    inline void compile_settings()
    {
        // side effects from common
        if (log_mask & (1 << LOG_VERBOSE)) {
            options.fAllverbose = true;
        }

        if ((log_mask & ((1 << LOG_PERCENT) | (1 << LOG_STAT) | (1 << LOG_PROGRESS))) == 0) {
            options.fNoverbose = true;
        }

        // set target BSP type
        if (hlbsp.boolValue()) {
            set_target_version(&bspver_hl);
        }

        if (q2bsp.boolValue()) {
            set_target_version(&bspver_q2);
        }

        if (qbism.boolValue()) {
            set_target_version(&bspver_qbism);
        }

        if (bsp2.boolValue()) {
            set_target_version(&bspver_bsp2);
        }

        if (bsp2rmq.boolValue()) {
            set_target_version(&bspver_bsp2rmq);
        }

        // if we wanted hexen2, update it now
        if (hexen2.boolValue()) {
            if (options.target_version == &bspver_bsp2) {
                options.target_version = &bspver_h2bsp2;
            } else if (options.target_version == &bspver_bsp2rmq) {
                options.target_version = &bspver_h2bsp2rmq;
            } else {
                options.target_version = &bspver_h2;
            }
        } else {
            if (!options.target_version) {
                options.target_version = &bspver_q1;
            }
        }

        // update target game
        options.target_game = options.target_version->game;
    }
};

/*
==================
InitQBSP
==================
*/
static void InitQBSP(int argc, const char **argv)
{
    settings::globalSettings.usage = "qbsp performs geometric level processing of Quake .MAP files to create\nQuake .BSP files.\n\n";
    settings::globalSettings.programName = fs::path(argv[0]).stem().string();
    settings::globalSettings.remainderName = "sourcefile.map [destfile.bsp]";
    settings::register_settings();

    settings::globalSettings.printHelp();
    
    settings::compile_settings();

    if (auto file = fs::load("qbsp.ini")) {
        LogPrint("Loading options from qbsp.ini\n");
        //ParseOptions(reinterpret_cast<char *>(file->data()));
    }

    //ParseOptions(szBuf);

    //if (options.szMapName.empty())
    //    PrintOptions();

    options.szMapName.replace_extension("map");

    // The .map extension gets removed right away anyways...
    if (options.szBSPName.empty())
        options.szBSPName = options.szMapName;

    /* Start logging to <bspname>.log */
    options.szBSPName.replace_extension("log");
    InitLog(options.szBSPName);

    LogPrintSilent(IntroString);

    /* If no wadpath given, default to the map directory */
    if (settings::wadpaths.pathsValue().empty()) {
        settings::wadpath wp { options.szMapName.parent_path(), false };

        // If options.szMapName is a relative path, StrippedFilename will return the empty string.
        // In that case, don't add it as a wad path.
        if (!wp.path.empty()) {
            settings::wadpaths.addPath(wp);
        }
    }

    // Remove already existing files
    if (!settings::onlyents.boolValue() && settings::convertmapformat.enumValue() == conversion_t::none) {
        options.szBSPName.replace_extension("bsp");
        remove(options.szBSPName);

        // Probably not the best place to do this
        LogPrint("Input file: {}\n", options.szMapName);
        LogPrint("Output file: {}\n\n", options.szBSPName);

        options.szBSPName.replace_extension("prt");
        remove(options.szBSPName);

        options.szBSPName.replace_extension("pts");
        remove(options.szBSPName);

        options.szBSPName.replace_extension("por");
        remove(options.szBSPName);
    }

    // onlyents might not load this yet
    if (options.target_game) {
        options.target_game->init_filesystem(options.szMapName);
    }
}

#include <fstream>

/*
==================
main
==================
*/
int qbsp_main(int argc, const char **argv)
{
    LogPrint(IntroString);

    InitQBSP(argc, argv);

    // do it!
    auto start = I_FloatTime();
    ProcessFile();
    auto end = I_FloatTime();

    LogPrint("\n{:.3} seconds elapsed\n", (end - start));

    //      FreeAllMem();
    //      PrintMem();

    CloseLog();

    return 0;
}
