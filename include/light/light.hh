/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <common/cmdlib.hh>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/log.hh>
#include <common/threads.hh>
#include <common/polylib.hh>
#include <common/imglib.hh>
#include <common/settings.hh>

#include <light/litfile.hh>
#include <light/trace.hh>

#include <vector>
#include <map>
#include <set>
#include <string>
#include <cassert>
#include <limits>
#include <sstream>

#include <common/qvec.hh>

constexpr vec_t ON_EPSILON = 0.1;
constexpr vec_t ANGLE_EPSILON = 0.001;
constexpr vec_t EQUAL_EPSILON = 0.001;

// FIXME: use maximum dimension of level
constexpr vec_t MAX_SKY_DIST = 1000000;

struct lightsample_t
{
    qvec3d color, direction;
};

// CHECK: isn't average a bad algorithm for color brightness?
template<typename T>
constexpr float LightSample_Brightness(const T &color)
{
    return ((color[0] + color[1] + color[2]) / 3.0);
}

/**
 * A directional light, emitted from "sky*" textured faces.
 */
class sun_t
{
public:
    qvec3d sunvec;
    vec_t sunlight;
    qvec3d sunlight_color;
    bool dirt;
    float anglescale;
    int style;
    std::string suntexture;
};

/* for vanilla this would be 18. some engines allow higher limits though, which will be needed if we're scaling lightmap
 * resolution. */
/*with extra sampling, lit+lux etc, we need at least 46mb space per thread. yes, that's a lot. on the plus side,
 * it doesn't affect bsp complexity (actually, can simplify it a little)*/
constexpr size_t MAXDIMENSION = 255 + 1;

struct texorg_t
{
    qmat4x4f texSpaceToWorld;
    const gtexinfo_t *texinfo;
    vec_t planedist;
};

class modelinfo_t;
class globalconfig_t;

class lightmap_t
{
public:
    int style;
    lightsample_t *samples; // new'ed array of numpoints   //FIXME: this is stupid, we shouldn't need to allocate
                            // extra data here for -extra4
};

using lightmapdict_t = std::vector<lightmap_t>;

/*Warning: this stuff needs explicit initialisation*/
struct lightsurf_t
{
    const globalconfig_t *cfg;
    const modelinfo_t *modelinfo;
    const mbsp_t *bsp;
    const mface_t *face;
    /* these take precedence the values in modelinfo */
    vec_t minlight;
    qvec3d minlight_color;
    bool nodirt;

    qplane3d plane;
    qvec3d snormal;
    qvec3d tnormal;

    /* 16 in vanilla. engines will hate you if this is not power-of-two-and-at-least-one */
    float lightmapscale;
    bool curved; /*normals are interpolated for smooth lighting*/

    int texmins[2];
    int texsize[2];
    qvec2d exactmid;
    qvec3d midpoint;

    int numpoints;
    qvec3d *points; // new'ed array of numpoints
    qvec3d *normals; // new'ed array of numpoints
    bool *occluded; // new'ed array of numpoints
    int *realfacenums; // new'ed array of numpoints

    /*
     raw ambient occlusion amount per sample point, 0-1, where 1 is
     fully occluded. dirtgain/dirtscale are not applied yet
     */
    float *occlusion; // new'ed array of numpoints

    /* for sphere culling */
    qvec3d origin;
    vec_t radius;
    /* for AABB culling */
    aabb3d bounds = qvec3d(0);

    // for radiosity
    qvec3d radiosity;
    qvec3d texturecolor;

    /* stuff used by CalcPoint */
    texorg_t texorg;
    int width, height;

    /* for lit water. receive light from either front or back. */
    bool twosided;

    // ray batch stuff
    raystream_occlusion_t *occlusion_stream;
    raystream_intersection_t *intersection_stream;

    lightmapdict_t lightmapsByStyle;
};

/* debug */

enum class debugmodes
{
    none = 0,
    phong,
    phong_obj,
    dirt,
    bounce,
    bouncelights,
    debugoccluded,
    debugneighbours,
    phong_tangents,
    phong_bitangents
};

extern debugmodes debugmode;

/* tracelist is a std::vector of pointers to modelinfo_t to use for LOS tests */
extern std::vector<const modelinfo_t *> tracelist;
extern std::vector<const modelinfo_t *> selfshadowlist;
extern std::vector<const modelinfo_t *> shadowworldonlylist;
extern std::vector<const modelinfo_t *> switchableshadowlist;

extern int numDirtVectors;

// other flags

extern bool dirt_in_use; // should any dirtmapping take place? set in SetupDirt

constexpr qvec3d vec3_white{255};

extern int dump_facenum;
extern int dump_vertnum;

class modelinfo_t
{
    static constexpr vec_t DEFAULT_PHONG_ANGLE = 89.0;

public:
    const mbsp_t *bsp;
    const dmodelh2_t *model;
    float lightmapscale;
    qvec3d offset{};

    settings::lockable_scalar minlight{"minlight", 0};
    settings::lockable_scalar shadow{"shadow", 0};
    settings::lockable_scalar shadowself{settings::strings{"shadowself", "selfshadow"}, 0};
    settings::lockable_scalar shadowworldonly{"shadowworldonly", 0};
    settings::lockable_scalar switchableshadow{"switchableshadow", 0};
    settings::lockable_int32 switchshadstyle{"switchshadstyle", 0};
    settings::lockable_scalar dirt{"dirt", 0};
    settings::lockable_scalar phong{"phong", 0};
    settings::lockable_scalar phong_angle{"phong_angle", 0};
    settings::lockable_scalar alpha{"alpha", 1.0};
    settings::lockable_color minlight_color{settings::strings{"minlight_color", "mincolor"}, 255.0, 255.0, 255.0};
    settings::lockable_bool lightignore{"lightignore", false};

    settings::dict settings{&minlight, &shadow, &shadowself, &shadowworldonly, &switchableshadow, &switchshadstyle,
        &dirt, &phong, &phong_angle, &alpha, &minlight_color, &lightignore};

    float getResolvedPhongAngle() const
    {
        const float s = phong_angle.value();
        if (s != 0) {
            return s;
        }
        if (phong.value() > 0) {
            return DEFAULT_PHONG_ANGLE;
        }
        return 0;
    }

    bool isWorld() const { return &bsp->dmodels[0] == model; }

    modelinfo_t(const mbsp_t *b, const dmodelh2_t *m, float lmscale) : bsp{b}, model{m}, lightmapscale{lmscale} { }
};

//
// worldspawn keys / command-line settings
//

namespace settings
{
extern settings_group worldspawn_group;

extern fs::path sourceMap;
extern lockable_bool surflight_dump;
extern lockable_scalar surflight_subdivide;
extern lockable_scalar gate;
extern lockable_int32 sunsamples;
extern lockable_bool arghradcompat;
extern lockable_bool nolighting;
extern lockable_bool highlightseams;

// slight modification to lockable_numeric that supports
// a default value if a non-number is supplied after parsing
class lockable_soft : public lockable_int32
{
public:
    using lockable_int32::lockable_int32;

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        try {
            int32_t f = static_cast<int32_t>(std::stoull(parser.token));

            setValueFromParse(f, locked);

            return true;
        }
        catch (std::exception &) {
            // if we didn't provide a (valid) number, then
            // assume it's meant to be the default of -1
            if (parser.token[0] == '-') {
                setValueFromParse(-1, locked);
                return true;
            } else {
                return false;
            }
        }
    }

    virtual std::string format() const { return "[n]"; }
};

extern lockable_soft soft;

class lockable_extra : public lockable_value<int32_t>
{
public:
    using lockable_value::lockable_value;

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false)
    {
        if (settingName.back() == '4') {
            setValueFromParse(4, locked);
        } else {
            setValueFromParse(2, locked);
        }

        return true;
    }

    virtual std::string stringValue() const override { return std::to_string(_value); };

    virtual std::string format() const override { return ""; };
};

extern lockable_extra extra;
extern lockable_bool novisapprox;
extern lockable_bool litonly;
extern lockable_bool nolights;
} // namespace settings

class globalconfig_t
{
public:
    settings::lockable_scalar scaledist{"dist", 1.0, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_scalar rangescale{"range", 0.5, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_scalar global_anglescale{
        settings::strings{"anglescale", "anglesense"}, 0.5, 0.0, 1.0, &settings::worldspawn_group};
    settings::lockable_scalar lightmapgamma{"gamma", 1.0, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_bool addminlight{"addmin", false, &settings::worldspawn_group};
    settings::lockable_scalar minlight{settings::strings{"light", "minlight"}, 0, &settings::worldspawn_group};
    settings::lockable_color minlight_color{
        settings::strings{"minlight_color", "mincolor"}, 255.0, 255.0, 255.0, &settings::worldspawn_group};
    settings::lockable_bool spotlightautofalloff{"spotlightautofalloff", false, &settings::worldspawn_group}; // mxd
    settings::lockable_int32 compilerstyle_start{
        "compilerstyle_start", 32, &settings::worldspawn_group}; // start index for switchable light styles, default 32

    /* dirt */
    settings::lockable_bool globalDirt{settings::strings{"dirt", "dirty"}, false,
        &settings::worldspawn_group}; // apply dirt to all lights (unless they override it) + sunlight + minlight?
    settings::lockable_scalar dirtMode{"dirtmode", 0.0f, &settings::worldspawn_group};
    settings::lockable_scalar dirtDepth{
        "dirtdepth", 128.0, 1.0, std::numeric_limits<vec_t>::infinity(), &settings::worldspawn_group};
    settings::lockable_scalar dirtScale{"dirtscale", 1.0, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_scalar dirtGain{"dirtgain", 1.0, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_scalar dirtAngle{"dirtangle", 88.0, 1.0, 90.0, &settings::worldspawn_group};
    settings::lockable_bool minlightDirt{
        "minlight_dirt", false, &settings::worldspawn_group}; // apply dirt to minlight?

    /* phong */
    settings::lockable_bool phongallowed{"phong", true, &settings::worldspawn_group};
    settings::lockable_scalar phongangle{"phong_angle", 0, &settings::worldspawn_group};

    /* bounce */
    settings::lockable_bool bounce{"bounce", false, &settings::worldspawn_group};
    settings::lockable_bool bouncestyled{"bouncestyled", false, &settings::worldspawn_group};
    settings::lockable_scalar bouncescale{"bouncescale", 1.0, 0.0, 100.0, &settings::worldspawn_group};
    settings::lockable_scalar bouncecolorscale{"bouncecolorscale", 0.0, 0.0, 1.0, &settings::worldspawn_group};

    /* Q2 surface lights (mxd) */
    settings::lockable_scalar surflightscale{
        "surflightscale", 0.3, &settings::worldspawn_group}; // Strange defaults to match arghrad3 look...
    settings::lockable_scalar surflightbouncescale{"surflightbouncescale", 0.1, &settings::worldspawn_group};
    settings::lockable_scalar surflightsubdivision{settings::strings{"surflightsubdivision", "choplight"}, 16.0, 1.0,
        8192.0, &settings::worldspawn_group}; // "choplight" - arghrad3 name

    /* sunlight */
    /* sun_light, sun_color, sun_angle for http://www.bspquakeeditor.com/arghrad/ compatibility */
    settings::lockable_scalar sunlight{
        settings::strings{"sunlight", "sun_light"}, 0.0, &settings::worldspawn_group}; /* main sun */
    settings::lockable_color sunlight_color{
        settings::strings{"sunlight_color", "sun_color"}, 255.0, 255.0, 255.0, &settings::worldspawn_group};
    settings::lockable_scalar sun2{"sun2", 0.0, &settings::worldspawn_group}; /* second sun */
    settings::lockable_color sun2_color{"sun2_color", 255.0, 255.0, 255.0, &settings::worldspawn_group};
    settings::lockable_scalar sunlight2{"sunlight2", 0.0, &settings::worldspawn_group}; /* top sky dome */
    settings::lockable_color sunlight2_color{
        settings::strings{"sunlight2_color", "sunlight_color2"}, 255.0, 255.0, 255.0, &settings::worldspawn_group};
    settings::lockable_scalar sunlight3{"sunlight3", 0.0, &settings::worldspawn_group}; /* bottom sky dome */
    settings::lockable_color sunlight3_color{
        settings::strings{"sunlight3_color", "sunlight_color3"}, 255.0, 255.0, 255.0, &settings::worldspawn_group};
    settings::lockable_scalar sunlight_dirt{"sunlight_dirt", 0.0, &settings::worldspawn_group};
    settings::lockable_scalar sunlight2_dirt{"sunlight2_dirt", 0.0, &settings::worldspawn_group};
    settings::lockable_mangle sunvec{settings::strings{"sunlight_mangle", "sun_mangle", "sun_angle"}, 0.0, -90.0, 0.0,
        &settings::worldspawn_group}; /* defaults to straight down */
    settings::lockable_mangle sun2vec{
        "sun2_mangle", 0.0, -90.0, 0.0, &settings::worldspawn_group}; /* defaults to straight down */
    settings::lockable_scalar sun_deviance{"sunlight_penumbra", 0.0, 0.0, 180.0, &settings::worldspawn_group};
    settings::lockable_vec3 sky_surface{settings::strings{"sky_surface", "sun_surface"}, 0, 0, 0,
        &settings::worldspawn_group} /* arghrad surface lights on sky faces */;

    settings::dict settings{&scaledist, &rangescale, &global_anglescale, &lightmapgamma, &addminlight, &minlight,
        &minlight_color,
        &spotlightautofalloff, // mxd
        &compilerstyle_start, &globalDirt, &dirtMode, &dirtDepth, &dirtScale, &dirtGain, &dirtAngle, &minlightDirt,
        &phongallowed, &bounce, &bouncestyled, &bouncescale, &bouncecolorscale, &surflightscale, &surflightbouncescale,
        &surflightsubdivision, // mxd
        &sunlight, &sunlight_color, &sun2, &sun2_color, &sunlight2, &sunlight2_color, &sunlight3, &sunlight3_color,
        &sunlight_dirt, &sunlight2_dirt, &sunvec, &sun2vec, &sun_deviance, &sky_surface};
};

extern uint8_t *filebase;
extern uint8_t *lit_filebase;
extern uint8_t *lux_filebase;

extern std::vector<surfflags_t> extended_texinfo_flags;

// public functions

void SetGlobalSetting(std::string name, std::string value, bool cmdline);
void FixupGlobalSettings(void);
void GetFileSpace(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int size);
void GetFileSpace_PreserveOffsetInBsp(uint8_t **lightdata, uint8_t **colordata, uint8_t **deluxdata, int lightofs);
const modelinfo_t *ModelInfoForModel(const mbsp_t *bsp, int modelnum);
/**
 * returns nullptr for "skip" faces
 */
const modelinfo_t *ModelInfoForFace(const mbsp_t *bsp, int facenum);
const img::texture *Face_Texture(const mbsp_t *bsp, const mface_t *face);
int light_main(int argc, const char **argv);
