/*
 * Copyright (c) 2013, Laminar Research.
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

#include "WED_Validate.h"
#include "WED_ValidateList.h"
#include "WED_ValidateATCRunwayChecks.h"

#include "WED_Globals.h"
#include "WED_Sign_Parser.h"
#include "WED_Runway.h"
#include "WED_Sealane.h"
#include "WED_Helipad.h"
#include "WED_Airport.h"
#include "WED_AirportBoundary.h"
#include "WED_AirportSign.h"
#include "WED_FacadePlacement.h"
#include "WED_ForestPlacement.h"
#include "WED_ObjPlacement.h"
#include "WED_StringPlacement.h"
#include "WED_AutogenPlacement.h"
#include "WED_LinePlacement.h"
#include "WED_PolygonPlacement.h"
#include "WED_DrapedOrthophoto.h"
#include "WED_OverlayImage.h"
#include "WED_FacadeNode.h"
#include "WED_RampPosition.h"
#include "WED_RoadEdge.h"
#include "WED_RoadNode.h"
#include "WED_Taxiway.h"
#include "WED_TaxiRoute.h"
#include "WED_TruckDestination.h"
#include "WED_TruckParkingLocation.h"
#include "WED_TowerViewpoint.h"
#include "WED_ATCFlow.h"
#include "WED_ATCFrequency.h"
#include "WED_ATCRunwayUse.h"
#include "WED_ATCWindRule.h"
#include "WED_ATCTimeRule.h"
#include "WED_GISPoint.h"

#include "WED_GISUtils.h"
#include "WED_ToolUtils.h"
#include "WED_HierarchyUtils.h"

#include "WED_Group.h"
#include "WED_EnumSystem.h"
#include "WED_Menus.h"
#include "WED_GatewayExport.h"
#include "WED_GroupCommands.h"
#include "WED_MetaDataKeys.h"
#include "WED_MetaDataDefaults.h"

#include "IResolver.h"
#include "ILibrarian.h"
#include "WED_LibraryMgr.h"
#include "WED_PackageMgr.h"
#include "WED_ResourceMgr.h"

#include "CompGeomUtils.h"

#include "BitmapUtils.h"
#include "GISUtils.h"
#include "FileUtils.h"
#include "MemFileUtils.h"
#include "PlatformUtils.h"
#include "STLUtils.h"
#include "MathUtils.h"

#include "WED_Document.h"
#include "WED_FileCache.h"
#include "WED_Url.h"
#include "GUI_Resources.h"
#include "XESConstants.h"

#include <iomanip>
#include <thread>

// maximum airport size allowed for gateway, only warned about for custom scenery
// 7 nm = 13 km = 42500 feet
#define MAX_SPAN_GATEWAY_NM 7

// maximum distance for any scenery from the airport boundary, gateway only
#define APT_OVERSIZE_NM  0.6

// ATC flow tailwind components and wind rule coverage tested up to this windspeed
#define ATC_FLOW_MAX_WIND 35

// Checks for zero length sides - can be turned off for grandfathered airports.
#define CHECK_ZERO_LENGTH 1

#define DBG_LIN_COLOR 1,0,1,1,0,1

static int strlen_utf8(const std::string& str)
{
    unsigned char c;
    int i,q;
    int l = str.length();
    for (q=0, i=0; i < l; i++, q++)
    {
        c = str[i];
        if(c & 0x80)
        {
				 if ((c & 0xE0) == 0xC0) i+=1;
			else if ((c & 0xF0) == 0xE0) i+=2;
			else if ((c & 0xF8) == 0xF0) i+=3;
			else return 0;  //not a valid utf8 code
        }
    }
    return q;
}

static int get_opposite_rwy(int rwy_enum)
{
	DebugAssert(rwy_enum != atc_Runway_None);

	int r = ENUM_Export(rwy_enum);
	int o = atc_19 - atc_1;

	if(rwy_enum >= atc_1 && rwy_enum < atc_19)
	{
		if((r % 10) == 1)
			return rwy_enum + o + 2;
		else if((r % 10) == 3)
			return rwy_enum + o - 2;
		else
			return rwy_enum + o;
	}
	else if(rwy_enum >= atc_19 && rwy_enum <= atc_36W)
	{
		if((r % 10) == 1)
			return rwy_enum - o + 2;
		else if((r % 10) == 3)
			return rwy_enum - o - 2;
		else
			return rwy_enum - o;
	}
	DebugAssert(!"Bad enum");
	return atc_Runway_None;
}

static std::string format_freq(int f)
{
	int mhz = f / 1000;
	int khz = f % 1000;
	stringstream ss;
	ss << mhz << "." << std::setw(3) << std::setfill('0') << khz;
	return ss.str();
}

// This template buidls an error list for a subset of objects that have the same name - one validation error is generated
// for each std::set of same-named objects.
template <typename T>
static bool CheckDuplicateNames(const T& container, validation_error_vector& msgs, WED_Airport * owner, const std::string& msg)
{
	typedef std::map<std::string, std::vector<typename T::value_type> > name_map_t;
	name_map_t name_index;
	for(typename T::const_iterator i = container.begin(); i != container.end(); ++i)
	{
		std::string n;
		(*i)->GetName(n);
		typename name_map_t::iterator ni = name_index.find(n);
		if(ni == name_index.end())
			ni = name_index.insert(typename name_map_t::value_type(n, typename name_map_t::mapped_type())).first;

		ni->second.push_back(*i);
	}

	bool ret = false;
	for(typename name_map_t::iterator ii = name_index.begin(); ii != name_index.end(); ++ii)
	{
		if(ii->second.size() > 1)
		{
			ret = true;
			validation_error_t err;
			err.msg = msg;
			err.err_code = err_duplicate_name;
			copy(ii->second.begin(),ii->second.end(),back_inserter(err.bad_objects));
			err.airport = owner;
			msgs.push_back(err);
		}
	}

	return ret;
}

template <typename T>
bool all_in_range(const T* values, T lower_limit, T upper_limit)
{
	for(int i = 0; i < sizeof(T); ++i)
		if(values[i] < lower_limit || values[i] > upper_limit)
			return false;
	return true;
}

static void ValidateOnePointSequence(WED_Thing* who, validation_error_vector& msgs, IGISPointSequence* ps, WED_Airport * apt)
{
        /* Point Sequence Rules
             - at least two nodes
             - at least three nodes, if its part of an area feature
             - no zero length segments = duplicate nodes
                 if any are found,  select the first node connected to each zero length segment,
                 so it can be fixed by deleting those. Is much easier than writing an extra merge function.
        */
	int nn = ps->GetNumPoints();
	if(nn < 2)
	{
		std::string msg = "Linear feature '" + std::string(who->HumanReadableType()) + "' needs at least two points. Delete the selected item to fix this.";
		msgs.push_back(validation_error_t(msg, err_gis_poly_linear_feature_at_least_two_points, dynamic_cast<WED_Thing *>(ps),apt));
	}
	WED_Thing * parent = who->GetParent();

	if ((parent) &&
	    (parent->GetClass() == WED_DrapedOrthophoto::sClass ||
	     parent->GetClass() == WED_PolygonPlacement::sClass ||
	     parent->GetClass() == WED_Taxiway::sClass ||          // we also test those elsewhere, but not for zero length segments
	     parent->GetClass() == WED_ForestPlacement::sClass ||
	     parent->GetClass() == WED_AirportBoundary::sClass ||
	     parent->GetClass() == WED_FacadePlacement::sClass ))
	{
		bool is_area = true;

		WED_FacadePlacement * fac = dynamic_cast<WED_FacadePlacement *>(parent);
		if (fac && fac->GetTopoMode() == WED_FacadePlacement::topo_Chain )
			is_area = false;

		if(is_area && nn < 3)
		{
			std::string msg = "Polygon feature '" + std::string(parent->HumanReadableType()) + "' needs at least three points.";
			msgs.push_back(validation_error_t(msg, err_gis_poly_linear_feature_at_least_three_points, parent,apt));
		}
	}
	else
	{
		parent = who;   // non-area linear features do not have a meaningfull parent
		return;         // don't check anything else like lines/strings/etc. Comment this out to enable checks.
	}

	std::set<WED_Thing*> problem_children;

	if ((parent) && parent->GetClass() == WED_DrapedOrthophoto::sClass)
    {
        // Find UV coordinate combinations that are out of range know to cause OGL tesselator crashes, i.e. can not be exported to DSF
        problem_children.clear();
       	for(int n = 0; n < nn; ++n)
        {
            Point2 p;
            IGISPoint * ptr = ps->GetNthPoint(n);
            ptr->GetLocation(gis_UV,p);

			if(p.x() < -65535.0 || p.x() > 65535.0 ||
				p.y() < -65535.0 || p.y() > 65535.0)
            {
                // add first node of each zero length segment to list
                problem_children.insert(dynamic_cast<WED_Thing *>(ps->GetNthPoint(n)));
            }
        }

        if (problem_children.size() > 0)
        {
            std::string msg = std::string(parent->HumanReadableType()) + std::string(" has nodes with UV coordinates out of bounds.");
            msgs.push_back(validation_error_t(msg, err_orthophoto_bad_uv_map, problem_children, apt));
        }

        // Find UV coordinates that a nearly or truely co-located. During tessalation - any two modes in the polygon may end up
        // as vertices in the same triangle. SO we dont
        problem_children.clear();
       	for(int n = 0; n < nn; ++n)
        {
            Point2 p1;
            IGISPoint * ptr = ps->GetNthPoint(n);
            ptr->GetLocation(gis_UV,p1);

            for(int m = n+1; m < nn; ++m)
            {
                Point2 p2;
                ptr = ps->GetNthPoint(m);
                ptr->GetLocation(gis_UV,p2);

                if(p1.squared_distance(p2) < 1E-10)   // that is less than 1/25 of a pixel even on a 4k texture
                {
                    // add first node of each zero length segment to list
                    problem_children.insert(dynamic_cast<WED_Thing *>(ps->GetNthPoint(n)));
                }
            }

        }
        if (problem_children.size() > 0)
        {
            std::string msg = std::string(parent->HumanReadableType()) + std::string(" has nodes with UV coordinates too close together.");
            msgs.push_back(validation_error_t(msg, err_orthophoto_bad_uv_map, problem_children, apt));
        }

    }

#if CHECK_ZERO_LENGTH
	double min_seg_len   = 0.1;
	if (gExportTarget == wet_gateway)
	{
		if (parent->GetClass() == WED_AirportBoundary::sClass)
			min_seg_len = 30.0;
//		else if (parent->GetClass() == WED_ForestPlacement::sClass)
//			min_seg_len = 10.0;
	}

	double min_len_sq = dob_sqr(min_seg_len * MTR_TO_DEG_LAT);
	Point2 pt;
	ps->GetNthPoint(0)->GetLocation(gis_Geo, pt);
	double inv_cos_lat_sq = dob_sqr(1.0 / cos(pt.y() * DEG_TO_RAD));

	problem_children.clear();
	nn = ps->GetNumSides();

	for(int n = 0; n < nn; ++n)
	{
		Bezier2 b;
		ps->GetSide(gis_Geo,n,b);

		// if(b.p1 == b.p2)
		if(dob_sqr(b.p1.x() - b.p2.x()) + inv_cos_lat_sq * dob_sqr(b.p1.y() - b.p2.y()) < min_len_sq)
		{
			// add first node of each zero length segment to list
			problem_children.insert(dynamic_cast<WED_Thing *>(ps->GetNthPoint(n)));
		}
	}
	if (problem_children.size() > 0)
	{
		char c[24];
		if (min_seg_len > 0.5) 
			snprintf(c, sizeof(c), "too close (<%d%c)", intround(gIsFeet ? min_seg_len * MTR_TO_FT : min_seg_len), gIsFeet ? '\'' : 'm');
		else 
			snprintf(c, sizeof(c), "duplicate");
		std::string msg = std::string(parent->HumanReadableType()) + " has " + c + " vertices. Delete selected vertices to fix this.";
		msgs.push_back(validation_error_t(msg, err_gis_poly_zero_length_side, problem_children, apt));
	}
#endif
}

static void ValidatePointSequencesRecursive(WED_Thing * who, validation_error_vector& msgs, WED_Airport * apt)
{
	// Don't validate hidden stuff - we won't export it!
	WED_Entity * ee = dynamic_cast<WED_Entity *>(who);
	if(ee && ee->GetHidden())
		return;

	IGISPointSequence * ps = dynamic_cast<IGISPointSequence *>(who);
	if(ps)
	{
		ValidateOnePointSequence(who,msgs,ps,apt);
	}
	int nn = who->CountChildren();
	for(int n = 0; n < nn; ++n)
	{
		WED_Thing * c = who->GetNthChild(n);
		if(c->GetClass() != WED_Airport::sClass)
			ValidatePointSequencesRecursive(c,msgs,apt);
	}
}


//------------------------------------------------------------------------------------------------------------------------------------
// DSF VALIDATIONS
//------------------------------------------------------------------------------------------------------------------------------------
#pragma mark -

static void ValidateOneFacadePlacement(WED_Thing* who, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Facade Validate Rules--------------------------------------------------
		wet_xplane_900 rules
		  - Custom facade wall choices are only in X-Plane 10 and newer
		  - Curved facades are only supported in X-Plane 10 and newer
		Other rulesOnly real application that get
		  - Facades may not have holes in them
	 */

	WED_FacadePlacement * fac = dynamic_cast<WED_FacadePlacement*>(who);
	DebugAssert(who);
	if(gExportTarget == wet_xplane_900 && fac->HasCustomWalls())
	{
		msgs.push_back(validation_error_t("Custom facade wall choices are only supported in X-Plane 10 and newer.", err_gis_poly_facade_custom_wall_choice_only_for_gte_xp10, who,apt));
	}

	if(fac->GetNumHoles() > 0)
	{
		msgs.push_back(validation_error_t("Facades may not have holes in them.", err_gis_poly_facades_may_not_have_holes, who,apt));
	}

	if(WED_HasBezierPol(fac))
	{
		if(gExportTarget == wet_xplane_900)
			msgs.push_back(validation_error_t("Curved facades are only supported in X-Plane 10 and newer.", err_gis_poly_facades_curved_only_for_gte_xp10, who,apt));
		else if(fac->GetType() < 2)
			msgs.push_back(validation_error_t("Only Type2 facades support curved segments.", warn_facades_curved_only_type2, who,apt));
	}

	if(fac->HasLayer(gis_Param))
	{
		auto maxWalls = fac->GetNumWallChoices();
		auto ips = fac->GetOuterRing();
		int nn = ips->GetNumPoints();
		std::set<WED_Thing*> bad_walls;
		for(int i = 0; i < nn; ++i)
		{
			Point2 pt;
			auto igp = ips->GetNthPoint(i);
			igp->GetLocation(gis_Param, pt);

			if(pt.x() >= maxWalls && (ips->IsClosed() || i < nn - 1 ))
				bad_walls.insert(dynamic_cast<WED_Thing *>(igp));
		}
		if (!bad_walls.empty())
			msgs.push_back(validation_error_t("Facade node specifies wall not defined in facade resource.", err_facade_illegal_wall, bad_walls, apt));
	}

	// In case facades gain new height capabilities we want the existing ones to be reasonably close to an actually supported height going forward
	auto allHeights = fac->GetHeightChoices();
	if (allHeights.size() > 1 || (allHeights.size() == 1 && allHeights.front() > 2.5f))   // don't be to nitpicky about really low stuff like fences etc
	{
		float next_h_up = 9999.0f;
		float next_h_down = 0.0f;
		for (auto h : allHeights)
		{
			if (h >= fac->GetHeight())
			{
				if (h < next_h_up) next_h_up = h;
			}
			else
			{
				if (h > next_h_down) next_h_down = h;
			}
		}
		auto dist_up = next_h_up - fac->GetHeight();
		auto dist_dn = fac->GetHeight() - next_h_down;
		if (dist_up > 1.0f && dist_dn > 1.0f)
		{
			char c[128];
			if (allHeights.size() > 1.0f && next_h_up < 9999.0f && next_h_down > 0.0f && fltrange(dist_up / dist_dn, 0.5, 2.0))
				sprintf(c, "Facade height not close to actual supported heights. Closest supported are %.0f, %.0f", next_h_down, next_h_up);
			else
				sprintf(c, "Facade height not close to actual supported heights. Closest supported is %.0f", dist_up < dist_dn ? next_h_up : next_h_down);
			msgs.push_back(validation_error_t(c, gExportTarget == wet_gateway ? warn_facade_height : warn_facade_height, who, apt));  // only warn for now
		}
	}

	// JW facades are a hybrid apt.dat/DSF things. So the usual protection for apt.dat items getting dragged outside an airport hierachy won't work
	if(gExportTarget >= wet_xplane_1200 && fac->HasDockingCabin())
	{
		if(!apt)
			msgs.push_back(validation_error_t("Facades with Docking Jetways must be inside an airport hierachy", err_facade_illegal_wall, who, apt));
	}
}

static void ValidateOneForestPlacement(WED_Thing* who, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Forest Placement Rules
		wet_xplane_900 rules
			- Line and point are only supported in X-Plane 10 and newer
		*/

	WED_ForestPlacement * fst = dynamic_cast<WED_ForestPlacement *>(who);
	DebugAssert(fst);
	if(gExportTarget == wet_xplane_900 && fst->GetFillMode() != dsf_fill_area)
		msgs.push_back(validation_error_t("Line and point forests are only supported in X-Plane 10 and newer.", err_gis_poly_line_and_point_forests_only_for_gte_xp10, who,apt));
}

static void AddNodesOfSegment(const IGISPointSequence * ips, const int seg, std::set<WED_GISPoint *>& nlist)
{
	WED_GISPoint *n;
	if (n = dynamic_cast<WED_GISPoint *> (ips->GetNthPoint(seg)))
			nlist.insert(n);
	if (n = dynamic_cast<WED_GISPoint *> (ips->GetNthPoint((seg+1) % ips->GetNumPoints())))
			nlist.insert(n);
}

static void ValidateOnePolygon(WED_GISPolygon* who, validation_error_vector& msgs, WED_Airport * apt)
{
	// check for outer ring wound CCW (best case it will not show in XP, worst case it will assert in DSF export)
	// check for self-intersecting polygons

	if((who->GetGISClass() == gis_Polygon && who->GetClass() != WED_OverlayImage::sClass ) ||	// exempt RefImages, since WEDbing places them with CW nodes
	   (who->GetClass() == WED_AirportBoundary::sClass))										// ALWAYS validate airport boundary like a polygon - it's really a collection of sequences, but self intersection is NOT allowed.
    {
		for(int child = 0; child < who->CountChildren(); ++child)
		{
			IGISPointSequence * ips = SAFE_CAST(IGISPointSequence,who->GetNthChild(child));

			if (ips)
			{
				{   // this would be much easier if we'd  code that as a member function of WED_GisChain: So we'd have access to mCachePts, i.e. the ordered std::vector of points
				    // until then - here we build our own std::vector of points by polling some other std::vector of points ...
					std::vector <Point2> seq;
					int n_pts = ips->GetNumPoints();
					for(int n = 0; n < n_pts; ++n)
					{
						IGISPoint * igp = ips->GetNthPoint(n);
						if (igp)
						{
							Point2 p;
							igp->GetLocation(gis_Geo, p);
//							if (!(p == seq.back()))  // skip over zero length segemnts, as they cause
								seq.push_back(p);    // false positives in the clockwise wound test. And taxiways are allowed to have ZLSegments !!
						}
					}
					if ( (child == 0) != is_ccw_polygon_pt(seq.begin(), seq.end())) // Holes need to be CW, Outer rings CCW
					{
						std::string nam; who->GetName(nam);
						std::string msg = std::string(child ? "Hole in " : "") + who->HumanReadableType() + " '" + nam + "' is wound " +
											(child ? "counter" : "") + "clock wise. Reverse selected component to fix this.";
						msgs.push_back(validation_error_t(msg, 	err_gis_poly_wound_clockwise, who->GetNthChild(child), apt));
					}
				}
				{
					std::set<WED_GISPoint *> nodes_next2crossings;
					int n_sides = ips->GetNumSides();

					for (int i = 0; i < n_sides; ++i)
					{
						Bezier2 b1;
						bool isb1 = ips->GetSide(gis_Geo, i, b1);

						if (isb1 && b1.self_intersect(10))
							AddNodesOfSegment(ips,i,nodes_next2crossings);

						for (int j = i + 1; j < n_sides; ++j)
						{
							Bezier2 b2;
							bool isb2 = ips->GetSide(gis_Geo, j, b2);

							if (isb1 || isb2)
							{
								if (b1.intersect(b2, 10))      // Note this test aproximate and recursive, causing the curve to
								{							   // be broken up into 2^10 = 1024 sub-segments at the most
									AddNodesOfSegment(ips,i,nodes_next2crossings);
									AddNodesOfSegment(ips,j,nodes_next2crossings);
								}
							}
							else // precision and speed would not matter, we would not have to treat linear segments separately ...
							{
								if (b1.p1 != b2.p1 &&      // check if segments share a node, the
									b1.p2 != b2.p2 &&      // linear segment intersect check returns a false positive
									b1.p1 != b2.p2 &&      // (unlike the bezier intersect test)
									b1.p2 != b2.p1)
								{
									Point2 x;
									if (b1.as_segment().intersect(b2.as_segment(), x))
									{
										//if(i == 0 && j == n_sides-1 && x == b1.p2) // touching ends of its just "closing a ring"
										{
											AddNodesOfSegment(ips,i,nodes_next2crossings);
											AddNodesOfSegment(ips,j,nodes_next2crossings);
										}
									}
								}
							}
						}
					}
					if (!nodes_next2crossings.empty())
					{
						std::string nam; who->GetName(nam);
						std::string msg = std::string(who->HumanReadableType()) + " '" + nam + "' has crossing or self-intersecting segments.";
						msgs.push_back(validation_error_t(msg, 	err_gis_poly_self_intersecting, nodes_next2crossings, apt));
					}
				}
			}
		}
		if(who->GetClass() == WED_AutogenPlacement::sClass)
		{
			auto ags = dynamic_cast<WED_AutogenPlacement *>(who);
			std::string res;
			ags->GetResource(res);
			if(res[res.size()-1] == 'b')
			{
				if(ags->GetNthChild(0)->CountChildren() != 4)
					msgs.push_back(validation_error_t("AutoGenBlock polygons must have exactly 4 sides.", err_agb_poly_not_4_sided, who, apt));
				if(ags->CountChildren() > 1)
					msgs.push_back(validation_error_t("AutoGenBlock polygons must not have holes.", err_agb_poly_has_holes, who, apt));
			}
		}
	}
}

static void ValidateDSFRecursive(WED_Thing * who, WED_LibraryMgr* lib_mgr, validation_error_vector& msgs, WED_Airport * parent_apt)
{
	// Don't validate hidden stuff - we won't export it!
	WED_Entity * ee = dynamic_cast<WED_Entity *>(who);
	if(ee && ee->GetHidden())
		return;

	if(who->GetClass() == WED_FacadePlacement::sClass)
		ValidateOneFacadePlacement(who, msgs, parent_apt);

	if(who->GetClass() == WED_ForestPlacement::sClass)
		ValidateOneForestPlacement(who, msgs, parent_apt);

	if(gExportTarget == wet_gateway)
	{
		if(who->GetClass() != WED_Group::sClass)
		if(!parent_apt)
			msgs.push_back(validation_error_t("Elements of your project are outside the hierarchy of the airport you are trying to export.", err_airport_elements_outside_hierarchy, who,NULL));

		if(who->GetClass() == WED_ObjPlacement::sClass)
		{
			auto obj = static_cast<WED_ObjPlacement *>(who);
			if (int t = obj->HasCustomMSL())
			{
				if (t == 2) // don't warn about set_AGL if the .agp has scrapers
				{
					const agp_t * agp;
					std::string vpath;
					WED_ResourceMgr * rmgr = WED_GetResourceMgr(who->GetArchive()->GetResolver());
					obj->GetResource(vpath);
					if (rmgr && rmgr->GetAGP(vpath, agp))
						for (const auto& o :agp->tiles.front().objs)
							if (o.scp_step > 0.0)
							{
								t = 0;
								break;
							}
				}
				stringstream ss;
				ss << "The use of " << (t == 1 ? "set_MSL=" : "set_AGL=") << (int)obj->GetCustomMSL() << '.' << abs((int)(obj->GetCustomMSL()*10.0)) % 10 << 'm';
				if (t == 1)
				{
					ss << " is not allowed on the scenery gateway.";
					msgs.push_back(validation_error_t(ss.str(), err_object_custom_elev, who, parent_apt));
				}
				else if(t == 2)
				{
					ss << " is discouraged on the scenery gateway. Use only in well justified cases.";
					msgs.push_back(validation_error_t(ss.str(), warn_object_custom_elev, who, parent_apt));
				}
			}
		}
	}

	//--Validate resources-----------------------------------------------------
	IHasResource* who_hasRes = dynamic_cast<IHasResource*>(who);

	if(who_hasRes != NULL)
	{
		std::string res;
		who_hasRes->GetResource(res);

		if(gExportTarget == wet_gateway)
		{
			if(!lib_mgr->IsResourceDefault(res))
				msgs.push_back(validation_error_t(std::string("The library path '") + res + "' is not part of X-Plane's default installation and cannot be submitted to the global airport database.",
				err_gateway_resource_not_in_default_library, who, parent_apt));
			if(lib_mgr->IsResourceDeprecatedOrPrivate(res))
				msgs.push_back(validation_error_t(std::string("The library path '") + res + "' is a deprecated or private X-Plane resource and cannot be used in global airports.",
				err_gateway_resource_private_or_depricated,	who, parent_apt));
		}

		std::string path;
		if (GetSupportedType(res) != -1)  // strictly - only Draped Orthos may have that
			path = gPackageMgr->ComputePath(lib_mgr->GetLocalPackage(), res);
		else
			path = lib_mgr->GetResourcePath(res);

		if(!(FILE_exists(path.c_str()) || ( gExportTarget < wet_gateway && res == "::FLATTEN::.pol")))
				msgs.push_back(validation_error_t(std::string(who->HumanReadableType()) + "'s resource " + res + " cannot be found.", err_resource_cannot_be_found, who, parent_apt));

		//3. What happen if the user free types a real resource of the wrong type into the box?
		bool matches = false;
#define EXTENSION_DOES_MATCH(CLASS,EXT) (who->GetClass() == CLASS::sClass && FILE_get_file_extension(res) == EXT) ? true : false;
		matches |= EXTENSION_DOES_MATCH(WED_DrapedOrthophoto, "pol");
		matches |= EXTENSION_DOES_MATCH(WED_DrapedOrthophoto, FILE_get_file_extension(path)); //This may be a tautology
		matches |= EXTENSION_DOES_MATCH(WED_FacadePlacement,  "fac");
		matches |= EXTENSION_DOES_MATCH(WED_ForestPlacement,  "for");
		matches |= EXTENSION_DOES_MATCH(WED_LinePlacement,    "lin");
		matches |= EXTENSION_DOES_MATCH(WED_ObjPlacement,     "obj");
		matches |= EXTENSION_DOES_MATCH(WED_ObjPlacement,     "agp");
		matches |= EXTENSION_DOES_MATCH(WED_PolygonPlacement, "pol");
		matches |= EXTENSION_DOES_MATCH(WED_StringPlacement,  "str");
		matches |= EXTENSION_DOES_MATCH(WED_AutogenPlacement, "ags");
		matches |= EXTENSION_DOES_MATCH(WED_AutogenPlacement, "agb");
		matches |= EXTENSION_DOES_MATCH(WED_RoadEdge,         "net");

		if(matches == false)
		{
			msgs.push_back(validation_error_t("Resource '" + res + "' does not have the correct file type",
								err_resource_does_not_have_correct_file_type, who, parent_apt));
		}
	}

	WED_GISPolygon * poly = dynamic_cast<WED_GISPolygon  *> (who);
	if (poly)
	{
		ValidateOnePolygon(poly, msgs, parent_apt);
		return;  // There are no nested polygons. No need to digg deeper.
	}

	//-----------------------------------------------------------------------//
	int nn = who->CountChildren();
	for (int n = 0; n < nn; ++n)
	{
		WED_Thing * c = who->GetNthChild(n);
		if(c->GetClass() != WED_Airport::sClass)
			ValidateDSFRecursive(c, lib_mgr, msgs, parent_apt);
	}
}


//------------------------------------------------------------------------------------------------------------------------------------
// ATC VALIDATIONS
//------------------------------------------------------------------------------------------------------------------------------------

static bool ValidateAirportFrequencies(const std::vector<WED_ATCFrequency*> frequencies, WED_Airport* who, validation_error_vector& msgs)
{
	// Collection of all freq by ATC type, regardless of frequency
	std::map<int, std::vector<WED_ATCFrequency*> > any_by_type;
	for (const auto& freq_itr : frequencies)
	{
		AptATCFreq_t freq_info;
		freq_itr->Export(freq_info);
		any_by_type[freq_info.atc_type].push_back(freq_itr);
	}

	std::vector<WED_ATCFrequency* > has_atc;
	bool has_tower = false;

	// Collection of all freq that fall into the airband, i.e.e used by the sim, mapped by frequency
	std::map<int, std::vector<WED_ATCFrequency*> > airband_by_freq;

	//For all groups see if each group has atleast one valid member (especially for Delivery, Ground, and Tower)
	for(const auto& atc_type : any_by_type)
	{
		bool found_one_valid = false;
		bool found_one_oob = false;		// found an out-of-band frequency for our use
		bool is_xplane_atc_related = false;
		//Contains values like "128.80" or "0.25" or "999.13"
		AptATCFreq_t freq_info;

		DebugAssert(!atc_type.second.empty());

		for(const auto& freq : atc_type.second)
		{
			freq->Export(freq_info);
			std::string freq_str = format_freq(freq_info.freq);

			airband_by_freq[freq_info.freq].push_back(freq);

			const int freq_type = ENUM_Import(ATCFrequency, freq_info.atc_type);
			is_xplane_atc_related = freq_type == atc_Delivery || freq_type == atc_Ground || freq_type == atc_Tower;

			int ATC_min_frequency = 118000;   // start of VHF air band
			if(freq_type == atc_AWOS)
				ATC_min_frequency = 108000;       // AWOS can be broadcasted as part of VOR's

			if(freq_type == atc_Tower)
				has_tower = true;
			else if(is_xplane_atc_related)
				has_atc.push_back(freq);

			if(freq_info.freq < ATC_min_frequency || freq_info.freq >= 1000000 || (freq_info.freq >= 137000 && freq_info.freq < 200000) )
			{
				msgs.push_back(validation_error_t(std::string("Frequency ") + freq_str + " not in the range of " + std::to_string(ATC_min_frequency/1000) +
				                                         " .. 137 or 200 .. 1000 MHz.", err_freq_not_between_0_and_1000_mhz, freq, who));
				continue;
			}

			if(freq_info.freq < ATC_min_frequency || freq_info.freq >= 137000)
			{
				found_one_oob = true;
			}
			else
			{
				if (freq_info.freq > 121475 && freq_info.freq < 121525)
						msgs.push_back(validation_error_t(std::string("The ATC frequency ") + freq_str + " is within the guardband of the emergency frequency.",
								err_atc_freq_must_be_on_25khz_spacing,	freq, who));

				int mod25 = freq_info.freq % 25;
				bool is_25k_raster	= mod25 == 0;
				bool is_833k_chan = mod25 == 5 || mod25 == 10 || mod25 == 15;

				if(!is_833k_chan && !is_25k_raster)
				{
					msgs.push_back(validation_error_t(std::string("The ATC frequency ") + freq_str + " is not a valid 8.33kHz channel number.",
						err_atc_freq_must_be_on_8p33khz_spacing, freq, who));
				}
				else if(!is_25k_raster && gExportTarget < wet_xplane_1130)
				{
					msgs.push_back(validation_error_t(std::string("The ATC frequency ") + freq_str + " is not a multiple of 25kHz as required prior to X-plane 11.30.",
						err_atc_freq_must_be_on_25khz_spacing, freq, who));
				}
				else
				{
					if(is_xplane_atc_related)
						found_one_valid = true;

					Bbox2 bounds;
					who->GetBounds(gis_Geo, bounds);
					if(!is_25k_raster && (bounds.ymin() < 34.0 || bounds.xmin() < -11.0 || bounds.xmax() > 35.0) )     // rougly the outline of europe
					{
						msgs.push_back(validation_error_t(std::string("ATC frequency ") + freq_str + " on 8.33kHz raster is used outside of Europe.",
							warn_atc_freq_on_8p33khz_spacing, freq, who));
					}
				}
			}
		}

		if(found_one_valid == false && is_xplane_atc_related)
		{
			stringstream ss;
			ss  << "Could not find at least one VHF band ATC Frequency for group " << ENUM_Desc(ENUM_Import(ATCFrequency, freq_info.atc_type)) << ". "
			    << "VHF band is 118 - 137 MHz and frequency raster 25/8.33kHz depending on targeted X-plane version.";
			msgs.push_back(validation_error_t(ss.str(), err_freq_could_not_find_at_least_one_valid_freq_for_group, atc_type.second, who));
		}
	}

	for (const auto& freq : airband_by_freq)
	{
		std::vector<WED_ATCFrequency *> services_on_same_freq;
		copy_if(freq.second.begin(), freq.second.end(), back_inserter(services_on_same_freq), [](WED_ATCFrequency * itr)
		{
			AptATCFreq_t apt_atc_freq;
			itr->Export(apt_atc_freq);
			const int freq_type = ENUM_Import(ATCFrequency, apt_atc_freq.atc_type);
			return freq_type == atc_AWOS   || freq_type == atc_Delivery ||
				   freq_type == atc_Ground || freq_type == atc_Tower;
		} );

		if (services_on_same_freq.size() > 1)
		{
			msgs.push_back(validation_error_t(std::string("The frequency ") + format_freq(freq.first) + " is used for more than one service at this airport.",
				err_freq_duplicate_freq, services_on_same_freq, who));
		}
	}

	if(!has_atc.empty() && !has_tower)
	{
		msgs.push_back(validation_error_t("This airport has ground or delivery but no tower.  Add a control tower frequency or remove ground/delivery.",
			err_freq_airport_has_gnd_or_del_but_no_tower, has_atc, who));
	}
	return has_tower;
}

static void ValidateOneATCRunwayUse(WED_ATCRunwayUse* use, validation_error_vector& msgs, WED_Airport * apt, const std::vector<int> dep_freqs)
{
	AptRunwayRule_t urule;
	use->Export(urule);
	if(urule.operations == 0)
		msgs.push_back(validation_error_t("ATC runway use must support at least one operation type.", err_rwy_use_must_have_at_least_one_op,  use, apt));
	else if(urule.equipment == 0)
		msgs.push_back(validation_error_t("ATC runway use must support at least one equipment type.", err_rwy_use_must_have_at_least_one_equip, use, apt));

	AptInfo_t ainfo;
	apt->Export(ainfo);
    if(gExportTarget == wet_gateway && ainfo.has_atc_twr)
	if(find(dep_freqs.begin(), dep_freqs.end(), urule.dep_freq) == dep_freqs.end())
	{
		msgs.push_back(validation_error_t("ATC runway use departure frequency is not matching any ATC departure frequency defined at this airport.", err_rwy_use_no_matching_dept_freq, use, apt));
	}
}

typedef std::vector<int> surfWindVec_t;  // the maximum amount of wind from any given direction that should be tested for

static void ValidateOneATCFlow(WED_ATCFlow * flow, validation_error_vector& msgs, const std::set<int>& legal_rwy_oneway, WED_Airport * apt, const std::vector<int>& dep_freqs, surfWindVec_t& sWindsCov)
{
	// Check ATC Flow visibility > 0, ceiling > 0, ICAO code is std::set, at least one arrival and one departure runway and
	// is not using any runway in opposing directions simultaneously for either arr or dep
	// warn if tailwind component is large, i.e. user mixed up wind direction vs landing directions

	std::string name;
	flow->GetName(name);
	AptFlow_t exp;
	flow->Export(exp);
	if(exp.icao.empty())
		msgs.push_back(validation_error_t(std::string("ATC Flow '") + name + "' has a blank ICAO code for its visibility METAR source.", err_flow_blank_ICAO_for_METAR,  flow, apt));
	if( (exp.visibility_sm < 0.0) ||  (exp.ceiling_ft < 0))
		msgs.push_back(validation_error_t(std::string("ATC Flow '") + name + "' ceiling and visibility must be positive numbers.", err_flow_visibility_negative, flow, apt));
	if (exp.visibility_sm > 20.0)
		msgs.push_back(validation_error_t(std::string("ATC Flow '") + name + "' visibility is probably unintentionally high.", warn_atc_flow_visibility_unlikely, flow, apt));
	if (exp.ceiling_ft > 10000.0)
		msgs.push_back(validation_error_t(std::string("ATC Flow '") + name + "' ceiling is probably unintentionally high.", warn_atc_flow_ceiling_unlikely, flow, apt));

	if(name.empty())
		msgs.push_back(validation_error_t("An ATC Flow has a blank name. You must name every flow.", err_flow_blank_name, flow, apt));

	std::vector<WED_ATCWindRule*>	windR;
	std::vector<WED_ATCTimeRule*>	timeR;
	std::vector<WED_ATCRunwayUse*>	useR;

	CollectRecursive(flow, back_inserter(windR),  IgnoreVisiblity, TakeAlways, WED_ATCWindRule::sClass);
	CollectRecursive(flow, back_inserter(timeR), IgnoreVisiblity, TakeAlways, WED_ATCTimeRule::sClass);
	CollectRecursive(flow, back_inserter(useR),  IgnoreVisiblity, TakeAlways, WED_ATCRunwayUse::sClass);

	if(legal_rwy_oneway.count(flow->GetPatternRunway()) == 0)
		msgs.push_back(validation_error_t(std::string("The pattern runway ") + std::string(ENUM_Desc(flow->GetPatternRunway())) + " is illegal for the ATC flow '" + name + "' because it is not a runway at this airport.", err_flow_pattern_runway_not_in_airport, flow, apt));

	// Check ATC Wind rules having directions within 0 ..360 deg, speed from 1..99 knots.  Otherweise XP 10.51 will give an error.

	surfWindVec_t sWindThisFlow(360, 0);
	bool flowCanBeReached = false;

	for(auto wrule : windR)
	{
		AptWindRule_t windData;
		wrule->Export(windData);
		if(windData.icao.empty())
			msgs.push_back(validation_error_t("ATC wind rule has a blank ICAO code for its METAR source.", err_atc_rule_wind_blank_ICAO_for_METAR, wrule, apt));

		if((windData.dir_lo_degs_mag < 0) || (windData.dir_lo_degs_mag > 359) || (windData.dir_hi_degs_mag < 0) || (windData.dir_hi_degs_mag > 360) // 360 is ok with XP10.51, but as a 'from' direction its poor style.
							|| (windData.dir_lo_degs_mag == windData.dir_hi_degs_mag))
			msgs.push_back(validation_error_t("ATC wind rule has invalid from and/or to directions.", err_atc_rule_wind_invalid_directions, wrule, apt));

		if((windData.max_speed_knots < 1) || (windData.max_speed_knots >999))
			msgs.push_back(validation_error_t("ATC wind rule has maximum wind speed outside 1..999 knots range.", err_atc_rule_wind_invalid_speed, wrule, apt));

		int minWindFixed = intlim(windData.dir_lo_degs_mag,0,359);
		int maxWindFixed = intlim(windData.dir_hi_degs_mag,0,359);
		int thisFlowSpdFixed = intlim(windData.max_speed_knots,1,ATC_FLOW_MAX_WIND);

		// get all winds that the rules allow for this flow and and are still "available, i.e. not handled by prior flows already
		if (minWindFixed < maxWindFixed)
		{
			for(int i = minWindFixed; i <= maxWindFixed; i++)
				if(thisFlowSpdFixed > sWindsCov[i])
				{
					flowCanBeReached = true;
					sWindThisFlow[i] = max(sWindThisFlow[i],thisFlowSpdFixed);
				}
		}
		else
		{
			for(int i = minWindFixed; i < 360; i++)
				if(thisFlowSpdFixed > sWindsCov[i])
				{
					flowCanBeReached = true;
					sWindThisFlow[i] = max(sWindThisFlow[i],thisFlowSpdFixed);
				}
			for(int i = 0; i <= maxWindFixed; i++)
				if(thisFlowSpdFixed > sWindsCov[i])
				{
					flowCanBeReached = true;
					sWindThisFlow[i] = max(sWindThisFlow[i],thisFlowSpdFixed);
				}
		}
	}
	if(windR.empty())
		for(int i = 0; i < 360; ++i)
			if(ATC_FLOW_MAX_WIND > sWindsCov[i])
			{
				flowCanBeReached = true;
				sWindThisFlow[i] = ATC_FLOW_MAX_WIND;
			}

	if (!flowCanBeReached)
		msgs.push_back(validation_error_t(std::string("ATC Flow '") + name + "' can never be reached. All winds up to " + std::to_string(ATC_FLOW_MAX_WIND) +
		       " kts are covered by flows listed ahead of it. This is not taking time restrictions into account", warn_atc_flow_never_reached, flow, apt));

	// Check ATC Time rules having times being within 00:00 .. 24:00 hrs, 0..59 minutes and start != end time. Otherweise XP will give an error.
	bool isActive24_7 = true;
	for(auto trule : timeR)
	{
		AptTimeRule_t timeData;
		trule->Export(timeData);
		if((timeData.start_zulu < 0) || (timeData.start_zulu > 2359) || (timeData.end_zulu < 0) || (timeData.end_zulu > 2400)     // yes, 24:00z is OK with XP 10.51
							|| (timeData.start_zulu == timeData.end_zulu) || (timeData.start_zulu % 100 > 59) || (timeData.end_zulu % 100 > 59))
			msgs.push_back(validation_error_t("ATC time rule has invalid start and/or stop time.", err_atc_rule_time_invalid_times, trule, apt));

		if(timeData.start_zulu > 0 || timeData.end_zulu < 2359)
			isActive24_7 = false;

		int wrapped_end_zulu = timeData.start_zulu < timeData.end_zulu ? timeData.end_zulu : timeData.end_zulu + 2400;
		if(wrapped_end_zulu - timeData.start_zulu < 100)
			msgs.push_back(validation_error_t("ATC time rule specifies implausible short duration.", warn_atc_flow_short_time, trule, apt));
	}

	if(isActive24_7 && exp.visibility_sm < 0.1 && exp.ceiling_ft == 0)    // only consider winds covered from now on if its a no vis/time condition flow. May cause a few false tailwind warnings
		for(int i = 0; i < 360; ++i)                                       // in complex multi-time or ceiling flows settings when ALL prior flows have time rules that together cover 24hrs.
			sWindsCov[i] = max(sWindThisFlow[i], sWindsCov[i]);             // Such is bad style - one shold rather have one flow with a time rule followed by a time-unlimited flow.

	std::map<int,std::vector<WED_ATCRunwayUse*> >		arrival_rwys;
	std::map<int,std::vector<WED_ATCRunwayUse*> >		departure_rwys;

	for(auto u : useR)
	{
		ValidateOneATCRunwayUse(u,msgs,apt,dep_freqs);
		int rwy = u->GetRunway();
		if(rwy == atc_Runway_None)
		{
			msgs.push_back(validation_error_t("Runway use has no runway selected.", err_rwy_use_no_runway_selected, u, apt));
		}
		else
		{
			if(u->HasArrivals())
			{
				if(arrival_rwys.count(get_opposite_rwy(rwy)))
				{
					msgs.push_back(validation_error_t("Airport flow has opposite direction arrivals.", err_flow_has_opposite_arrivals, arrival_rwys[get_opposite_rwy(rwy)], apt));
					msgs.back().bad_objects.push_back(u);
				}
				arrival_rwys[rwy].push_back(u);
			}
			if(u->HasDepartures())
			{
				if(departure_rwys.count(get_opposite_rwy(rwy)))
				{
					msgs.push_back(validation_error_t("Airport flow has opposite direction departures.", err_flow_has_opposite_departures, departure_rwys[get_opposite_rwy(rwy)], apt));
					msgs.back().bad_objects.push_back(u);
				}
				departure_rwys[rwy].push_back(u);
			}

			double maxTailwind(0);
			int thisUseHdgMag = ((rwy - atc_1 + 1)/(atc_2 - atc_1) + 1) * 10;
			for(int i = 0; i < 360; i++)
			{
				double relTailWindAngle = i-thisUseHdgMag;
				maxTailwind	= max(maxTailwind, -sWindThisFlow[i] * cos(relTailWindAngle * DEG_TO_RAD));
			}
			// if this is a propper "catch all" last flow, it should have no wind rules at all defined, so maxTailwind is still zero here.
			if(maxTailwind > (u->HasArrivals() ? 10.0 : 15.0))   // allow a bit more tailwind for departure only runways, helps noise abatement one-way flows
			{
					std::string txt("Wind Rules in flow '");
					txt += name + "' allow Runway " + ENUM_Desc(rwy);
					txt += " to be used with up to " + std::to_string(intround(maxTailwind)) + " kts tailwind component @ " + std::to_string(ATC_FLOW_MAX_WIND) + " kts winds";
					msgs.push_back(validation_error_t(txt, warn_atc_flow_excessive_tailwind, u, apt));
			}
		}
	}
	if (arrival_rwys.empty() || departure_rwys.empty())
		msgs.push_back(validation_error_t("Airport flow must specify at least one active arrival and one departure runway", err_flow_no_arr_or_no_dep_runway, flow, apt));
}

static void ValidateATCFlows(const std::vector<WED_ATCFlow*>& flows, const std::vector<WED_ATCFrequency*> ATC_freqs, WED_Airport* apt, validation_error_vector& msgs, const std::set<int>& legal_rwy_oneway)
{
	if(!flows.empty() && gExportTarget == wet_xplane_900)
		msgs.push_back(validation_error_t("ATC flows are only supported in X-Plane 10 and newer.", err_flow_flows_only_for_gte_xp10, flows, apt));

	if(CheckDuplicateNames(flows, msgs, apt, "Two or more airport flows have the same name."))
		return;

	std::vector<int> departure_freqs;
	for(auto f : ATC_freqs)
	{
		AptATCFreq_t freq_info;
		f->Export(freq_info);
		if(ENUM_Import(ATCFrequency, freq_info.atc_type) == atc_Departure)
			departure_freqs.push_back(freq_info.freq);
	}
	surfWindVec_t covSurfWinds(360, 0);                  // winds up to this level have been covered by ATC flows

	for(auto f : flows)
		ValidateOneATCFlow(f, msgs, legal_rwy_oneway, apt, departure_freqs, covSurfWinds);

	int uncovSpd = ATC_FLOW_MAX_WIND;
	if(!flows.empty())
		for(int i = 0; i < 360; i++)
			uncovSpd = min(uncovSpd, covSurfWinds[i]);

	if(uncovSpd < ATC_FLOW_MAX_WIND)
	{
		int i=0;
		while(i<360)
		{
			int uncovHdgMin = -1, uncovHdgMax = -1;

			while (i<360 && covSurfWinds[i] != 	uncovSpd) i++;
			uncovHdgMin = i;
			while (i<360 && covSurfWinds[i] == 	uncovSpd) i++;
			uncovHdgMax = i-1;
			while (i<360 && covSurfWinds[i] != 	uncovSpd) i++;

			if(uncovHdgMax < 360)
			{
				std::string txt("The ATC flows do not cover winds from ");
				txt += std::to_string(uncovHdgMin) + " to " + std::to_string(uncovHdgMax) + " above " + std::to_string(uncovSpd) + " kts.";
				txt += " Remove all time, wind, visibility rules from last flow to make it a 'catch all' flow";
				msgs.push_back(validation_error_t(txt , warn_atc_flow_insufficient_coverage, flows.back(), apt));
			}
		}
	}
}

//------------------------------------------------------------------------------------------------------------------------------------
// AIRPORT VALIDATIONS
//------------------------------------------------------------------------------------------------------------------------------------
#pragma mark -

static int ValidateOneRampPosition(WED_RampPosition* ramp, validation_error_vector& msgs, WED_Airport * apt, const std::vector<WED_Runway *>& runways)
{
	AptGate_t	g;
	ramp->Export(g);
	int is_ai_capable(0);

	if(gExportTarget == wet_xplane_900)
		if(g.equipment != 0)
			if(g.type != atc_ramp_misc || g.equipment != atc_traffic_all)
				msgs.push_back(validation_error_t("Ramp starts with specific traffic and types are only supported in X-Plane 10 and newer.", err_ramp_start_with_specific_traffic_and_types_only_for_gte_xp10, ramp,apt));

	if(g.equipment == 0)
		msgs.push_back(validation_error_t("Ramp starts must have at least one valid type of equipment selected.", err_ramp_start_must_have_at_least_one_equip, ramp,apt));

	if(gExportTarget >= wet_xplane_1050)
	{
		if(g.type == atc_ramp_misc || g.type == atc_ramp_hangar)
		{
			if(!g.airlines.empty() || g.ramp_op_type != ramp_operation_none)
			{
				msgs.push_back(validation_error_t("Ramp operation types and airlines are only allowed at real ramp types, e.g. gates and tie-downs, not misc and hangars.", err_ramp_op_type_and_airlines_only_allowed_at_gates_and_tie_downs, ramp,apt));
			}
		}

		if((g.type == atc_ramp_gate || g.type == atc_ramp_tie_down) && (apt->GetAirportType() == type_Airport))
        {
            double req_rwy_len = 0.0;
            double req_rwy_wid = 0.0;
            bool   unpaved_OK = false;

            switch(g.width)
            {
                case  atc_width_F:
                case  atc_width_E:
                    req_rwy_len = 6000.0; req_rwy_wid = 100.0; break;
                case  atc_width_D:
                case  atc_width_C:
                    req_rwy_len = 3000.0; req_rwy_wid =  70.0; break;
                default:
                    unpaved_OK = true;
            }
            req_rwy_len *= FT_TO_MTR;
            req_rwy_wid *= FT_TO_MTR;

            std::vector<WED_Runway *>::const_iterator r(runways.begin());
            while(r != runways.end())
            {
                if(((*r)->GetSurface() < surf_Grass || (*r)->GetSurface() == surf_Trans || unpaved_OK)
					&& (*r)->GetLength() >= req_rwy_len && (*r)->GetWidth() >= req_rwy_wid)
                        break;
                ++r;
            }

            if(r == runways.end())
            {
                msgs.push_back(validation_error_t("Ramp size is implausibly large given largest available runway at this airport.", warn_ramp_start_size_implausible, ramp, apt));
            }
        }
		if(g.type == atc_ramp_gate || g.type == atc_ramp_tie_down)
			is_ai_capable = 1;

		std::string airlines_str = WED_RampPosition::CorrectAirlinesString(g.airlines);
		std::string orig_airlines_str = ramp->GetAirlines();

		//Our flag to keep going until we find an error
		if(airlines_str.empty())
		{
			//Error:"not really an error, we're just done here"
			return is_ai_capable;
		}

		//Add another space on the end, so everything should be exactly "ABC " or "ABC DEF GHI ..."
		airlines_str.insert(0,1,' ');

		if(airlines_str.size() >= 4)
		{
			if(airlines_str.size() % 4 != 0)
			{
				msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' is not in groups of three letters.", err_ramp_airlines_is_not_in_groups_of_three, ramp, apt));
				return is_ai_capable;
			}
			if (gExportTarget == wet_gateway && airlines_str.size() > 100)
			{
				msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' is too long.", err_ramp_airlines_too_long, ramp, apt));
				return is_ai_capable;
			}

			for(int i = airlines_str.length() - 1; i > 0; i -= 4)
			{
				if(airlines_str[i - 3] != ' ')
				{
					msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' must have a space between every three letter airline code.", err_ramp_airlines_is_not_spaced_correctly, ramp, apt));
					break;
				}

				std::string s = airlines_str.substr(i - 2, 3);

				for(std::string::iterator itr = s.begin(); itr != s.end(); ++itr)
				{
					if(*itr < 'a' || *itr > 'z')
					{
						if(*itr == ' ')
						{
							msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' is not in groups of three letters.", err_ramp_airlines_is_not_in_groups_of_three, ramp, apt));
							return is_ai_capable;
						}
						else
						{
							msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' may contains only lowercase ASCII letters.", err_ramp_airlines_contains_non_lowercase_letters, ramp, apt));
							break;
						}
					}
				}
			}
		}
		else
		{
			msgs.push_back(validation_error_t(std::string("Ramp start airlines std::string '") + orig_airlines_str + "' does not contain at least one valid airline code.", err_ramp_airlines_no_valid_airline_codes, ramp, apt));
		}
	}
	return is_ai_capable;
}

static void ValidateOneRunwayOrSealane(WED_Thing* who, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Runway/Sealane Validation Rules----------------------------------------
		Duplicate Runway/Sealane Name
		Low-end Name rules
		  - Empty Low-end Name
		  - Illegal suffix for low-end runway
		  - Illegal characters in its low-end name
		  - Illegal low-end number, must be between 1 and 36
		High-end Name rules
		  - Empty High-end Name
		  - Illegal suffix for high-end runway
		  - Illegal characters in its high-end name
		  - Illegal high-end number, must be between 19 and 36
		Other Suffix rules
		  - Mismatched suffixes, L vs R etc
		  - Suffix on only one end
		Runway Numbers rules
		  - Low number must be first
		  - High end is not the reciprocal of the low end
		Runway Other rules
		  - Width must be atleast 1 meter wide
		  - Water us bit a valid surface type (runway only)
		  - Overlapping displaced thresholds
		  - Illegal surface roughness, shuold be between 0 and 1, inclusive
		  - Has end outside world map
		  - Needs to be reversed to match its name
		  - Misaligned with its runway name
	 */

	std::string name, n1, n2;
	who->GetName(name);

	std::string::size_type p = name.find("/");
	if (p != name.npos)
	{
		n1 = name.substr(0,p);
		n2 = name.substr(p+1);
	} else
		n1 = name;

	int suf1 = 0, suf2 = 0;
	int	num1 = -1, num2 = -1;

	if (n1.empty())
		msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an empty low-end name.", err_rwy_name_low_name_empty, who,apt));
	else {
		int suffix = n1[n1.length()-1];
		if (suffix < '0' || suffix > '9')
		{
			if (suffix == 'L' || suffix == 'R' || suffix == 'C' || suffix == 'S' || suffix == 'T' || suffix == 'W') suf1 = suffix;
			else msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal suffix for the low-end runway.", err_rwy_name_low_illegal_suffix, who,apt));
			n1.erase(n1.length()-1);
		}

		int i;
		for (i = 0; i < n1.length(); ++i)
		if (n1[i] < '0' || n1[i] > '9')
		{
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal character(s) in its low-end name.", err_rwy_name_low_illegal_characters, who,apt));
			break;
		}
		if (i == n1.length())
		{
			num1 = atoi(n1.c_str());
		}
		if (num1 < 1 || num1 > 36)
		{
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal low-end number, which must be between 1 and 36.", err_rwy_name_low_illegal_end_number, who,apt));
			num1 = -1;
		}
	}

	if (p != name.npos)
	{
		if (n2.empty())	msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an empty high-end name.", err_rwy_name_high_name_empty, who,apt));
		else {
			int suffix = n2[n2.length()-1];
			if (suffix < '0' || suffix > '9')
			{
				if (suffix == 'L' || suffix == 'R' || suffix == 'C' || suffix == 'S' || suffix == 'T' || suffix == 'W' ) suf2 = suffix;
				else msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal suffix for the high-end runway.", err_rwy_name_high_illegal_suffix, who,apt));
				n2.erase(n2.length()-1);
			}

			int i;
			for (i = 0; i < n2.length(); ++i)
			if (n2[i] < '0' || n2[i] > '9')
			{
				msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal character(s) in its high-end name.", err_rwy_name_high_illegal_characters, who,apt));
				break;
			}
			if (i == n2.length())
			{
				num2 = atoi(n2.c_str());
			}
			if (num2 < 19 || num2 > 36)
			{
				msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an illegal high-end number, which must be between 19 and 36.", err_rwy_name_high_illegal_end_number, who,apt));
				num2 = -1;
			}
		}
	}

	if (suf1 != 0 && suf2 != 0)
	{
		if ((suf1 == 'L' && suf2 != 'R') ||
			(suf1 == 'R' && suf2 != 'L') ||
			(suf1 == 'C' && suf2 != 'C') ||
			(suf1 == 'S' && suf2 != 'S') ||
			(suf1 == 'T' && suf2 != 'T') ||
			(suf1 == 'W' && suf2 != 'W'))
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has mismatched suffixes.", err_rwy_name_suffixes_match, who,apt));
	}
	else if((suf1 == 0) != (suf2 == 0))
	{
		msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has a suffix on only one end.", err_rwy_name_suffix_only_on_one_end, who,apt));
	}
	if (num1 != -1 && num2 != -1)
	{
		if (num2 < num1)
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has reversed runway numbers - the low number must be first.", err_rwy_name_reversed_runway_numbers_low_snd, who,apt));
		else if (num2 != num1 + 18)
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has mismatched runway numbers - high end is not the reciprocal of the low-end.", err_rwy_name_mismatched_runway_numbers, who,apt));
	}

	auto * lw = dynamic_cast<WED_GISLine_Width *>(who);
	if(lw)
	{
		if (lw->GetWidth() < 5 || lw->GetLength() < 100)
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' must be at least 5 meters wide by 100 meters long.", err_rwy_unrealistically_small, who, apt));
		Point2 ends[2];

		lw->GetNthPoint(0)->GetLocation(gis_Geo,ends[0]);
		lw->GetNthPoint(1)->GetLocation(gis_Geo,ends[1]);

		Bbox2	runway_extent(ends[0],ends[1]);
		if (runway_extent.xmin() < -180.0 ||
			runway_extent.xmax() >  180.0 ||
			runway_extent.ymin() <  -90.0 ||
			runway_extent.ymax() >   90.0)
		{
			msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' has an end outside the World Map.", err_rwy_end_outside_of_map, who,apt));
		}
		else
		{
				auto grievance = gExportTarget == wet_gateway ? err_rwy_misaligned_with_name : warn_rwy_misaligned_with_name;

				double true_heading, len;
				Point2 ctr;
				Quad_2to1(ends, ctr, true_heading, len);
				double name_heading = num1 * 10.0;
				if (name.back() == 'T')
				{
					// T suffix runways can be named either true north or 'GRID north'. Test if it matches either definition before squawking
					double grid_heading = ctr.y() > 0.0 ? true_heading - ctr.x() : true_heading + ctr.x();
					double grid_delta = fabs(dobwrap(name_heading - grid_heading, -180.0, 180.0));
					double true_delta = fabs(dobwrap(name_heading - true_heading, -180.0, 180.0));
					if(grid_delta > 10.0 && true_delta > 10.0)
						msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' name is not matching neither true nor grid north heading.", grievance, who,apt));
				}
				else
				{
					double mag_heading = true_heading - MagneticDeviation(ctr.x(), ctr.y());
					double mag_delta = fabs(dobwrap(name_heading - mag_heading, -180.0, 180.0));
					if(mag_delta > 135.0)
						msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' needs to be reversed to match its name.", err_rwy_must_be_reversed_to_match_name, who,apt));
					else if(mag_delta > 25.0)
						msgs.push_back(validation_error_t(std::string("The runway/sealane '") + name + "' is misaligned (~" + std::to_string(intround(mag_heading)) + " deg mag) with its runway name.", grievance, who,apt));
				}
		}
	}

	auto * rwy = dynamic_cast<WED_Runway *>(who);
	if (rwy)
	{
		if (rwy->GetSurface() == surf_Water)
			msgs.push_back(validation_error_t("Water is no valid surface type for runways.", err_rwy_surface_water_not_valid, who, apt));
		if (gExportTarget == wet_gateway && rwy->GetSurface() == surf_Trans)
			msgs.push_back(validation_error_t("Transparent runways are not allowed on the Scenery Gateway.", err_rwy_surface_water_not_valid, who, apt));
		if (rwy->GetDisp1() + rwy->GetDisp2() > rwy->GetLength())
			msgs.push_back(validation_error_t(std::string("The runway '") + name + "' has overlapping displaced thresholds.", err_rwy_overlapping_displaced_thresholds, who, apt));
		if (rwy->GetRoughness() < 0.0 || rwy->GetRoughness() > 1.0)
			msgs.push_back(validation_error_t(std::string("The runway '") + name + "' has an illegal surface roughness. It should be in the range 0 to 1.", err_rwy_surface_illegal_roughness, who, apt));
		AptRunway_t r;
		rwy->Export(r);
		if (gExportTarget >= wet_xplane_1200)
		{
			if (r.has_centerline > 0 && r.edge_light_code == apt_edge_LIRL)
				msgs.push_back(validation_error_t("Edge Light intensity will be increased to MIRL by X-Plane 12 due to centerline light presence", warn_rwy_edge_light_not_matching_center_lights, who, apt));
			if ((r.has_tdzl[0] > 0 || r.has_tdzl[1] > 0) && r.edge_light_code <= apt_edge_MIRL)
				msgs.push_back(validation_error_t("Edge Light intensity will be increased to HIRL by X-Plane 12 due to touchdown light presence", warn_rwy_edge_light_not_matching_center_lights, who, apt));
			if ((r.app_light_code[0] > 0 || r.app_light_code[1] > 0) && r.edge_light_code <= apt_edge_MIRL)
				msgs.push_back(validation_error_t("Edge Light intensity will be increased to HIRL by X-Plane 12 due to approach light presence", warn_rwy_edge_light_not_matching_center_lights, who, apt));
		}
#if ROWCODE_105
		if(!all_in_range(r.skids, 0.0f, 1.0f))
			msgs.push_back(validation_error_t("Runway skid mark density and length properties must all be in the range 0 to 1.", err_rwy_dirt_prop_illegal, who, apt));
		if (r.number_size != 0.0 && ( r.number_size < 2.0 || r.number_size > 18.0))
			msgs.push_back(validation_error_t("The size of the runway numbers must be zero (automatic) or between 2 and 18 meters.", err_rwy_number_size_illegal, who, apt));
#endif
	}
}

static void ValidateOneHelipad(WED_Helipad* heli, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Helipad Validation Rules-----------------------------------------------
		Helipad Name rules
		  - Name already used
		  - The selected helipad has no name
		  - Name does not start with letter H
		  - Name is longer than 3 characters
		  - Contains illegal characters, must be in the form of H<number>
		Helipad Width rules
		  - Helipad is less than one meter wide
		  - Helipad is less than one meter long
	 */
	std::string name, n1;
	heli->GetName(name);

	n1 = name;
	if (n1.empty())
	{
		msgs.push_back(validation_error_t("The selected helipad has no name.", err_heli_name_none, heli,apt));
	}
	else
	{
		if (n1[0] != 'H')
		{
			msgs.push_back(validation_error_t(std::string("The helipad '") + name + "' does not start with the letter H.", err_heli_name_does_not_start_with_h, heli, apt));
		}
		else
		{
			if(n1.length() > 3)
				msgs.push_back(validation_error_t(std::string("The helipad '") + name + "' is longer than the maximum 3 characters.", err_heli_name_longer_than_allowed, heli, apt));

			n1.erase(0,1);
			for (int i = 0; i < n1.length(); ++i)
			{
				if (n1[i] < '0' || n1[i] > '9')
				{
					msgs.push_back(validation_error_t(std::string("The helipad '") + name + "' contains illegal characters in its name.  It must be in the form H<number>.", err_heli_name_illegal_characters, heli, apt));
					break;
				}
			}
		}
	}

	if (heli->GetWidth() < 1.0)
		msgs.push_back(validation_error_t(std::string("The helipad '") + name + "' is less than one meter wide.", err_heli_not_adequetely_wide, heli, apt));
	if (heli->GetLength() < 1.0)
		msgs.push_back(validation_error_t(std::string("The helipad '") + name + "' is less than one meter long.", err_heli_not_adequetely_long, heli, apt));
}

static bool has_a_number(const std::string& s)
{
	if (!s.empty())
	{
		return s.find_first_of("0123456789") != std::string::npos;
	}
	return false;
}

static bool is_a_number(const std::string& s)
{
	if (!s.empty())
	{
		if (isspace(s[0]) == false)
		{
			char* p;
			strtod(s.c_str(), &p);
			return *p == 0;
		}
	}
	return false;
}

static bool is_all_alnum(const std::string& s)
{
	return count_if(s.begin(), s.end(), ::isalnum) == s.size();
}


// finds substring, but only if its a full free-standing word, i.e. not just part of a word
static bool contains_word(const std::string& s, const char* word)
{
	size_t p = s.find(word);
	if(p != std::string::npos)
	{
		char c_preceed = p > 0 ? s[p-1] : ' ';
		char c_follow  = p < s.length()+strlen(word) ? s[p+strlen(word)] : ' ';
		if(!isalpha(c_preceed) && !isalpha(c_follow))
			return true;
	}
	return false;
}


static bool air_org_code_valid(int min_char, int max_char, bool mix_letters_and_numbers, const std::string& org_code, std::string& error_content)
{
	if (is_all_alnum(org_code) == false)
	{
		error_content = "'" + org_code + "' contains non-ASCII alphanumeric characters. Use only the standard English alphabet";
		return false;
	}

	if (org_code.size() >= min_char && org_code.size() <= max_char)
	{
		if (mix_letters_and_numbers == false && has_a_number(org_code))
		{
			error_content = "'" + org_code + "' contains numbers when it shouldn't";
			return false;
		}
		else
		{
			return true;
		}
	}
	else
	{
		stringstream ss;
		ss << "'" << org_code << "' should be ";
		if (min_char == max_char)
		{
			ss << min_char;
		}
		else
		{
			ss << "between " << min_char << " and " << max_char;
		}

		ss << " characters long";
		error_content = ss.str();
		return false;
	}
}

static void add_formated_metadata_error(const std::string& error_template, int key_enum, const std::string& error_content, WED_Airport* who, validation_error_vector& msgs, WED_Airport* apt)
{
	char buf[200] = { '\0' };
	snprintf(buf, 200, error_template.c_str(), META_KeyDisplayText(key_enum).c_str(), error_content.c_str());
	msgs.push_back(validation_error_t(std::string(buf), err_airport_metadata_invalid, who, apt));
}

static void ValidateAirportMetadata(WED_Airport* who, validation_error_vector& msgs, WED_Airport * apt)
{
	std::string error_template = "Metadata key '%s' is invalid: %s"; //(Key Display Name/value) is invalid: error_content

	std::vector<std::string> all_keys;

	if(who->ContainsMetaDataKey(wed_AddMetaDataCity))
	{
		std::string city = who->GetMetaDataValue(wed_AddMetaDataCity);
		if (city.empty() == false)
		{
			std::string error_content;

			//This is included because of VTCN being located in Nan, Thailand. strtod turns this into IEEE NaN.
			//Yes, thats a real name, and its probably filled with people named Mr. Null and Ms. Error and their son Bobby Tables
			if (!(city == "Nan" &&  who->GetMetaDataValue(wed_AddMetaDataCountry) == "Thailand"))
			{
				if (is_a_number(city) == true)
				{
					error_content = "City cannot be a number";
				}
			}

			if (error_content.empty() == false)
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataCity, error_content, who, msgs, apt);
			}
		}
		all_keys.push_back(city);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataCountry))
	{
		std::string country = who->GetMetaDataValue(wed_AddMetaDataCountry);

		if (country.size())
		{
			std::string error_content;

			bool has_iso_code = country.size() >= 3;
			for (int i = 0; i < 3 && has_iso_code; i++)
				has_iso_code &= (bool) isalpha(country[i]);
			if (country.size() > 3)
				has_iso_code &= country[3] == ' ';

			if (has_iso_code)
			{
				std::string c = country.substr(0, 3);
				country.erase(0, 3);
				while(country[0] == ' ') country.erase(0, 1);

				has_iso_code = false;
				for(const auto& iso : iso3166_codes)
					if (c == iso.front())
					{
						has_iso_code = true;
						break;
					}
				if(!has_iso_code)
					error_content = std::string("First 3 letters '") + c + "' are not a valid, upper case iso3166 country code";
				else if (country[3] == ' ')
				{
					bool multi_prefix = false;
					std::string d = country.substr(0, 3);
						for (const auto& iso : iso3166_codes)
							if (c == iso.front())
							{
								multi_prefix = true;
								break;
							}
					if (multi_prefix)
						error_content = std::string("Country name has multiple prefixes '") + c + "' and '" + d + "'.Delete all extraneous prefixes but one.";
				}
			}
			else
				error_content = "First 3 letters must be 3-letter iso3166 country code, followed by an optional name";

			if (error_content.size())
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataCountry, error_content, who, msgs, apt);
				error_content.clear();
			}

			if (is_a_number(country))
				error_content = "Country name cannot be a number";
			else if (isdigit(country[0]))
				error_content = "Country name cannot start with a number";

			if (error_content.size())
				add_formated_metadata_error(error_template, wed_AddMetaDataCountry, error_content, who, msgs, apt);
		}
		all_keys.push_back(country);
	}

	bool lat_lon_problems = false;
	if(who->ContainsMetaDataKey(wed_AddMetaDataDatumLat) || who->ContainsMetaDataKey(wed_AddMetaDataDatumLon))
	{
		std::string datum_lat = who->ContainsMetaDataKey(wed_AddMetaDataDatumLat) ? who->GetMetaDataValue(wed_AddMetaDataDatumLat) : "";
		std::string datum_lon = who->ContainsMetaDataKey(wed_AddMetaDataDatumLon) ? who->GetMetaDataValue(wed_AddMetaDataDatumLon) : "";

		if(datum_lat.size() || datum_lon.size())
		{
			lat_lon_problems = true;
			if(!is_a_number(datum_lat))
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataDatumLat, "Not a number", who, msgs, apt);
			}
			if(!is_a_number(datum_lon))
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataDatumLon, "Not a number", who, msgs, apt);
			}
			if(is_a_number(datum_lon) && is_a_number(datum_lat))
			{
				Bbox2 apt_bounds;
				apt->GetBounds(gis_Geo, apt_bounds);
				apt_bounds.expand(1.0/60.0 / cos(apt_bounds.centroid().y() * DEG_TO_RAD), 1.0/60.0);

				Point2 apt_datum(stod(datum_lon), stod(datum_lat));

				if(apt_bounds.contains(apt_datum))
				{
					lat_lon_problems = false;
				}
				else
				{
					if(apt_datum.x() < apt_bounds.xmin() || apt_datum.x() > apt_bounds.xmax())
						add_formated_metadata_error(error_template, wed_AddMetaDataDatumLon,
						"Coordinate not within 1 nm of the airport.", who, msgs, apt);
					if(apt_datum.y() < apt_bounds.ymin() || apt_datum.y() > apt_bounds.ymax())
						add_formated_metadata_error(error_template, wed_AddMetaDataDatumLat,
						"Coordinate not within 1 nm of the airport.", who, msgs, apt);
				}
			}
		}
	}
	if(lat_lon_problems)
	{
		msgs.push_back(validation_error_t(std::string("Metadata 'Datum latitude / longitude' must both be valid and come in a std::pair"), err_airport_metadata_invalid, who, apt));
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataFAA))
	{
		std::string faa_code         = who->GetMetaDataValue(wed_AddMetaDataFAA);
		std::string error_content;

		if(air_org_code_valid(3,5, true, faa_code, error_content) == false && faa_code.empty() == false)
		{
			add_formated_metadata_error(error_template, wed_AddMetaDataFAA, error_content, who, msgs, apt);
		}
		all_keys.push_back(faa_code);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataIATA))
	{
		std::string iata_code        = who->GetMetaDataValue(wed_AddMetaDataIATA);
		std::string error_content;

		if(air_org_code_valid(3,3, false, iata_code, error_content) == false && iata_code.empty() == false)
		{
			add_formated_metadata_error(error_template, wed_AddMetaDataIATA, error_content, who, msgs, apt);
		}
		all_keys.push_back(iata_code);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataICAO))
	{
		std::string icao_code        = who->GetMetaDataValue(wed_AddMetaDataICAO);
		std::string error_content;

		if (!icao_code.empty() && (air_org_code_valid(4,4, false, icao_code, error_content) == false || tolower(icao_code[0]) == 'x'))
		{
			add_formated_metadata_error(error_template, wed_AddMetaDataICAO, error_content, who, msgs, apt);
		}
		all_keys.push_back(icao_code);
	}

	//Local Code (feature request)

	if(who->ContainsMetaDataKey(wed_AddMetaDataLocal))
	{
		std::string code        = who->GetMetaDataValue(wed_AddMetaDataLocal);
		std::string error_content;

		if (!air_org_code_valid(3,7, true, code, error_content) && !code.empty())
		{
			add_formated_metadata_error(error_template, wed_AddMetaDataLocal, error_content, who, msgs, apt);
		}
		all_keys.push_back(code);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataLocAuth))
	{
		std::string code        = who->GetMetaDataValue(wed_AddMetaDataLocAuth);
//		std::string code        = "Metadata '" + META_KeyDisplayText(wed_AddMetaDataLocAuth) + "' should specify an akronym: ";
		std::string error_content;

		if (!air_org_code_valid(3,16, false, code, error_content) && !code.empty())
		{
			code = "Metadata key '" + META_KeyDisplayText(wed_AddMetaDataLocAuth) + "' should specify an akronym: " + error_content;
			msgs.push_back(validation_error_t(code, err_airport_metadata_invalid, who, apt));
		}
		all_keys.push_back(code);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataFAA) && who->ContainsMetaDataKey(wed_AddMetaDataLocal))
	{
		std::string codeFAA    = who->GetMetaDataValue(wed_AddMetaDataFAA);
		std::string codeLocal  = who->GetMetaDataValue(wed_AddMetaDataLocal);
		std::string error      = "Do only specify one of the two Meta-data tags 'FAA code' or 'Local Code' !";

		if (!codeFAA.empty() && !codeLocal.empty())
		{
			msgs.push_back(validation_error_t(error, err_airport_metadata_invalid, who , apt));
		}
		all_keys.push_back(codeFAA);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataRegionCode))
	{
		const int NUM_REGION_CODES = 251;
		std::string legal_region_codes[NUM_REGION_CODES] = {
			"A1", "AG", "AN", "AY", "BG", "BI", "BK", "CF",
			"DT", "DX", "EB", "ED", "EE", "EF", "EG", "EH",
			"EY", "FA", "FB", "FC", "FD", "FE", "FG", "FH",
			"FQ", "FS", "FT", "FV", "FW", "FX", "FY", "FZ",
			"GO", "GQ", "GU", "GV", "HA", "HB", "HC", "HD",
			"K1", "K2", "K3", "K4", "K5", "K6", "K7", "KZ",
			"LI", "LJ", "LK", "LL", "LM", "LO", "LP", "LQ",
			"MB", "MD", "MG", "MH", "MK", "MM", "MN", "MP",
			"NF", "NG", "NI", "NL", "NS", "NT", "NV", "NW",
			"CY", "DA", "DB", "DF", "DG", "DI", "DN", "DR",
			"EI", "EK", "EL", "EN", "EP", "ES", "ET", "EV",
			"FI", "FJ", "FK", "FL", "FM", "FN", "FO", "FP",
			"GA", "GB", "GC", "GE", "GF", "GG", "GL", "GM",
			"HE", "HH", "HK", "HL", "HR", "HS", "HT", "HU",
			"LA", "LB", "LC", "LD", "LE", "LF", "LG", "LH",
			"LR", "LS", "LT", "LU", "LW", "LX", "LY", "LZ",
			"MR", "MS", "MT", "MU", "MW", "MY", "MZ", "NC",
			"NZ", "OA", "OB", "OE", "OI", "OJ", "OK", "OL",
			"OM", "OO", "OP", "OR", "OS", "OT", "OY", "PA",
			"PC", "PG", "PH", "PK", "PL", "PM", "PT", "PW",
			"RC", "RJ", "RK", "RO", "RP", "S1", "SA", "SB",
			"SC", "SE", "SF", "SG", "SK", "SL", "SM", "SO",
			"SP", "SU", "SV", "SY", "TA", "TB", "TD", "TF",
			"TG", "TI", "TJ", "TK", "TL", "TN", "TQ", "TR",
			"TT", "TU", "TV", "TX", "UA", "UB", "UC", "UD",
			"UE", "UG", "UH", "UI", "UK", "UL", "UM", "UN",
			"UO", "UR", "US", "UT", "UU", "UW", "VA", "VC",
			"VD", "VE", "VG", "VH", "VI", "VL", "VM", "VN",
			"VO", "VQ", "VR", "VT", "VV", "VY", "WA", "WB",
			"WI", "WM", "WP", "WR", "WS", "YB", "YM", "ZB",
			"ZG", "ZH", "ZJ", "ZK", "ZL", "ZM", "ZP", "ZS",
			"ZU", "ZW", "ZY" };

		std::string region_code      = who->GetMetaDataValue(wed_AddMetaDataRegionCode);
		all_keys.push_back(region_code);
		::transform(region_code.begin(), region_code.end(), region_code.begin(), ::toupper);

		std::vector<std::string> region_codes = std::vector<std::string>(NUM_REGION_CODES);
		region_codes.insert(region_codes.end(), &legal_region_codes[0], &legal_region_codes[NUM_REGION_CODES]);
		if (find(region_codes.begin(), region_codes.end(), region_code) == region_codes.end())
		{
			add_formated_metadata_error(error_template, wed_AddMetaDataRegionCode, "Unknown Region code", who, msgs, apt);
		}
	}


	if (who->ContainsMetaDataKey(wed_AddMetaDataState))
	{
		std::string state = who->GetMetaDataValue(wed_AddMetaDataState);
		if (state.empty() == false)
		{
			std::string error_content;
			if (is_a_number(state) == true)
			{
				error_content = "State cannot be a number";
			}
			else if (isdigit(state[0]))
			{
				error_content = "State cannot start with a number";
			}

			if (error_content.empty() == false)
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataState, error_content, who, msgs, apt);
			}
		}
		all_keys.push_back(state);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataTransitionAlt))
	{
		std::string transition_alt   = who->GetMetaDataValue(wed_AddMetaDataTransitionAlt);

		if (is_a_number(transition_alt) == true)
		{
			double altitiude = 0.0;

			istringstream iss(transition_alt);
			iss >> altitiude;

			if (altitiude <= 200.0)
			{
				add_formated_metadata_error(error_template, wed_AddMetaDataTransitionAlt, transition_alt + " is too low to be a reasonable value", who, msgs, apt);
			}
		}
		all_keys.push_back(transition_alt);
	}

	if(who->ContainsMetaDataKey(wed_AddMetaDataTransitionLevel))
	{
		std::string transition_level = who->GetMetaDataValue(wed_AddMetaDataTransitionLevel);
		//std::string error_content;

		//No validations for transition level
		all_keys.push_back(transition_level);
	}

	for(std::vector<std::string>::iterator itr = all_keys.begin(); itr != all_keys.end(); ++itr)
	{
		::transform(itr->begin(), itr->end(), itr->begin(), ::tolower);
		if(itr->find("http") != std::string::npos)
		{
			msgs.push_back(validation_error_t("Metadata value " + *itr + " contains 'http', is likely a URL", err_airport_metadata_invalid, who, apt));
		}
	}

	if (who->ContainsMetaDataKey(wed_AddMetaDataCircuits))
	{
		std::string metaValue = who->GetMetaDataValue(wed_AddMetaDataCircuits);
		if (metaValue != "0" && metaValue != "1")
		{
			std::string txt = "Metadata key '" + META_KeyDisplayText(wed_AddMetaDataCircuits) + "'";
			msgs.push_back(validation_error_t(txt + " must be either 0 or 1", err_airport_metadata_invalid, who, apt));
		}
	}

	if (who->ContainsMetaDataKey(wed_AddMetaDataTowerCaps))
	{
		std::string metaValue = who->GetMetaDataValue(wed_AddMetaDataTowerCaps);
		if (metaValue != "atc" && metaValue != "fiso")
		{
			std::string txt = "Metadata key '" + META_KeyDisplayText(wed_AddMetaDataTowerCaps) + "'";
			msgs.push_back(validation_error_t(txt + " must be either 'atc' or 'fiso'", err_airport_metadata_invalid, who, apt));
		}
	}

	std::string txt = "Metadata key '" + META_KeyDisplayText(wed_AddMetaDataLGuiLabel) + "'";

	if (who->ContainsMetaDataKey(wed_AddMetaDataLGuiLabel))
	{
		std::string metaValue = who->GetMetaDataValue(wed_AddMetaDataLGuiLabel);
		if (metaValue != "2D" && metaValue != "3D")
			msgs.push_back(validation_error_t(txt + " must be either '2D' or '3D'", err_airport_metadata_invalid, who, apt));
	}

	if(gExportTarget >= wet_xplane_1130 && gExportTarget != wet_gateway)   // For the gateway target - the gui_label tags are forced prior to export, anyways.
	{                                                                      // So don't bother the user with this detail or force him to std::set it 'right'
		if(who->ContainsMetaDataKey(wed_AddMetaDataLGuiLabel))
		{
			const char * has3D = GatewayExport_has_3d(who) ? "3D" : "2D";
			std::string metaValue = who->GetMetaDataValue(wed_AddMetaDataLGuiLabel);
			if(metaValue != has3D)
				msgs.push_back(validation_error_t(txt + " does not match current (" + has3D + ") scenery content", warn_airport_metadata_invalid, who, apt));
		}
		else
			msgs.push_back(validation_error_t(txt + " does not exist, but is needed by the XP 11.35+ GUI", warn_airport_metadata_invalid, who, apt));
	}

}

static void ValidateOneTaxiSign(WED_AirportSign* airSign, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Taxi Sign Validation Rules---------------------------------------------
		See Taxi Sign spec and parser for detailed validation rules
	 */

	std::string signName;
	airSign->GetName(signName);
	if(signName.empty())
	{
		msgs.push_back(validation_error_t("Taxi Sign is blank.", err_sign_error, airSign, apt));
	}
	else
	{
		//Create the necessary parts for a parsing operation
		parser_in_info in(signName);
		parser_out_info out;

		ParserTaxiSign(in,out);
		if(out.errors.size() > 0)
		{
			int MAX_ERRORS = 12;//TODO - Is this good?
			std::string m;
			for (int i = 0; i < MAX_ERRORS && i < out.errors.size(); i++)
			{
				m += out.errors[i].msg;
				m += '\n';
			}
			msgs.push_back(validation_error_t(m, err_sign_error, airSign,apt));
		}
	}
}

static void ValidateOneTaxiway(WED_Taxiway* twy, validation_error_vector& msgs, WED_Airport * apt)
{
	/*--Taxiway Validation Rules-----------------------------------------------
		Water is not a valide surface type for taxiways
		Outer boundry of taxiway is not at least 3 sided
		Hole of taxiway is not at least 3 sided
	 */

	if(twy->GetSurface() == surf_Water)
		msgs.push_back(validation_error_t("Water is not a valid surface type for taxiways.", err_taxiway_surface_water_not_valid_type, twy,apt));

	IGISPointSequence * ps;
	ps = twy->GetOuterRing();
	if(!ps->IsClosed() || ps->GetNumSides() < 3)
	{
		msgs.push_back(validation_error_t("Outer boundary of taxiway does not have at least 3 sides.", err_taxiway_outer_boundary_does_not_have_at_least_3_sides, twy,apt));
	}
	else
	{
		for(int h = 0; h < twy->GetNumHoles(); ++h)
		{
			ps = twy->GetNthHole(h);
			if(!ps->IsClosed() || ps->GetNumSides() < 3)
			{
				// Ben says: two-point holes are INSANELY hard to find.  So we do the rare thing and intentionally
				// hilite the hole so that the user can nuke it.
				WED_Thing * h = dynamic_cast<WED_Thing *>(ps);
				{
					msgs.push_back(validation_error_t("Taxiway hole does not have at least 3 sides.", err_taxiway_hole_does_not_have_at_least_3_sides, h ? h : (WED_Thing *) twy, apt));
				}
			}
		}
	}
}

static void ValidateOneTruckDestination(WED_TruckDestination* destination,validation_error_vector& msgs, WED_Airport* apt)
{
	std::string name;
	destination->GetName(name);
	std::set<int> truck_types;
	destination->GetTruckTypes(truck_types);

	if (truck_types.empty() == true)
	{
		msgs.push_back(validation_error_t("Truck destination " + name + " must have at least once truck type selected", err_truck_dest_must_have_at_least_one_truck_type_selected, destination,apt));
	}
}

static void ValidateOneTruckParking(WED_TruckParkingLocation* truck_parking, WED_LibraryMgr* lib_mgr, validation_error_vector& msgs, WED_Airport* apt)
{
	std::string name;
	truck_parking->GetName(name);
	int num_cars = truck_parking->GetNumberOfCars();

	int MAX_CARS = 10;

	if (num_cars < 0 || num_cars > MAX_CARS)
	{
		std::string ss("Truck parking location ") ;
		ss += name +" must have a car count between 0 and " + std::to_string(MAX_CARS);
		msgs.push_back(validation_error_t(ss, err_truck_parking_car_count, truck_parking, apt));
	}
	AptTruckParking_t park;
	truck_parking->Export(park);
	if (gExportTarget >= wet_gateway)
	{
		if(park.vpath.size())
			msgs.push_back(validation_error_t("Custom Trucks are not allowed on the gateway", err_truck_custom, truck_parking, apt));
	}
}

MFMemFile * ReadCIFP()
{
	WED_file_cache_request  mCacheRequest;
	mCacheRequest.in_domain = cache_domain_metadata_csv;    // cache expiration time = 1 day
	mCacheRequest.in_folder_prefix = "scenery_packs";
	mCacheRequest.in_url = WED_URL_CIFP_RUNWAYS;

	WED_file_cache_response res = gFileCache.request_file(mCacheRequest);

	/* ToDo: get a better way to do automatic retryies for cache updates.
		Ultimately, during the actual gateway submission we MUST wait and get full verification
		at all times. */
	for (int i = 0; i < 5; ++i)
	{
		if (res.out_status == cache_status_downloading)
		{
			printf("Download of Runway Data in progress, trying again in 1 sec\n");
			this_thread::sleep_for(chrono::seconds(1));
			res = gFileCache.request_file(mCacheRequest);
		}
	}

	if (res.out_status != cache_status_available)
	{
		stringstream ss;
		ss << "Error downloading list of CIFP data compliant runway names and coordinates from scenery gateway.\n" << res.out_error_human;
		ss << "\nSkipping this part of validation.";
		DoUserAlert(ss.str().c_str());
		return nullptr;
	}
	else
		return MemFile_Open(res.out_path.c_str());
}


static void ValidateCIFP(const std::vector<WED_Runway *>& runways, const std::vector<WED_Sealane *>& sealanes, const std::set<int>& legal_rwy_oneway,
				MFMemFile * mf, validation_error_vector& msgs, WED_Airport* apt)
{
		std::map<int,Point3> CIFP_rwys;
		std::set<int> rwys_missing;
		std::string icao;

		if(apt->ContainsMetaDataKey(wed_AddMetaDataICAO))
			icao = apt->GetMetaDataValue(wed_AddMetaDataICAO);
		if (icao.empty() && apt->ContainsMetaDataKey(wed_AddMetaDataFAA))
			icao = apt->GetMetaDataValue(wed_AddMetaDataFAA);
		if (icao.empty() && apt->ContainsMetaDataKey(wed_AddMetaDataLocal))
		{
			icao = apt->GetMetaDataValue(wed_AddMetaDataLocal);
			if (!icao.empty())	icao = "*";  // choose something that NEVER matches any CIFP data
		}
		if (icao.empty())
			apt->GetICAO(icao);

		if (mf)
		{
			MFScanner	s;
			MFS_init(&s, mf);
			MFS_string_eol(&s,NULL);    // skip first line, so Tyler can put a comment/version in there

			while(!MFS_done(&s))        // build a list of all runways CIFP dats knows about at this airport
			{
				if(MFS_string_match_no_case(&s,icao.c_str(),false))
				{
					std::string rnam;
					MFS_string(&s,&rnam);
					double lat = MFS_double(&s);
					double lon = MFS_double(&s);
					double disp= MFS_double(&s);

					int rwy_enum=ENUM_LookupDesc(ATCRunwayOneway,rnam.c_str());
					if (rwy_enum != atc_rwy_None)
					{
						CIFP_rwys[rwy_enum]=Point3(lon,lat,disp);
						rwys_missing.insert(rwy_enum);
					}
				}
				MFS_string_eol(&s,NULL);
			}
		}
		// first check: all runway present at current airport

		for(auto r : legal_rwy_oneway)
			rwys_missing.erase(r);

		for(auto i : sealanes)
		{
			std::string name;	i->GetName(name);
			std::vector<std::string> parts;
			tokenize_string(name.begin(),name.end(),back_inserter(parts), '/');

			for(auto p : parts)
			{
				if(p.back() == 'W')	p.pop_back();                       // We want to allow sealanes with or without W suffix to satisfy CIFP validation
				int e = ENUM_LookupDesc(ATCRunwayOneway,p.c_str());
				if(legal_rwy_oneway.find(e) == legal_rwy_oneway.end())   // but only if that name does not collide with a paved runway at the same airport
					rwys_missing.erase(e);
			}
		}
		if (!rwys_missing.empty())
		{
			std::string msg = "Could not find runway(s) ";
			for(auto r : rwys_missing)
				if(r > 1) { msg += ENUM_Desc(r); msg += " "; }
			msg += "required by CIFP data at airport " + icao + ". ";
			msgs.push_back(validation_error_t(msg, err_airport_no_runway_matching_cifp, apt, apt));
		}

		// second check: all verify location accuracy of runways present

		for(auto r : runways)
		{
			int r_enum[2];
			std::pair<int,int> p = r->GetRunwayEnumsOneway();
			r_enum[0] = p.first; r_enum[1] = p.second;

			Point2 r_loc[2];
			r->GetSource()->GetLocation(gis_Geo, r_loc[0]);
			r->GetTarget()->GetLocation(gis_Geo, r_loc[1]);

			float CIFP_LOCATION_ERROR = 10.0;

			if(r->GetSurface() >= surf_Grass)   // for unpaved runways ...
			{
				float r_wid = r->GetWidth() / 2.0;
				CIFP_LOCATION_ERROR =  fltlim(r_wid, CIFP_LOCATION_ERROR, 50.0);   // allow the error circle to be as wide as a unpaved runway, within reason
			}

			for(int i = 0; i < 2; ++i)       // loop to cover both runway ends
			{
				std::map<int,Point3>::iterator r_cifp;
				if((r_cifp = CIFP_rwys.find(r_enum[i])) != CIFP_rwys.end())     // check if this runway is mentioned in CIFP data
				{
					Point2 rwy_cifp(Point2(r_cifp->second.x, r_cifp->second.y));
					float rwy_err = LonLatDistMeters(r_loc[i], rwy_cifp);

					Point2 thr_cifp(rwy_cifp);
					if (r_cifp->second.z > 0.0)
					{
						Point2 opposing_rwy_cifp(Point2(CIFP_rwys[r_enum[1-i]].x, CIFP_rwys[r_enum[1-i]].y));      // what to do if CIFP only exist for one runway end ?

						float rwy_len_cifp = LonLatDistMeters(rwy_cifp, opposing_rwy_cifp);
						thr_cifp += Vector2(rwy_cifp,opposing_rwy_cifp) / rwy_len_cifp * r_cifp->second.z;
					}

					Point2 thr_loc(r_loc[i]);
					if (i)
					{
						Point2 corners[4];
						if (r->GetCornersDisp2(corners))
							thr_loc = Midpoint2(corners[0],corners[3]);
					}
					else
					{
						Point2 corners[4];
						if (r->GetCornersDisp1(corners))
							thr_loc = Midpoint2(corners[1],corners[2]);
					}

					float thr_err = LonLatDistMeters(thr_loc, thr_cifp);

					if (thr_err > CIFP_LOCATION_ERROR)
					{
						stringstream ss;
						if (rwy_err < CIFP_LOCATION_ERROR)
							ss  << "Runway " << ENUM_Desc(r_cifp->first) << " threshold displacement not matching gateway CIFP data. Move runway displaced threshold to indicated location.";
						else
							ss  << "Runway " << ENUM_Desc(r_cifp->first) << " threshold not within " <<  CIFP_LOCATION_ERROR << "m of location mandated by gateway CIFP data.";
						msgs.push_back(validation_error_t(ss.str(), err_runway_matching_cifp_mislocated, r, apt));
#if DEBUG_VIS_LINES
						const int NUM_PTS = 20;
						Point2 pt_cir[NUM_PTS];
						for (int j = 0; j < NUM_PTS; ++j)
							pt_cir[j] = Point2(CIFP_LOCATION_ERROR*sin(2.0*j*M_PI/NUM_PTS), CIFP_LOCATION_ERROR*cos(2.0*j*M_PI/NUM_PTS));

						MetersToLLE(thr_cifp, NUM_PTS, pt_cir);
						for (int j = 0; j < NUM_PTS; ++j)
							debug_mesh_line(pt_cir[j],pt_cir[(j+1)%NUM_PTS], DBG_LIN_COLOR);
#endif
					}
					if (rwy_err > CIFP_LOCATION_ERROR)
					{
						stringstream ss;
						if (thr_err > CIFP_LOCATION_ERROR)
							ss  << "Runway " << ENUM_Desc(r_cifp->first) << " end not within " <<  CIFP_LOCATION_ERROR << "m of location recommended by gateway CIFP data.";
						else
							ss  << "Runway " << ENUM_Desc(r_cifp->first) << " end not within " <<  CIFP_LOCATION_ERROR << "m of location recommended by gateway CIFP data. Move runway end to indicated location and pull back displaced threshold distance so runway threshold stays at current location";
						msgs.push_back(validation_error_t(ss.str(), warn_runway_matching_cifp_mislocated, r, apt));
#if DEBUG_VIS_LINES
						const int NUM_PTS = 20;
						Point2 pt_cir[NUM_PTS];
						for (int j = 0; j < NUM_PTS; ++j)
							pt_cir[j] = Point2(CIFP_LOCATION_ERROR*sin(2.0*j*M_PI/NUM_PTS), CIFP_LOCATION_ERROR*cos(2.0*j*M_PI/NUM_PTS));
						MetersToLLE(rwy_cifp, NUM_PTS, pt_cir);
						for (int j = 0; j < NUM_PTS; j+=2)                                             // draw a dashed circle only, as its only a warning
							debug_mesh_line(pt_cir[j],pt_cir[(j+1)%NUM_PTS], DBG_LIN_COLOR);
#endif
					}
				}
			}
		}
}

static void ValidateAptName(const std::string name, const std::string icao, validation_error_vector& msgs, WED_Airport* apt)
{
	if (name.empty())
		msgs.push_back(validation_error_t("Airport has no name.", err_airport_name, apt, apt));
	else
	{
		validate_error_t err_type = gExportTarget == wet_gateway ? err_airport_name : warn_airport_name_style;

		if (strlen_utf8(name) > 30)
			msgs.push_back(validation_error_t(std::string("Airport name '") + name + "' is longer than 30 characters.", err_type, apt, apt));

		if (isspace(name[0]) || isspace(name[name.length() - 1]))
			msgs.push_back(validation_error_t("Airport name includes leading or trailing spaces.", err_type, apt, apt));

		int lcase = count_if(name.begin(), name.end(), ::islower);
		int ucase = count_if(name.begin(), name.end(), ::isupper);
		if (ucase > 2 && lcase == 0)
			msgs.push_back(validation_error_t("Airport name is all upper case.", err_type, apt, apt));

		std::string name_lcase(name), icao_lcase(icao);
		::transform(name_lcase.begin(), name_lcase.end(), name_lcase.begin(), ::tolower);  // waiting for C++11 ...
		::transform(icao_lcase.begin(), icao_lcase.end(), icao_lcase.begin(), ::tolower);  // waiting for C++11 ...

		if (contains_word(name_lcase, "airport"))
			msgs.push_back(validation_error_t("The airport name should not include the word 'Airport'.", warn_airport_name_style, apt, apt));
		if (contains_word(name_lcase, "international") || contains_word(name_lcase, "int") || contains_word(name_lcase, "regional")
			|| contains_word(name_lcase, "municipal"))
			msgs.push_back(validation_error_t("The airport name should use the abbreviations 'Intl', 'Rgnl' and 'Muni' instead of full words.", warn_airport_name_style, apt, apt));
		if (icao_lcase != "niue" && contains_word(name_lcase, icao_lcase.c_str()))
			msgs.push_back(validation_error_t("The airport name should not include the ICAO code. Use the common name only.", warn_airport_name_style, apt, apt));

		size_t p = name.find_first_of("({[");
		if(p != std::string::npos)
		{
			size_t p2 = name.find_first_of(")}");
			if((p2 - p) == 2 && name_lcase[p+1] == 'x')
				msgs.push_back(validation_error_t("A closed airports name must start with '[X]'", err_type, apt, apt));
		}
		
	}
	if (icao.empty())
		msgs.push_back(validation_error_t(std::string("The airport '") + name + "' has an empty Airport ID.", err_airport_icao, apt, apt));
	else if (!is_all_alnum(icao))
		msgs.push_back(validation_error_t(std::string("The Airport ID for airport '") + name + "' must contain ASCII alpha-numeric characters only.", err_airport_icao, apt, apt));
}

static bool near_but_not_on_boundary(Point2& p)
{
	double dlon = fabs(round(p.x()) - p.x());
	double dlat = fabs(round(p.y()) - p.y());
	if(dlon == 0.0 || dlat == 0.0)
		return false;
	return  dlon < 3 * MTR_TO_DEG_LAT || dlat <  2 * MTR_TO_DEG_LAT;    // not precise - fast, but good enough. There are no roads at high lattitudes :)
}

static void ValidateRoads(const std::vector<WED_RoadEdge *> roads, validation_error_vector& msgs, WED_Airport* apt, const Bbox2& roads_bbox)
{
	// Hard problems
	// referencing unknown (v)road-type (e.g. after changing the resource property)

	// Soft problems (only partially implemented, yet)
	// zero length segments (length under 3m)
	// disconnected vertices
	// T-junctions                                                     not yet done
	// colocated segments (sharing both ends with another segment)

	// Style issues - Gateway no-no's
	// resource not right

	std::unordered_map<WED_Thing *, Point2> nodes;
	nodes.reserve(roads.size());

	std::set<WED_RoadEdge*> roads_outside, roads_bad_resource;

	for(auto r : roads)
	{
		if(r->GetStartLayer() < 0 || r->GetStartLayer() > 5 ||
			 r->GetEndLayer() < 0 || r->GetEndLayer() > 5)
			msgs.push_back(validation_error_t(std::string("All road layers must be in the range of 0 to 5"), err_net_resource, r, apt));

		if(r->GetNthSource(0) == r->GetNthSource(1))
			msgs.push_back(validation_error_t("Road edge erroneous. Loop to itself.", err_net_edge_loop, r, apt));

		Bezier2 s;
		int ns = r->GetNumSides();
		for(int i = 0; i < ns; i++)
		{
			r->GetSide(gis_Geo, i, s);

			if(!r->IsValidSubtype())
				msgs.push_back(validation_error_t("Road references undefined road type", err_net_undefined_type, r, apt));

			if(i == 0)
			{
				nodes[dynamic_cast<WED_Thing *>(r->GetNthPoint(i))] = s.p1;
				if(near_but_not_on_boundary(s.p1))
					msgs.push_back(validation_error_t("Road nodes must be either exactly on or a few meters away from DSF tile boundaries.", err_net_crosses_tile_bdy, r, apt));
			}
			nodes[dynamic_cast<WED_Thing *>(r->GetNthPoint(i+1))] = s.p2;
			if(near_but_not_on_boundary(s.p2))
				msgs.push_back(validation_error_t("Road nodes must be either exactly on or a few meters away from DSF tile boundaries.", err_net_crosses_tile_bdy, r, apt));

			if (gExportTarget >= wet_gateway)
				if (!roads_bbox.contains(s.as_segment()))
					roads_outside.insert(r);
		}

		if(gExportTarget >= wet_gateway)
		{
#if 0
			msgs.push_back(validation_error_t("Roads networks are not (yet) allowed on the gateway", err_net_resource, roads, apt));
			return;
#else
			std::string res;
			r->GetResource(res);
			if (res != "lib/g10/roads.net" && res != "lib/g10/roads_EU.net")
				roads_bad_resource.insert(r);
#endif
		}
	}
	if (roads_outside.size())
	{
		msgs.push_back(validation_error_t("Road network stretches too far away from airport", err_net_outside_apt, roads_outside, apt));
		debug_mesh_segment(roads_bbox.left_side(), DBG_LIN_COLOR);
		debug_mesh_segment(roads_bbox.right_side(), DBG_LIN_COLOR);
		debug_mesh_segment(roads_bbox.top_side(), DBG_LIN_COLOR);
		debug_mesh_segment(roads_bbox.bottom_side(), DBG_LIN_COLOR);
	}
	if (roads_bad_resource.size())
		msgs.push_back(validation_error_t("Only roads from lib/g10/roads.net or lib/g10/roads_EU.net are allowed on the gateway", err_net_resource, roads_bad_resource, apt));

	// any nodes too close to each other and not connected
	for(auto x = nodes.begin(); x != nodes.end(); ++x)
	{
		if(x->first->CountViewers() > 1)
		{
			std::set<WED_Thing *> viewers;
			x->first->GetAllViewers(viewers);
			int layers[5] = { 0 };
			for(auto v : viewers)
			{
				bool isStart = v->GetNthSource(0) == x->first;
				if(auto e = dynamic_cast<WED_RoadEdge *>(v))
					if(isStart)
						++layers[intlim(e->GetStartLayer(), 0, 4)];
					else
						++layers[intlim(e->GetEndLayer(), 0, 4)];
			}
			for(int i = 0; i < 5; i++)
				if(layers[i] == 1)
				{
					msgs.push_back(validation_error_t("Mismatched road layers at intersection", warn_net_level_mismatch, x->first, apt));
					break;
				}
		}

		auto y = x;
		for(++y; y !=  nodes.end(); ++y)
		{
			if(LonLatDistMeters(x->second, y->second) < 3.0)
			{
				std::set<WED_Thing *> sx, sy;
				x->first->GetAllViewers(sx);
				if(sx.empty()) sx.insert(x->first->GetParent());
				y->first->GetAllViewers(sy);
				if(sy.empty()) sy.insert(y->first->GetParent());
				bool isShort = false;

				for(auto xi : sx)
				{
					for(auto yi : sy)
					{
						if(xi == yi)
						{
							msgs.push_back(validation_error_t("Road has one or more short (<3m) segments", err_net_zero_length, xi, apt));
							isShort = true;
							break;
						}
						auto re_x = dynamic_cast<WED_RoadEdge *>(xi);
						auto re_y = dynamic_cast<WED_RoadEdge *>(yi);
						std::string res_x, res_y;
						re_x->GetResource(res_x);
						re_y->GetResource(res_y);
						if(res_x != res_y)
						{
							isShort = true; // dont run doubled nodes check on unmerged roads belonging to different nets
							break;
						}
					}
					if (isShort) break;
				}
				if(!isShort)
					if(x->first->CountViewers() == 0 || y->first->CountViewers() == 0)
					{
						std::vector<WED_Thing *> unmergeable { x->first, y->first };
						msgs.push_back(validation_error_t("Road intersections can not be at shape points, split and merge.", err_net_unmerged, unmergeable, apt));
					}
					else
					{
						std::vector<WED_Thing *> unmerged { x->first, y->first };
						if( x->second.x() != (double) ((int) x->second.x()) && x->second.y() != (double) ((int) x->second.y()) )
							msgs.push_back(validation_error_t("Doubled road junction. These should be merged.", err_net_unmerged, unmerged, apt));
					}
			}
		}
	}
}

void ValidateOneViewpoint(WED_TowerViewpoint* v, const std::vector<WED_ObjPlacement*>objs, validation_error_vector& msgs, WED_Airport* apt)
{
	AptTowerPt_t info;
	v->Export(info);

	double closest_dist(99999);
	WED_ObjPlacement* closest_obj = nullptr;
for (auto o : objs)
		if (o->GetTowerViewHgt() >= 0.0)
		{
			Point2 obj_loc;
			o->GetLocation(gis_Geo, obj_loc);
			double dist = LonLatDistMeters(info.location, obj_loc);
			if (dist < closest_dist)
			{
				closest_dist = dist;
				closest_obj = o;
			}
		}

	if (closest_obj == nullptr) return;

	if (closest_dist < 10.0)
	{
		if (fabs(closest_obj->GetTowerViewHgt() - info.height_ft * FT_TO_MTR) > 0.3)
		{
			char c[100];
			double x = closest_obj->GetTowerViewHgt();
			snprintf(c, sizeof(c), "Tower Viewpoint height does not match nearby tower object cabin height of %.1lf%s",
				x * (gIsFeet ? MTR_TO_FT : 1.0), gIsFeet ? "ft" : "m");
			std::vector<WED_Thing*> parts;
			parts.push_back(v);
			parts.push_back(closest_obj);
			msgs.push_back(validation_error_t(c, warn_viewpoint_mislocated, parts, apt));
		}
	}
	else
	{
		std::vector<WED_Thing*> parts;
		parts.push_back(v);
		parts.push_back(closest_obj);
		msgs.push_back(validation_error_t("Tower Viewpoint not near tower object", warn_viewpoint_mislocated, parts, apt));
	}

}

//------------------------------------------------------------------------------------------------------------------------------------
#pragma mark -
//------------------------------------------------------------------------------------------------------------------------------------

static void ValidateOneAirport(WED_Airport* apt, validation_error_vector& msgs, WED_LibraryMgr * lib_mgr, WED_ResourceMgr * res_mgr, MFMemFile * mf)
{
	std::vector<WED_Runway *>			runways;
	std::vector<WED_Helipad *>			helipads;
	std::vector<WED_Sealane *>			sealanes;
	std::vector<WED_AirportSign *>		signs;
	std::vector<WED_Taxiway *>			taxiways;
	std::vector<WED_TruckDestination*>   truck_destinations;
	std::vector<WED_TruckParkingLocation*> truck_parking_locs;
	std::vector<WED_TaxiRoute *>			taxiroutes;
	std::vector<WED_RampPosition*>		ramps;
	std::vector<WED_AirportBoundary *>	boundaries;
	std::vector<WED_ATCFlow *>			flows;
	std::vector<WED_ATCFrequency*>		freqs;
	std::vector<WED_TowerViewpoint*>		viewpts;
	std::vector<WED_ObjPlacement*>		objects;
	std::vector<WED_RoadEdge*>			roads;

	std::vector<WED_DrapedOrthophoto *>	orthos;

	// those Thing <-> Entity dynamic_cast's take forever. 50% of CPU time in validation is for casting.
	// CollectRecursive(apt, back_inserter(runways),  WED_Runway::sClass);
	//CollectRecursive(apt, back_inserter(sealanes), WED_Sealane::sClass);
	// ...
	// so replace this by ONE recursion that captures all we need

	std::function<void(WED_Thing *)> CollectEntitiesRecursive = [&] (WED_Thing * thing)
	{
		const auto c = thing->GetClass();
#define COLLECT(type, type_name) \
		if(c == type::sClass) { \
			auto p = static_cast<type *>(thing); \
			if(!p->GetHidden())	type_name.push_back(p); \
			return; \
		}
			 COLLECT(WED_Runway,       runways)
		else COLLECT(WED_Helipad,      helipads)
		else COLLECT(WED_Sealane,      sealanes)
		else COLLECT(WED_AirportSign,  signs)
		else COLLECT(WED_Taxiway,      taxiways)
		else COLLECT(WED_RampPosition, ramps)
		else COLLECT(WED_AirportBoundary,      boundaries)
		else COLLECT(WED_TowerViewpoint,       viewpts)
		else COLLECT(WED_ObjPlacement,         objects)
		else COLLECT(WED_TruckDestination,     truck_destinations)
		else COLLECT(WED_TruckParkingLocation, truck_parking_locs)
		else COLLECT(WED_TaxiRoute,            taxiroutes)
		else COLLECT(WED_DrapedOrthophoto,     orthos)
		else COLLECT(WED_RoadEdge,  		   roads)
#undef COLLECT
		else if (c == WED_ATCFlow::sClass) {
				auto p = static_cast<WED_ATCFlow *>(thing);
				if (p) flows.push_back(p);
				return;
		}
		else if (c == WED_ATCFrequency::sClass) {
			auto p = static_cast<WED_ATCFrequency *>(thing);
			if (p) freqs.push_back(p);
			return;
		}
		else
		{
			auto p = dynamic_cast<WED_Entity *>(thing);
			if(!p || p->GetHidden()) return;         // don't recurse into non-entities, we don't need what could be in there
		}
		int nc = thing->CountChildren();
		for (int n = 0; n < nc; ++n)
			CollectEntitiesRecursive(thing->GetNthChild(n));
	};

	CollectEntitiesRecursive(apt);

	std::vector<WED_Thing *>			runway_or_sealane;
	copy(runways.begin(), runways.end(), back_inserter(runway_or_sealane));
	copy(sealanes.begin(), sealanes.end(), back_inserter(runway_or_sealane));

	std::vector<WED_TaxiRoute *>	GT_routes;
	copy_if(taxiroutes.begin(), taxiroutes.end(), back_inserter(GT_routes), [](WED_TaxiRoute * t) { return t->AllowTrucks(); });

	std::set<int>		legal_rwy_oneway;
	std::set<int>		legal_rwy_twoway;
	WED_GetAllRunwaysOneway(apt, legal_rwy_oneway);
	WED_GetAllRunwaysTwoway(apt, legal_rwy_twoway);

	std::string name, icao;
	apt->GetName(name);
	apt->GetICAO(icao);

	ValidateAptName(name, icao, msgs, apt);

	validate_error_t err_type = gExportTarget == wet_gateway ? err_airport_no_rwys_sealanes_or_helipads : warn_airport_no_rwys_sealanes_or_helipads;
	switch(apt->GetAirportType())
	{
		case type_Airport:
			if(runways.empty())
				msgs.push_back(validation_error_t("The airport contains no runways.", err_type, apt,apt));
			break;
		case type_Heliport:
			if(helipads.empty())
				msgs.push_back(validation_error_t("The heliport contains no helipads.", err_type, apt,apt));
			break;
		case type_Seaport:
			if(sealanes.empty())
				msgs.push_back(validation_error_t("The seaport contains no sea lanes.", err_type, apt,apt));
			break;
		default:
			Assert("Unknown Airport Type");
	}

	std::set<WED_Thing*> points = WED_select_doubles(apt);
	if (points.size())
		msgs.push_back(validation_error_t("Airport contains doubled ATC routing nodes. These should be merged.", err_airport_ATC_network, points, apt));

	CheckDuplicateNames(helipads,msgs,apt,"A helipad name is used more than once.");
	if(!CheckDuplicateNames(runway_or_sealane,msgs,apt,"A runway or sealane name is used more than once."))
	{
	   // there checks in these that create utterly misleading results if runway names are ambigeous
		WED_DoATCRunwayChecks(*apt, msgs, taxiroutes, runways, legal_rwy_oneway, legal_rwy_twoway, flows, res_mgr, ramps, roads);
		ValidateATCFlows(flows, freqs, apt, msgs, legal_rwy_oneway);
	}

	bool has_ATC = ValidateAirportFrequencies(freqs, apt, msgs);

	for(auto s : signs)
		ValidateOneTaxiSign(s, msgs, apt);

	for (auto v : viewpts)
		ValidateOneViewpoint(v, objects, msgs, apt);

	for(auto t : taxiways)
		ValidateOneTaxiway(t, msgs, apt);

	for(auto t_dest : truck_destinations)
		ValidateOneTruckDestination(t_dest, msgs, apt);

	for(auto t_park : truck_parking_locs)
		ValidateOneTruckParking(t_park, lib_mgr, msgs ,apt);

	for(auto r : runway_or_sealane)
		ValidateOneRunwayOrSealane(r, msgs, apt);

	for(auto h : helipads)
		ValidateOneHelipad(h, msgs,apt);

	int ai_useable_ramps = 0;
	for(auto r : ramps)
		ai_useable_ramps += ValidateOneRampPosition(r, msgs, apt, runways);


	if(gExportTarget >= wet_xplane_1050)
	{
		ValidateAirportMetadata(apt,msgs,apt);
		if(has_ATC && ai_useable_ramps < 1)
			msgs.push_back(validation_error_t("Airports with ATC towers frequencies must have at least one Ramp Start of type=gate or tiedown.", err_ramp_need_starts_suitable_for_ai_ops, apt, apt));
	}

	err_type = gExportTarget == wet_gateway ? err_airport_impossible_size : warn_airport_impossible_size;
	Bbox2 bounds;
	apt->GetBounds(gis_Geo, bounds);
	int lg_apt_mult = ((icao == "KEDW" || icao == "KSEA") ? 3.0 : 1.0);  // runways on all surrounding salt flats included or space needle
	if(bounds.xspan() > lg_apt_mult * MAX_SPAN_GATEWAY_NM / 60.0 / cos(bounds.centroid().y() * DEG_TO_RAD) ||     // correction for higher lattitudes
			bounds.yspan() > lg_apt_mult* MAX_SPAN_GATEWAY_NM / 60.0)
	{
		msgs.push_back(validation_error_t("This airport is impossibly large. Perhaps a part of the airport has been accidentally moved far away or is not correctly placed in the hierarchy?", err_type, apt,apt));
	}

	if (truck_parking_locs.size() && GT_routes.empty())
		msgs.push_back(validation_error_t("Truck parking locations require at least one taxi route for ground trucks", err_truck_parking_no_ground_taxi_routes, truck_parking_locs.front(), apt));

	if(GT_routes.size() && truck_parking_locs.empty())
		msgs.push_back(validation_error_t("Ground routes are defined, but no service vehicle starts. This disables all ground traffic, including auto generated pushback vehicles.", warn_truckroutes_but_no_starts, apt,apt));

	if(gExportTarget == wet_gateway)
	{
		if(!runways.empty() && boundaries.empty())
            msgs.push_back(validation_error_t("This airport contains runway(s) but no airport boundary.", 	err_airport_no_boundary, apt,apt));

		Bbox2 apt_bounds;
		auto oob_runways(runways);
		auto oob_taxiways(taxiways);
		auto oob_ramps(ramps);

		for(auto b : boundaries)
		{
			if(WED_HasBezierPol(b))
				msgs.push_back(validation_error_t("Do not use bezier curves in airport boundaries.", err_apt_boundary_bez_curve_used, b, apt));

			Bbox2	 bdy_bounds;
			b->GetBounds(gis_Geo,bdy_bounds);
			apt_bounds += bdy_bounds;

			Polygon2 bdy;
			auto ps = b->GetOuterRing();
			int np = ps->GetNumPoints();
			bdy.reserve(np);

			for(int i = 0; i < np; i++)
			{
				Point2 pt;
				ps->GetNthPoint(i)->GetLocation(gis_Geo, pt);
				bdy.push_back(pt);
			}

			for(auto r = oob_runways.begin(); r != oob_runways.end();)
			{
				Point2 corners[4];
				(*r)->GetCorners(gis_Geo, corners);
				for(int i = 0; i < 4; i++)
				{
					if(!bdy.inside(corners[i]))
					{
						++r;
						break;
					}
					if(i == 3)
						r = oob_runways.erase(r);
				}
			}
			std::vector<WED_Thing *> oob_vertices;
			for(auto t = oob_taxiways.begin(); t != oob_taxiways.end();)
			{
				auto t_ps = (*t)->GetOuterRing();
				int t_np = t_ps->GetNumPoints();
				oob_vertices.clear();

				for(int i = 0; i < t_np; i++)
				{
					Point2 pt;
					t_ps->GetNthPoint(i)->GetLocation(gis_Geo, pt);
					if(!bdy.inside(pt))
						oob_vertices.push_back((*t)->GetNthChild(0)->GetNthChild(i));
				}
				if(oob_vertices.size() == 0)
					t = oob_taxiways.erase(t);
				else if(oob_vertices.size() == t_np)
					++t;                           // fully outside -> keep checking with next boundary
				else
				{
					msgs.push_back(validation_error_t("Taxiway not fully inside airport boundary.", err_airport_outside_boundary, oob_vertices, apt));
					t = oob_taxiways.erase(t);
				}
			}
			for(auto r = oob_ramps.begin(); r != oob_ramps.end();)
			{
				Point2 pt;
				(*r)->GetLocation(gis_Geo, pt);
				if(bdy.inside(pt))
					r = oob_ramps.erase(r);
				else
					++r;
			}
		}
		for(auto r : oob_runways)
			msgs.push_back(validation_error_t("Runway not fully inside airport boundary.", err_airport_outside_boundary, r, apt));
		for(auto t : oob_taxiways)
			msgs.push_back(validation_error_t("Taxiway not inside airport boundary.", err_airport_outside_boundary, t, apt));
		for(auto r : oob_ramps)
			msgs.push_back(validation_error_t("Ramp Start not inside airport boundary.", err_airport_outside_boundary, r, apt));

		apt_bounds.expand(APT_OVERSIZE_NM / cos(apt_bounds.centroid().y() * DEG_TO_RAD) / 60.0, APT_OVERSIZE_NM / 60.0 );
		if(!boundaries.empty() && !apt_bounds.contains(bounds))
		{
			std::vector<WED_Thing *> not_hidden;
			CollectRecursive(apt, back_inserter(not_hidden), ThingNotHidden, [&] (WED_Thing* v)
					{
						Bbox2 b;
						if(auto p = dynamic_cast<WED_GISPolygon *>(v))
						{
							p->GetBounds(gis_Geo, b);
							return !apt_bounds.contains(b);
						}
						else if(auto p = dynamic_cast<WED_GISPoint *>(v))
						{
							p->GetBounds(gis_Geo, b);
							return !apt_bounds.contains(b);
						}
						return false;
					});
			if(not_hidden.size())
			{
				msgs.push_back(validation_error_t("Airport contains scenery far outside the airport boundary.", err_airport_far_outside_boundary, not_hidden, apt));
				debug_mesh_segment(apt_bounds.left_side(), DBG_LIN_COLOR);
				debug_mesh_segment(apt_bounds.right_side(), DBG_LIN_COLOR);
				debug_mesh_segment(apt_bounds.top_side(), DBG_LIN_COLOR);
				debug_mesh_segment(apt_bounds.bottom_side(), DBG_LIN_COLOR);
			}
		}
		// allow some draped orthophotos (like grund painted signs)
		std::vector<WED_DrapedOrthophoto *> orthos_illegal;
		for(std::vector<WED_DrapedOrthophoto *>::iterator o = orthos.begin(); o != orthos.end(); ++o)
		{
			std::string res;
			const pol_info_t * pol;

			(*o)->GetResource(res);
			res_mgr->GetPol(res,pol);

			if (!pol || !pol->mSubBoxes.size())
				orthos_illegal.push_back(*o);
//			else
//				printf("kosher ortho, has %ld subtex\n", pol->mSubBoxes.size());
		}
		if(!orthos_illegal.empty())
			msgs.push_back(validation_error_t("Only Orthophotos with automatic subtexture selection can be exported to the Gateway. Please hide or remove selected Orthophotos.",
						err_gateway_orthophoto_cannot_be_exported, orthos_illegal, apt));
		if(mf)
			ValidateCIFP(runways, sealanes, legal_rwy_oneway, mf, msgs, apt);

		if (!roads.empty())
			ValidateRoads(roads, msgs, apt, apt_bounds);
	}
	else
		if (!roads.empty())
			ValidateRoads(roads, msgs, apt, Bbox2());


	ValidatePointSequencesRecursive(apt, msgs,apt);
	ValidateDSFRecursive(apt, lib_mgr, msgs, apt);
}

validation_result_t	WED_ValidateApt(WED_Document * resolver, WED_MapPane * pane, WED_Thing * wrl, bool skipErrorDialog, const char * abortMsg)
{
#if DEBUG_VIS_LINES
	//Clear the previously drawn lines before every validation
	gMeshPoints.clear();
	gMeshLines.clear();
	gMeshPolygons.clear();
#endif
	validation_error_vector		msgs;

	if(wrl == NULL) wrl = WED_GetWorld(resolver);
	WED_LibraryMgr * lib_mgr = 	WED_GetLibraryMgr(resolver);
	WED_ResourceMgr * res_mgr = WED_GetResourceMgr(resolver);

	std::vector<WED_Airport *> apts;
	CollectRecursiveNoNesting(wrl, back_inserter(apts), WED_Airport::sClass); // problem: Finds Airports only 1 level deep.

	// get data about runways from CIFP data
	MFMemFile * mf = nullptr;
	if(gExportTarget == wet_gateway)
		mf = ReadCIFP();

#if 0 // DEV
	auto t0 = std::chrono::high_resolution_clock::now();
#endif
	for(auto a : apts)
		ValidateOneAirport(a, msgs, lib_mgr, res_mgr, mf);

	std::vector<WED_RoadEdge*> off_airport_roads;

	std::function<void(WED_Thing *)> CollectEntitiesRecursiveNoApts = [&] (WED_Thing * thing)
	{
		const auto c = thing->GetClass();
#define COLLECT(type,type_name) \
		if(c == type::sClass) { \
			auto p = static_cast<type *>(thing); \
			if(!p->GetHidden())	type_name.push_back(p); \
			return; \
		}
		COLLECT(WED_RoadEdge,	off_airport_roads)
#undef COLLECT
		if(c != WED_Group::sClass)
			return;         // don't recurse into anything but groups.
		else
		{
			auto p = static_cast<WED_Group *>(thing);
			if(p->GetHidden())
				return;
			int nc = thing->CountChildren();
			for (int n = 0; n < nc; ++n)
				CollectEntitiesRecursiveNoApts(thing->GetNthChild(n));
		}
	};

	CollectEntitiesRecursiveNoApts(wrl);
	ValidateRoads(off_airport_roads, msgs, nullptr, Bbox2());

	// These are programmed to NOT iterate up INTO airports.  But you can START them at an airport.
	// So...IF wrl (which MIGHT be the world or MIGHt be a selection or might be an airport) turns out to
	// be an airport, we hvae to tell it "this is our credited airport."  Dynamic cast gives us the airport
	// or null for 'off airport' stuff.

	ValidatePointSequencesRecursive(wrl, msgs, dynamic_cast<WED_Airport *>(wrl));
	ValidateDSFRecursive(wrl, lib_mgr, msgs, dynamic_cast<WED_Airport *>(wrl));

#if 0// DEV
	auto t1 = std::chrono::high_resolution_clock::now();
	chrono::duration<double> elapsed = t1-t0;
	char c[50]; snprintf(c, 50, "Validation time was %.3lf s.", elapsed.count());
	msgs.push_back(validation_error_t(c, warn_airport_impossible_size, wrl, nullptr));
#endif
	if (mf) MemFile_Close(mf);

	std::string logfile(gPackageMgr->ComputePath(lib_mgr->GetLocalPackage(), "validation_report.txt"));
	FILE * fi = fopen(logfile.c_str(), "w");

	bool warnings_only = true;
	for(const auto& v : msgs)
	{
		const char * warn = "";
		std::string aname;
		if(v.airport)
			v.airport->GetICAO(aname);

		if(v.err_code > warnings_start_here)
			warn = "(warning only)";
		else
			warnings_only = false;

		if (fi)	fprintf(fi, "%s: %s %s\n", aname.c_str(), v.msg.c_str(), warn);
//		fprintf(stdout, "%s: %s %s\n", aname.c_str(), v.msg.c_str(), warn);
	}
	fclose(fi);

	if(!msgs.empty())
	{
		if(!skipErrorDialog) new WED_ValidateDialog(resolver, pane, msgs, abortMsg);

		return warnings_only ? validation_warnings_only : validation_errors;
	}
	else return validation_clean;
}
