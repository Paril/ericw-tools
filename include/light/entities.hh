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

#include <map>
#include <string>
#include <vector>

#include <common/entdata.h>
#include <common/mathlib.hh>
#include <common/bspfile.hh>
#include <common/imglib.hh>
#include <light/light.hh>

constexpr vec_t DEFAULTLIGHTLEVEL = 300.0;

/*
 * Light attenuation formalae
 * (relative to distance 'x' from the light source)
 */
constexpr vec_t LF_SCALE = 128;
enum light_formula_t
{
    LF_LINEAR = 0, /* Linear (x) (DEFAULT) */
    LF_INVERSE = 1, /* Inverse (1/x), scaled by 1/128 */
    LF_INVERSE2 = 2, /* Inverse square (1/(x^2)), scaled by 1/(128^2) */
    LF_INFINITE = 3, /* No attenuation, same brightness at any distance */
    LF_LOCALMIN = 4, /* No attenuation, non-additive minlight effect within
                        line of sight of the light source. */
    LF_INVERSE2A = 5, /* Inverse square, with distance adjusted to avoid
                         exponentially bright values near the source.
                           (1/(x+128)^2), scaled by 1/(128^2) */
    LF_COUNT
};

class light_t
{
public:
    bool spotlight = false;
    qvec3d spotvec{}; // computed
    float spotfalloff = 0;
    float spotfalloff2 = 0;
    const img::texture *projectedmip = nullptr; /*projected texture*/ // mxd. miptex_t -> rgba_miptex_t
    std::array<vec_t, 16> projectionmatrix{}; /*matrix used to project the specified texture. already contains origin.*/

    const entdict_t *epairs = nullptr;

    const entdict_t *targetent = nullptr;

    bool generated = false; // if true, don't write to the bsp

    aabb3d bounds;

    settings::lockable_scalar light{"light", DEFAULTLIGHTLEVEL};
    settings::lockable_scalar atten{"wait", 1.0, 0.0, std::numeric_limits<vec_t>::max()};
    settings::lockable_numeric<light_formula_t> formula{"delay", LF_LINEAR, LF_LINEAR, LF_INVERSE2A};
    settings::lockable_scalar spotangle{"angle", 40.0};
    settings::lockable_scalar spotangle2{"softangle", 0.0};
    settings::lockable_numeric<int32_t> style{"style", 0.0, 0, 254};
    settings::lockable_scalar anglescale{settings::strings{"anglesense", "anglescale"}, -1.0}; // fallback to worldspawn
    settings::lockable_scalar dirtscale{"dirtscale", 0.0};
    settings::lockable_scalar dirtgain{"dirtgain", 0};
    settings::lockable_scalar dirt{"dirt", 0};
    settings::lockable_scalar deviance{"deviance", 0};
    settings::lockable_int32 samples{"samples", 16, 0, std::numeric_limits<int32_t>::max()};
    settings::lockable_scalar projfov{"project_fov", 90};
    settings::lockable_scalar bouncescale{"bouncescale", 1.0};
    settings::lockable_scalar dirt_off_radius{"dirt_off_radius", 0.0};
    settings::lockable_scalar dirt_on_radius{"dirt_on_radius", 0.0};
    settings::lockable_bool sun{"sun", false}; // mxd
    settings::lockable_bool sunlight2{"sunlight2", 0};
    settings::lockable_bool sunlight3{"sunlight3", 0};
    settings::lockable_scalar falloff{"falloff", 0.0, 0.0, std::numeric_limits<vec_t>::max()}; // mxd
    settings::lockable_bool bleed{"bleed", false};
    settings::lockable_vec3 origin{"origin", 0, 0, 0};
    settings::lockable_color color{"color", 255.0, 255.0, 255.0};
    settings::lockable_vec3 mangle{"mangle", 0, 0, 0}; // not transformed to vec
    settings::lockable_vec3 projangle{"project_mangle", 20, 0, 0}; // not transformed to vec
    settings::lockable_string project_texture{"project_texture", ""};
    settings::lockable_string suntexture{"suntexture", ""};
    settings::lockable_bool nostaticlight{"nostaticlight", false};

    settings::dict settings{&light, &atten, &formula, &spotangle, &spotangle2, &style, &bleed, &anglescale, &dirtscale,
        &dirtgain, &dirt, &deviance, &samples, &projfov, &bouncescale, &dirt_off_radius, &dirt_on_radius,
        &sun, // mxd
        &sunlight2, &sunlight3,
        &falloff, // mxd
        &origin, &color, &mangle, &projangle, &project_texture, &suntexture, &nostaticlight};

    const char *classname() const;

    const light_formula_t &getFormula() const { return formula.value(); }

    void initAABB() { bounds = origin.value(); }

    void expandAABB(const qvec3d &pt) { bounds += pt; }
};

/*
 * atten:
 *    Takes a float as a value (default 1.0).
 *    This reflects how fast a light fades with distance.
 *    For example a value of 2 will fade twice as fast, and a value of 0.5
 *      will fade half as fast. (Just like arghlite)
 *
 *  mangle:
 *    If the entity is a light, then point the spotlight in this direction.
 *    If it is the worldspawn, then this is the sunlight mangle
 *
 *  lightcolor:
 *    Stores the RGB values to determine the light color
 */

std::string TargetnameForLightStyle(int style);
const std::vector<light_t> &GetLights();
const std::vector<sun_t> &GetSuns();

const entdict_t *FindEntDictWithKeyPair(const std::string &key, const std::string &value);
const char *ValueForKey(const light_t *ent, const char *key);
qvec3d EntDict_VectorForKey(const entdict_t &ent, const std::string &key);

void SetWorldKeyValue(const std::string &key, const std::string &value);
const std::string &WorldValueForKey(const std::string &key);

void LoadEntities(const globalconfig_t &cfg, const mbsp_t *bsp);
void SetupLights(const globalconfig_t &cfg, const mbsp_t *bsp);
bool ParseLightsFile(const std::filesystem::path &fname);
void WriteEntitiesToString(const globalconfig_t &cfg, mbsp_t *bsp);
aabb3d EstimateVisibleBoundsAtPoint(const qvec3d &point);

bool EntDict_CheckNoEmptyValues(const mbsp_t *bsp, const entdict_t &entdict);

bool EntDict_CheckTargetKeysMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);

bool EntDict_CheckTargetnameKeyMatched(
    const mbsp_t *bsp, const entdict_t &entity, const std::vector<entdict_t> &all_edicts);
