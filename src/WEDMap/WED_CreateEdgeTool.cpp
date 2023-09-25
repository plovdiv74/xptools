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

#include "WED_CreateEdgeTool.h"
#include "WED_ToolUtils.h"
#include "WED_TaxiRouteNode.h"
#include "WED_RoadNode.h"
#include "WED_TaxiRoute.h"
#include "WED_RoadEdge.h"
#include "WED_SimpleBezierBoundaryNode.h"
#include "WED_MapZoomerNew.h"
#include "WED_ResourceMgr.h"
#include "WED_GISUtils.h"
#include "WED_HierarchyUtils.h"
#include "WED_EnumSystem.h"
#include "STLUtils.h"
#include <sstream>

#include "WED_GroupCommands.h"

#define DEBUG_CREATE_ROADS 0

static const char * kCreateCmds[] = { "Taxiway Route Line", "Road" };
static const int kIsAirport[] = { 1, 0 };

static bool is_edge_curved(CreateEdge_t tool_type)
{
	#if ROAD_EDITING
		if(tool_type == create_Road)
			return true;
	#endif
	#if HAS_CURVED_ATC_ROUTE
		return true;
	#endif

	return false;
}

WED_CreateEdgeTool::WED_CreateEdgeTool(
					const char *		tool_name,
					GUI_Pane *			host,
					WED_MapZoomerNew *	zoomer,
					IResolver *			resolver,
					WED_Archive *		archive,
					CreateEdge_t		tool) :
	WED_CreateToolBase(tool_name, host, zoomer, resolver, archive,
	2,						// min pts,
	99999999,				// max pts - yes, I am a hack.
	is_edge_curved(tool),	// curve allowed?
	0,						// curve required?
	1,						// close allowed?
	0),						// close required
	mType(tool),
	mVehicleClass(tool == create_TaxiRoute ? this : NULL,PROP_Name("Allowed Vehicles",XML_Name("","")), ATCVehicleClass, atc_Vehicle_Aircraft),
	mName(                                   this,       PROP_Name("Name",            XML_Name("","")), "N"),
	mOneway(tool == create_TaxiRoute ?       this : NULL,PROP_Name("Oneway",          XML_Name("","")), 1),
	mRunway(tool == create_TaxiRoute ?       this : NULL,PROP_Name("Runway",          XML_Name("","")), ATCRunwayTwoway, atc_rwy_None),
	mHotDepart(tool == create_TaxiRoute ?    this : NULL,PROP_Name("Departure",       XML_Name("","")), ATCRunwayOneway,false),
	mHotArrive(tool == create_TaxiRoute ?    this : NULL,PROP_Name("Arrival",         XML_Name("","")), ATCRunwayOneway,false),
	mHotILS(tool == create_TaxiRoute ?       this : NULL,PROP_Name("ILS",             XML_Name("","")), ATCRunwayOneway,false),
	mWidth(tool == create_TaxiRoute ?        this : NULL,PROP_Name("Size",            XML_Name("","")), ATCIcaoWidth, width_E),
#if ROAD_EDITING
	mLayer(tool == create_Road ?             this : NULL,PROP_Name("Layer",           XML_Name("","")), 0, 2),
	mSubtype(tool == create_Road ?           this : NULL,PROP_Name("Type",            XML_Name("","")), 100, 3),
	mResource(tool == create_Road ?          this : NULL,PROP_Name("Resource",        XML_Name("","")), "lib/g10/roads.net"),
#endif
	mSlop(                                   this,        PROP_Name("Slop",           XML_Name("","")), 10, 2)
{
}

WED_CreateEdgeTool::~WED_CreateEdgeTool()
{
}

struct sort_by_seg_rat {
	sort_by_seg_rat(const Point2& i) : a(i) { }
	Point2	a;
	bool operator()(const std::pair<IGISPointSequence *, Point2>& p1, const std::pair<IGISPointSequence *, Point2>& p2) const {
		return a.squared_distance(p1.second) < a.squared_distance(p2.second);
	}
	bool operator()(const Point2& p1, const Point2& p2) const {
		return a.squared_distance(p1) < a.squared_distance(p2);
	}
};

template <class A, class B>
struct compare_second {
	bool operator()(const std::pair<A,B>& lhs, const std::pair<A,B>& rhs) const {
		return lhs.second == rhs.second; }
};

static void SortSplits(const Segment2& s, std::vector<Point2>& splits)
{
	sort(splits.begin(), splits.end(), sort_by_seg_rat(s.p1));

	// Nuke dupe pts.  A hack?  NO!  Intentional.  When we GIS-iterate through our hierarchy
	// we pick up all our graph end pts many times - once as nodes, and once as the points making
	// up the pt sequences that is the edges.
	splits.erase(unique(splits.begin(),splits.end()),splits.end());
}

static void SortSplits(const Segment2& s, std::vector<std::pair<IGISPointSequence *, Point2 > >& splits)
{
	sort(splits.begin(), splits.end(), sort_by_seg_rat(s.p1));
	splits.erase(unique(splits.begin(),splits.end(), compare_second<IGISPointSequence*,Point2>()),splits.end());
}



split_edge_info_t cast_WED_GISEdge_to_split_edge_info_t(WED_GISEdge* edge, bool active)
{
	DebugAssert(edge != NULL);
	return split_edge_info_t(edge, active);
}

void		WED_CreateEdgeTool::AcceptPath(
			const std::vector<Point2>&	in_pts,
			const std::vector<Point2>&	in_dirs_lo,
			const std::vector<Point2>&	in_dirs_hi,
			const std::vector<int>		in_has_dirs,
			const std::vector<int>		in_has_split,
			int						closed)
{
	std::vector<Point2>	pts(in_pts);
	std::vector<Point2>	dirs_lo(in_dirs_lo);
	std::vector<Point2>	dirs_hi(in_dirs_hi);
	std::vector<int>		has_dirs(in_has_dirs);
	std::vector<int>		has_split(in_has_split);

	int idx;
	WED_Thing * host_for_parent = GetHost(idx);
	if (host_for_parent == NULL) return;

	WED_Thing * host_for_merging = WED_GetContainerForHost(GetResolver(), host_for_parent, kIsAirport[mType], idx);

	std::string cname = std::string("Create ") + kCreateCmds[mType];

	GetArchive()->StartCommand(cname.c_str());

	ISelection *	sel = WED_GetSelect(GetResolver());
	sel->Clear();
	double frame_dist = fabs(GetZoomer()->YPixelToLat(mSlop.value)-GetZoomer()->YPixelToLat(0));

	sClass_t edge_class = WED_TaxiRoute::sClass;
#if ROAD_EDITING
	if(mType == create_Road)
		edge_class = WED_RoadEdge::sClass;
#endif
	/************************************************************************************************
	 * FIRST SNAPPING PASS - NODE TO NODE
	 ************************************************************************************************/

	// For each node we want to add, we are going to find a nearby existing node - and if we find one, we
	// lock our location to theirs.  This "direct hit" will get consoldiated during create.  (By moving our
	// path first, we don't get false intersections when the user meant to hit end to end.)

	// limited to things inside the same group !!!
	for(int p = 0; p < pts.size(); ++p)
	{
		double	dist=frame_dist*frame_dist;
		WED_Thing * who = NULL;
		FindNear(host_for_merging, NULL, edge_class, pts[p],who,dist);
		if (who != NULL)
		{
			IGISPoint * pp = dynamic_cast<IGISPoint *>(who);
			if(pp)
				pp->GetLocation(gis_Geo,pts[p]);
		}
	}

	/************************************************************************************************
	 * SECOND SNAPPING PASS - LOCK NEW PTS TO EXISTING EDGES
	 ************************************************************************************************/
	// Next: we need to see if our nodes go near existing by existing edges...in that case,
	// split the edges and snap us over.

	// limited to things inside the same group !!!

	for (int p = 0; p < pts.size(); ++p)
	{
		double sqdist=frame_dist*frame_dist;
		IGISPointSequence * seq = NULL;
		FindNearP2S(host_for_merging, NULL, edge_class, pts[p], seq, sqdist ,frame_dist);
		if(seq)
		{
			IGISPoint * pp;
			if(auto e = dynamic_cast<IGISEdge *>(seq))  // should always be the case, as we're only finding edges
				pp = e->SplitEdge(pts[p], 0.001);
			else
				pp = seq->SplitSide(pts[p], 0.001);
			if(pp)
			{
				pp->GetLocation(gis_Geo,pts[p]);
			}
		}
	}

	/************************************************************************************************
	 *
	 ************************************************************************************************/
	std::vector<WED_GISEdge*> tool_created_edges;
	Bbox2 tool_created_bounds;
	double	dist=frame_dist*frame_dist;
	WED_Thing * src = NULL, * dst = NULL;

	if(mType == create_TaxiRoute)
	{
		WED_TaxiRoute *	new_edge = NULL;

		FindNear(host_for_merging, NULL, edge_class,pts[0],src,dist);
		if(src == NULL)
		{
			src = WED_TaxiRouteNode::CreateTyped(GetArchive());
			src->SetParent(host_for_parent,idx);
			src->SetName(mName.value + "_start");
			static_cast<WED_GISPoint *>(src)->SetLocation(gis_Geo,pts[0]);
		}

		int stop = closed ? pts.size() : pts.size()-1;
		for(int p = 1; p<= stop; p++)
		{
			int sp = p - 1;
			int dp = p % pts.size();

			new_edge = WED_TaxiRoute::CreateTyped(GetArchive());
			new_edge->SetName(mName);
			new_edge->SetWidth(mWidth.value);
			new_edge->SetOneway(mOneway.value);
			new_edge->SetVehicleClass(mVehicleClass.value);
			if(mVehicleClass.value == atc_Vehicle_Aircraft)
			{
				new_edge->SetRunway(mRunway.value);
				new_edge->SetHotDepart(mHotDepart.value);
				new_edge->SetHotArrive(mHotArrive.value);
				new_edge->SetHotILS(mHotILS.value);
			}

			new_edge->AddSource(src,0);
			dst = NULL;

			dist=frame_dist*frame_dist;
			FindNear(host_for_merging, NULL, edge_class,pts[dp],dst,dist);
			if(dst == NULL)
			{
				dst = WED_TaxiRouteNode::CreateTyped(GetArchive());
				dst->SetParent(host_for_parent,idx);
				dst->SetName(mName.value+"_stop");
				static_cast<WED_GISPoint *>(dst)->SetLocation(gis_Geo,pts[dp]);
			}
			new_edge->AddSource(dst,1);

			if(has_dirs[sp])
			{
				if(has_dirs[dp])
					new_edge->SetSideBezier(gis_Geo,Bezier2(in_pts[sp],dirs_hi[sp],dirs_lo[dp],in_pts[dp]));
				else
					new_edge->SetSideBezier(gis_Geo,Bezier2(in_pts[sp],dirs_hi[sp],in_pts[dp],in_pts[dp]));
			}
			else
			{
				if(has_dirs[dp])
					new_edge->SetSideBezier(gis_Geo,Bezier2(in_pts[sp],in_pts[sp],dirs_lo[dp],in_pts[dp]));
			}
			// Do this last - half-built edge inserted the world destabilizes accessors.
			new_edge->SetParent(host_for_parent,idx);
			tool_created_edges.push_back(new_edge);
			Bbox2 new_edge_bounds;
			new_edge->GetBounds(gis_Geo,new_edge_bounds);
			tool_created_bounds += new_edge_bounds;
			sel->Insert(new_edge);
			src = dst;
		}
	}
	else // mType == create_Road
	{
		WED_RoadEdge * new_edge = NULL;

		bool start_edge = true;
		bool stop_edge = false;

		int sp = 0;
		int stop = pts.size(); // closed ? pts.size() : pts.size()-1;

		for(int p = 0; p < stop; p++)
		{
			stop_edge = (p == stop-1);
			dst = nullptr;
			dist = frame_dist*frame_dist;
			FindNear(host_for_merging, NULL, edge_class, pts[p], dst, dist);
			if(!dst)
			{
				if(start_edge || stop_edge)
				{
					dst = WED_RoadNode::CreateTyped(GetArchive());
					if(p == stop-1)
						dst->SetName(mName.value + "_stop");
					else
						dst->SetName(mName.value + "_start");

					dst->SetParent(host_for_parent,idx);
					static_cast<WED_GISPoint *>(dst)->SetLocation(gis_Geo,pts[p]);
				}
				else
				{
					dst = WED_SimpleBezierBoundaryNode::CreateTyped(GetArchive());
					dst->SetName("Shape Point");
					dst->SetParent(new_edge,new_edge->CountChildren());
					auto wbp = static_cast<WED_GISPoint_Bezier *>(dst);
					wbp->SetLocation(gis_Geo,pts[p]);
					wbp->SetControlHandleLo(gis_Geo, has_dirs[p] ? dirs_lo[p] : pts[p]);
					wbp->SetControlHandleHi(gis_Geo, has_dirs[p] ? dirs_hi[p] : pts[p]);
				}
			}
			else // dst node hit , but it could be a shape node , must converted and dst edge splitted
			{
				if(dst->GetClass() != WED_RoadNode::sClass)
				{
					if(dst->GetClass() == WED_SimpleBezierBoundaryNode::sClass)
					{
						auto wbp = static_cast<WED_GISPoint_Bezier *>(dst);
						wbp->GetLocation(gis_Geo,pts[p]);
						WED_GISEdge * dst_edge = dynamic_cast<WED_GISEdge *>(dst->GetParent());
						if(dst_edge != nullptr)
						{
							WED_Thing * dst_np = dynamic_cast<WED_Thing *>(dst_edge->SplitEdge(Point2(pts[p]),0.0));
							DebugAssert(dst_np != nullptr);
							if(dst_np) dst = dst_np;
						}
					}
				}
				stop_edge = (p > 0);
			}

			if(stop_edge)
			{
				#if DEV && DEBUG_CREATE_ROADS
				printf("End Edge\n");
				#endif
				new_edge->AddSource(dst,1);
				new_edge->SetSideBezier(gis_Geo,Bezier2(pts[sp],
														has_dirs[sp]   ? dirs_hi[sp]   : pts[sp],
														has_dirs[p] ? dirs_lo[p] : pts[p],
														pts[p]),  -1);
				// Do this last - half-built edge inserted the world destabilizes accessors.
				new_edge->SetParent(host_for_parent,idx);
				tool_created_edges.push_back(new_edge);
				Bbox2 new_edge_bounds;
				new_edge->GetBounds(gis_Geo,new_edge_bounds);
				tool_created_bounds += new_edge_bounds;
				sel->Insert(new_edge);
				stop_edge = false;
				start_edge = p != stop-1;
			}

			if(start_edge)
			{
				#if DEV && DEBUG_CREATE_ROADS
				printf("Start Edge\n");
				#endif
				new_edge = WED_RoadEdge::CreateTyped(GetArchive());
				new_edge->SetSubtype(mSubtype.value);
				new_edge->SetStartLayer(mLayer.value);
				new_edge->SetEndLayer(mLayer.value);
				new_edge->SetName(mName);
				new_edge->SetResource(mResource.value);
				new_edge->AddSource(dst,0);
				sp = p;
				start_edge = false;
			}

			#if DEV && DEBUG_CREATE_ROADS
			printf("next interation with start = %d\n",start_edge);
			#endif
		}
	}

	//Collect edges in the current airport
	std::vector<WED_GISEdge*> all_edges;
	CollectRecursive(host_for_parent, back_inserter(all_edges), edge_class);

	//Filter out edges not want to split ; powerlines or edges from different resource
	auto e = all_edges.begin();
	while(e != all_edges.end())
	{
		if( (*e)->GetClass() == WED_RoadEdge::sClass )
		{
			auto r = static_cast<WED_RoadEdge *>(*e);
			std::string resource;
			r->GetResource(resource);
			if( r->HasWires() || mResource.value != resource )
			{
				e = all_edges.erase(e);
				continue;
			}
		}
		++e;
	}

	//filter them for just the crossing ones
	std::set<WED_GISEdge*> crossing_edges = WED_do_select_crossing(all_edges, tool_created_bounds);

	//convert, and run split!
	std::vector<split_edge_info_t> edges_to_split;

	for(std::set<WED_GISEdge*>::iterator e = crossing_edges.begin(); e != crossing_edges.end(); ++e)
	{
		edges_to_split.push_back(cast_WED_GISEdge_to_split_edge_info_t(*e, find(tool_created_edges.begin(), tool_created_edges.end(), *e) != tool_created_edges.end()));
	}

	edge_to_child_edges_map_t new_pieces = run_split_on_edges(edges_to_split,true);

	//For all the tool_created_edges that were split
	for(std::vector<WED_GISEdge*>::iterator itr = tool_created_edges.begin();
		itr != tool_created_edges.end() && new_pieces.size() > 0;
		++itr)
	{
		//Save the children as selected
		edge_to_child_edges_map_t::mapped_type& edge_map_entry = new_pieces[*itr];

		//Select only the new pieces
		std::set<ISelectable*> iselectable_new_pieces(edge_map_entry.begin(), edge_map_entry.end());
		sel->Insert(iselectable_new_pieces);
	}

	GetArchive()->CommitCommand();
}

bool		WED_CreateEdgeTool::CanCreateNow(void)
{
	int n;
	return GetHost(n) != NULL;
}

WED_Thing *	WED_CreateEdgeTool::GetHost(int& idx)
{
		return WED_GetCreateHost(GetResolver(), kIsAirport[mType], true, idx);
}

const char *		WED_CreateEdgeTool::GetStatusText(void)
{
	static char buf[256];
	int n;
	if (GetHost(n) == NULL)
	{
			sprintf(buf,"You must create an airport before you can add a %s.",kCreateCmds[mType]);
		return buf;
	}
	return NULL;
}

void WED_CreateEdgeTool::FindNear(WED_Thing * host, IGISEntity * ent, const char * filter, const Point2& loc, WED_Thing *& out_thing, double& out_dsq)
{
	IGISEntity * e = ent ? ent : dynamic_cast<IGISEntity*>(host);
	WED_Thing * t = host ? host : dynamic_cast<WED_Thing *>(ent);
	if(!IsVisibleNow(e))	return;
	if(IsLockedNow(e))		return;
	if(e && t)
	{
		Point2	l;
		IGISPoint * p;
		IGISPointSequence * ps;
		IGISComposite * c;

		switch(e->GetGISClass()) {
		case gis_PointSequence:
		case gis_Line:
		case gis_Line_Width:
		case gis_Ring:
		case gis_Edge:
		case gis_Chain:
#if ROAD_EDITING
			if(filter == WED_RoadEdge::sClass && t->GetClass() == WED_RoadEdge::sClass )
			{
				WED_RoadEdge * wre = static_cast<WED_RoadEdge *>(t);
				std::string resource;
				wre->GetResource(resource);
				if(mResource.value != resource) return;
			}
#endif
			if(filter == NULL || filter == t->GetClass())
			if((ps = dynamic_cast<IGISPointSequence*>(e)) != NULL)
			{
				#if DEV && DEBUG_CREATE_ROADS
				printf("FindNear NumPts = %d\n", ps->GetNumPoints());
				#endif

				for(int n = 0; n < ps->GetNumPoints(); ++n)
				{
					p = ps->GetNthPoint(n);
					if(p)
					{
						p->GetLocation(gis_Geo,l);
						double my_dist = Segment2(loc,l).squared_length();
						if(my_dist < out_dsq)
						{
							t = dynamic_cast<WED_Thing *>(p);
							if(t)
							{
								out_thing = t;
								out_dsq = my_dist;
							}
						}
					}
				}
			}
			break;
		case gis_Composite:
			if((c = dynamic_cast<IGISComposite *>(e)) != NULL)
			{
				for(int n = 0; n < c->GetNumEntities(); ++n)
				{
					FindNear(NULL,c->GetNthEntity(n), filter, loc, out_thing, out_dsq);
				}
			}
		}
	}
	else
	{
		for(int n = 0; n < host->CountChildren(); ++n)
			FindNear(host->GetNthChild(n), NULL, filter, loc, out_thing, out_dsq);
	}
}

void WED_CreateEdgeTool::FindNearP2S(WED_Thing * host, IGISEntity * ent, const char * filter, const Point2& loc, IGISPointSequence *& out_thing, double& out_dsq ,const double dst)
{
	IGISEntity * e = ent ? ent : dynamic_cast<IGISEntity*>(host);
	WED_Thing * t = host ? host : dynamic_cast<WED_Thing *>(ent);
	WED_Entity * et = t ? dynamic_cast<WED_Entity *>(t) : NULL;
	if(!IsVisibleNow(et))	return;
	if(IsLockedNow(et))		return;
	if(e && t)
	{
		Point2	l;
		IGISPoint * p;
		IGISPointSequence * ps;
		IGISComposite * c;

		switch(e->GetGISClass()) {
		case gis_PointSequence:
		case gis_Line:
		case gis_Line_Width:
		case gis_Ring:
		case gis_Edge:
		case gis_Chain:
#if ROAD_EDITING
			if(filter == WED_RoadEdge::sClass && t->GetClass() == WED_RoadEdge::sClass)
			{
				WED_RoadEdge * wre = static_cast<WED_RoadEdge *>(t);
				std::string resource;
				wre->GetResource(resource);
				if(mResource.value != resource) return;
			}
#endif
			if(filter == NULL || t->GetClass() == filter)
			if((ps = dynamic_cast<IGISPointSequence*>(e)) != NULL)
			{
				int ns = ps->GetNumSides();
				for(int n = 0; n < ns; ++n)
				{
					Bezier2 b;
					if(ps->GetSide(gis_Geo,n,b))
					{
						if(loc != b.p1 && loc != b.p2)
						{
							if(b.is_near(loc,dst))
							{
								out_thing = ps;
							}
						}
					}
					else
					{
						if(loc != b.p1 && loc != b.p2)
						{
							double d = b.as_segment().squared_distance(loc);
							if(d < out_dsq)
							{
								out_dsq = d;
								out_thing = ps;
							}
						}
					}
				}
			}
			break;
		case gis_Composite:
			if((c = dynamic_cast<IGISComposite *>(e)) != NULL)
			{
				for(int n = 0; n < c->GetNumEntities(); ++n)
					FindNearP2S(NULL,c->GetNthEntity(n), filter, loc, out_thing, out_dsq, dst);
			}
		}
	}
	else
	{
		for(int n = 0; n < host->CountChildren(); ++n)
			FindNearP2S(host->GetNthChild(n), NULL, filter, loc, out_thing, out_dsq, dst);
	}
}

#if ROAD_EDITING
void	WED_CreateEdgeTool::SetResource(const std::string& r)
{
	if(mType == create_Road)
	{
		mResource.value = r;
	}
}
#endif

void	WED_CreateEdgeTool::GetNthPropertyDict(int n, PropertyDict_t& dict) const
{
	dict.clear();

#if ROAD_EDITING
	if(n == PropertyItemNumber(&mSubtype))
	{
		if(auto r = get_valid_road_info())
		{
			for(auto i : r->vroad_types)
			{
				dict[i.first] = std::make_pair(i.second.description, true);
			}
			return;
		}
	}
	else if(n == PropertyItemNumber(&mRunway))
#else
	if (n == PropertyItemNumber(&mRunway))
#endif
	{
		WED_Airport * airport = WED_GetCurrentAirport(GetResolver());
		if(airport)
		{
			PropertyDict_t full;
			WED_CreateToolBase::GetNthPropertyDict(n,full);
			std::set<int> legal;
			WED_GetAllRunwaysTwoway(airport, legal);
			legal.insert(mRunway.value);
			legal.insert(atc_rwy_None);
			dict.clear();
			for(PropertyDict_t::iterator f = full.begin(); f != full.end(); ++f)
			if(legal.count(f->first))
				dict.insert(PropertyDict_t::value_type(f->first,f->second));
		}
	}
	else if (n == PropertyItemNumber(&mHotDepart) ||
			 n == PropertyItemNumber(&mHotArrive) ||
			 n == PropertyItemNumber(&mHotILS))
	{
		WED_Airport * airport = WED_GetCurrentAirport(GetResolver());
		if(airport)
		{
			PropertyDict_t full;
			WED_CreateToolBase::GetNthPropertyDict(n,full);
			std::set<int> legal;
			WED_GetAllRunwaysOneway(airport, legal);
			PropertyVal_t val;
			this->GetNthProperty(n,val);
			DebugAssert(val.prop_kind == prop_EnumSet);
			copy(val.set_val.begin(),val.set_val.end(),set_inserter(legal));
			dict.clear();
			for(PropertyDict_t::iterator f = full.begin(); f != full.end(); ++f)
			if(legal.count(f->first))
				dict.insert(PropertyDict_t::value_type(f->first,f->second));
		}
	}
	else
		WED_CreateToolBase::GetNthPropertyDict(n,dict);
}

void		WED_CreateEdgeTool::GetNthPropertyInfo(int n, PropertyInfo_t& info) const
{
	WED_CreateToolBase::GetNthPropertyInfo(n, info);
#if ROAD_EDITING
	if(n == PropertyItemNumber(&mSubtype))
	{
		if(get_valid_road_info())
		{
			info.prop_kind = prop_RoadType;
			return;
		}
	}
#endif

	//Ensures only the relevant properties are shown with atc_Vehicles_Ground_Trucks selected
	PropertyVal_t prop;
	mVehicleClass.GetProperty(prop);

	if (prop.int_val == atc_Vehicle_Ground_Trucks)
	{
		if (n == PropertyItemNumber(&mRunway)    ||
			n == PropertyItemNumber(&mHotDepart) ||
			n == PropertyItemNumber(&mHotArrive) ||
			n == PropertyItemNumber(&mHotILS)    ||
			n == PropertyItemNumber(&mWidth))
		{
			//"." is the special hardcoded "disable me" std::string, see IPropertyObject.h
			info.prop_name = ".";
			info.can_edit = false;
			info.can_delete = false;
		}
	}
}

void		WED_CreateEdgeTool::GetNthProperty(int n, PropertyVal_t& val) const
{
	WED_CreateToolBase::GetNthProperty(n, val);
#if ROAD_EDITING
	if(n == PropertyItemNumber(&mSubtype))
	{
		if(get_valid_road_info())
		{
			val.prop_kind = prop_RoadType;
		}
	}
#endif
}

void		WED_CreateEdgeTool::SetNthProperty(int n, const PropertyVal_t& val)
{
#if ROAD_EDITING
	if(n == PropertyItemNumber(&mSubtype))
	{
		if(get_valid_road_info())
		{
			PropertyVal_t v(val);
			v.prop_kind = prop_Int;
			WED_CreateToolBase::SetNthProperty(n, v);
			return;
		}
	}
#endif
	WED_CreateToolBase::SetNthProperty(n, val);
}


void		WED_CreateEdgeTool::GetNthPropertyDictItem(int n, int e, std::string& item) const
{
#if ROAD_EDITING
	if(n == PropertyItemNumber(&mSubtype))
	{
		if(auto r = get_valid_road_info())
		{
			auto i = r->vroad_types.find(mSubtype.value);
			if (i != r->vroad_types.end())
			{
				item = i->second.description;
				return;
			}
			else
			{
				if (mSubtype.value == 1)
				{
					item = "None";
				}
				else
				{
					stringstream ss;
					ss << mSubtype.value;
					item = ss.str();
				}
				return;
			}
		}
	}
#endif
	WED_CreateToolBase::GetNthPropertyDictItem(n, e, item);
}


#if ROAD_EDITING
const road_info_t *	WED_CreateEdgeTool::get_valid_road_info(void) const
{
	WED_ResourceMgr * rmgr = WED_GetResourceMgr(GetResolver());
	const road_info_t * r;
	if(rmgr && rmgr->GetRoad(mResource.value, r))
		if(r->vroad_types.size() > 0)
			return r;
	return nullptr;
}
#endif
