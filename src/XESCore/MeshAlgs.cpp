
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

#include "MapDefs.h"
#include "MapHelpers.h"
#include "MeshAlgs.h"
#include "ParamDefs.h"
#include "CompGeomDefs2.h"
#include "CompGeomDefs3.h"
#include "CompGeomUtils.h"
#include "PolyRasterUtils.h"
#include "AssertUtils.h"
#include "PlatformUtils.h"
#include "PerfUtils.h"
#include "MapAlgs.h"
#include "DEMAlgs.h"
#include "DEMGrid.h"
#include "DEMTables.h"
#include "GISUtils.h"
#include "XESConstants.h"
#include "GreedyMesh.h"
#include "MeshSimplify.h"
#include "NetHelpers.h"
#include "Zoning.h"	// for urban cheat table.
#if OPENGL_MAP
#include "GISTool_Globals.h"
#endif

//typedef CGAL::Mesh_2::Is_locally_conforming_Delaunay<CDT>	LCP;

#if HD_MESH
	// This is the frequency of triangulation in open water, where we have no height, as
	// a multiple of DEM pts.
	#define LOW_RES_WATER_INTERVAL 80
	// This is the freuqency of forced triangulation at airports as a multiple of DEM points.
	#define APT_INTERVAL 8
#elif UHD_MESH
	#define LOW_RES_WATER_INTERVAL 140
	#define APT_INTERVAL 14
#elif PHONE
	#define LOW_RES_WATER_INTERVAL 50
	#define APT_INTERVAL 40
#else
	#define LOW_RES_WATER_INTERVAL 40
	#define APT_INTERVAL 2
#endif

// This turns off DE's shapefile height path.
#define KILL_SPLIT_CLIFF_EXTRACT 1

// This adds more vertices to cliffs.
#define SPLIT_CLIFFS 1

// Don't do ANY borders - really only for debugging - when we want to see the mesh tri choice with NO borders (since wide borders can "swamp" a triangle).
#define NO_BORDERS_AT_ALL 0

// This disables borders from neighboring DSFs.  Generally you ALWAYS want border sharing on.  If you see weird borders along the edge of the DSF, turn
// this off to see if the neighboring DSF is causing them.
#define NO_BORDER_SHARING 0

// This causes the alg to print out timing of individual meshing steps.
#define PROFILE_PERFORMANCE 1

// Stop RFUI to show in progress triangulation
#define SHOW_STEPS 0


// This guarantees that we don't have "beached" triangles - that is, water triangles where all 3 points are coastal, and thus the water depth is ZERO in the entire
// thing.
#if PHONE
	#define SPLIT_BEACHED_WATER 0
#else
	#define SPLIT_BEACHED_WATER 1
#endif

// This sets the range of legal edges for subdivisions of constrained edges - in meters.
#define MAX_EDGE_DIST	500.0
#define MIN_EDGE_DIST	50.0

// This is how much LESS to subdivide the edge of a constraint than we theoretically need ot - in this case
// we are subidving half as much as required.  Since we also add points for too-long edges and elevation
// changes, we don't need to overdo basic subdivision - this produces acceptable triangles.
#define REDUCE_SUBDIVIDE 2

// This is how far to match a border point from the neighboring file.  If this is too huge and water doesn't match
// across tiles (which can happen on recuts) we get sort of silly border matching. 
#define MAX_BORDER_MATCH 0.001

// Andras: define max slope for non flattened water edges and the number of iterations
#if UHD_MESH
	#define MAX_WATER_SLOPE 0.4
	#define WATER_SMOOTHER_ITERATIONS 15
#elif HD_MESH
	#define MAX_WATER_SLOPE 0.4
	#define WATER_SMOOTHER_ITERATIONS 15
#else
	#define MAX_WATER_SLOPE 0.2
	#define WATER_SMOOTHER_ITERATIONS 25
#endif

#if SHOW_STEPS

#include "RF_Notify.h"
#include "RF_Msgs.h"

#define PAUSE_STEP(x) \
		RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL); \
		DoUserAlert(x);

#else

#define PAUSE_STEP(x) 

#endif

#if PROFILE_PERFORMANCE
#define TIMER(x)	StElapsedTime	__PerfTimer##x(#x);
#else
#define TIMER(x)
#endif

#ifndef PHONE
#define PHONE 0
#endif


#if HD_MESH
	MeshPrefs_t gMeshPrefs = {		/*iphone*/
	/* max_points		*/	PHONE ?		25000	: 350000,
	/* max_error		*/	PHONE ?		15		: 2.2,
	/* border_match		*/	PHONE ?		1		: 1,
	/* optimize_borders	*/	PHONE ?		1		: 1,
	/* max_tri_size_m	*/	PHONE ?		6000	: 250,
	/* rep_switch_m		*/	PHONE ?		50000	: 50000
	};
#elif UHD_MESH
	MeshPrefs_t gMeshPrefs = {		/*iphone*/
	/* max_points		*/	PHONE ?		25000	: 800000,
	/* max_error		*/	PHONE ?		15		: 0.7,
	/* border_match		*/	PHONE ?		1		: 1,
	/* optimize_borders	*/	PHONE ?		1		: 1,
	/* max_tri_size_m	*/	PHONE ?		6000	: 200,
	/* rep_switch_m		*/	PHONE ?		50000	: 50000
	};
#else
	MeshPrefs_t gMeshPrefs = {		/*iphone*/
	/* max_points		*/	PHONE ?		25000	: 78000,
	/* max_error		*/	PHONE ?		15		: 5.0,
	/* border_match		*/	PHONE ?		1		: 1,
	/* optimize_borders	*/	PHONE ?		1		: 1,
	/* max_tri_size_m	*/	PHONE ?		6000	: 1500,
	/* rep_switch_m		*/	PHONE ?		50000	: 50000
	};
#endif

Pmwx::Halfedge_handle	mesh_to_pmwx_he(CDT& io_mesh, CDT::Edge& e);

inline bool IsCustom(int n)
{
	if (n == terrain_Water) return false;
	return gNaturalTerrainInfo[n].custom_ter != tex_not_custom;
}

inline bool is_border(const CDT& inMesh, CDT::Face_handle f)
{
	for (int n = 0; n < 3; ++n)
	{
		if (f->neighbor(n)->has_vertex(inMesh.infinite_vertex()))
			return true;
	}
	return false;
}

inline void FindNextEast(CDT& ioMesh, CDT::Face_handle& ioFace, int& index, bool is_bot_edge)
{
	CDT::Vertex_handle sv = ioFace->vertex(index);
	CDT::Point p = sv->point();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	//printf("Starting with: %lf, %lf\n", CGAL::to_double(sv->point().x()), CGAL::to_double(sv->point().y()));

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		//printf("Checking: %lf, %lf\n", CGAL::to_double(now->point().x()), CGAL::to_double(now->point().y()));
		if (now != ioMesh.infinite_vertex())
		if (cy(now->point(), p) == CGAL::EQUAL)
		if (cx(now->point(), p) == CGAL::LARGER)
		{
			CDT::Face_handle	a_face;
			CDT::Vertex_circulator next = now;
			if(is_bot_edge)	++next;
			else			--next;
			Assert(ioMesh.is_face(sv, now, next, a_face));
			Assert(!ioMesh.is_infinite(a_face));
			ioFace = a_face;
			index = ioFace->index(now);
			return;
		}
		++now;
	} while (stop != now);
	AssertPrintf("Next mesh point not found.");
}

inline void FindNextNorth(CDT& ioMesh, CDT::Face_handle& ioFace, int& index, bool is_right_edge)
{
	CDT::Vertex_handle sv = ioFace->vertex(index);
	CDT::Point p = sv->point();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	//printf("Starting with: %lf, %lf\n", CGAL::to_double(sv->point().x()), CGAL::to_double(sv->point().y()));

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		//printf("Checking: %lf, %lf\n", CGAL::to_double(now->point().x()), CGAL::to_double(now->point().y()));
		if (now != ioMesh.infinite_vertex())
		if (cx(now->point(), p) == CGAL::EQUAL)
		if (cy(now->point(), p) == CGAL::LARGER)
		{
			CDT::Face_handle	a_face;
			CDT::Vertex_circulator next = now;
			if(is_right_edge)	++next;
			else				--next;
			Assert(ioMesh.is_face(sv, now, next, a_face));
			Assert(!ioMesh.is_infinite(a_face));
			ioFace = a_face;
			index = ioFace->index(now);
			return;
		}
		++now;
	} while (stop != now);
	Assert(!"Next pt not found.");
}

#pragma mark -
/************************************************************************************************************************
 * BORDER MATCHING
 ************************************************************************************************************************

	BORDER MATCHING - THEORY

	We cannot do proper blending and transitions across DSF borders because we write one DSF at a time - we have no way
	to go back and edit a previous DSF when we get to the next one and find a transition should have leaked across files.
	So instead we use a master slave system...the west and south files always dominate the north and east.

	The right and top borders of a DSF are MASTER borders and the left and bottom are SLAVES.

	When we write a DSF we write out the border info for the master borders into text files - this includes both vertex
	position along the border and texturing.

	When we write a new DSF we find our old master borders via text file and use it to conform our work.

	VERTEX MATCHING

	We write out all vertices on our master border.  For the slave border we add the MINIMUM number of points to the slave
	border - basically just mandatory water-body edges.  We then do a nearest-fit match from the master and add any non-
	matched master vertices to the slave.  This gives exact matchups except for mandatrory features which should be close.
	If the water bodies are not totally discontinuous this works.  X-plane can also resolve very slight vertex discrepancies.

	TRANSITION AND LANDUSE MATCHING

 	Each master edge vertex will contain some level of blending for each border that originates there as well as a std::set of
 	base transitions from each incident triangle.  (Each base layer can be thought of as being represented at 100%.)  When
 	sorted by priority this forms a total std::set of 'stuff' intruding from this vertex.

	To blend the border, we build overlays on the slave triangles incident to these borders that have the master's mix levels
	on the incident vertices and 0 levels on the interior.

	REBASING

	There is one problem with the above scheme - if the border from above is LOWER priority than the terrain it will cover,
	the border will not work.  Fundamentally we can't force a border to go left to right against priority!

	So we use a trick called "rebasing".  Basically given a slave tri with a high prio ("HIGH") and a master vertex with a
	low prio terrain ("LOW") we std::set the base of the slave tri to "LOW" and add a border of type "HIGH" to the slave with 0%
	blend on the edges and 100% in the interior.  We then also find all tris not touching the border incident to the 100% vertex
	and blend from 100% back to 0%.


 */


// This is one vertex from our master
struct	mesh_match_vertex_t {
	Point_2					loc;			// Location in master
	double					height;			// Height in master
	hash_map<int, float>	blending;		// List of borders and blends in master
	CDT::Vertex_handle		buddy;			// Vertex on slave that is matched to it
};

// This is one edge from our master
struct	mesh_match_edge_t {
	int						base;			// For debugging
	std::set<int>				borders;		// For debugging
	CDT::Face_handle		buddy;			// Tri in our mesh that corresponds
};

struct	mesh_match_t {
	vector<mesh_match_vertex_t>	vertices;
	vector<mesh_match_edge_t>	edges;
};

inline bool MATCH(const char * big, const char * msmall)
{
	return strncmp(big, msmall, strlen(msmall)) == 0;
}

static mesh_match_t gMatchBorders[4];


// Given a border plus the matched slaves, we identify our triangles...
static void border_find_edge_tris(CDT& ioMesh, mesh_match_t& ioBorder)
{
//	printf("Finding edge tris for %d edgse.\n",ioBorder.edges.size());
	DebugAssert(ioBorder.vertices.size() == (ioBorder.edges.size()+1));
	for (int n = 0; n < ioBorder.edges.size(); ++n)
	{
#if DEV
		DebugAssert(ioBorder.vertices[n  ].buddy != CDT::Vertex_handle());
		DebugAssert(ioBorder.vertices[n+1].buddy != CDT::Vertex_handle());
		DebugAssert(ioBorder.vertices[n  ].buddy != ioMesh.infinite_vertex());
		DebugAssert(ioBorder.vertices[n+1].buddy != ioMesh.infinite_vertex());
		CDT::Point	p1 = ioBorder.vertices[n  ].buddy->point();
		CDT::Point	p2 = ioBorder.vertices[n+1].buddy->point();
#endif
		if (!(ioMesh.is_face(ioBorder.vertices[n].buddy, ioBorder.vertices[n+1].buddy, ioMesh.infinite_vertex(), ioBorder.edges[n].buddy)))
//		if (!(ioMesh.is_face(ioBorder.vertices[n+1].buddy, ioBorder.vertices[n].buddy, ioMesh.infinite_vertex(), ioBorder.edges[n].buddy)))
		{
/*
			CDT::Vertex_circulator	circ, stop;
			printf("    Vert 1 vert = %lf,%lf (0x%08X)\n", ioBorder.vertices[n].buddy->point().x(), ioBorder.vertices[n].buddy->point().y(), &*ioBorder.vertices[n].buddy);
			circ = stop = ioMesh.incident_vertices(ioBorder.vertices[n].buddy);
			do {
				printf("    Buddy 1 vert = %lf,%lf (0x%08X)\n", circ->point().x(), circ->point().y(), &*circ);
				++circ;
			} while (circ != stop);
			circ = stop = ioMesh.incident_vertices(ioBorder.vertices[n+ 1].buddy);
			printf("    Vert 2 vert = %lf,%lf (0x%08X)\n", ioBorder.vertices[n+ 1].buddy->point().x(), ioBorder.vertices[n+ 1].buddy->point().y(), &*ioBorder.vertices[n+ 1].buddy);
			do {
				printf("    Buddy 2 vert = %lf,%lf (0x%08X)\n", circ->point().x(), circ->point().y(), &*circ);
				++circ;
			} while (circ != stop);
			AssertPrintf("Border match failure: %lf,%lf to %lf,%lf\n",
				ioBorder.vertices[n  ].buddy->point().x(),
				ioBorder.vertices[n  ].buddy->point().y(),
				ioBorder.vertices[n+1].buddy->point().x(),
				ioBorder.vertices[n+1].buddy->point().y());
*/
		// BEN SEZ: this used to be an error but - there are cases where the SLAVE file has a lake ENDING at the edge...there is no way the MASTER
		// could have induced these pts, so we're screwed.  For now - we'll just blunder on.
			ioBorder.edges[n].buddy = CDT::Face_handle();
//			debug_mesh_line(cgal2ben(p1),cgal2ben(p2),1,0,0,1,0,0);
		} else {
			int idx = ioBorder.edges[n].buddy->index(ioMesh.infinite_vertex());
			ioBorder.edges[n].buddy = ioBorder.edges[n].buddy->neighbor(idx);
		}
	}
}

inline void AddZeroMixIfNeeded(CDT::Face_handle f, int layer)
{
	if (f->info().terrain == terrain_Water) return;
	DebugAssert(!IsCustom(f->info().terrain));
	DebugAssert(layer != -1);
	f->info().terrain_border.insert(layer);
	for (int i = 0; i < 3; ++i)
	{
		CDT::Vertex_handle vv = f->vertex(i);
		if (vv->info().border_blend.count(layer) == 0)
			vv->info().border_blend[layer] = 0.0;
	}
}

inline void ZapBorders(CDT::Vertex_handle v)
{
	for (hash_map<int, float>::iterator i = v->info().border_blend.begin(); i != v->info().border_blend.end(); ++i)
		i->second = 0.0;
}

// We generally are only missing a terrain from a border file when a MeshTool user 
// doesn't include the border orthos in the scripts to both sessions - the second
// session doesn't know what's _on_ the border, let alone whether it should make
// border tris or not.  Try to give them an error message that explains how to fix
// it.  This should never happen for the global sceney case unless we are seriously
// fubar!
#define MISSING_ORTHO_WARNING \
"A neighboring DSF that you already created uses the terrain or orthophoto %s.\n"\
"That terrain or orthophoto touches the border with the DSF you are rendering now.\n"\
"But the terrain is not defined in the script file for this DSF.  You must add the\n"\
"terrain or orthophoto definition to the script file for this DSF.\n"
static bool	load_match_file(const char * path, mesh_match_t& outLeft, mesh_match_t& outBottom, mesh_match_t& outRight, mesh_match_t& outTop)
{
	outTop.vertices.clear();
	outTop.edges.clear();
	outRight.vertices.clear();
	outRight.edges.clear();
	outBottom.vertices.clear();
	outBottom.edges.clear();
	outLeft.vertices.clear();
	outLeft.edges.clear();

	FILE * fi = fopen(path, "r");
	if (fi == NULL) return false;
	char buf[80];
	bool go = true;
	int count;
	float mix;
	char ter[80];
	double x, y;
	int token;

	for(int b = 0; b < 4; ++b)
	{
		go = true;
		mesh_match_t *	dest;
		switch(b) {
		case 0: dest = &outLeft;	break;
		case 1: dest = &outBottom;	break;
		case 2: dest = &outRight;	break;
		case 3: dest = &outTop;		break;
		}

		while (go)
		{
			if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
			if (MATCH(buf, "VT"))
			{
				dest->vertices.push_back(mesh_match_vertex_t());
				sscanf(buf, "VT %lf, %lf, %lf", &x, &y, &dest->vertices.back().height);
				dest->vertices.back().loc = Point_2(x,y);
				dest->vertices.back().buddy = CDT::Vertex_handle();
			}
			if (MATCH(buf, "VC"))
			{
				go = false;
				dest->vertices.push_back(mesh_match_vertex_t());
				sscanf(buf, "VC %lf, %lf, %lf", &x, &y, &dest->vertices.back().height);
				dest->vertices.back().loc = Point_2(x,y);
				dest->vertices.back().buddy = CDT::Vertex_handle();
			}
			if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
			sscanf(buf, "VBC %d", &count);
			while (count--)
			{
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "VB %f %s", &mix, ter);
				dest->vertices.back().blending[token=LookupToken(ter)] = mix;
				if(token == -1)
				{
					fprintf(stderr,MISSING_ORTHO_WARNING,ter);
					exit(1);
				}
			}
			if (go)
			{
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "TERRAIN %s", ter);
				dest->edges.push_back(mesh_match_edge_t());
				dest->edges.back().base = token=LookupToken(ter);
				if(token == -1)
				{
					fprintf(stderr,MISSING_ORTHO_WARNING,ter);
					exit(1);
				}
				if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
				sscanf(buf, "BORDER_C %d", &count);
				while (count--)
				{
					if (fgets(buf, sizeof(buf), fi) == NULL) goto bail;
					sscanf(buf, "BORDER_T %s", ter);
					dest->edges.back().borders.insert( token=LookupToken(ter));
					if(token == -1)
					{
						fprintf(stderr,MISSING_ORTHO_WARNING,ter);
						exit(1);
					}
				}
			}
		}
	}

	return true;

bail:
	outTop.vertices.clear();
	outTop.edges.clear();
	outRight.vertices.clear();
	outRight.edges.clear();
	outBottom.vertices.clear();
	outBottom.edges.clear();
	outLeft.vertices.clear();
	outLeft.edges.clear();
	fclose(fi);
	return false;
}

// Given a point on the left edge of the top border or top edge of the right border, this fetches all border
// points in order of distance from that origin.
void	fetch_border(CDT& ioMesh, const Point_2& origin, map<double, CDT::Vertex_handle>& outPts, int side_num)
{
	CDT::Vertex_handle sv = ioMesh.infinite_vertex();
	CDT::Vertex_circulator stop, now;
	stop = now = ioMesh.incident_vertices(sv);

	CDT::Point	pt(origin.x(), origin.y());

	outPts.clear();

	CDT::Geom_traits::Compare_y_2 cy;
	CDT::Geom_traits::Compare_x_2 cx;
	do {
		double dist;
		if ((side_num == 0 || side_num == 2) && cx(now->point(), pt) == CGAL::EQUAL)
		{
			dist = CGAL::to_double(now->point().y() - origin.y());
			DebugAssert(outPts.count(dist)==0);
			outPts[dist] = now;
		}
		if ((side_num == 1 || side_num == 3) && cy(now->point(), pt) == CGAL::EQUAL)
		{
			dist = CGAL::to_double(now->point().x() - origin.x());
			DebugAssert(outPts.count(dist)==0);
			outPts[dist] = now;
		}

		++now;
	} while (stop != now);
}

// Border matching:
// We are going to go through a master edge from an old render and our slave render and try to correlate
// vertices.  This is a 3-part algorithm:
// 1. Find all of the slave edge points.
// 2. Match existing slave points with master points.
// 3. Induce any extra slave points as needed.

void	match_border(CDT& ioMesh, mesh_match_t& ioBorder, int side_num)
{
	map<double, CDT::Vertex_handle>	slaves;					// Slave map, from relative border offset to the handle.  Allows for fast slave location.
	Point_2	origin = ioBorder.vertices.front().loc;			// Origin of our tile.

	// Step 1.  Fetch the entire border from the mesh.
	fetch_border(ioMesh, origin, slaves, side_num);

	// Step 2. Until we have exhausted all of the slaves, we are going to try to find the neaerest master-slave std::pair and link them.

	while (!slaves.empty())
	{
		multimap<double, std::pair<double, mesh_match_vertex_t *> >	nearest;	// This is a slave/master std::pair - slave is IDed by its offset.

		// Go through each non-assigned vertex.
		for (vector<mesh_match_vertex_t>::iterator pts = ioBorder.vertices.begin(); pts != ioBorder.vertices.end(); ++pts)
		if (pts->buddy == CDT::Vertex_handle())
		{
			// Find the nearest slave for it by decreasing distance.
			for (map<double, CDT::Vertex_handle>::iterator sl = slaves.begin(); sl != slaves.end(); ++sl)
			{
				double myDist = (side_num == 0 || side_num == 2) ? (CGAL::to_double(pts->loc.y() - sl->second->point().y())) : (CGAL::to_double(pts->loc.x() - sl->second->point().x()));
				if (myDist < 0.0) myDist = -myDist;
				if(myDist < MAX_BORDER_MATCH)
				nearest.insert(multimap<double, std::pair<double, mesh_match_vertex_t *> >::value_type(myDist, std::pair<double, mesh_match_vertex_t *>(sl->first, &*pts)));
			}
		}

		// If we have not found a nearest std::pair, it means that we have assigned all masters to slaves and still have slaves left over!  This happens when we
		// cannot conform the border due to more water in the slave than master (or at least different water).  The most common case is the US-Canada border,
		// where the US is the master, Canada is the slave; because the US is not hydro-reconstructed it does not force the Canada border to water match.  We just
		// accept a discontinuity on the 49th parallel for now. :-(
		if (nearest.empty())
			break;

		// File off the match, and nuke the slave.
		std::pair<double, mesh_match_vertex_t *> best_match = nearest.begin()->second;
		DebugAssert(slaves.count(best_match.first) > 0);
		best_match.second->buddy = slaves[best_match.first];
		//printf("Matched: %lf,%lf to %lf,%lf\n",			CGAL::to_double(best_match.second->buddy->point().x()),
		//	   CGAL::to_double(best_match.second->buddy->point().y()),
		//	   CGAL::to_double(best_match.second->loc.x()),			CGAL::to_double(best_match.second->loc.y()));
		slaves.erase(best_match.first);

//		debug_mesh_point(cgal2ben(best_match.second->buddy->point()	),1,1,0);
//		debug_mesh_point(cgal2ben(best_match.second->loc			),0,1,0);		
	}

	// Step 3.  Go through all unmatched masters and insert them directly into the mesh.
	CDT::Face_handle	nearf = CDT::Face_handle();
	for (vector<mesh_match_vertex_t>::iterator pts = ioBorder.vertices.begin(); pts != ioBorder.vertices.end(); ++pts)
	if (pts->buddy == CDT::Vertex_handle())
	{	
		//printf("Found no buddy for: %lf,%lf\n", CGAL::to_double(pts->loc.x()), CGAL::to_double(pts->loc.y()));
		pts->buddy = ioMesh.insert(CDT::Point(CGAL::to_double(pts->loc.x()), CGAL::to_double(pts->loc.y())), nearf);
		nearf = pts->buddy->face();
		pts->buddy->info().height = pts->height;
//		debug_mesh_point(cgal2ben(pts->loc),1,0,0);
		
	}

	// At this point all masters have a slave, and some slaves may be connected to a master.
}

// RebaseTriangle -

inline bool has_no_xon(int tex1, int tex2)
{
	NaturalTerrainInfo_t& rec1(gNaturalTerrainInfo[tex1]);
	NaturalTerrainInfo_t& rec2(gNaturalTerrainInfo[tex2]);

	return rec1.xon_dist == 0.0 || rec2.xon_dist == 0.0;
}

static void RebaseTriangle(CDT& ioMesh, CDT::Face_handle tri, int new_base, CDT::Vertex_handle v1, CDT::Vertex_handle v2, std::set<CDT::Vertex_handle>& ioModVertices)
{
	int old_base = tri->info().terrain;

	if (old_base == terrain_Water || new_base == terrain_Water)
		return;
	if(has_no_xon(old_base,new_base))
		return;

	DebugAssert(new_base != terrain_Water);
	DebugAssert(tri->info().terrain != terrain_Water);
	tri->info().terrain = new_base;
	if (new_base != terrain_Water)
	{
		DebugAssert(old_base != -1);
		DebugAssert(!IsCustom(tri->info().terrain));
		tri->info().terrain_border.insert(old_base);

		for (int i = 0; i < 3; ++i)
		{
			CDT::Vertex_handle v = tri->vertex(i);
			if (v == v1 || v == v2)
				v->info().border_blend[old_base] = max(v->info().border_blend[old_base], 0.0f);
			else {
				v->info().border_blend[old_base] = 1.0;
				ioModVertices.insert(v);
			}
		}
	}
}

// Safe-smear border: when we have a vertex involved in a border from a master file
// then we need to make sure all incident triangles can transition out1
void SafeSmearBorder(CDT& mesh, CDT::Vertex_handle vert, int layer)
{
	if (vert->info().border_blend[layer] > 0.0)
	{
		CDT::Face_circulator iter, stop;
		iter = stop = mesh.incident_faces(vert);
		do {
			if (!mesh.is_infinite(iter))
			if (iter->info().terrain != layer)
			if (iter->info().terrain != terrain_Water)
			if (!IsCustom(iter->info().terrain))
			{
				DebugAssert(layer != -1);
				DebugAssert(!IsCustom(iter->info().terrain));
				iter->info().terrain_border.insert(layer);
				for (int n = 0; n < 3; ++n)
				{
					CDT::Vertex_handle v = iter->vertex(n);
					v->info().border_blend[layer] = max(0.0f, v->info().border_blend[layer]);
				}
			}
			++iter;
		} while (iter != stop);
	}
}

#pragma mark -
/************************************************************************************************************************
 * TRANSITIONS
 ************************************************************************************************************************/

inline int MAJORITY_RULES(int a, int b, int c, int d)
{
	int la = 1, lb = 1, lc = 1, ld = 1;
	if (a == b) ++la, ++lb;
	if (a == c) ++la, ++lc;
	if (a == d) ++la, ++ld;
	if (b == c) ++lb, ++lc;
	if (b == d) ++lb, ++ld;
	if (c == d) ++lc, ++ld;

	if (la >= lb && la >= lc && la >= ld) return a;
	if (lb >= la && lb >= lc && lb >= ld) return b;
	if (lc >= la && lc >= lb && lc >= ld) return c;
	if (ld >= la && ld >= lb && ld >= lc) return d;
	return a;
}

inline float SAFE_AVERAGE(float a, float b, float c)
{
	int i = 0;
	float t = 0.0;
	if (a != DEM_NO_DATA) t += a, ++i;
	if (b != DEM_NO_DATA) t += b, ++i;
	if (c != DEM_NO_DATA) t += c, ++i;
	if (i == 0) return DEM_NO_DATA;
	return t / i;
}

inline float SAFE_MAX(float a, float b, float c)
{
	return max(a, max(b, c));
}

inline double GetXonDist(int layer1, int layer2, double y_normal)
{
	NaturalTerrainInfo_t& rec1(gNaturalTerrainInfo[layer1]);
	NaturalTerrainInfo_t& rec2(gNaturalTerrainInfo[layer2]);

#if DEV
	const char * t1 = FetchTokenString(layer1);
	const char * t2 = FetchTokenString(layer2);
#endif

	double dist_1 = rec1.xon_dist;
	double dist_2 = rec2.xon_dist;

	double base_dist = min(dist_1, dist_2);

	return base_dist * y_normal;
}



inline double	DistPtToTri(CDT::Vertex_handle v, CDT::Face_handle f)
{
	// Find the closest a triangle comes to a point.  Inputs are in lat/lon, otuput is in meters!
	Point2		vp(cgal2ben(v->point()));
	Point2		tp1(cgal2ben(f->vertex(0)->point()));
	Point2		tp2(cgal2ben(f->vertex(1)->point()));
	Point2		tp3(cgal2ben(f->vertex(2)->point()));

	double	DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(vp.y()) * DEG_TO_RAD);
	tp1.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp1.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	tp2.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp2.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	tp3.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	tp3.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);
	 vp.x_ *= (DEG_TO_NM_LON * NM_TO_MTR);
	 vp.y_ *= (DEG_TO_NM_LAT * NM_TO_MTR);

	Segment2	s1(tp1, tp2);
	Segment2	s2(tp2, tp3);
	Segment2	s3(tp3, tp1);

	// BEN SAYS: squared dist to line is the dist to the supporting line if we are withn the interval, or dist to vertices
	// otherwise.  So this is good enough to do the whole tri.  This is INCORRECT for pts in the INTERIOR of the tri, but 
	// we have to trust CGAL's mesh to never give us a point INSIDE the tri!
	double d1 = s1.squared_distance(vp);
	double d2 = s2.squared_distance(vp);
	double d3 = s3.squared_distance(vp);

	return sqrt(min(min(d1,d2),d3));
}







#pragma mark -
/***************************************************************************
 * ALGORITHMS TO FIND VALUABLE POINTS IN A DEM *****************************
 ***************************************************************************
 *
 * These routines take a fully populated DEM and copy points of interest into
 * an empty DEM to build up a small number of points we can use to triangulate.
 * 'orig' is always the main DEM and 'deriv' the sparse one.  The goal is to
 * get about 20,000-30,000 points that provide good coverage and capture the
 * terrain morphology.
 */

/*
 * InsertDEMPoint - insert one point from the DEM into the mesh.
 *
 */
CDT::Vertex_handle InsertDEMPoint(
				const DEMGeo&			in_orig,
					  DEMMask&			io_used,
					  CDT&				io_mesh,
					  int				x,
					  int				y,
					  CDT::Face_handle&	hint)
{
	float h = in_orig.get(x,y);
	DebugAssert(h != DEM_NO_DATA);
	
	CDT::Point	p(in_orig.x_to_lon(CGAL::to_double(x)),in_orig.y_to_lat(CGAL::to_double(y)));

	CDT::Vertex_handle np = io_mesh.insert(p, hint);
	np->info().height = h;
	hint = np->face();
	
	io_used.set(x,y,true);
	
	return np;
}

/*
 * InsertAnyPoint - insert a non-DEM-aligned point into the mesh with interpolation.
 *
 */
CDT::Vertex_handle InsertAnyPoint(
			const DEMGeo&			in_orig,
			CDT&					io_mesh,
			const Point_2&			p,
			CDT::Face_handle&		hint,
			boost::optional<double>	ext_e = {})
{
	float e;
	if (ext_e)
	{
		e = float(*ext_e);
	}
	else
	{
		e = in_orig.value_linear(CGAL::to_double(p.x()), CGAL::to_double(p.y()));
		if (e == DEM_NO_DATA)
			e = in_orig.xy_nearest(CGAL::to_double(p.x()), CGAL::to_double(p.y()));

		DebugAssert(e != DEM_NO_DATA);
	}

	CDT::Vertex_handle v = io_mesh.insert(p, hint);
	hint = v->face();
	v->info().height = e;
	v->info().explicit_height = ext_e.has_value();
	
	return v;
}




/*
 * InsertMidPoints
 *
 * Given two already inserted points, we keep adding mid-points to subdivide to be less than the max edge distance.
 *
 */
void InsertMidPoints(const DEMGeo& in_orig, CDT& io_mesh, CDT::Vertex_handle v1, CDT::Vertex_handle v2, CDT::Face_handle& hint)
{
	NT d_sqr = CGAL::squared_distance(v1->point(),v2->point());
	bool want_split = d_sqr > (MAX_EDGE_DIST * MAX_EDGE_DIST * MTR_TO_DEG_LAT * MTR_TO_DEG_LAT);
	Point_2 midp(CGAL::midpoint(v1->point(),v2->point()));
	if(want_split)
	{
//		debug_mesh_point(cgal2ben(midp),1,1,0);
	}
	else if (d_sqr > (MIN_EDGE_DIST * MIN_EDGE_DIST * MTR_TO_DEG_LAT * MTR_TO_DEG_LAT))
	{
		float h1 = in_orig.value_linear(CGAL::to_double(v1->point().x()),CGAL::to_double(v1->point().y()));
		float h2 = in_orig.value_linear(CGAL::to_double(v2->point().x()),CGAL::to_double(v2->point().y()));
		float hc = in_orig.value_linear(CGAL::to_double(midp.x()),CGAL::to_double(midp.y()));
		float ha = (h1 + h2) * 0.5;
		if (fabs(ha - hc) > gMeshPrefs.max_error)
		{
			want_split = true;		
//			debug_mesh_point(cgal2ben(midp),1,0,0);
		}
	}

	if(want_split)
	{
		CDT::Vertex_handle vm = InsertAnyPoint(in_orig, io_mesh, midp,hint);
								InsertMidPoints(in_orig,io_mesh,v1,vm,hint);
								InsertMidPoints(in_orig,io_mesh,vm,v2,hint);
	}
}


/*
 * CopyWetPoints
 *
 * This routine inserts every Nth point that is inside water into the mesh.  This is used
 * to put a sparse mesh inside the water areas.
 *
 */
double CopyWetPoints(
				const DEMGeo&			in_orig,
					  DEMMask&			io_used,
					  CDT&				io_mesh,
					  int				in_skip,
					  int				in_terrain,
				const Pmwx& 			map)		// The map we get the water bodies from
{
	// BEN NOTE ON CLAMPING: I think we do NOT care if an edge is microscopically outside the DEM
	// in this case...xy_nearest could care less...and the polygon rasterizer doesn't care much
	// either.  We do not generate any coastline edges here.

	PolyRasterizer<double>	rasterizer;
	SetupWaterRasterizer(map, in_orig, rasterizer, in_terrain);

	CDT::Face_handle	hint;

	int total = in_orig.mWidth * in_orig.mHeight;
	int wet = 0;

	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for(int x = x1; x < x2; ++x)
			{
				if((x % in_skip == 0) && (y % in_skip == 0))
					InsertDEMPoint(in_orig, io_used,io_mesh,x,y,hint);
				++wet;
			}
		}
		// Yeah we could be more clever about modulus in the Y axis, but..the rasterizer might
		// be unhappy skipping scanlines with "events" on them.
		++y;
		if (y >= in_orig.mHeight) break;
		rasterizer.AdvanceScanline(y);
	}
	
	return (double) wet / (double) total;
}

void CopyBathymetryToPlate(DEMGeo& ioPlate, const DEMGeo& inBathy, const Pmwx& map, int terrain_type_mask)
{
	PolyRasterizer<double>	rasterizer;
	SetupWaterRasterizer(map, ioPlate, rasterizer, terrain_type_mask);

	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for(int x = x1; x < x2; ++x)
			{
				ioPlate.set(x,y,inBathy.value_linear(ioPlate.x_to_lon(x), ioPlate.y_to_lat(y)));
			}
		}
		++y;
		if (y >= ioPlate.mHeight)
			break;
		rasterizer.AdvanceScanline(y);
	}
}

double CopyWetPointsWithSDF(
				const DEMGeo&			in_orig,
					  DEMMask&			io_used,
					  CDT&				io_mesh,
				const DEMGeo&			in_sdf,
					  int				in_terrain,
					  int				in_skip,
				const Pmwx& 			map)		// The map we get the water bodies from
{
	// BEN NOTE ON CLAMPING: I think we do NOT care if an edge is microscopically outside the DEM
	// in this case...xy_nearest could care less...and the polygon rasterizer doesn't care much
	// either.  We do not generate any coastline edges here.

	PolyRasterizer<double>	rasterizer;
	SetupWaterRasterizer(map, in_orig, rasterizer, in_terrain);

	CDT::Face_handle	hint;

	int total = in_orig.mWidth * in_orig.mHeight;
	int wet = 0;

	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for(int x = x1; x < x2; ++x)
			{
				int sdf_x = in_sdf.lon_to_x(in_orig.x_to_lon(x));
				int sdf_y = in_sdf.lat_to_y(in_orig.y_to_lat(y));
				
				float dist =  in_sdf.get(sdf_x,sdf_y);
				
				int skip = 2.0;
				while(skip < dist && skip < in_skip)
					skip *= 2;
				skip = min(in_skip, skip);
				if((x % skip == 0) && (y % skip == 0))
					InsertDEMPoint(in_orig, io_used,io_mesh,x,y,hint);
				++wet;
			}
		}
		// Yeah we could be more clever about modulus in the Y axis, but..the rasterizer might
		// be unhappy skipping scanlines with "events" on them.
		++y;
		if (y >= in_orig.mHeight) break;
		rasterizer.AdvanceScanline(y);
	}
	
	return (double) wet / (double) total;
}

/*
 * AddEdgePoints
 *
 * This function adds the edges to the DEMs, at the interval specified.
 *
 */
void AddEdgePoints(
			const DEMGeo& 		orig, 			// The original DEM
			DEMMask& 			deriv, 			// Edge points are added to this
			int 				interval,		// The interval - add an edge point once every N points.
			int					divisions,		// Number of divisions - 1 means 1 big, "2" means 4 parts, etc.
			bool				has_border[4],	// Useful in making sure our borders match up.
			CDT&				mesh)
{

	int	div_skip_x = (deriv.mWidth-1) / divisions;
	int	div_skip_y = (deriv.mHeight-1) / divisions;
	int x, y, dx, dy;
	bool has_left = has_border[0];
	bool has_bottom = has_border[1];
	bool has_right = has_border[2];
	bool has_top = has_border[3];

	CDT::Face_handle hint;

	for (x = (has_left ? div_skip_x : 0); x < (deriv.mWidth - (has_right ? div_skip_x : 0)); x += div_skip_x)
	for (dy = 0; dy < deriv.mHeight; dy += interval)
	{
		InsertDEMPoint(orig, deriv, mesh, x, dy, hint);
	}

	for (y = (has_bottom ? div_skip_y : 0); y < (deriv.mHeight - (has_top ? div_skip_y : 0)) ; y += div_skip_y)
	for (dx = 0; dx < deriv.mWidth; dx += interval)
	{
		InsertDEMPoint(orig, deriv, mesh, dx, y, hint);
	}
	
	if(has_left || has_right)
	for(y = 0; y < orig.mHeight; ++y)
	{
		if(has_left ) deriv.set(0			  ,y,true);
		if(has_right) deriv.set(deriv.mWidth-1,y,true);
	}

	if(has_bottom || has_top)
	for(x = 0; x < orig.mWidth; ++x)
	{
		if(has_bottom) deriv.set(x,0			  ,true);
		if(has_top   ) deriv.set(x,deriv.mHeight-1,true);
	}
}

/*
 * AddConstraintPoints
 *
 * This routine calculates the constraints we need based on burned-in edges from the map, water, etc.
 * It inserts the vertices of the constraints now (so that triangulation can take advantage of them) and returns
 * the pairs that become constraints.  We don't add constraints now as that would screw up the quality of triangulation.
 *
 */
void	AddConstraintPoints(
				Pmwx& 								inMap, 		// Vec Map of waterbodies
				const DEMGeo& 						master, 	// Master DEM with elevations
				CDT& 								outMesh) 	// Vertices and constraints added to this mesh
{
	/*******************************************************************************************
	 * FIND POLYGON GROUPS THAT CONTAIN LAND USE DIFFERENCES
	 *******************************************************************************************/

	// We are going to go through the whole map and find every halfedge that represents a real land use
	// change.

		CDT::Face_handle	locale = CDT::Face_handle();	// For cache coherency
		CDT::Vertex_handle	v1, v2;
		float				e1, e2;

		Pmwx::Halfedge_iterator he;

	for (he = inMap.halfedges_begin(); he != inMap.halfedges_end(); ++he)
		he->data().mMark = false;

	for (he = inMap.halfedges_begin(); he != inMap.halfedges_end(); ++he)
	if (!he->twin()->data().mMark)
	if (!he->data().mMark)
	{
		Pmwx::Face_const_handle	f1 = he->face();
		Pmwx::Face_const_handle	f2 = he->twin()->face();
		if (must_burn_he(he))
		{
			// At least one side of the edge should be inside the degree
			DebugAssert(!f1->is_unbounded() || !f2->is_unbounded());

			// If the Vertex has elevation data, use that instead
			const auto& source_elevation = he->source()->data().mElevation;
			const auto& target_elevation = he->target()->data().mElevation;

			v1 = InsertAnyPoint(master, outMesh, he->source()->point(), locale, source_elevation);
			v2 = InsertAnyPoint(master, outMesh, he->target()->point(), locale, target_elevation);
			v1->info().orig_vertex = he->source();
			v2->info().orig_vertex = he->target();
			v1->info().edge_of_the_world = v2->info().edge_of_the_world = f1->is_unbounded() || f2->is_unbounded();

			// Ben says: constrain now!  This will force near-edge triangles to flip to the way they 
			// will have to be, which will then help the greedy mesh understand where the worst errors are.
			outMesh.insert_constraint(v1,v2);
			locale = CDT::Face_handle();			// Face handle may be trashed by constraint propagation!
		}
	}
}

/*
 * SubdivideConstraints
 *
 * This routine finds all of the constraints in the CDT and subdivides them based on a DEM indicating ideal
 * mesh density and also some subidvision rules.
 *
 */
void SubdivideConstraints(CDT& io_mesh, const DEMGeo& master, const DEMGeo& ideal_density)
{
	std::list<std::pair<CDT::Vertex_handle,CDT::Vertex_handle> >		edges;
	for (CDT::Finite_edges_iterator eit = io_mesh.finite_edges_begin(); eit != io_mesh.finite_edges_end(); ++eit)
	{
		if (!io_mesh.is_constrained(*eit))
			continue;

		auto source = CDT_he_source(*eit);
		if (source->info().edge_of_the_world)
			continue;

		edges.emplace_back(source, CDT_he_target(*eit));
	}

	CDT::Face_handle	locale = CDT::Face_handle();	// For cache coherency
	
	for(std::list<std::pair<CDT::Vertex_handle,CDT::Vertex_handle> >::iterator e = edges.begin(); e != edges.end(); ++e)
	{
		vector<CDT::Vertex_handle>	pts;
		pts.push_back(e->first);

		Vector_2	vec(e->first->point(),e->second->point());

		const bool has_shp_elevation = e->first->info().orig_vertex->data().mElevation &&
				e->second->info().orig_vertex->data().mElevation;

		int num_verts = IntegLine(
								ideal_density, 
									ideal_density.lon_to_x(CGAL::to_double(e->first->point().x())),
									ideal_density.lat_to_y(CGAL::to_double(e->first->point().y())),
									ideal_density.lon_to_x(CGAL::to_double(e->second->point().x())),
									ideal_density.lat_to_y(CGAL::to_double(e->second->point().y())),4) / REDUCE_SUBDIVIDE;

		for(int n = 0; n < num_verts; ++n)
		{
			float r = ((float) (n+1)) / (float) (num_verts+1);
			DebugAssert(r > 0.0);
			DebugAssert(r < 1.0);
			Point_2 p = e->first->point() + (vec * r);
			boost::optional<double> el;
			if (has_shp_elevation)
			{
				el = e->first->info().height + (e->second->info().height - e->first->info().height) * r;
			}

			const auto inserted = InsertAnyPoint(master, io_mesh, p, locale, el);
			pts.push_back(inserted);
//			debug_mesh_point(cgal2ben(p),0,1,1);
		}
		
		pts.push_back(e->second);

		if (!has_shp_elevation)
		for(int n = 1; n < pts.size(); ++n)
			InsertMidPoints(master, io_mesh,pts[n-1],pts[n], locale);
	}
			
}


/* This routine sets the feature type for the mesh tris from the terrain that required burn-in for constraints.
 * This is how we know that our water tris should be wet.  We std::set every tri on the border of a constraint, then 
 * flood-fill.*/
void	SetTerrainForConstraints(CDT& ioMesh, const DEMGeo& allPts)
{
	std::set<CDT::Face_handle>		wet_faces;
	std::set<CDT::Face_handle>		visited;

	vector<CDT::Face_handle>	seed_faces;

	// FIRST: we are going to go throguh and std::set everybody to either uninited/natural (if we aren't constrained)
	// or, for any constrain-edged triangle, we're going to figure out who our initial face was and init like that.

	for (CDT::Finite_faces_iterator ffi = ioMesh.finite_faces_begin(); ffi != ioMesh.finite_faces_end(); ++ffi)
	{
		ffi->info().terrain = terrain_Natural;
		ffi->info().feature = NO_VALUE;
		ffi->info().orig_face = Face_handle();

		for(int n = 0; n < 3; ++n)
		if(ffi->is_constrained(n))
		{
			CDT::Edge e(ffi,n);
//			// DEBUG CODE
//			{
//				Halfedge_handle he = mesh_to_pmwx_he(ioMesh, e);
//				DebugAssert(he != Halfedge_handle());
//				if(he_has_any_roads(he))
//					debug_mesh_line(cgal2ben(he->source()->point()),cgal2ben(he->target()->point()),1,0,0, 1,0,0);
//				else
//					debug_mesh_line(cgal2ben(he->source()->point()),cgal2ben(he->target()->point()),0,0,1, 0,0,1);
//			}
			
			
			CDT::Vertex_handle source(CDT_he_source(e));
			Vertex_handle orig_source = source->info().orig_vertex;			
//			if(orig_source != Vertex_handle())		// only sync first constraint along a run. -- no sync all - to get whether we have roads along da constraints.
			{
				Halfedge_handle orig_he = mesh_to_pmwx_he(ioMesh, e);
				DebugAssert(orig_he != Halfedge_handle());
				Face_handle	orig_face = orig_he->face();
//				if(orig_face == Face_handle())
//				{
//					debug_mesh_point(cgal2ben(orig_source->point()),1,0,0);
//					debug_mesh_point(cgal2ben(orig_target->point()),0,1,0);
//				}
				DebugAssert(orig_face != Face_handle());
				
				ffi->info().terrain = orig_face->data().mTerrainType;
				ffi->info().feature = orig_face->data().mTerrainType;
				ffi->info().orig_face = orig_face;
				wet_faces.insert(ffi);				
				
				
				if(orig_he->data().HasRoads() || orig_he->twin()->data().HasRoads())
				if(!orig_he->data().HasRoadOfType(powerline_Generic) || !orig_he->data().HasRoadOfType(powerline_Generic))
					ffi->info().set_edge_feature(n,true);				
			}
		}
	}

	// Now flood fill the rest of the triangles from the constrain-edged triangles.
	while (!wet_faces.empty())
	{
		CDT::Face_handle f = *wet_faces.begin();
		wet_faces.erase(f);
		visited.insert(f);

		int tg = f->info().terrain;
		const Face_handle of = f->info().orig_face;
		f->info().flag = 0;
		CDT::Face_handle	fn;
		for (int vi = 0; vi < 3; ++ vi)
		if (!ioMesh.is_constrained(CDT::Edge(f,vi)))
		{
			fn = f->neighbor(vi);
			if (!ioMesh.is_infinite(fn))
			if (visited.find(fn) == visited.end())
			{
				if (fn->info().terrain != terrain_Natural && fn->info().terrain != tg) {
					printf("Error: conflicting terrain assignment between %s and %s, near %lf, %lf\n",
							FetchTokenString(fn->info().terrain), FetchTokenString(tg),
							CGAL::to_double(f->vertex(vi)->point().x()), CGAL::to_double(f->vertex(vi)->point().y()));
				} else {
				fn->info().terrain = tg;
				fn->info().feature = tg;
				}
				if (fn->info().orig_face == Face_handle()) fn->info().orig_face = of;
				wet_faces.insert(fn);
			}
		}
	}

	for (CDT::Finite_faces_iterator ffi = ioMesh.finite_faces_begin(); ffi != ioMesh.finite_faces_end(); ++ffi)
	if (ffi->info().terrain == terrain_Water)
	for (int vi = 0; vi < 3; ++vi)
	{
		int xw, yw;
		float e = allPts.xy_nearest(CGAL::to_double(ffi->vertex(vi)->point().x()),CGAL::to_double(ffi->vertex(vi)->point().y()), xw, yw);

		//e = allPts.get_lowest_heuristic(xw, yw, 5);
		if (e != DEM_NO_DATA)
			ffi->vertex(vi)->info().height = e;
	}
}

/* Calculate the normal of one face. */
inline Vector3 CalculateMeshNormal(CDT::Face_handle f)
{
	Point3	p1(CGAL::to_double(f->vertex(0)->point().x()), CGAL::to_double(f->vertex(0)->point().y()), f->vertex(0)->info().height);
	Point3	p2(CGAL::to_double(f->vertex(1)->point().x()), CGAL::to_double(f->vertex(1)->point().y()), f->vertex(1)->info().height);
	Point3	p3(CGAL::to_double(f->vertex(2)->point().x()), CGAL::to_double(f->vertex(2)->point().y()), f->vertex(2)->info().height);

	Vector3 v1(p1,p2);
	Vector3 v2(p1,p3);
	v1.dx *= (DEG_TO_MTR_LAT * cos(p1.y * DEG_TO_RAD));
	v2.dx *= (DEG_TO_MTR_LAT * cos(p1.y * DEG_TO_RAD));
	v1.dy *= (DEG_TO_MTR_LAT);
	v2.dy *= (DEG_TO_MTR_LAT);
				
	if((v1.dx == 0.0 && v1.dy == 0.0 && v1.dz == 0.0) ||
	   (v2.dx == 0.0 && v2.dy == 0.0 && v2.dz == 0.0))
	{
		return Vector3(0,0,1);
	}
	else 
	{
	
		v1.normalize();
		v2.normalize();
		Vector3 normal(v1.cross(v2));
		if(normal.dz <= 0.0)
		{
			return Vector3(0,0,1);
		} 
		else {
			normal.normalize();
			return normal;
		}
	}
}

/* Is this triangle a cliff?  For special hadnling. */
inline bool tri_is_cliff(CDT& io_mesh, CDT::Face_handle f)
{
	if(io_mesh.is_infinite(f)) return false;
	Vector3 n = CalculateMeshNormal(f);
	return n.dz < 0.7;
}

struct sort_cdt_face_by_lowest_height {
	bool operator()(const CDT::Face_handle& lhs, const CDT::Face_handle& rhs) const {
		double	lh = fltmin3(
						lhs->vertex(0)->info().height,
						lhs->vertex(1)->info().height,
						lhs->vertex(2)->info().height);
		double	rh = fltmin3(
						lhs->vertex(0)->info().height,
						lhs->vertex(1)->info().height,
						lhs->vertex(2)->info().height);
		if (lh == rh) 
			return &*lhs < &*rhs;
		return lh < rh;
	}
	bool operator()(const CDT::Vertex_handle& lhs, const CDT::Vertex_handle& rhs) const {
		if(lhs->info().height == rhs->info().height)
			return &*lhs < &*rhs;
		return lhs->info().height < rhs->info().height;
	}
};

#define NEW_ALG 1
// this is faster - by going in order from bottom to top we avoid a crapload of retries on neighboring verts.
// going by MESH FACE is not so good - mesh face is at THREE alts at once..in theory at least.  or something.
void FlattenWater(CDT& ioMesh, const DEMGeo& water_surface)
{
	for(CDT::Finite_vertices_iterator v = ioMesh.finite_vertices_begin(); v != ioMesh.finite_vertices_end(); ++v)
	{
		if(CategorizeVertex(ioMesh, v,terrain_Water) <= 0)
		if(!IsNoFlattenVertex(ioMesh,v))
		{
			Point2 p = cgal2ben(v->point());
			v->info().height = water_surface.search_nearest(p.x(),p.y());
		}
	}
	
	return;
	

#if NEW_ALG
	std::set<CDT::Vertex_handle, sort_cdt_face_by_lowest_height>		to_do;
	
	for(CDT::Finite_vertices_iterator v = ioMesh.finite_vertices_begin(); v != ioMesh.finite_vertices_end(); ++v)
	{
		if(CategorizeVertex(ioMesh, v,terrain_Water) <= 0)
		if(!IsNoFlattenVertex(ioMesh,v))
			to_do.insert(v);
	}
	//printf("Q: %zd vertices.\n", to_do.size());
	
	double hwm = 0.0;
	
	if(!to_do.empty())
	{	
		hwm = (*to_do.begin())->info().height;
		
		std::set<CDT::Vertex_handle, sort_cdt_face_by_lowest_height>::iterator i,p;
		p = i = to_do.begin();
		++i;
		while(i != to_do.end())
		{
			//printf("   %p: %lf\n", &**i,(*i)->info().height);
			DebugAssert((*i)->info().height >= (*p)->info().height);
			p = i;
			++i;
		}
	}
	
	while(!to_do.empty())
	{
		CDT::Vertex_handle v = *to_do.begin();
		to_do.erase(to_do.begin());
		
		//printf("Vert %p: %lf, old hwm: %lf\n", &*v, v->info().height, hwm);

		DebugAssert(v->info().height >= hwm);
//		DebugAssert((v->info().height - hwm) > -0.00001);
		hwm = v->info().height;
		//printf("hwm now %lf from %p\n", hwm, &*v);
		
		Point2 low_p(cgal2ben(v->point()));	
		double ok = v->info().height;
		CDT::Face_circulator circ, stop;
		circ = stop =v->incident_faces();
		do {
			if(!ioMesh.is_infinite(circ))
			if(circ->info().terrain == terrain_Water)
			if(CanFlatten(circ))
			{
				CDT::Vertex_handle n = circ->vertex(CDT::ccw(circ->index(v)));

				double dist_m = LonLatDistMeters(low_p.x(),low_p.y(), CGAL::to_double(n->point().x()),CGAL::to_double(n->point().y()));
				double h_lim = ok + dist_m / 100.0 + 0.01;
				if(n->info().height > h_lim)
				{	
					//printf("   %p: was %lf, must be %lf\n", &*n, n->info().height, h_lim);
					//debug_mesh_point(cgal2ben(n->point()),1,0,0);
					to_do.erase(n);
					n->info().height = h_lim;
					to_do.insert(n);					
				}
			}
		} while(++circ != stop);
	}
	
#else
	std::set<CDT::Face_handle, sort_cdt_face_by_lowest_height>	changed;
	for(CDT::Finite_faces_iterator f = ioMesh.finite_faces_begin(); f != ioMesh.finite_faces_end(); ++f)
	{
		if (f->info().terrain == terrain_Water)
		if(CanFlatten(f))
			changed.insert(f);
	}
	
	int c = 0;
	
	while(!changed.empty())
	{
		++c;
		CDT::Face_handle w = *changed.begin();
		
		changed.erase(w);		

		double DEG_TO_MTR_LON = DEG_TO_MTR_LAT * cos(CGAL::to_double(w->vertex(0)->point().x()) * DEG_TO_RAD);
		
		int low_i = 0;
		double lowest = w->vertex(0)->info().height;
		for(int n = 1; n < 2; ++n)
		if(w->vertex(n)->info().height < w->vertex(low_i)->info().height)
			low_i = n;
		
		Point2	low_p = cgal2ben(w->vertex(low_i)->point());
		
		double ok = w->vertex(low_i)->info().height;

		//printf("Face %p, low height is %f at %lf,%lf (vert %d/%p)\n",&*w,ok,low_p.x(),low_p.y(), low_i, &*w->vertex(low_i));

		
		for(int n = 0; n < 3; ++n)
		if(n != low_i)
		{
			if(w->vertex(n)->info().height > ok)
			{
				double dist_m = LonLatDistMetersWithScale(low_p.x(),low_p.y(), CGAL::to_double(w->vertex(n)->point().x()),
									CGAL::to_double(w->vertex(n)->point().y()),DEG_TO_MTR_LON, DEG_TO_MTR_LAT);
				double h_lim = ok + dist_m / 100.0 + 0.01;
				
				//printf("Vertex %d (%p), h_lim is %lf, ok is %lf, dist is %lf\n",n, &*w->vertex(n), h_lim, ok, dist_m);
				
				if(w->vertex(n)->info().height > h_lim)
				{
//					double gap = w->vertex(n)->info().height - ok;
//					if(gap > 15.0)
//						debug_mesh_point(cgal2ben(w->vertex(n)->point()),1,0,0);
//					else if(gap > 10.0)
//						debug_mesh_point(cgal2ben(w->vertex(n)->point()),1,1,0);
//					else if(gap > 5.0)
//						debug_mesh_point(cgal2ben(w->vertex(n)->point()),0,1,0);
//					else
//						debug_mesh_point(cgal2ben(w->vertex(n)->point()),0,0,1);
										
					//printf("  Lowering %p from %f to %f\n", &*w->vertex(n),w->vertex(n)->info().height,h_lim);
					w->vertex(n)->info().height = ok + dist_m / 100.0;
					CDT::Face_circulator circ, stop;
					circ = stop = w->vertex(n)->incident_faces();
					do {
						if(circ != w)
						if(!ioMesh.is_infinite(circ))
						if(circ->info().terrain == terrain_Water)
						if(CanFlatten(circ))
						{
							//printf("   Q face %p\n", &*circ);
							changed.erase(circ);
							changed.insert(circ);
						}	
					} while(++circ != stop);
				}
			}
		}
	}
	//printf("processed: %d\n", c);
#endif
	/////////////////////////////////////////////
	//Andras: Water smoothing for rivers etc.
	/////////////////////////////////////////////

	for(int it_n = 0; it_n < WATER_SMOOTHER_ITERATIONS; ++it_n)
	{
		int water_vertices=0;
		int changed_vertices=0;
		// check all water vertices
		for(CDT::Finite_vertices_iterator v = ioMesh.finite_vertices_begin(); v != ioMesh.finite_vertices_end(); ++v)
		{
			if(CategorizeVertex(ioMesh, v,terrain_Water) <= 0)
			if(IsNoFlattenVertex(ioMesh,v)) {

				water_vertices++;
				double v_height = v->info().height;

				double DEG_TO_MTR_LON = DEG_TO_MTR_LAT * cos(CGAL::to_double(v->point().x()) * DEG_TO_RAD);

				Point2	v_p = cgal2ben(v->point());

				//printf("---   %p: has x: %lf y: %lf height %lf \n", &*v, v_p.x(),v_p.y(), v_height);

				bool is_first = true;
				double steepest_height = 0;
				double steepest_slope = 0;
				double steepest_dist = 0;

				// circulate each vertex and get all the surrounding vertices, then check how they relate to the central vertex (especially, what the slope of the edge is)
				CDT::Face_circulator circ, stop;
				circ = stop =v->incident_faces();
				do {
					if(!ioMesh.is_infinite(circ))
					{
						CDT::Vertex_handle vs = circ->vertex(CDT::ccw(circ->index(v)));

						if(CategorizeVertex(ioMesh, vs,terrain_Water) <= 0)
						if(IsNoFlattenVertex(ioMesh,vs)) {
							double vs_height = vs->info().height;
							double dist_m = LonLatDistMetersWithScale(v_p.x(),v_p.y(), CGAL::to_double(vs->point().x()),
									CGAL::to_double(vs->point().y()),DEG_TO_MTR_LON, DEG_TO_MTR_LAT);
							DebugAssert(dist_m != 0.0);

							double vvs_slope = (v_height - vs_height) / dist_m;
							double height_diff = (v_height - vs_height) ;

							//printf("--- ---  %p: has  x: %lf y: %lf height %lf , height_diff %lf , distance %lf , slope %lf \n", &*vs, CGAL::to_double(vs->point().x()), CGAL::to_double(vs->point().y()), vs_height, height_diff, dist_m, vvs_slope);

							// here we are only looking for points which are lower than central vertex and the slope to them is steeper than we are comfortable with
							if(vvs_slope > MAX_WATER_SLOPE) {
								if(is_first) {
									steepest_height = vs_height;
									steepest_slope = vvs_slope;
									steepest_dist = dist_m;
									is_first = false;
								} else {
									if(steepest_slope < vvs_slope) {
										steepest_slope = vvs_slope;
										steepest_height = vs_height;
										steepest_dist = dist_m;
									}
								}
							}
						}
					}
				} while(++circ != stop);

				// if there was at least one relevant too steep & low candidate, then change center to a height which would bring it to the limit of acceptable slope
				if(!is_first)
				{
					changed_vertices++;
					double new_height = steepest_height + (MAX_WATER_SLOPE * steepest_dist);

					//printf("###   %p: was %lf, must be %lf\n", &*v, v_height, new_height);
					v->info().height = new_height;
				}

			}

		}
		printf("Water smoothing iteration %d , water vertices: %d , changed vertices: %d .\n", it_n, water_vertices, changed_vertices);
	}
}

/*
 * CalculateMeshNormals
 *
 * This routine calcs the normals per vertex.
 *
 */
void CalculateMeshNormals(CDT& ioMesh)
{
	for(CDT::Finite_faces_iterator f = ioMesh.finite_faces_begin(); f != ioMesh.finite_faces_end(); ++f)
	{
		Point3	selfP(CGAL::to_double(f->vertex(0)->point().x()), CGAL::to_double(f->vertex(0)->point().y()), f->vertex(0)->info().height);
		Point3  lastP(CGAL::to_double(f->vertex(1)->point().x()), CGAL::to_double(f->vertex(1)->point().y()), f->vertex(1)->info().height);
		Point3  nowiP(CGAL::to_double(f->vertex(2)->point().x()), CGAL::to_double(f->vertex(2)->point().y()), f->vertex(2)->info().height);
		Vector3 v1(selfP, lastP);
		Vector3 v2(selfP, nowiP);
		v1.dx *= (DEG_TO_MTR_LAT * cos(selfP.y * DEG_TO_RAD));
		v2.dx *= (DEG_TO_MTR_LAT * cos(selfP.y * DEG_TO_RAD));
		v1.dy *= (DEG_TO_MTR_LAT);
		v2.dy *= (DEG_TO_MTR_LAT);
		
		Vector3 normal;
		
		if((v1.dx == 0.0 && v1.dy == 0.0 && v1.dz == 0.0) ||
		   (v2.dx == 0.0 && v2.dy == 0.0 && v2.dz == 0.0))
		{
			normal = Vector3(0,0,1);
		}
		else 
		{
		
			v1.normalize();
			v2.normalize();
			normal = v1.cross(v2);
			if(normal.dz <= 0.0)
			{
				normal = Vector3(0,0,1);
			} 
			else
				normal.normalize();
		}

		f->info().normal[0] = normal.dx;
		f->info().normal[1] = normal.dy;
		f->info().normal[2] = normal.dz;		
	}

	for (CDT::Finite_vertices_iterator i = ioMesh.finite_vertices_begin(); i != ioMesh.finite_vertices_end(); ++i)
	{
		Vector3	total(0.0, 0.0, 0.0);
		CDT::Face_circulator circ, stop;
		circ = stop = ioMesh.incident_faces(i);

		do {
			if(!ioMesh.is_infinite(circ))
			{
				total.dx += circ->info().normal[0];
				total.dy += circ->info().normal[1];
				total.dz += circ->info().normal[2];
			}
		} while (++circ != stop);
		
        DebugAssert(total.dx != 0.0 || total.dy != 0.0 || total.dz != 0.0);
        DebugAssert(total.dz > 0.0);
        total.normalize();
        i->info().normal[0] = total.dx;
        i->info().normal[1] = total.dy;
        i->info().normal[2] = total.dz;
	}
}

/*******************************************************************************************
 *******************************************************************************************
 ** GENERATION OF A MESH MASTER ROUTINE ****************************************************
 *******************************************************************************************
 *******************************************************************************************/


double dist_from_line(const Point_2& p, const Point_2& q, const Point_2& r)
{
	Line_2 l(p,r);
	return CGAL::to_double(CGAL::squared_distance(l,q));
}





void	TriangulateMesh(Pmwx& inMap, CDT& outMesh, DEMGeoMap& inDEMs, const char * mesh_folder, ProgressFunc prog)
{
	DEMGeo& sdf = inDEMs[dem_Wizard];
	sdf.resize(1200,1200);
	sdf.copy_geo_from(inDEMs[dem_Elevation]);
	sdf.mPost=0;
	CreateWaterSDF(inMap, sdf);

	TIMER(Total)
	outMesh.clear();

	int		x, y;
	DEMGeo&	orig(inDEMs[dem_Elevation]);
	DEMGeo plate = orig;
	DEMGeo& bathy(inDEMs[dem_Bathymetry]);

	Assert(orig.get(0			 ,0				) != DEM_NO_DATA);
	Assert(orig.get(orig.mWidth-1,orig.mHeight-1) != DEM_NO_DATA);
	Assert(orig.get(0			 ,orig.mHeight-1) != DEM_NO_DATA);
	Assert(orig.get(orig.mWidth-1,orig.mHeight-1) != DEM_NO_DATA);

	DEMMask	deriv(orig.mWidth, orig.mHeight,false);					// A mash-up of points we will add to the final mesh.
	deriv.copy_geo_from(orig);

	/************************************************************************************************************
	 * PRE-SET UP -- LOAD BORDERS
	 ************************************************************************************************************/

	bool	has_borders[4];
	
	{
		// This adds edge points to the DEM if we need to (e.g. no slaving) or loads slaves.

		TIMER(edges);

		char	fname_lef[512];
		char	fname_bot[512];
		char	fname_rgt[512];
		char	fname_top[512];

		string border_loc = mesh_folder;

		make_cache_file_path(border_loc.c_str(),deriv.mWest-1, deriv.mSouth,"border",fname_lef);
		make_cache_file_path(border_loc.c_str(),deriv.mWest+1, deriv.mSouth,"border",fname_rgt);
		make_cache_file_path(border_loc.c_str(),deriv.mWest, deriv.mSouth-1,"border",fname_bot);
		make_cache_file_path(border_loc.c_str(),deriv.mWest, deriv.mSouth+1,"border",fname_top);

		mesh_match_t junk1, junk2, junk3;
		has_borders[0] = gMeshPrefs.border_match ? load_match_file(fname_lef, junk1, junk2, gMatchBorders[0], junk3) : false;
		has_borders[1] = gMeshPrefs.border_match ? load_match_file(fname_bot, junk1, junk2, junk3, gMatchBorders[1]) : false;
		has_borders[2] = gMeshPrefs.border_match ? load_match_file(fname_rgt, gMatchBorders[2], junk1, junk2, junk3) : false;
		has_borders[3] = gMeshPrefs.border_match ? load_match_file(fname_top, junk1, gMatchBorders[3], junk2, junk3) : false;
	}

	/************************************************************************************************************
	 * PRE-SET UP -- PRE-TRIANGULATION
	 ************************************************************************************************************/

	DEMGeo	best_density(100,100);
	best_density.copy_geo_from(orig);
	
	{
		// We are going to do a temporary triangulation to measure the ideal point distribution for our mesh
		// given our budget and its actual shape, if elevation was the only concern.  We do this by just
		// running the greedy mesh.  
		CDT		temp_mesh;

		CDT::Face_handle temp_hint;
		InsertDEMPoint(orig, deriv, temp_mesh, 0, 0, temp_hint);
		InsertDEMPoint(orig, deriv, temp_mesh, orig.mWidth-1, 0, temp_hint);
		InsertDEMPoint(orig, deriv, temp_mesh, orig.mWidth-1, orig.mHeight-1, temp_hint);
		InsertDEMPoint(orig, deriv, temp_mesh, 0, orig.mHeight-1, temp_hint);

//		for(int b=0;b<4;++b)
//		if (!gMatchBorders[b].vertices.empty())
//			match_border(temp_mesh, gMatchBorders[b], b);

		bool fake_has_borders[4] = { false, false, false, false };
		AddEdgePoints(orig, deriv, 20, 1, fake_has_borders, temp_mesh);

//		DEMGrid	gridlines(orig);
		GreedyMeshBuild(temp_mesh, orig, deriv, inMap, gMeshPrefs.max_error, 0.0, gMeshPrefs.max_points, prog);
		
		// Now iterate and accumulate the vertices into a low res DEM - we will end up with linear vertex density per
		// tile.
		
		for(CDT::Finite_vertices_iterator i = temp_mesh.finite_vertices_begin(); i != temp_mesh.finite_vertices_end(); ++i)
		{
			int x = round(best_density.lon_to_x(CGAL::to_double(i->point().x())));
			int y = round(best_density.lat_to_y(CGAL::to_double(i->point().y())));
			if(x >= 0 && x < best_density.mWidth &&
			   y >= 0 && y < best_density.mHeight)
			best_density(x,y) = best_density.get(x,y) + 1;
		}

		for(DEMGeo::iterator i = best_density.begin(); i != best_density.end(); ++i)
			*i = sqrtf(*i);

//		#if DEV
//		inDEMs[dem_Wizard] = best_density;		
//		#endif
	}
	
	deriv = false;

	/************************************************************************************************************
	 * ACTUAL TRIANGULATION
	 ************************************************************************************************************/
	
	/* TRIANGULATE CORNERS */
	
	CDT::Face_handle hint;
	InsertDEMPoint(orig, deriv, outMesh, 0, 0, hint);
	InsertDEMPoint(orig, deriv, outMesh, orig.mWidth-1, 0, hint);
	InsertDEMPoint(orig, deriv, outMesh, orig.mWidth-1, orig.mHeight-1, hint);
	InsertDEMPoint(orig, deriv, outMesh, 0, orig.mHeight-1, hint);

	PAUSE_STEP("Finished corners")
	
	/* TRIANGULATE CONSTRAINTS */

	AddConstraintPoints(inMap, orig, outMesh);
	
	PAUSE_STEP("Pre-simplify")
	
	/* SIMPLIFY CONSTRAINTS TO CUT DOWN MESH DENSITY */
	
	{
		StElapsedTime simp("simplify edges");
		
		printf("Before simplify: %zd/%zd\n",outMesh.number_of_vertices(),outMesh.number_of_faces());
//		RF_Notifiable::Notify(rf_Cat_File, rf_Msg_TriangleHiChange, NULL); 
		MeshSimplify	simplify_me(outMesh, dist_from_line);
		simplify_me.simplify(0.0001 * 0.0001);
		printf("After simplify: %zd/%zd\n",outMesh.number_of_vertices(),outMesh.number_of_faces());
	}

	PAUSE_STEP("Finished constraints")

	/* SUBDIVIDE CONSTRAINTS TO AVOID CREASE LINES IN MESH */
	
	SubdivideConstraints(outMesh, orig, best_density);

	PAUSE_STEP("Finished subdivide constraints")
		
	/* TRIANGULATE SLAVED BORDER */
	
	for(int b=0;b<4;++b)
	if (!gMatchBorders[b].vertices.empty())
		match_border(outMesh, gMatchBorders[b], b);

	PAUSE_STEP("Finished borders")
	
	/* TRIANGULATE NON-SLAVED EDGES */

	AddEdgePoints(orig, deriv, 20, 1, has_borders, outMesh);

	PAUSE_STEP("Finished edges")
	
	/* TRIANGULATE WATER INTERIOR */
	
	CopyBathymetryToPlate(plate, bathy, inMap, terrain_Water);
//	bathy = sdf;
//	bathy *= -4.0;
//	for(DEMGeo::iterator it = bathy.begin(); it != bathy.end(); ++it)
//	{
//		*it = max(*it,-50.0f);
//		std::pair<int,int> xy = bathy.to_coordinates(it);
//		*it = *it + orig.value_linear(
//					bathy.x_to_lon(xy.first),
//					bathy.y_to_lat(xy.second));
//	}
	double wet_ratio = CopyWetPointsWithSDF(orig, deriv, outMesh, sdf, terrain_Water, LOW_RES_WATER_INTERVAL, inMap);
					   CopyWetPoints(orig, deriv, outMesh,APT_INTERVAL, terrain_Airport, inMap);
	double dry_ratio = 1.0 - wet_ratio;

	PAUSE_STEP("Finished water interior")
	
	/* TRINAGULATE GREEDILY */

	GreedyMeshBuild(outMesh, orig, deriv, inMap, /*gridlines,*/ gMeshPrefs.max_error, 0.0, (dry_ratio * 0.8 + 0.2) * gMeshPrefs.max_points, prog);

	PAUSE_STEP("Finished greedy1")

	GreedyMeshBuild(outMesh, orig, deriv, inMap, /*gridlines,*/ 0.0, gMeshPrefs.max_tri_size_m * MTR_TO_NM * NM_TO_DEG_LAT, gMeshPrefs.max_points, prog);

	PAUSE_STEP("Finished greedy2")

#if SPLIT_CLIFFS

	// Cliff splitting: any time we have a triangle that is a cliff whose three neighbors are all NOT a cliff, we have
	// a problem: since the landuse on the cliff will be bordered by something else on all 3 sides, the cliff will be lost
	// to borders.  So...we will subdivide the triangle into four by inserting the midpoints of each side of the triangle.
	// We expect this to produce slightly more 'regular' results than subdiving into three triangles with the centroid.
	{
		Locator lp {inMap};

#if KILL_SPLIT_CLIFF_EXTRACT
		const auto split_cliff = [&](const auto& f, int i0, int i1) -> std::pair<Point_2,boost::optional<double>> {
			auto point = CGAL::midpoint(f->vertex(i0)->point(),f->vertex(i1)->point());
			return { point, {  } };
		};
		
#else
		const auto split_cliff = [&](const auto& f, int i0, int i1) -> std::pair<Point_2,boost::optional<double>> {
			auto point = CGAL::midpoint(f->vertex(i0)->point(),f->vertex(i1)->point());
			boost::optional<double> elevation {};
			bool use_existing_height = false;

			const auto& r = lp.locate(point);
			const Pmwx::Face_const_handle* face;
			const Pmwx::Halfedge_const_handle* he;
			if ((face = boost::get<Pmwx::Face_const_handle>(&r)))
			{
				use_existing_height = (*face)->data().mHasElevation;
			}
			else if ((he = boost::get<Pmwx::Halfedge_const_handle>(&r)))
			{
				use_existing_height = (*he)->face()->data().mHasElevation;
			}

			if (use_existing_height)
			{
				elevation = (f->vertex(i0)->info().height + f->vertex(i1)->info().height) * 0.5f;
			}
			return { point, { elevation } };
		};
#endif

		map<Point_2, boost::optional<double>> splits_needed;
		for (CDT::Finite_faces_iterator f = outMesh.finite_faces_begin(); f != outMesh.finite_faces_end(); ++f)
		{
			if(!tri_is_cliff(outMesh, f)) continue;

			if(!tri_is_cliff(outMesh, f->neighbor(0)) ||
			   !tri_is_cliff(outMesh, f->neighbor(1)) ||
			   !tri_is_cliff(outMesh, f->neighbor(2)))
			{
				splits_needed.insert(split_cliff(f, 0, 1));
				splits_needed.insert(split_cliff(f, 1, 2));
				splits_needed.insert(split_cliff(f, 2, 0));
			}
		}
		
		printf("Need %zd splits.\n", splits_needed.size());
		hint = CDT::Face_handle();
		for(const auto& s : splits_needed)
		{
			InsertAnyPoint(orig, outMesh, s.first, hint, s.second);
	//		debug_mesh_point(cgal2ben(*n), 1, 0, 0);
		}
	}
	
	PAUSE_STEP("Finished Split Cliffs")
	
#endif

	// BEN SAYS: we are no longer trying to 'conform' the mesh because:
	// 1. It makes a TON of points.
	// 2. We don't have sqrt in our NT so we have to hack this to make it work and
	// 2a. Sometimes the conformer goes insane and inserts like a billion points in one place, which is bad.
	//	  Instead we simply assure that our mesh lines are reasonably subdivided.

#if !PHONE && 0
	int n_vert = outMesh.number_of_vertices();					// Ben says: typically the end() iterator for the triangulation is _not_ stable across inserts.
	CGAL::make_conforming_Delaunay_2(outMesh);
//	CGAL::make_conforming_any_2<CDT,LCP>(outMesh);				// Because the finite iterator is a filtered wrapper around the triangulation, it too is not stable
																// across inserts.  To get around this, simply note how many vertices we inserted.  Note that we are assuming
	CDT::Vertex_iterator v1,v2,v;								// vertices to be inserted into the END of the iteration std::list!
	v1 = outMesh.vertices_begin();
	v2 = outMesh.vertices_end();	
	DebugAssert(outMesh.number_of_vertices() >= n_vert);
	std::advance(v1,n_vert);
	
	int n_added = outMesh.number_of_vertices() - n_vert;
		
	printf("Conformer built %d verts.\n", n_added);
	
	for(v=v1;v!=v2;++v)
	{
		v->info().height = orig.value_linear(CGAL::to_double(v->point().x()),CGAL::to_double(v->point().y()));
//		debug_mesh_point(cgal2ben(v->point()),1,0,0);
		#if DEV
		if(!gMatchBorders[0].vertices.empty())
			DebugAssert(v->point().x() != orig.mWest);
		if(!gMatchBorders[1].vertices.empty())
			DebugAssert(v->point().y() != orig.mSouth);
		if(!gMatchBorders[2].vertices.empty())
			DebugAssert(v->point().x() != orig.mEast);
		if(!gMatchBorders[3].vertices.empty())
			DebugAssert(v->point().y() != orig.mNorth);
		#endif	
	}
	
	PAUSE_STEP("Conform")
	
#endif
	
	/*********************************************************************************************************************
	 * LAND USE CALC (A LITTLE BIT) AND WATER PROCESSING
	 *********************************************************************************************************************/

	PROGRESS_START(prog,1,3,"Calculating Wet Areas");
	{
		SetTerrainForConstraints(outMesh, orig);
	}

	// To guarantee that the sea floor of wet triangles can be flat (e.g. we don't have three coastal vertices) we 
	// find two-side coastal triangles and subdivide.  If we have a single wet tri, we insert the centroid; otherwise
	// we subdivide the 'open' side (to try to get less slivery triangles).

#if SPLIT_BEACHED_WATER

	std::set<Point_2> splits_needed;
	int ctr=0,tot=outMesh.number_of_faces();
	for (CDT::Finite_faces_iterator f = outMesh.finite_faces_begin(); f != outMesh.finite_faces_end(); ++f,++ctr)
	{
		if( f->info().terrain == terrain_Water)
		{
			PROGRESS_SHOW(prog,1,3,"Calculating Wet Areas",ctr,tot);
			int c0 = CategorizeVertex(outMesh, f->vertex(0), terrain_Water);
			int c1 = CategorizeVertex(outMesh, f->vertex(1), terrain_Water);
			int c2 = CategorizeVertex(outMesh, f->vertex(2), terrain_Water);
			
			if(c1 == 0 && c2 == 0 && c0 == 0)
			{
				Point_2 c(CGAL::centroid(outMesh.triangle(f)));
				splits_needed.insert(c);
//				debug_mesh_point(cgal2ben(c),1,1,0);
			}

			// Prevent "bridging" between coastal points across waterways that should have depth
			if (c0 == 0 && c1 == 0)
			{
				auto nf = f->neighbor(2);
				if (!outMesh.is_infinite(nf) && nf->info().terrain == terrain_Water)
				{
					splits_needed.insert(CGAL::midpoint(f->vertex(0)->point(), f->vertex(1)->point()));
				}
			}
			if (c0 == 0 && c2 == 0)
			{
				auto nf = f->neighbor(1);
				if (!outMesh.is_infinite(nf) && nf->info().terrain == terrain_Water)
				{
					splits_needed.insert(CGAL::midpoint(f->vertex(0)->point(), f->vertex(2)->point()));
				}
			}
			if (c1 == 0 && c2 == 0)
			{
				auto nf = f->neighbor(0);
				if (!outMesh.is_infinite(nf) && nf->info().terrain == terrain_Water)
				{
					splits_needed.insert(CGAL::midpoint(f->vertex(1)->point(), f->vertex(2)->point()));
				}
			}
		}
	}

	PROGRESS_DONE(prog,1,3,"Calculating Wet Areas");

	printf("Need %zd splits for beaches and waterways.\n", splits_needed.size());
	hint = CDT::Face_handle();
	std::set<CDT::Face_handle>	who;
	for(std::set<Point_2>::iterator n = splits_needed.begin(); n != splits_needed.end(); ++n)
	{
		CDT::Vertex_handle v = InsertAnyPoint(orig, outMesh, *n, hint);
		CDT::Face_circulator circ,stop;
		circ=stop=outMesh.incident_faces(v);
		do {
			who.insert(circ);
		} while(++circ != stop);
	}
	
	SetTerrainForConstraints(outMesh, orig);

	for(std::set<CDT::Face_handle>::iterator w = who.begin(); w != who.end(); ++w)
	{
		DebugAssert((*w)->info().terrain == terrain_Water);
	}

	PAUSE_STEP("Split Beached Water")

#endif

	FlattenWater(outMesh, inDEMs[dem_Water_Surface]);

	/*********************************************************************************************************************
	 * CLEANUP - CALC MESH NORMALS
	 *********************************************************************************************************************/


	if (prog) prog(2, 3, "Calculating Wet Areas", 0.5);
	CalculateMeshNormals(outMesh);

	if (prog) prog(2, 3, "Calculating Wet Areas", 1.0);

//	orig.swap(water);
}


#pragma mark -
/*******************************************************************************************
 *******************************************************************************************
 ** MESH LANDUSE ASSIGNMENT ****************************************************************
 *******************************************************************************************
 *******************************************************************************************/

/*
	NOTE ON TERRAIN TYPES:
		The vector map contains a terrain type like none or airport or water.
		From this we then get natural, airport, or water in the mesh.  We then substitute
		on all but water through the spreadsheet.
*/

float enum_sample_tri(DEMGeo& d, double x0, double y0, double x1, double y1, double x2, double y2, double center_x, double center_y)
{
/*
	float lu0  = d.search_nearest(center_x, center_y);
	float lu1 = d.search_nearest(x0,y0);
	float lu2 = d.search_nearest(x1,y1);
	float lu3 = d.search_nearest(x2,y2);
	float lu = MAJORITY_RULES(lu0,lu1,lu2, lu3);
	return lu;
*/
	x0 = d.lon_to_x(x0);
	x1 = d.lon_to_x(x1);
	x2 = d.lon_to_x(x2);
	y0 = d.lat_to_y(y0);
	y1 = d.lat_to_y(y1);
	y2 = d.lat_to_y(y2);

		PolyRasterizer<double>	 pr;
	pr.AddEdge(x0,y0,x1,y1);
	pr.AddEdge(x1,y1,x2,y2);
	pr.AddEdge(x2,y2,x0,y0);
	int x, y, xs, xe;
	pr.SortMasters();
	
	if(pr.masters.empty())
	{
		// triangle is so f--- thin that when rounded it is colinear.  No masters get kept. bail now to avoid seg fault?1
		return d.xy_nearest(center_x,center_y);
	}
	y = intmax2(floor(pr.masters.front().y1), 0);

	
	hash_map<int,int>	histo;
	
	while(!pr.DoneScan())
	{
		while (pr.GetRange(xs, xe))
		{
			for(x = xs; x < xe; ++x)
			{
				int lu = d.get(x,y);
				if(lu != DEM_NO_DATA)
					histo[lu]++;
			}
		}
		++y;
		pr.AdvanceScanline(y);
	}
	if(histo.empty())
		return d.xy_nearest(center_x,center_y);
	hash_map<int,int>::iterator l, best, town;
	best = histo.begin();
	town = histo.end();
	for(l = histo.begin(); l !=histo.end(); ++l)
	{
		if(best->second < l->second)
			best = l;
		if(town == histo.end() || town->second < l->second)
		if(gLandClassInfo.count(l->first) && gLandClassInfo[l->first].urban_density > 0.0)
			town = l;
	}
	
	if(town != histo.end())
	// This is the RATIO of how much to AMPLIFY the urban to overcome other land classes.
	// Set to 1.0 to disable magic behavior.
	if((town->second * 1) > best->second)
		return town->first;
	return best->first;
}

void	AssignLandusesToMesh(	DEMGeoMap& inDEMs,
								CDT& ioMesh,
								const char * mesh_folder,
								ProgressFunc	inProg)
{


		CDT::Finite_faces_iterator tri;
		CDT::Finite_vertices_iterator vert;

		int	rock_enum = LookupToken("rock_gray.ter");

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.0);

//	DEMGeo&	inClimate(inDEMs[dem_Clima0te]);
	DEMGeo&	inClimStyle(inDEMs[dem_ClimStyle]);
	DEMGeo&	inAgriStyle(inDEMs[dem_AgriStyle]);
	DEMGeo&	inSoilStyle(inDEMs[dem_SoilStyle]);
	DEMGeo&	inElevation(inDEMs[dem_Elevation]);
	DEMGeo&	inSlope(inDEMs[dem_Slope]);
	DEMGeo&	inSlopeHeading(inDEMs[dem_SlopeHeading]);
	DEMGeo&	inRelElev(inDEMs[dem_RelativeElevation]);
	DEMGeo&	inRelElevRange(inDEMs[dem_ElevationRange]);
	DEMGeo&	inTemp(inDEMs[dem_Temperature]);
	DEMGeo&	inTempRng(inDEMs[dem_TemperatureRange]);
	DEMGeo&	inRain(inDEMs[dem_Rainfall]);
	DEMGeo& inUrbanDensity(inDEMs[dem_UrbanDensity]);
	DEMGeo& inUrbanRadial(inDEMs[dem_UrbanRadial]);
	DEMGeo& inUrbanTransport(inDEMs[dem_UrbanTransport]);
	DEMGeo& usquare(inDEMs[dem_UrbanSquare]);

	DEMGeo	landuse(inDEMs[dem_LandUse]);

// BEN SEZ: do NOT overwrite interrupted and other such areas with nearest landuse - that causes problems.
	for (int y = 0; y < landuse.mHeight;++y)
	for (int x = 0; x < landuse.mWidth; ++x)
	{
		float e = landuse(x,y);
		if (e == NO_VALUE ||
//			e == lu_usgs_INTERRUPTED_AREAS ||
//			e == lu_usgs_URBAN_SQUARE ||
//			e == lu_usgs_URBAN_IRREGULAR ||
			e == lu_globcover_WATER)
//			e == lu_usgs_SEA_WATER)
//			e == lu_usgs_DEM_NO_DATA)
			landuse(x,y) = DEM_NO_DATA;
	}
	landuse.fill_nearest();

	/***********************************************************************************************
	 * ASSIGN BASIC LAND USES TO MESH
	 ***********************************************************************************************/

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.1);
	for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
	{
		// First assign a basic land use type.
		{
			tri->info().flag = 0;
			// Hires - take from DEM if we don't have one.
			if (tri->info().terrain != terrain_Water)
			{
				double x0 = CGAL::to_double(tri->vertex(0)->point().x());
				double y0 = CGAL::to_double(tri->vertex(0)->point().y());
				double x1 = CGAL::to_double(tri->vertex(1)->point().x());
				double y1 = CGAL::to_double(tri->vertex(1)->point().y());
				double x2 = CGAL::to_double(tri->vertex(2)->point().x());
				double y2 = CGAL::to_double(tri->vertex(2)->point().y());
				double	center_x = (x0 + x1 + x2) / 3.0;
				double	center_y = (y0 + y1 + y2) / 3.0;

				float lu = enum_sample_tri(landuse, x0,y0,x1,y1,x2,y2, center_x, center_y);

				float cs0 = inClimStyle.search_nearest(center_x, center_y);
				float cs1 = inClimStyle.search_nearest(x0,y0);
				float cs2 = inClimStyle.search_nearest(x1,y1);
				float cs3 = inClimStyle.search_nearest(x2,y2);
				float cs = MAJORITY_RULES(cs0,cs1,cs2,cs3);

				float as0 = inAgriStyle.search_nearest(center_x, center_y);
				float as1 = inAgriStyle.search_nearest(x0,y0);
				float as2 = inAgriStyle.search_nearest(x1,y1);
				float as3 = inAgriStyle.search_nearest(x2,y2);
				float as = MAJORITY_RULES(as0,as1,as2,as3);

				float ss0 = inSoilStyle.search_nearest(center_x, center_y);
				float ss1 = inSoilStyle.search_nearest(x0,y0);
				float ss2 = inSoilStyle.search_nearest(x1,y1);
				float ss3 = inSoilStyle.search_nearest(x2,y2);
				float ss = MAJORITY_RULES(ss0,ss1,ss2,ss3);
				

//				float cl  = inClimate.search_nearest(center_x, center_y);
//				float cl1 = inClimate.search_nearest(x0,y0);
//				float cl2 = inClimate.search_nearest(x1,y1);
//				float cl3 = inClimate.search_nearest(x2,y2);

				// Ben sez: tiny island in the middle of nowhere - do NOT expect LU.  That's okay - Sergio doesn't need it.
//				if (lu == DEM_NO_DATA)
//					fprintf(stderr, "NO data anywhere near %f, %f\n", center_x, center_y);
//				cl = MAJORITY_RULES(cl, cl1, cl2, cl3);

//				float	el1 = inElevation.value_linear(x0,y0);
//				float	el2 = inElevation.value_linear(x1,y1);
//				float	el3 = inElevation.value_linear(x2,y2);
//				float	el = SAFE_AVERAGE(el1, el2, el3);

				float	sl1 = inSlope.value_linear(x0,y0);
				float	sl2 = inSlope.value_linear(x1,y1);
				float	sl3 = inSlope.value_linear(x2,y2);
				float	sl = SAFE_MAX	 (sl1, sl2, sl3);	// Could be safe max.
				if (sl<0.0) sl=0.0;

				float	tm1 = inTemp.value_linear(x0,y0);
				float	tm2 = inTemp.value_linear(x1,y1);
				float	tm3 = inTemp.value_linear(x2,y2);
				float	tm = SAFE_AVERAGE(tm1, tm2, tm3);	// Could be safe max.

				float	tmr1 = inTempRng.value_linear(x0,y0);
				float	tmr2 = inTempRng.value_linear(x1,y1);
				float	tmr3 = inTempRng.value_linear(x2,y2);
				float	tmr = SAFE_AVERAGE(tmr1, tmr2, tmr3);	// Could be safe max.

				float	rn1 = inRain.value_linear(x0,y0);
				float	rn2 = inRain.value_linear(x1,y1);
				float	rn3 = inRain.value_linear(x2,y2);
				float	rn = SAFE_AVERAGE(rn1, rn2, rn3);	// Could be safe max.

//				float	sh1 = inSlopeHeading.value_linear(x0,y0);
//				float	sh2 = inSlopeHeading.value_linear(x1,y1);
///				float	sh3 = inSlopeHeading.value_linear(x2,y2);
//				float	sh = SAFE_AVERAGE(sh1, sh2, sh3);	// Could be safe max.

				float	re1 = inRelElev.value_linear(x0,y0);
				float	re2 = inRelElev.value_linear(x1,y1);
				float	re3 = inRelElev.value_linear(x2,y2);
				float	re = SAFE_AVERAGE(re1, re2, re3);	// Could be safe max.

				float	er1 = inRelElevRange.value_linear(x0,y0);
				float	er2 = inRelElevRange.value_linear(x1,y1);
				float	er3 = inRelElevRange.value_linear(x2,y2);
				float	er = SAFE_AVERAGE(er1, er2, er3);	// Could be safe max.

				int		near_water =(tri->neighbor(0)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(0))) ||
									(tri->neighbor(1)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(1))) ||
									(tri->neighbor(2)->info().terrain == terrain_Water && !ioMesh.is_infinite(tri->neighbor(2)));

				float	uden1 = inUrbanDensity.value_linear(x0,y0);
				float	uden2 = inUrbanDensity.value_linear(x1,y1);
				float	uden3 = inUrbanDensity.value_linear(x2,y2);
				float	uden = SAFE_AVERAGE(uden1, uden2, uden3);	// Could be safe max.

				float	urad1 = inUrbanRadial.value_linear(x0,y0);
				float	urad2 = inUrbanRadial.value_linear(x1,y1);
				float	urad3 = inUrbanRadial.value_linear(x2,y2);
				float	urad = SAFE_AVERAGE(urad1, urad2, urad3);	// Could be safe max.

				float	utrn1 = inUrbanTransport.value_linear(x0,y0);
				float	utrn2 = inUrbanTransport.value_linear(x1,y1);
				float	utrn3 = inUrbanTransport.value_linear(x2,y2);
				float	utrn = SAFE_AVERAGE(utrn1, utrn2, utrn3);	// Could be safe max.

				float usq  = usquare.search_nearest(center_x, center_y);
				float usq1 = usquare.search_nearest(x0,y0);
				float usq2 = usquare.search_nearest(x1,y1);
				float usq3 = usquare.search_nearest(x2,y2);
				usq = MAJORITY_RULES(usq, usq1, usq2, usq3);

//				float	el1 = tri->vertex(0)->info().height;
//				float	el2 = tri->vertex(1)->info().height;
//				float	el3 = tri->vertex(2)->info().height;
//				float	el_tri = (el1 + el2 + el3) / 3.0;

				float	sl_tri = 1.0 - tri->info().normal[2];
				float	flat_len = sqrt(tri->info().normal[1] * tri->info().normal[1] + tri->info().normal[0] * tri->info().normal[0]);
				float	sh_tri = tri->info().normal[1];
				if (flat_len != 0.0)
				{
					sh_tri /= flat_len;
					sh_tri = max(-1.0f, min(sh_tri, 1.0f));
				}

				float	patches = (gMeshPrefs.rep_switch_m == 0.0) ? 100.0 : (60.0 * NM_TO_MTR / gMeshPrefs.rep_switch_m);
				int x_variant = fabs(center_x /*+ RandRange(-0.03, 0.03)*/) * patches; // 25.0;
				int y_variant = fabs(center_y /*+ RandRange(-0.03, 0.03)*/) * patches; // 25.0;
//				int variant_blob = ((x_variant + y_variant * 2) % 4) + 1;
//				int variant_head = (tri->info().normal[0] > 0.0) ? 6 : 8;
//
//				if (sh_tri < -0.7)	variant_head = 7;
//				if (sh_tri >  0.7)	variant_head = 5;

				//fprintf(stderr, " %d", tri->info().feature);
				int zoning = NO_VALUE;//(tri->info().orig_face == Pmwx::Face_handle()) ? NO_VALUE : tri->info().orig_face->data().GetZoning();
				if(zoning == NO_VALUE && tri->info().orig_face != Pmwx::Face_handle())
					zoning = tri->info().orig_face->data().GetParam(af_Variant,-1.0) + 1.0;
				int terrain = FindNaturalTerrain(tri->info().feature, zoning, lu, ss, as,cs, sl, sl_tri, tm, tmr, rn, near_water, sh_tri, re, er, uden, urad, utrn, usq, fabs((float) center_y)/*, variant_blob, variant_head*/);
				if (terrain == -1)
					AssertPrintf("Cannot find terrain for: %s, %f\n", FetchTokenString(lu), /*FetchTokenString(cl), el, */ sl);

				tri->info().mesh_temp = tm;
				tri->info().mesh_rain = rn;
			#if OPENGL_MAP
				tri->info().debug_terrain_orig = terrain;
				tri->info().debug_slope_dem = sl;
				tri->info().debug_slope_tri = sl_tri;
				tri->info().debug_temp_range = tmr;
				tri->info().debug_heading = sh_tri;
				tri->info().debug_re = re;
				tri->info().debug_er = er;				
				tri->info().debug_lu[0] = lu;
				tri->info().debug_lu[1] = lu;
				tri->info().debug_lu[2] = lu;
				tri->info().debug_lu[3] = lu;
				tri->info().debug_lu[4] = lu ;
			#endif
				if (terrain == -1)
				{
					AssertPrintf("No rule. lu=%s, slope=%f, trislope=%f, temp=%f, temprange=%f, rain=%f, water=%d, heading=%f, lat=%f\n",
						FetchTokenString(lu), /*el,*/ acos(1-sl)*RAD_TO_DEG, acos(1-sl_tri)*RAD_TO_DEG, tm, tmr, rn, near_water, sh_tri, center_y);
				}
				//fprintf(stderr, "->%d", terrain);

				tri->info().terrain = terrain;

			}

		}
	}
	
	/***********************************************************************************************
	 * ASSIGN BASIC LAND USES TO MESH
	 ***********************************************************************************************/

	if (inProg) inProg(0, 1, "Fixing Airports", 0.2);
	{
//		float colors[] = {	1,0,0,
//							1,1,0,
//							0,1,0,
//							0,1,1,
//							0,0,1,
//							1,0,1 };
//		
//		auto tcentroid = [=](CDT::Face_handle f) -> Point_2 {
//					return CGAL::centroid(ioMesh.triangle(f));
//				};
//
//		auto mark_tri = [=](float r, float g, float b, CDT::Face_handle f){
//			Point2 p0 = cgal2ben(f->vertex(0)->point());
//			Point2 p1 = cgal2ben(f->vertex(1)->point());
//			Point2 p2 = cgal2ben(f->vertex(2)->point());
//			Point2 pc = cgal2ben(tcentroid(f));
//
//			debug_mesh_line(p0,p1,r,g,b,r,g,b);
//			debug_mesh_line(p1,p2,r,g,b,r,g,b);
//			debug_mesh_line(p2,p0,r,g,b,r,g,b);
//			debug_mesh_line(p0,pc,r,g,b,r,g,b);
//			debug_mesh_line(p1,pc,r,g,b,r,g,b);
//			debug_mesh_line(p2,pc,r,g,b,r,g,b);
//
//		};
//
//		float * c = colors;
//		float * colors_stop = colors + 18;

		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
			tri->info().flag = false;

		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
		if(tri->info().flag == false)
		{
			int lu = tri->info().terrain;
			if(IsAirportTerrain(lu))
			{
				std::set<CDT::Face_handle> tris;
				map<int, double>	area_to_lu;
				double best_a = -1.0f;
				int best_lu = NO_VALUE;
	
				vector<CDT::Face_handle>	todo;
				todo.reserve(10000);
				
				todo.push_back(tri);
				tri->info().flag = true;
				
				while(!todo.empty())
				{
					CDT::Face_handle p = todo.front();
					todo.erase(todo.begin());
					tris.insert(p);
					for(int i = 0; i < 3; ++i)
					{
						CDT::Face_handle n = p->neighbor(i);
						if(n->info().flag == false)
						if(IsAirportTerrain(n->info().terrain))
						{
							n->info().flag = true;
							todo.push_back(n);
						}
					}
				}
				
				for(auto& t : tris)
				{
					auto nt = ioMesh.triangle(t);
					auto n_area = nt.area();
					double aa = CGAL::to_double(n_area);
					
					int alu = t->info().terrain;
					double new_max = area_to_lu[alu] += aa;
					if(new_max > best_a)
					{
						best_a = new_max;
						best_lu = alu;
					}
				}
				//DebugAssert(best_lu != NO_VALUE);
				for(auto& t : tris)
				{
					DebugAssert(!ioMesh.is_infinite(t));
					t->info().terrain = best_lu;
					//mark_tri(c[0],c[1],c[2],t);
				}
			
				//c += 3;
				//if(c == colors_stop) c = colors;
			}
		}
		Assert(!IsAirportTerrain(ioMesh.infinite_face()->info().terrain));
			
	}

	/***********************************************************************************************
	 * TRY TO CONSOLIDATE BLOBS
	 ***********************************************************************************************/
	// If a blob's total area is less than the blobbing distance, it's not really needed!  Simplify
	// it.

/*
	int tri_merged = 0;
	std::set<CDT::Face_handle>	all_variants;

	for (CDT::Finite_faces_iterator f = ioMesh.finite_faces_begin(); f != ioMesh.finite_faces_end(); ++f)
	if (f->info().terrain != terrain_Water)
	if (HasVariant(f->info().terrain))
		all_variants.insert(f);

	float max_rat = gMeshPrefs.rep_switch_m * MTR_TO_NM * NM_TO_DEG_LAT;

	while (!all_variants.empty())
	{
		CDT::Face_handle		w = *all_variants.begin();
		int						base = SpecificVariant(w->info().terrain,0);
		std::set<CDT::Face_handle>	tri_set;
		Bbox_2					bounds;
		FindAllCovariant(ioMesh, w, tri_set, bounds);

		bool devary = (bounds.ymax() - bounds.ymin() < max_rat) && (bounds.xmax() - bounds.xmin()) < max_rat;

		for (std::set<CDT::Face_handle>::iterator kill = tri_set.begin(); kill != tri_set.end(); ++kill)
		{
			if (devary)
			{
				(*kill)->info().terrain = base;
				++tri_merged;
			}
			all_variants.erase(*kill);
		}
	}
*/

	/***********************************************************************************************
	 * DEAL WITH INTRUSION FROM OUR MASTER SIDE
	 ***********************************************************************************************/

	// BEN SEZ - IS THIS COMMENT TRUE?
	// ??? This must be POST optmize - we can go OUT OF ORDER on the borders because must have left-master/right-slave.
	// ??? So the optmizer will NUKE this stuff. :-(

	// First build a correlation between our border info and some real tris in the mesh.
	int b;
	for(b=0;b<4;++b)
	if (!gMatchBorders[b].vertices.empty())
		border_find_edge_tris(ioMesh, gMatchBorders[b]);
	int lowest;
	int n;
#if !NO_BORDER_SHARING

	std::set<CDT::Vertex_handle>	vertices;
	// Now we have to "rebase" our edges.  Basically it is possible that we are getting intruded from the left
	// by a lower priority texture.  If we just use borders, that low prio tex will end up UNDER our base, and we'll
	// never see it.  So we need to take the tex on our right side and reduce it.
	for(b=0;b < 4; ++b)
	{
		for (n = 0; n < gMatchBorders[b].edges.size(); ++n)
               if(!IsCustom(gMatchBorders[b].edges[n].base))
 		if (gMatchBorders[b].edges[n].buddy != CDT::Face_handle())
		{
			lowest = gMatchBorders[b].edges[n].buddy->info().terrain;
			if (LowerPriorityNaturalTerrain(gMatchBorders[b].edges[n].base, lowest))
				lowest = gMatchBorders[b].edges[n].base;
			for (std::set<int>::iterator bl = gMatchBorders[b].edges[n].borders.begin(); bl != gMatchBorders[b].edges[n].borders.end(); ++bl)
			if(!IsCustom(*bl))
			{
				if (LowerPriorityNaturalTerrain(*bl, lowest))
					lowest = *bl;
			}

			if (lowest != gMatchBorders[b].edges[n].buddy->info().terrain)
				RebaseTriangle(ioMesh, gMatchBorders[b].edges[n].buddy, lowest, gMatchBorders[b].vertices[n].buddy, gMatchBorders[b].vertices[n+1].buddy, vertices);
		}

		for (n = 0; n < gMatchBorders[b].vertices.size(); ++n)
		{
			CDT::Face_circulator circ, stop;
			circ = stop = ioMesh.incident_faces(gMatchBorders[b].vertices[n].buddy);
			do {
				if (!ioMesh.is_infinite(circ))
				if (!is_border(ioMesh, circ))
				{
					lowest = circ->info().terrain;
					if(!IsCustom(lowest))					
					for (hash_map<int, float>::iterator bl = gMatchBorders[b].vertices[n].blending.begin(); bl != gMatchBorders[b].vertices[n].blending.end(); ++bl)
					if(!IsCustom(bl->first))
					if (bl->second > 0.0)
					if (LowerPriorityNaturalTerrain(bl->first, lowest))
						lowest = bl->first;

					if (lowest != circ->info().terrain)
						RebaseTriangle(ioMesh, circ, lowest, gMatchBorders[b].vertices[n].buddy, CDT::Vertex_handle(), vertices);
				}
				++circ;
			} while (circ != stop);
		}
	}

	// These vertices were given partial borders by rebasing - go in and make sure that all incident triangles match them.
	for (std::set<CDT::Vertex_handle>::iterator rebased_vert = vertices.begin(); rebased_vert != vertices.end(); ++rebased_vert)
	{
		CDT::Face_circulator circ, stop;
		circ = stop = ioMesh.incident_faces(*rebased_vert);
		do {
			if (!ioMesh.is_infinite(circ))
			for (hash_map<int, float>::iterator bl = (*rebased_vert)->info().border_blend.begin(); bl != (*rebased_vert)->info().border_blend.end(); ++bl)
			if (bl->second > 0.0)
				AddZeroMixIfNeeded(circ, bl->first);
			++circ;
		} while (circ != stop);
	}

#endif

	/***********************************************************************************************
	 * CALCULATE BORDERS
	 ***********************************************************************************************/

	if (inProg) inProg(0, 1, "Assigning Landuses", 0.5);

#if !NO_BORDERS_AT_ALL

	/* 	Here's the idea:
		We are going to go through each triangle, which now has a land use, and figure ouet which
		ones have borders.  A triangle that has a border will get:
		(1) the land use of the border triangle in its std::set of "border landuses", so it
		 	can easily be identified in that mesh, and
		(2) for each of its vertices, a hash map entry with the alpha level for the border at that
			point, so we can figure out how  the border fades.

		To do this we say: for each triangle, we do a "spreading" type algorithm, e.g. we collect
		non-visited neighbors that meet our criteria in a std::set and go outward.  We only take neighbors
		that have a lower natural land use and haven't been visited.  We calc our distance to the
		corners to get the blend, and if we're not all faded out, keep going.
	*/

	int		visited = 0;	// flag value - by using a rolling flag, we don't have to reset
							// this all of the time.
	int		tri_total = 0, tri_border = 0, tri_check = 0, tri_opt = 0;
	for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
	if (tri->info().terrain != terrain_Water)
	{
		++visited;
		std::set<CDT::Face_handle>	to_visit;
		to_visit.insert(tri);
		bool					spread;
		int						layer = tri->info().terrain;
		tri->info().flag = visited;

		while (!to_visit.empty())
		{
			CDT::Face_handle	border = *to_visit.begin();
			to_visit.erase(border);
			spread = false;
			if (&*border != &*tri)
			{
				// Calculation phase - figure out alphas of
				// the corners.
				CDT::Vertex_handle v1 = border->vertex(0);
				CDT::Vertex_handle v2 = border->vertex(1);
				CDT::Vertex_handle v3 = border->vertex(2);
				double	dist1 = DistPtToTri(v1, tri);
				double	dist2 = DistPtToTri(v2, tri);
				double	dist3 = DistPtToTri(v3, tri);
				double	dist_max = GetXonDist(layer, border->info().terrain, border->info().normal[2]);

				if (dist_max > 0.0)
				{
					dist1 = max(0.0, min((dist_max-dist1)/dist_max,1.0));
					dist2 = max(0.0, min((dist_max-dist2)/dist_max,1.0));
					dist3 = max(0.0, min((dist_max-dist3)/dist_max,1.0));

					++tri_check;
					if (dist1 > 0.0 || dist2 > 0.0 || dist3 > 0.0)
					{
						double	odist1 = v1->info().border_blend[layer];
						double	odist2 = v2->info().border_blend[layer];
						double	odist3 = v3->info().border_blend[layer];

						// Border propagation - we only want to std::set the levels of this border if we are are adjacent to ourselves..this way we don't std::set the far-side distance
						// unless there will be another border tri to continue with.

						bool has_0 = false, has_1 = false, has_2 = false;
						if (border->neighbor(0)->info().terrain_border.count(layer) || border->neighbor(0)->info().terrain == layer) { has_1 = true; has_2 = true; }
						if (border->neighbor(1)->info().terrain_border.count(layer) || border->neighbor(1)->info().terrain == layer) { has_2 = true; has_0 = true; }
						if (border->neighbor(2)->info().terrain_border.count(layer) || border->neighbor(2)->info().terrain == layer) { has_0 = true; has_1 = true; }

						// BUT...if we're at the edge of the file, go across anyway, what the hell...
						// Ben sez: no- try to limit cross-border madness or we get projection mismatches.
//						if (!has_0 && IsEdgeVertex(ioMesh, v1))	has_0 = true;
//						if (!has_1 && IsEdgeVertex(ioMesh, v2))	has_1 = true;
//						if (!has_2 && IsEdgeVertex(ioMesh, v3))	has_2 = true;

						if (!has_0) dist1 = 0.0;
						if (!has_1) dist2 = 0.0;
						if (!has_2) dist3 = 0.0;

						// If we're not faded out totally, record an increase.  ONLY keep
						// searching if we are increasing one of the vertices.  Otherwise
						// someone else has been over this territory who is already closer
						// and we're just wasting our time.
						if (dist1 > odist1) { spread = true; v1->info().border_blend[layer] = dist1; }
						if (dist2 > odist2) { spread = true; v2->info().border_blend[layer] = dist2; }
						if (dist3 > odist3) { spread = true; v3->info().border_blend[layer] = dist3; }

						// HACK - does always extending the borders fix a bug?
						DebugAssert(layer != -1);
						DebugAssert(!IsCustom(border->info().terrain));
						border->info().terrain_border.insert(layer);
						spread = true;
					}
				}
			} else
				spread = true;

			border->info().flag = visited;

			// Spreading case: check our neighbors to make sure we haven't seen them and it makes
			// sense to check them.
			if (spread)
			{
				CDT::Face_handle b1 = border->neighbor(0);
				CDT::Face_handle b2 = border->neighbor(1);
				CDT::Face_handle b3 = border->neighbor(2);

				if (b1->info().flag != visited && !ioMesh.is_infinite(b1) && b1->info().terrain != terrain_Water && !border->info().get_edge_feature(0) && LowerPriorityNaturalTerrain(b1->info().terrain, layer))	to_visit.insert(b1);
				if (b2->info().flag != visited && !ioMesh.is_infinite(b2) && b2->info().terrain != terrain_Water && !border->info().get_edge_feature(1) && LowerPriorityNaturalTerrain(b2->info().terrain, layer))	to_visit.insert(b2);
				if (b3->info().flag != visited && !ioMesh.is_infinite(b3) && b3->info().terrain != terrain_Water && !border->info().get_edge_feature(2) && LowerPriorityNaturalTerrain(b3->info().terrain, layer))	to_visit.insert(b3);
			}
		}
	}

	/***********************************************************************************************
	 * DEAL WITH INTRUSION FROM OUR MASTER SIDE
	 ***********************************************************************************************/
#if !NO_BORDER_SHARING
	// First - force border blend of zero at the slaved edge, no matter how ridiculous.  We can't possibly propagate
	// this border into a previously rendered file, so a hard stop is better than a cutoff.
	for(b=0;b<4;++b)
	for (n = 0; n < gMatchBorders[b].vertices.size(); ++n)
	for (hash_map<int, float>::iterator blev = gMatchBorders[b].vertices[n].buddy->info().border_blend.begin(); blev != gMatchBorders[b].vertices[n].buddy->info().border_blend.end(); ++blev)
		blev->second = 0.0;

	// Now we are going to go in and add borders on our slave edges from junk coming in on the left.  We have ALREADY
	// "rebased" the terrain.  This means that the border on the slave side is guaranteed to be lower priority than the border
	// on the master, so that we can make this border-extension safely.  For the base and borders on the master we just add
	// a border on the slave - the edge blend levels are the master's blend and the interior poiont gets a blend of 0 or whatever
	// was already there.

	for(b=0;b<4;++b)
	for (n = 0; n < gMatchBorders[b].edges.size(); ++n)
	if (gMatchBorders[b].edges[n].buddy != CDT::Face_handle())
	if (gMatchBorders[b].edges[n].buddy->info().terrain != terrain_Water)
	if(!IsCustom(gMatchBorders[b].edges[n].buddy->info().terrain))
	{
		// Handle the base terrain
		if (gMatchBorders[b].edges[n].buddy->info().terrain != gMatchBorders[b].edges[n].base)
		if(!IsCustom(gMatchBorders[b].edges[n].base))
		{
			AddZeroMixIfNeeded(gMatchBorders[b].edges[n].buddy, gMatchBorders[b].edges[n].base);
			gMatchBorders[b].vertices[n].buddy->info().border_blend[gMatchBorders[b].edges[n].base] = 1.0;
			SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n].buddy, gMatchBorders[b].edges[n].base);
			gMatchBorders[b].vertices[n+1].buddy->info().border_blend[gMatchBorders[b].edges[n].base] = 1.0;
			SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n+1].buddy, gMatchBorders[b].edges[n].base);
		}

		// Handle any overlay layers...
		for (std::set<int>::iterator bl = gMatchBorders[b].edges[n].borders.begin(); bl != gMatchBorders[b].edges[n].borders.end(); ++bl)
		if(!IsCustom(*bl))
		{
			if (gMatchBorders[b].edges[n].buddy->info().terrain != *bl)
			{
				AddZeroMixIfNeeded(gMatchBorders[b].edges[n].buddy, *bl);
				gMatchBorders[b].vertices[n].buddy->info().border_blend[*bl] = gMatchBorders[b].vertices[n].blending[*bl];
				SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n].buddy, *bl);
				gMatchBorders[b].vertices[n+1].buddy->info().border_blend[*bl] = gMatchBorders[b].vertices[n+1].blending[*bl];
				SafeSmearBorder(ioMesh, gMatchBorders[b].vertices[n+1].buddy, *bl);
			}
		}
	}

#endif

	/***********************************************************************************************
	 * OPTIMIZE BORDERS!
	 ***********************************************************************************************/
	if (inProg) inProg(0, 1, "Assigning Landuses", 0.75);

	if (gMeshPrefs.optimize_borders)
	{
		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
		if (tri->info().terrain != terrain_Water)
		{
			bool need_optimize = false;
			for (std::set<int>::iterator blayer = tri->info().terrain_border.begin();
				blayer != tri->info().terrain_border.end(); ++blayer)
			{
				if (tri->vertex(0)->info().border_blend[*blayer] == 1.0 &&
					tri->vertex(1)->info().border_blend[*blayer] == 1.0 &&
					tri->vertex(2)->info().border_blend[*blayer] == 1.0)
				{
					if (LowerPriorityNaturalTerrain(tri->info().terrain, *blayer))
					{
						tri->info().terrain = *blayer;
						need_optimize = true;
//						debug_mesh_point(cgal2ben(CGAL::centroid(ioMesh.triangle(tri))),1,0,1);
					}
				}
			}
			if (need_optimize)
			{
				std::set<int>	nuke;
				for (std::set<int>::iterator blayer = tri->info().terrain_border.begin();
					blayer != tri->info().terrain_border.end(); ++blayer)
				{
					if (!LowerPriorityNaturalTerrain(tri->info().terrain, *blayer))
						nuke.insert(*blayer);
				}
				for (std::set<int>::iterator nlayer = nuke.begin(); nlayer != nuke.end(); ++nlayer)
				{
					tri->info().terrain_border.erase(*nlayer);
					// DO NOT eliminate these - maybe our neighbor is using them!!
//					tri->vertex(0)->info().border_blend.erase(*nlayer);
//					tri->vertex(1)->info().border_blend.erase(*nlayer);
//					tri->vertex(2)->info().border_blend.erase(*nlayer);
					++tri_opt;
				}
			}
		}
	}

	{
		for (tri = ioMesh.finite_faces_begin(); tri != ioMesh.finite_faces_end(); ++tri)
		if (tri->info().terrain != terrain_Water)
		{
			tri_total++;
			tri_border += (tri->info().terrain_border.size());
		} else if (!tri->info().terrain_border.empty())
			AssertPrintf("BORDER ON WATER LAND USE!  Terrain = %s", FetchTokenString(tri->info().terrain));
		printf("Total: %d - border: %d - check: %d - opt: %d\n", tri_total, tri_border, tri_check, tri_opt);
	}

#endif /* NO_BORDERS_AT_ALL */

	/***********************************************************************************************
	 * WRITE OUT MESH
	 ***********************************************************************************************/

	// We need to write out an edge file for our next guy in line.

	if (gMeshPrefs.border_match)
	{
		double	west = inElevation.mWest;
		double	east = inElevation.mEast;
		double	south = inElevation.mSouth;
		double	north = inElevation.mNorth;
		char	fname[512];

		string border_loc = mesh_folder;

		make_cache_file_path(border_loc.c_str(),west, south,"border",fname);

		FILE * border = fopen(fname, "w");
		if (border == NULL) AssertPrintf("Unable to open file %s for writing.", fname);

		CDT::Point cur,stop;
		for(int b = 0; b < 4; ++b)
		{
			switch(b) {
			case 0:	cur = CDT::Point(west,south);	stop = CDT::Point(west,north);	break;
			case 1:	cur = CDT::Point(west,south);	stop = CDT::Point(east,south);	break;
			case 2:	cur = CDT::Point(east,south);	stop = CDT::Point(east,north);	break;
			case 3:	cur = CDT::Point(west,north);	stop = CDT::Point(east,north);	break;
			}

			CDT::Face_handle	f;
			int					i;
			CDT::Locate_type	lt;
			f = ioMesh.locate(cur, lt, i);
			Assert(lt == CDT::VERTEX);

			CDT::Face_circulator circ, circstop;

			do {
				fprintf(border, "VT %.12lf, %.12lf, %lf\n",
					CGAL::to_double(f->vertex(i)->point().x()),
					CGAL::to_double(f->vertex(i)->point().y()),
					CGAL::to_double(f->vertex(i)->info().height));

				hash_map<int, float>	borders;
				for (hash_map<int, float>::iterator hfi = f->vertex(i)->info().border_blend.begin(); hfi != f->vertex(i)->info().border_blend.end(); ++hfi)
				if (hfi->second > 0.0)
					borders[hfi->first] = max(borders[hfi->first], hfi->second);
				circ = circstop = ioMesh.incident_faces(f->vertex(i));
				do {
					if (!ioMesh.is_infinite(circ))
					{
						borders[circ->info().terrain] = 1.0;
					}
					++circ;
				} while (circ != circstop);

				fprintf(border, "VBC %llu\n", (unsigned long long)borders.size());
				for (hash_map<int, float>::iterator hfi = borders.begin(); hfi != borders.end(); ++hfi)
					fprintf(border, "VB %f %s\n", hfi->second, FetchTokenString(hfi->first));

				if(b == 1 || b == 3)				FindNextEast(ioMesh, f, i, b==1);
				else								FindNextNorth(ioMesh, f, i, b==2);
				DebugAssert(!ioMesh.is_infinite(f));

				fprintf(border, "TERRAIN %s\n", FetchTokenString(f->info().terrain));
				fprintf(border, "BORDER_C %llu\n", (unsigned long long)f->info().terrain_border.size());
				for (std::set<int>::iterator si = f->info().terrain_border.begin(); si != f->info().terrain_border.end(); ++si)
					fprintf(border, "BORDER_T %s\n", FetchTokenString(*si));

			} while (f->vertex(i)->point() != stop);

			fprintf(border, "VC %.12lf, %.12lf, %lf\n",
					CGAL::to_double(f->vertex(i)->point().x()),
					CGAL::to_double(f->vertex(i)->point().y()),
					CGAL::to_double(f->vertex(i)->info().height));
			fprintf(border, "VBC %llu\n", (unsigned long long)f->vertex(i)->info().border_blend.size());
			for (hash_map<int, float>::iterator hfi = f->vertex(i)->info().border_blend.begin(); hfi != f->vertex(i)->info().border_blend.end(); ++hfi)
				fprintf(border, "VB %f %s\n", hfi->second, FetchTokenString(hfi->first));
		}

		fprintf(border, "END\n");
		fclose(border);

	}

	if (inProg) inProg(0, 1, "Assigning Landuses", 1.0);

}


#pragma mark -
/*******************************************************************************************
 *	UTILITY ROUTINES
 *******************************************************************************************/
void SetupWaterRasterizer(const Pmwx& map, const DEMGeo& orig, PolyRasterizer<double>& rasterizer, int terrain_wanted)
{
	for (Pmwx::Edge_const_iterator i = map.edges_begin(); i != map.edges_end(); ++i)
	{
		bool	iWet = i->face()->data().mTerrainType == terrain_wanted && !i->face()->is_unbounded();
		bool	oWet = i->twin()->face()->data().mTerrainType == terrain_wanted && !i->twin()->face()->is_unbounded();

		if (iWet != oWet)
		{
			double xx1 = CGAL::to_double(i->source()->point().x());
			double yy1 = CGAL::to_double(i->source()->point().y());
			double xx2 = CGAL::to_double(i->target()->point().x());
			double yy2 = CGAL::to_double(i->target()->point().y());

			// This is a kludgy-AF bug fix: for some @#$@ reason, if we have an airport that ends on the north border,
			// CGAL _nails_ the horizontal line and we don't get rasterization on that top edge, which results in missing
			// nodes compared to the airport on the tile above us.
			//
			// So.... cheat and push the f---ing line 1 CM out of bounds.  The distortion to the line won't be
			// enough to care.
			//
			// Weirdly KPHX doesn't seem to show this bug on the east edge?  Otherwise we could push XX1 and XX2.
			if (yy1 == orig.mNorth)
				yy1 += 0.0000001;
			if (yy2 == orig.mNorth)
				yy2 += 0.0000001;

			double x1 = orig.lon_to_x(xx1);
			double y1 = orig.lat_to_y(yy1);
			double x2 = orig.lon_to_x(xx2);
			double y2 = orig.lat_to_y(yy2);

//				gMeshLines.push_back(i->source()->point());
//				gMeshLines.push_back(i->target()->point());

//				fprintf(fi,"%lf,%lf    %lf,%lf   %s\n", x1,y1,x2,y2, ((y1 == 0.0 || y2 == 0.0) && y1 != y2) ? "****" : "");

			rasterizer.AddEdge(x1,y1,x2,y2);
		}
	}
	rasterizer.SortMasters();
}

void	CreateWaterSDF(const Pmwx& inMap, DEMGeo& ioDem)
{
	ioDem = 0.0;
	PolyRasterizer<double>	rasterizer;
	SetupWaterRasterizer(inMap, ioDem, rasterizer, terrain_Water);

	int y = 0;
	rasterizer.StartScanline(y);
	while (!rasterizer.DoneScan())
	{
		int x1, x2;
		while (rasterizer.GetRange(x1, x2))
		{
			for(int x = x1; x < x2; ++x)
			{
				ioDem.set(x,y,FLT_MAX);
			}
		}
		// Yeah we could be more clever about modulus in the Y axis, but..the rasterizer might
		// be unhappy skipping scanlines with "events" on them.
		++y;
		if (y >= ioDem.mHeight)
			break;
		rasterizer.AdvanceScanline(y);
	}

	//https://mshgrid.com/2021/02/04/the-fast-sweeping-algorithm/
	int width = ioDem.mWidth;
	int height = ioDem.mHeight;

	const int NSweeps = 4;
    // sweep directions { start, end, step }
    const int dirX[NSweeps][3] = { {0, width - 1, 1} , {width - 1, 0, -1}, {width - 1, 0, -1}, {0, width - 1, 1} };
    const int dirY[NSweeps][3] = { {0, height - 1, 1}, {0, height - 1, 1}, {height - 1, 0, -1}, {height - 1, 0, -1} };

	float aa[2];
    double d_new, a, b;
    int s, ix, iy;
    const double h = 1.0, f = 1.0;
	
    for (int s = 0; s < NSweeps; s++)
    {
        for (int iy = dirY[s][0]; dirY[s][2] * iy <= dirY[s][1]; iy += dirY[s][2])
        {
            for (int ix = dirX[s][0]; dirX[s][2] * ix <= dirX[s][1]; ix += dirX[s][2])
            {
				float v = ioDem.get(ix, iy);
				if(v > 0.0)
				{
					if (iy == 0)
						aa[1] = min(v,ioDem.get(ix,iy+1));
					else if (iy == (height - 1))
						aa[1] = min(v, ioDem.get(ix,iy-1));
					else
						aa[1] = min(ioDem.get(ix,iy-1),ioDem.get(ix,iy+1));
				 
					if (ix == 0)
						aa[0] = min(v,ioDem.get(ix+1,iy));
					else if (ix == (width - 1))
						aa[0] = min(v,ioDem.get(ix-1,iy));
					else
						aa[0] = min(ioDem.get(ix-1,iy), ioDem.get(ix+1,iy));
 
					a = aa[0]; b = aa[1];
					d_new = (fabs(a - b) < f * h ? (a + b + sqrt(2.0 * f * f * h * h - (a - b) * (a - b))) * 0.5 : std::fminf(a, b) + f * h);

					if(d_new < v)
						ioDem.set(ix,iy,d_new);
                }
            }
        }
    }
}


void	Calc2ndDerivative(DEMGeo& deriv)
{
	int x, y;
	float 	h, ha, hr, hb, hl;
	for (y = 0; y < (deriv.mHeight-1); ++y)
	for (x = 0; x < (deriv.mWidth-1); ++x)
	{
		h  = deriv(x,y);
		ha = deriv(x,y+1);
		hr = deriv(x+1,y);

		if (h == DEM_NO_DATA || ha == DEM_NO_DATA || hr == DEM_NO_DATA)
			deriv(x,y) = DEM_NO_DATA;
		else
			deriv(x,y) = (ha - h) + (hr - h);
	}

	for (y = (deriv.mHeight-2); y >= 1; --y)
	for (x = (deriv.mWidth-2);  x >= 1; --x)
	{
		h  = deriv(x,y);
		hb = deriv(x,y-1);
		hl = deriv(x-1,y);

		if (h == DEM_NO_DATA || hb == DEM_NO_DATA || hl == DEM_NO_DATA)
			deriv(x,y) = DEM_NO_DATA;
		else
			deriv(x,y) = (h - hl) + (h - hb);
	}

	for (x = 0; x < deriv.mWidth; ++x)
	{
		deriv(x, 0) = DEM_NO_DATA;
		deriv(x, deriv.mHeight-1) = DEM_NO_DATA;
	}
	for (x = y; y < deriv.mHeight; ++y)
	{
		deriv(0, y) = DEM_NO_DATA;
		deriv(deriv.mWidth-1, y) = DEM_NO_DATA;
	}
}

double	HeightWithinTri(CDT& inMesh, CDT::Face_handle f, CDT::Point in)
{
	Assert(!inMesh.is_infinite(f));

	double	DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(in.y()) * DEG_TO_RAD);

	Point_3	p1((f->vertex(0)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(0)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(0)->info().height));

	Point_3	p2((f->vertex(1)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(1)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(1)->info().height));

	Point_3	p3((f->vertex(2)->point().x() * (DEG_TO_NM_LON * NM_TO_MTR)),
			   (f->vertex(2)->point().y() * (DEG_TO_NM_LAT * NM_TO_MTR)),
			   (f->vertex(2)->info().height));

	Vector_3	s1(p2, p3);
	Vector_3	s2(p2, p1);
	Vector_3	n = cross_product(s1, s2);
	//Plane_3	plane = Plane_3(p1, n);
	//plane.n = n;
	//plane.ndotp = n * Vector_3(p1);
	double r = CGAL::to_double(p1.z() - ((n.x() * (in.x() * (DEG_TO_NM_LON * NM_TO_MTR) - p1.x()) + (n.y() * (in.y() * (DEG_TO_NM_LAT * NM_TO_MTR) - p1.y()))) / n.z()));

	//Plane_3 plane = Plane_3(p2, p1, p3);
	//double r = CGAL::to_double(plane.to_3d(Point_2(in.x() * (DEG_TO_NM_LON * NM_TO_MTR), in.y() * (DEG_TO_NM_LAT * NM_TO_MTR))).z());

	//printf("%lf, %lf, %lf. ", CGAL::to_double(in.x()), CGAL::to_double(in.y()), r);
	return r;

}


double	MeshHeightAtPoint(CDT& inMesh, double inLon, double inLat, int hint_id)
{
	if (inMesh.number_of_faces() < 1) return DEM_NO_DATA;
	CDT::Face_handle	f = CDT::Face_handle();
	int	n;
	CDT::Locate_type lt;
	f = inMesh.locate_cache(CDT::Point(inLon, inLat), lt, n, hint_id);
	if (lt == CDT::VERTEX)
	{
		return f->vertex(n)->info().height;
	}
	if (lt == CDT::EDGE && inMesh.is_infinite(f))
	{
		f = f->neighbor(n);
	}

	if (!inMesh.is_infinite(f))
	{
		return HeightWithinTri(inMesh, f, CDT::Point(inLon, inLat));
	} else {
		printf("Requested point was off mesh: %lf, %lf\n", inLon, inLat);
		return DEM_NO_DATA;
	}
}

int	CalcMeshError(CDT& mesh, DEMGeo& elev, float& out_min, float& out_max, float& out_ave, float& std_dev, ProgressFunc inFunc)
{
	if (inFunc) inFunc(0, 1, "Calculating Error", 0.0);
	int ctr = 0;
	
	out_max = 0.0;
	out_ave = 0.0;
	std_dev = 0.0;
	out_min = 9.9e9;
	
	CDT::Face_handle	last_tri;
	Plane3				last_plane;
	Point2				last_tri_loc[3];
	double				DEG_TO_NM_LON = DEG_TO_NM_LAT;

	float				worst_pos = 0.0;
	float				worst_neg = 0.0;
	Point2				worst_pos_p;
	Point2				worst_neg_p;
	
	if(mesh.number_of_faces() >= 1)
	for (int y = 0; y < elev.mHeight; ++y)
	{
		if (inFunc && (y % 20) == 0) inFunc(0, 1, "Calculating Error", (float) y / (float) elev.mHeight);

		for (int x = 0; x < elev.mWidth ; ++x)
		{
			float ideal = elev.get(x,y);
			if (ideal != DEM_NO_DATA)
			{
				Point2	ll(elev.x_to_lon(x), elev.y_to_lat(y));
				if(last_tri == CDT::Face_handle() ||
				   Segment2(last_tri_loc[0],last_tri_loc[1]).on_right_side(ll) ||
				   Segment2(last_tri_loc[1],last_tri_loc[2]).on_right_side(ll) ||
				   Segment2(last_tri_loc[2],last_tri_loc[0]).on_right_side(ll))
				{

					CDT::Face_handle	f = CDT::Face_handle();
					int	n;
					CDT::Locate_type lt;
					f = mesh.locate(CDT::Point(ll.x(), ll.y()), lt, n, last_tri);
					if (lt == CDT::EDGE && mesh.is_infinite(f))
					{
						f = f->neighbor(n);
					}
					
					if(!mesh.is_infinite(f))
					{
						last_tri = f;

						last_tri_loc[0] = cgal2ben(f->vertex(0)->point());
						last_tri_loc[1] = cgal2ben(f->vertex(1)->point());
						last_tri_loc[2] = cgal2ben(f->vertex(2)->point());
						

						DEG_TO_NM_LON = DEG_TO_NM_LAT * cos(CGAL::to_double(ll.y()) * DEG_TO_RAD);

						Point3	p1((last_tri_loc[0].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[0].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(0)->info().height));

						Point3	p2((last_tri_loc[1].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[1].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(1)->info().height));

						Point3	p3((last_tri_loc[2].x()),// * (DEG_TO_NM_LON * NM_TO_MTR)),
								   (last_tri_loc[2].y()),// * (DEG_TO_NM_LAT * NM_TO_MTR)),
								   (last_tri->vertex(2)->info().height));

						Vector3	s1(p2, p3);
						Vector3	s2(p2, p1);
						Vector3	n = s1.cross(s2);
						n.normalize();
						last_plane = Plane3(p1,n);
					}
				}
				
				if(last_tri != CDT::Face_handle())
				{
					double real = (last_plane.n.dx * ll.x() +
								   last_plane.n.dy * ll.y() -
								   last_plane.ndotp) / -last_plane.n.dz;
					
					double	close = last_plane.distance_denormaled(Point3(ll.x(),ll.y(),ideal));
					float derr = real - ideal;
					derr = close;

					Point2	me(elev.x_to_lon(x), elev.y_to_lat(y));
					if(derr > worst_pos)
					{
						worst_pos = derr;
						worst_pos_p = me;
					}
					if(derr < worst_neg)
					{
						worst_neg = derr;
						worst_neg_p = me;
					}
					
					out_min = min(out_min,derr);
					out_max = max(out_max,derr);
					out_ave += derr;
					std_dev += (derr*derr);
					++ctr;
				}
			}
		}
	}
	if(worst_pos > 0.0)
	{	
//		debug_mesh_point(worst_pos_p,1,0,0);
		printf("Worst positive error is %f meters at %+08.6lf, %+09.7lf\n", worst_pos, worst_pos_p.x(), worst_pos_p.y());
	}	
	if(worst_neg < 0.0)
	{
		printf("Worst negative error is %f meters at %+08.6lf, %+09.7lf\n", worst_neg, worst_neg_p.x(), worst_neg_p.y());	
//		debug_mesh_point(worst_neg_p,1,0,1);
	}
	
	if(ctr > 0)
	{
		out_ave /= (float) ctr;
		std_dev = sqrt(std_dev / float(ctr));
	}
	
	if (inFunc) inFunc(0, 1, "Calculating Error", 1.0);
	return ctr;
}

int	CalcMeshTextures(CDT& inMesh, map<int, int>& out_lus)
{
	out_lus.clear();
	int total = 0;
	for(CDT::Face_iterator  f = inMesh.finite_faces_begin(); f != inMesh.finite_faces_end(); ++f)
	{
		out_lus[f->info().terrain]++;
		for(std::set<int>::iterator b =  f->info().terrain_border.begin();
							   b != f->info().terrain_border.end(); ++b)
			out_lus[*b]++;
		total += (1 + f->info().terrain_border.size());
	}
	return total;
}

/****************************************************************************************************************************************************************
 *
 ****************************************************************************************************************************************************************/
#pragma mark -

Pmwx::Halfedge_handle	mesh_to_pmwx_he(CDT& ioMesh, CDT::Edge& e)
{
	// Figure out our source vertex, which must be a sync point.  If it isn't already one, walk backward
	// via our twin...we either hit a sync point or an unsync-Y (in which case we're f---ed and bail.)
	CDT::Vertex_handle source(CDT_he_source(e));
	Vertex_handle orig_source = source->info().orig_vertex;
	if(orig_source == Vertex_handle())
	{
		CDT::Edge t = CDT_he_twin(e);
		while(t.first != CDT::Face_handle() && CDT_he_target(t)->info().orig_vertex == Vertex_handle())
			t = CDT_next_constraint(t);
			
		source = CDT_he_target(t);
		orig_source = source->info().orig_vertex;
		if (orig_source == Vertex_handle()) 
			return Halfedge_handle();
		e = CDT_he_twin(t);
	}
		
	CDT::Vertex_handle target(CDT_he_target(e));

	// This is a mess...since the relationship between the CDT and Pmwx is many-to-many,
	// the only way to help the face resolver not freak out is to tell it the nearest WRONG WAY
	// vertices - those are paths going NOT along our edge.  So...circulate, and for all other
	// constrained edges, walk the constraint until we find a sync point, and save that in
	// 'stop here' markers.
	std::set<Vertex_handle> wrong_ways;
	CDT::Vertex_circulator circ, stop;
	circ = stop = source->incident_vertices();
	do 
	{
		if(circ != target)
		{
			CDT::Edge e;
			if(ioMesh.is_edge(source,circ,e.first,e.second))
			if(ioMesh.is_constrained(e))
			{
				if (CDT_he_source(e) != source)
					e = CDT_he_twin(e);
				while(CDT_he_target(e)->info().orig_vertex == Vertex_handle())
				{
					e = CDT_next_constraint(e);
					DebugAssert(e.first != CDT::Face_handle());
				}
				DebugAssert(CDT_he_target(e)->info().orig_vertex != Vertex_handle());
				wrong_ways.insert(CDT_he_target(e)->info().orig_vertex);
			}
		}
	} while (++circ != stop);
	
	Vertex_handle orig_target = target->info().orig_vertex;
	while(orig_target == Vertex_handle())
	{
		e = CDT_next_constraint(e);
		DebugAssert(e.first != CDT::Face_handle());
		target = CDT_he_target(e);
		orig_target = target->info().orig_vertex;
	}

	return halfedge_for_vertices<Pmwx,must_burn_he>(orig_source,orig_target, wrong_ways);
}	
