/*
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#ifndef XOBJDEFS_H
#define XOBJDEFS_H

#include <string>
#include <vector>
using namespace std;

#include "ObjPointPool.h"

/****************************************************************************************
 * OBJ 2/7
 ****************************************************************************************
 *
 * Notes: no end command is written to the command stream.
 * LODs are inline as attributes.  The absense of LOD attributes means only a default LOD.
 *
 * Multiple primitives like lines, quads and tris must only have 2 3 and 4 vertices,
 * respectively for file-write.
 *
 */

enum
{

    type_None = 0,
    type_PtLine,  // OBJ 7
    type_Poly,    // OBJ 7
    type_Attr,    // OBJ 7 or 8
    type_Indexed, // OBJ 8
    type_Anim,    // OBJ 8
    type_Cust     // OBJ8
};

enum
{
    // OBJ7 commands
    obj_End = 0,
    obj_Light,
    obj_Line,
    obj_Tri,
    obj_Quad,
    obj_Quad_Hard,
    obj_Quad_Cockpit,
    obj_Movie,
    obj_Polygon,
    obj_Quad_Strip,
    obj_Tri_Strip,
    obj_Tri_Fan,

    // Shared commands
    attr_Shade_Flat,
    attr_Shade_Smooth,
    attr_Ambient_RGB,
    attr_Diffuse_RGB,
    attr_Emission_RGB,
    attr_Specular_RGB,
    attr_Shiny_Rat,
    attr_No_Depth,
    attr_Depth,
    attr_LOD,
    attr_Reset,
    attr_Cull,
    attr_NoCull,
    attr_Offset,
    obj_Smoke_Black,
    obj_Smoke_White,

    // OBJ8 commands
    obj8_Tris,
    obj8_Lines,
    obj8_Lights,

    attr_Tex_Normal,
    attr_Tex_Cockpit,
    attr_No_Blend,
    attr_Blend,
    attr_Hard,
    attr_Hard_Deck,
    attr_No_Hard,

    anim_Begin,
    anim_End,
    anim_Rotate,
    anim_Translate,

    // 850 commands
    obj8_LightCustom, // all in name??  param is pos?
    obj8_LightNamed,  // name has light name, param is pos
    attr_Layer_Group, // name has group name, param[0] has offset
    anim_Hide,        // only v1 and v2 are used
    anim_Show,

    // 900 commands
    attr_Tex_Cockpit_Subregion,
    // 920 commands
    attr_Manip_None,
    attr_Manip_Drag_2d,
    attr_Manip_Drag_Axis,
    attr_Manip_Command,
    attr_Manip_Command_Axis,
    attr_Manip_Noop,
    attr_Manip_Push,
    attr_Manip_Radio,
    attr_Manip_Toggle,
    attr_Manip_Delta,
    attr_Manip_Wrap,
    // 930 commands
    attr_Light_Level,
    attr_Light_Level_Reset,
    attr_Draw_Disable,
    attr_Draw_Enable,
    attr_Solid_Wall,
    attr_No_Solid_Wall,

    // 1000 commands
    attr_Draped,
    attr_NoDraped,
    /* LIGHT_SPILL_CUSTOM */
    /* ATTR_shadow_blend */
    /* ATTR_no_shadow */
    /* ATTR_shadow */

    attr_Manip_Drag_Axis_Pix,

    // 1050 commands
    attr_Manip_Command_Knob,
    attr_Manip_Command_Switch_Up_Down,
    attr_Manip_Command_Switch_Left_Right,
    attr_Manip_Axis_Knob,
    attr_Manip_Axis_Switch_Up_Down,
    attr_Manip_Axis_Switch_Left_Right,

    // 1100 commands
    attr_Cockpit_Device,
    attr_Cockpit_Lit_Only,
    attr_Manip_Drag_Rotate,
    attr_Manip_Command_Knob2,
    attr_Manip_Command_Switch_Up_Down2,
    attr_Manip_Command_Switch_Left_Right2,

    // Future particle system...
    attr_Emitter,

    // v11
    attr_Magnet,

    attr_Max
};

struct cmd_info
{
    int cmd_id;
    int cmd_type;
    const char* name;
    int elem_count;
    int v7;
    int v8;
};

extern cmd_info gCmds[];
extern int gCmdCount;

struct vec_tex
{
    float v[3];
    float st[2];
};

struct vec_rgb
{
    float v[3];
    float rgb[3];
};

struct XObjCmd
{

    int cmdType; // Are we a line, poly or attribute?
    int cmdID;   // What command are we?

    vector<float> attributes;
    vector<vec_tex> st;
    vector<vec_rgb> rgb;
};

struct XObj
{

    string texture;
    vector<XObjCmd> cmds;
};

int FindObjCmd(const char* inToken, bool obj_8);
int FindIndexForCmd(int inCmd);

/****************************************************************************************
 * OBJ 8
 ****************************************************************************************
 *
 * Notes: if the object has only a default LOD, the LOD range will be 0.0 to 0.0.
 *
 * The library does not merge consecutive-indexed tri commands on read or write.
 *
 */

// alternate implementation of ObjPointPool, but without point merging capabilities.
// For WED, we don't need it to optimize pools and its taking a LOT of extra time.

class ObjDataVec
{
public:
    ObjDataVec() : mDepth(8){};
    ~ObjDataVec(){};

    void clear(int depth); // Set zero points and number of floats per pt
    void resize(int pts);  // Set a lot of pts

    int append(const float pt[]);      // Add a pt to the end
    void set(int n, const float pt[]); // Set an existing pt

    int count(void) const;
    const float* get(int index) const;

    void get_minmax(float minCoords[3], float maxCoords[3]) const;

private:
    vector<float> mData;
    int mDepth;
};

struct XObjKey
{
    XObjKey()
    {
        key = 0.0f;
        v[0] = v[1] = v[2] = 0.0f;
    }
    float key;
    float v[3]; // angle for rotation, XYZ for translation

    bool eq_key(const XObjKey& rhs) const
    {
        return key == rhs.key;
    }
    bool eq_val(const XObjKey& rhs) const
    {
        return v[0] == rhs.v[0] && v[1] == rhs.v[1] && v[2] == rhs.v[2];
    }
    bool eq(const XObjKey& rhs) const
    {
        return eq_key(rhs) && eq_val(rhs);
    }
};

struct XObjDetentRange
{
    float lo, hi;
    float height;
};

struct XObjAnim8
{
    int cmd;
    string dataref;
    float axis[3]; // Used for rotations
    float loop;    // If not 0, modulo factor
    vector<XObjKey> keyframes;
};

struct XObjManip8
{
    XObjManip8() : v1_min(0.0f), v1_max(0.0f), v2_min(0.0f), v2_max(0.0f), mouse_wheel_delta(0.0f)
    {
        axis[0] = axis[1] = axis[2] = 0.0f;
    }
    string dataref1; // Commands for, cmd manips!
    string dataref2;
    float centroid[3];
    float axis[3];
    float angle_min;
    float angle_max;
    float lift;
    float v1_min, v1_max;
    float v2_min, v2_max;
    string cursor;
    string tooltip;
    float mouse_wheel_delta;

    vector<XObjKey> rotation_key_frames;
    vector<XObjDetentRange> detents;
};

struct XObjEmitter8
{
    string name;
    string dataref;
    float x, y, z;
    float psi, the, phi;
    float v_min, v_max;
};

struct XObjCmd8
{
    int cmd;
    float params[12];
    string name;
    int idx_offset;
    int idx_count;
};

struct XObjLOD8
{
    float lod_near;
    float lod_far;
    vector<XObjCmd8> cmds;
};

struct XObjPanelRegion8
{
    int left;
    int bottom;
    int right;
    int top;
};

struct XObj8
{
    string texture;
    string texture_normal_map;
    string texture_lit;
    // AC unused	string					texture_nrm;
    string texture_draped;
    int use_metalness;
    int glass_blending;

    string particle_system;
    vector<XObjPanelRegion8> regions;
    vector<int> indices;
#if WED
    ObjDataVec geo_tri;
    ObjDataVec geo_lines;
    ObjDataVec geo_lights;
#else
    ObjPointPool geo_tri;
    ObjPointPool geo_lines;
    ObjPointPool geo_lights;
#endif
#if XOBJ8_USE_VBO
    unsigned int geo_VBO;
    unsigned int idx_VBO;
    bool short_idx;
    XObj8(void) : geo_VBO(0), idx_VBO(0), short_idx(false){};
#endif
    vector<XObjAnim8> animation;
    vector<XObjManip8> manips;
    vector<XObjEmitter8> emitters;
    vector<XObjLOD8> lods;

    float xyz_min[3];
    float xyz_max[3];
    float fixed_heading;
    float viewpoint_height;
    string description;
};

#endif
