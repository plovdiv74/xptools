/*
 * Copyright (c) 2007, Laminar Research.
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

#include "WED_CreatePolygonTool.h"
#include "WED_AirportChain.h"
#include "WED_Taxiway.h"
#include "IResolver.h"
#include "GISUtils.h"
#include "WED_GISUtils.h"
#include "ISelection.h"
#include "WED_AirportNode.h"
#include "WED_FacadeNode.h"
#include "WED_ToolUtils.h"
#include "WED_EnumSystem.h"
#include "WED_AirportBoundary.h"
#include "WED_Airport.h"
#include "WED_Ring.h"
#include "WED_FacadePlacement.h"
#include "WED_ForestRing.h"
#include "WED_ForestPlacement.h"
#include "WED_FacadeRing.h"
#include "WED_PolygonPlacement.h"
#include "WED_DrapedOrthophoto.h"
#include "WED_LinePlacement.h"
#include "WED_StringPlacement.h"
#include "WED_AutogenPlacement.h"
#include "WED_AutogenNode.h"
#include "WED_SimpleBezierBoundaryNode.h"
#include "WED_SimpleBoundaryNode.h"
#include "WED_TextureNode.h"
#include "WED_TextureBezierNode.h"
#include "WED_ResourceMgr.h"
#include "MathUtils.h"

// this must match enum CreateTool_t in WED_CreatePolygonTool.h
static const char* kCreateCmds[] = {"Taxiway", "Boundary", "Marking", "Hole",    "Facade",
                                    "Forest",  "String",   "Line",    "Autogen", "Polygon"};

static const int kIsAirport[] = {1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const int kRequireClosed[] = {1, 1, 0, 1, 1, 1, 0, 0, 1, 1};
static const int kAllowCurved[] = {1, 0, 1, 1, 1, 0, 1, 1, 0, 1};

std::string stripped_resource(const std::string& r)
{
    std::string n(r);
    std::string::size_type p = n.find_last_of("/\\:");
    if (p != n.npos)
        n.erase(0, p + 1);
    return n;
}

WED_CreatePolygonTool::WED_CreatePolygonTool(const char* tool_name, GUI_Pane* host, WED_MapZoomerNew* zoomer,
                                             IResolver* resolver, WED_Archive* archive, CreateTool_t tool)
    : WED_CreateToolBase(tool_name, host, zoomer, resolver, archive,
                         kRequireClosed[tool] ? 3 : 2, // min pts
                         99999999,                     // max pts
                         kAllowCurved[tool],           // curve allowed
                         0,                            // curve required?
                         1,                            // close allowed
                         kRequireClosed[tool]),        // close required?
      mType(tool), mPavement(tool == create_Taxi ? this : NULL, PROP_Name("Pavement", XML_Name("", "")), Surface_Type,
                             surf_Concrete),
      mRoughness(tool == create_Taxi ? this : NULL, PROP_Name("Roughness", XML_Name("", "")), 0.25, 4, 2),
      mHeading(tool == create_Taxi || tool == create_Polygon ? this : NULL, PROP_Name("Heading", XML_Name("", "")), 0,
               5, 2),
      mMarkings(tool <= create_Hole ? this : NULL, PROP_Name(".Markings", XML_Name("", "")), LinearFeature, 0),
      mMarkingsLines(tool <= create_Hole ? this : NULL, PROP_Name("Markings", XML_Name("", "")), ".Markings", 1, 99, 1),
      mMarkingsLights(tool <= create_Hole ? this : NULL, PROP_Name("Lights", XML_Name("", "")), ".Markings", 101, 199,
                      1),

      mResource(tool > create_Hole ? this : NULL, PROP_Name("Resource", XML_Name("", "")), ""),
      mHeight(tool == create_Facade ? this : NULL, PROP_Name("Height", XML_Name("", "")), 10.0, 5, 2),
      mDensity(tool == create_Forest ? this : NULL, PROP_Name("Density", XML_Name("", "")), 1.0, 3, 2),

      mSpacing(tool == create_String ? this : NULL, PROP_Name("Spacing", XML_Name("", "")), 5.0, 5, 1),
      mAgsHght(tool == create_Autogen ? this : NULL, PROP_Name("Autogen Height", XML_Name("", "")), 10.0, 6, 1),

      mUVMap(tool == create_Polygon ? this : NULL, PROP_Name("Use Texture Map - Orthophoto", XML_Name("", "")), 0),
      mPickWalls(tool == create_Facade ? this : NULL, PROP_Name("Pick Walls", XML_Name("", "")), 0)
{
    mPavement.value = surf_Concrete;
    mPickWalls.value = true;
}

WED_CreatePolygonTool::~WED_CreatePolygonTool()
{
}

void WED_CreatePolygonTool::AcceptPath(const std::vector<Point2>& pts, const std::vector<Point2>& dirs_lo,
                                       const std::vector<Point2>& dirs_hi, const std::vector<int> has_dirs,
                                       const std::vector<int> has_split, int closed)
{
    char buf[256];

    int idx;
    WED_Thing* host = GetHost(idx);
    if (host == NULL)
        return;

    std::string cname = std::string("Create ") + kCreateCmds[mType];

    GetArchive()->StartCommand(cname.c_str());

    ISelection* sel = WED_GetSelect(GetResolver());
    if (mType != create_Hole)
        sel->Clear();

    int is_bezier = mType != create_Forest && mType != create_Boundary;
    int is_apt = mType <= create_Hole;
    int is_autogen = mType == create_Autogen;
    int is_poly = (mType != create_Hole && mType != create_String && mType != create_Line) || is_autogen;
    int is_texed = mType == create_Polygon ? mUVMap.value : 0;
    int is_forest = mType == create_Forest;
    int is_facade = mType == create_Facade;

    if (mType == create_Hole)
    {
        DebugAssert(host->CountChildren() > 0);
        WED_Thing* old_outer_ring = host->GetNthChild(0);
        DebugAssert(old_outer_ring->CountChildren() > 0);
        is_apt = dynamic_cast<WED_AirportChain*>(old_outer_ring) != NULL;
        is_bezier = dynamic_cast<IGISPoint_Bezier*>(old_outer_ring->GetNthChild(0)) != NULL;
        is_poly = true;
        is_texed = dynamic_cast<WED_TextureNode*>(old_outer_ring->GetNthChild(0)) != NULL ||
                   dynamic_cast<WED_TextureBezierNode*>(old_outer_ring->GetNthChild(0)) != NULL;
        is_forest = dynamic_cast<WED_ForestRing*>(old_outer_ring) != NULL;
        is_facade = dynamic_cast<WED_FacadeRing*>(old_outer_ring) != NULL;
    }

    WED_AirportChain* apt_ring = NULL;
    WED_Thing* outer_ring = NULL;

    if (is_apt)
        outer_ring = apt_ring = WED_AirportChain::CreateTyped(GetArchive());
    else if (is_forest)
        outer_ring = WED_ForestRing::CreateTyped(GetArchive());
    else if (is_facade)
        outer_ring = WED_FacadeRing::CreateTyped(GetArchive());
    else if (is_poly)
        outer_ring = WED_Ring::CreateTyped(GetArchive());

    static int n = 0;
    ++n;

    bool is_ccw = (mType == create_Marks || (mType == create_String && !is_autogen) || mType == create_Line)
                      ? true
                      : is_ccw_polygon_pt(pts.begin(), pts.end());
    if (mType == create_Hole)
        is_ccw = !is_ccw;

    WED_DrapedOrthophoto* dpol = NULL;
    WED_FacadePlacement* fac = NULL;

    switch (mType)
    {
    case create_Taxi: {
        WED_Taxiway* tway = WED_Taxiway::CreateTyped(GetArchive());
        outer_ring->SetParent(tway, 0);
        tway->SetParent(host, idx);

        sprintf(buf, "New Taxiway %d", n);
        tway->SetName(buf);
        sprintf(buf, "Taxiway %d Outer Ring", n);
        outer_ring->SetName(buf);

        tway->SetRoughness(mRoughness.value);
        tway->SetHeading(mHeading.value);
        tway->SetSurface(mPavement.value);

        sel->Select(tway);
    }
    break;
    case create_Boundary: {
        WED_AirportBoundary* bwy = WED_AirportBoundary::CreateTyped(GetArchive());
        outer_ring->SetParent(bwy, 0);
        bwy->SetParent(host, idx);

        sprintf(buf, "Airport Boundary %d", n);
        bwy->SetName(buf);
        sprintf(buf, "Airport Boundary %d Outer Ring", n);
        outer_ring->SetName(buf);

        sel->Select(bwy);
    }
    break;
    case create_Marks: {
        outer_ring->SetParent(host, idx);
        sprintf(buf, "Linear Feature %d", n);
        outer_ring->SetName(buf);

        if (mType != create_Hole)
            sel->Select(outer_ring);
    }
    break;
    case create_Hole: {
        outer_ring->SetParent(host, host->CountChildren());
        sprintf(buf, "Hole %d", n);
        outer_ring->SetName(buf);

        if (mType != create_Hole)
            sel->Select(outer_ring);
    }
    break;
    case create_Facade: {
        fac = WED_FacadePlacement::CreateTyped(GetArchive());
        outer_ring->SetParent(fac, 0);
        fac->SetParent(host, idx);
        fac->SetName(stripped_resource(mResource.value));
        sprintf(buf, "Facade %d outer ring", n);
        outer_ring->SetName(buf);
        sel->Select(fac);
        fac->SetResource(mResource.value);
        fac->SetHeight(mHeight.value);
        fac->SetCustomWalls(mPickWalls.value);
    }
    break;
    case create_Forest: {
        WED_ForestPlacement* fst = WED_ForestPlacement::CreateTyped(GetArchive());
        outer_ring->SetParent(fst, 0);
        fst->SetParent(host, idx);
        fst->SetName(stripped_resource(mResource.value));
        sprintf(buf, "Forest %d outer ring", n);
        outer_ring->SetName(buf);
        sel->Select(fst);
        fst->SetResource(mResource.value);
        fst->SetDensity(mDensity.value);
    }
    break;
    case create_Autogen: {
        WED_AutogenPlacement* ags = WED_AutogenPlacement::CreateTyped(GetArchive());
        outer_ring->SetParent(ags, 0);
        ags->SetParent(host, idx);
        ags->SetName(stripped_resource(mResource.value));
        sprintf(buf, "Autogen %d Outer Ring", n);
        outer_ring->SetName(buf);
        sel->Select(ags);
        ags->SetResource(mResource.value);
        ags->SetHeight(mAgsHght.value);
    }
    break;
    case create_String: {
        WED_StringPlacement* str = WED_StringPlacement::CreateTyped(GetArchive());
        outer_ring = str;
        str->SetParent(host, idx);
        str->SetName(stripped_resource(mResource.value));
        sel->Select(str);
        str->SetClosed(closed);
        str->SetResource(mResource.value);
        str->SetSpacing(mSpacing.value);
    }
    break;
    case create_Line: {
        WED_LinePlacement* lin = WED_LinePlacement::CreateTyped(GetArchive());
        outer_ring = lin;
        lin->SetParent(host, idx);
        sprintf(buf, "Line %d", n);
        lin->SetName(stripped_resource(mResource.value));
        sel->Select(lin);
        lin->SetClosed(closed);
        lin->SetResource(mResource.value);
    }
    break;
    case create_Polygon:
        if (is_texed)
        {
            dpol = WED_DrapedOrthophoto::CreateTyped(GetArchive());
            outer_ring->SetParent(dpol, 0);
            dpol->SetParent(host, idx);
            dpol->SetName(stripped_resource(mResource.value));
            sprintf(buf, "Orthophoto %d Outer Ring", n);
            outer_ring->SetName(buf);
            sel->Select(dpol);
            dpol->SetResource(mResource.value);
            dpol->SetHeading(mHeading.value);
        }
        else
        {
            WED_PolygonPlacement* pol = WED_PolygonPlacement::CreateTyped(GetArchive());
            outer_ring->SetParent(pol, 0);
            pol->SetParent(host, idx);
            pol->SetName(stripped_resource(mResource.value));
            sprintf(buf, "Polygon %d Outer Ring", n);
            outer_ring->SetName(buf);
            sel->Select(pol);
            pol->SetResource(mResource.value);
            pol->SetHeading(mHeading.value);
        }
        break;
    }

    if (apt_ring)
        apt_ring->SetClosed(closed);

    for (int n = 0; n < pts.size(); ++n)
    {
        int idx = is_ccw ? n : pts.size() - n - 1;

        WED_AirportNode* anode = NULL;
        WED_GISPoint_Bezier* bnode = NULL;
        WED_GISPoint* node = NULL;
        WED_TextureNode* tnode = NULL;
        WED_TextureBezierNode* tbnode = NULL;

        if (is_apt)
            node = bnode = anode = WED_AirportNode::CreateTyped(GetArchive());
        else if (is_facade)
            node = bnode = WED_FacadeNode::CreateTyped(GetArchive());
        else if (is_autogen)
            node = WED_AutogenNode::CreateTyped(GetArchive());
        else if (is_bezier && is_texed)
            node = bnode = tbnode = WED_TextureBezierNode::CreateTyped(GetArchive());
        else if (is_bezier)
            node = bnode = WED_SimpleBezierBoundaryNode::CreateTyped(GetArchive());
        else if (is_texed)
            node = tnode = WED_TextureNode::CreateTyped(GetArchive());
        else
            node = WED_SimpleBoundaryNode::CreateTyped(GetArchive());

        node->SetLocation(gis_Geo, pts[idx]);
        if (bnode)
        {
            if (!has_dirs[idx])
            {
                bnode->DeleteHandleHi();
                bnode->DeleteHandleLo();
            }
            else
            {
                bnode->SetSplit(has_split[idx]);
                if (is_ccw)
                {
                    bnode->SetControlHandleHi(gis_Geo, dirs_hi[idx]);
                    bnode->SetControlHandleLo(gis_Geo, dirs_lo[idx]);
                }
                else
                {
                    bnode->SetControlHandleHi(gis_Geo, dirs_lo[idx]);
                    bnode->SetControlHandleLo(gis_Geo, dirs_hi[idx]);
                }
            }
        }
        node->SetParent(outer_ring, n);
        if (anode)
            anode->SetAttributes(mMarkings.value);
        sprintf(buf, "Node %d", n + 1);
        node->SetName(buf);

        if (is_facade)
        {
            int xx;
            if (n == pts.size() - 2 && fac->IsJetway(&xx))
                dynamic_cast<WED_FacadeNode*>(node)->SetWallType(xx);
            else if (n == pts.size() - 3 && fac->IsJetway(nullptr, &xx))
                dynamic_cast<WED_FacadeNode*>(node)->SetWallType(xx);
        }
    }

    if (mType == create_Polygon && is_texed) // orthophoto's need the UV map std::set up
    {
        WED_ResourceMgr* rmgr = WED_GetResourceMgr(GetResolver());
        const pol_info_t* info;

        if (rmgr->GetPol(mResource.value, info))
            if (!info->mUVBox.is_null())
                dpol->SetSubTexture(info->mUVBox);
            else
                dpol->SetSubTexture(Bbox2(0, 0, 1, 1));

        dpol->Redrape();
    }
    else if (mType == create_Hole &&
             host->GetClass() == WED_DrapedOrthophoto::sClass) // holes in orthos also need UV map std::set
        dynamic_cast<WED_DrapedOrthophoto*>(host)->Redrape();

    GetArchive()->CommitCommand();
}

const char* WED_CreatePolygonTool::GetStatusText(void)
{
    static char buf[256];
    int n;
    if (GetHost(n) == NULL)
    {
        if (mType == create_Hole)
            sprintf(
                buf,
                "You must select a polygon before you can insert a hole into it.  Facades cannot have interior holes.");
        else
            sprintf(buf, "You must create an airport before you can add a %s.", kCreateCmds[mType]);
        return buf;
    }
    return NULL;
}

bool WED_CreatePolygonTool::CanCreateNow(void)
{
    int n;
    return GetHost(n) != NULL;
}

WED_Thing* WED_CreatePolygonTool::GetHost(int& idx)
{
    if (mType == create_Hole)
    {
        ISelection* sel = WED_GetSelect(GetResolver());
        if (sel->GetSelectionCount() != 1)
            return NULL;
        WED_GISPolygon* igp = dynamic_cast<WED_GISPolygon*>(sel->GetNthSelection(0));
        if (!igp)
            return NULL;
        // A few polygons do NOT get holes: facades, um...that's it for now.
        if (igp->GetClass() == WED_FacadePlacement::sClass)
            return NULL;
        if (igp->GetClass() == WED_AutogenPlacement::sClass)
        {
            auto ags = dynamic_cast<WED_AutogenPlacement*>(igp);
            if (ags && ags->IsAGBlock())
                return NULL;
        }
        return igp;
    }
    else
        return WED_GetCreateHost(GetResolver(), kIsAirport[mType], true, idx);
}

void WED_CreatePolygonTool::SetResource(const std::string& r)
{
    mResource.value = r;

    // Preset polygon / orthophoto flag when selecting resource. Still allows user overriding it in vertex tool.
    WED_ResourceMgr* rmgr = WED_GetResourceMgr(GetResolver());
    const pol_info_t* pol_i;
    const fac_info_t* fac_i;
    if (rmgr->GetPol(mResource.value, pol_i))
        mUVMap.value = !pol_i->wrap;
    else if (rmgr->GetFac(mResource.value, fac_i))
        mMinPts = !fac_i->is_ring && !fac_i->has_roof ? 2 : 3; // allow placement of some 2-node facades
}

void WED_CreatePolygonTool::GetNthPropertyDict(int n, PropertyDict_t& dict) const
{
    WED_CreateToolBase::GetNthPropertyDict(n, dict);
    if (n == PropertyItemNumber(&mPavement) && mPavement.value != surf_Water)
    {
        dict.erase(surf_Water);
    }
}
