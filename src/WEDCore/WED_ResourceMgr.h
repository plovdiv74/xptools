/*
 * Copyright (c) 2009, Laminar Research.
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

#ifndef WED_ResourceMgr_H
#define WED_ResourceMgr_H

/*
    WED_ResourceMgr - THEORY OF OPERATION

    This class maintains a lazy-create cache or art asset previews.  It currently supports two art asset classes:
    - OBJ
    - POL

    In the case of OBJ, we use the OBJ_ package for preview and data management, thus an OBJ preview is an XObj8 struct.
    For .pol since there is no package for .pol preview (since it is somewhat trivial) we define a struct.

    HERE'S THE HACK

    Traditionally the UI interface for WED is firewalled off from the document class/implementation using a purely
   virtual abstract interface.  (See ILibrarian.h for example.)  But...I have not had the time to do this here yet.  So
    WED_LibraryMgr is used directly as sort of its own interface.  This is definitely a hack, it's definitely "wrong",
   but it's also definitely not very dangerous at this point in the code's development - that is, WED is not so big that
   this represents a scalability issue.

*/

#include "GUI_Listener.h"
#include "GUI_Broadcaster.h"
#include "IBase.h"
#include "XObjDefs.h"
#include "CompGeomDefs2.h"
#include <list>

class WED_LibraryMgr;

struct pol_info_t
{
    std::string base_tex; // Relative path
    bool hasDecal;
    float proj_s;
    float proj_t;
    bool kill_alpha;
    bool wrap;
    std::string group;
    int group_offset;
    float latitude;
    float longitude;
    float height_Meters;
    int ddsHeight_Pxls;
    std::vector<Bbox2> mSubBoxes; // for subTexture selection in PreviewPanel
    Bbox2 mUVBox;                 // std::set by PreviewPanel from selected subTexture
    std::string description;
};

#include "WED_FacadePreview.h"

struct fac_info_t : public REN_FacadeLOD_t
{

    fac_info_t() : idx_vbo(0), vert_vbo(0)
    {
        is_new = false;
        is_ring = true;
        doubled = two_sided = false;
        min_floors = 1;
        max_floors = 999;
        has_roof = false;
        noroofmesh = nowallmesh = false;
        style_code = -1;
    }

    bool is_new; // std::set if version 1000, aka type 2
    std::string wall_tex;
    std::string roof_tex;
    bool is_ring; // can be drawn as open polygon
    bool two_sided;

    // Facade Scrapers
    std::vector<REN_facade_scraper_t> scrapers;

    // V1 only
    // std::vector<FacadeLOD_t>		lods;  // WED does not recognize anything but the LOD that starts at 0

    // V2 only
    bool noroofmesh;
    bool nowallmesh;
    list<REN_facade_floor_t> floors;
    std::vector<std::string> objs;   // names of type 2 objects
    std::vector<const XObj8*> xobjs; // names of type 2 objects

    float roof_scale_s;
    float roof_scale_t;

    // WED only
    std::vector<std::string> wallName; // wall names, for property window etc
    std::vector<std::string> wallUse;  // official width range supported by this wall
    std::string h_range;               // official heights (or height range) of the facade

    // jetway facade stuff
    struct tunnel_t
    {
        int idx;
        std::string obj;
        const XObj8* o;
        int size_code;
    };
    std::vector<tunnel_t> tunnels;
    int cabin_idx;
    int style_code;

    unsigned int vert_vbo;
    unsigned int idx_vbo;
};

struct lin_info_t
{
    std::string base_tex;
    float scale_s;
    float scale_t;
    float eff_width;
    float rgb[3];
    struct caps
    {
        float s1, sm, s2, t1, t2;
    };
    std::vector<float> s1, sm, s2;
    std::vector<caps> start_caps, end_caps;
    int align;
    bool hasDecal;
    std::string group;
    int group_offset;
    std::string description;
};

struct str_info_t
{
    float offset;
    float rotation;
    std::vector<std::string> objs;
    std::string description;
};

struct road_info_t
{
    struct vroad_t
    {
        std::string description; // text to display in menu's
        int rd_type;             // index into road_types
    };
    struct road_t
    {
        struct obj_t
        {
            std::string path;
            float lat_offs;
            float rotation;
        };
        struct wire_t
        {
            float lat_offs;
            float end_height;
            float droop;
        };
        struct seg_t
        {
            float left, right;     // lateral position in meters
            float s_left, s_right; // lateral s coordinates on texture (t is always 0 to 1)
        };
        int tex_idx;         // index into textures[]
        float width, length; // texture scaling
        float traffic_width; // inferred from CAR lanes
        bool oneway;
        std::vector<seg_t> segs;
        //		float 	s_left, s_right;	// st coordinates on texture (t is always 0 to 1)
        std::vector<obj_t> vert_objs;
        std::vector<obj_t> dist_objs;
        std::vector<wire_t> wires;
    };

    std::map<int, vroad_t> vroad_types;
    std::map<int, road_t> road_types;
    std::vector<std::string> textures;
};

struct agp_t
{
    struct obj_t
    {
        float x, y, z, r; // annotation position
        int show_lo, show_hi;
        std::string name;
        const XObj8* obj;                 // resolving name is slow - so keep the obj around
        float scp_min, scp_max, scp_step; // scp_step nonzero indicates scraper extension
        obj_t(void) : scp_step(0), scp_min(9999), scp_max(9999), obj(nullptr)
        {
        }
    };
    struct fac_t
    {
        float height;
        Polygon2 locs;
        std::vector<int> walls;
        std::string name;
        const fac_info_t* fac; // resolving name is slow - so keep the direct pointer around
    };
    struct tile_t
    {
        std::vector<float> tile; // the base tile in x,y,s,t quads.
        std::vector<obj_t> objs;
        std::vector<fac_t> facs;
        std::vector<float> cut_h, cut_v;
        float xyz_min[3];
        float xyz_max[3];
        float anchor_x, anchor_y;
        int id;
        bool has_scp;
        tile_t(void) : anchor_x(0), anchor_y(0), has_scp(false)
        {
        }
    };
    std::string base_tex;
    std::string mesh_tex;
    bool hide_tiles;
    std::vector<tile_t> tiles;
    std::string description;
    bool has_scp;
    agp_t(void) : has_scp(false), hide_tiles(false)
    {
    }
};

struct for_info_t
{
    struct tree_t
    {
        float s, t, w, h; // origin, width & height on texture
        float o;          // offset of tree center line (where the quads inersect)
        float pct;        // relative occurence percentage for this tree
        float hmin, hmax; // height range for this tree in meters
        int quads;        // number of quads the tree is constructed of
        std::string mesh_3d;
    };

    const XObj8 *preview, *preview_3d;
    std::string description;

    bool has_3D;
    std::map<int, std::vector<tree_t>> trees;
    for_info_t(void) : preview(nullptr), preview_3d(nullptr)
    {
    }
};

class WED_JWFacades
{
private:
    std::unordered_map<std::string, std::string> mJWFacades;
    bool mInitialized;
    void load(WED_LibraryMgr* lmgr, WED_ResourceMgr* rmgr);

public:
    WED_JWFacades(void) : mInitialized(false)
    {
    }
    std::string find(WED_LibraryMgr* lmgr, WED_ResourceMgr* rmgr, const std::string& tunnel_vpath);
};

class WED_ResourceMgr : public GUI_Broadcaster, public GUI_Listener, public virtual IBase
{
public:
    WED_ResourceMgr(WED_LibraryMgr* in_library);
    ~WED_ResourceMgr();

    void Purge(void);

    bool GetFac(const std::string& vpath, fac_info_t const*& info, int variant = 0);
    bool GetPol(const std::string& path, pol_info_t const*& info);
    bool SetPolUV(const std::string& path, Bbox2 box);
    bool GetLin(const std::string& path, lin_info_t const*& info);
    bool GetStr(const std::string& path, str_info_t const*& info);
    bool GetFor(const std::string& path, for_info_t const*& info);
    bool GetAllInDir(const std::string& vdir, std::vector<std::pair<std::string, int>>& vpaths);

    void WritePol(const std::string& abspath, const pol_info_t& out_info); // side note: shouldn't this be in_info?
    bool GetObj(const std::string& path, XObj8 const*& obj, int variant = 0);
    bool GetObjRelative(const std::string& obj_path, const std::string& parent_path, XObj8 const*& obj);
    bool GetAGP(const std::string& path, agp_t const*& info);
    bool GetRoad(const std::string& path, const road_info_t*& out_info);

    virtual void ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam);

    std::string GetJetwayVpath(const std::string& tunnel_vpath);

private:
    XObj8* LoadObj(const std::string& abspath);
    void setup_tile(agp_t::tile_t* agp, int rotation, const std::string& path);

    std::unordered_map<std::string, std::vector<fac_info_t>> mFac;
    std::unordered_map<std::string, pol_info_t> mPol;
    std::unordered_map<std::string, lin_info_t> mLin;
    std::unordered_map<std::string, str_info_t> mStr;
    std::unordered_map<std::string, for_info_t> mFor;
    std::unordered_map<std::string, std::vector<const XObj8*>> mObj;
    std::unordered_map<std::string, agp_t> mAGP;
#if ROAD_EDITING
    std::unordered_map<std::string, road_info_t> mRoad;
#endif
    WED_LibraryMgr* mLibrary;
    WED_JWFacades mJetways;
};

#endif /* WED_ResourceMgr_H */
