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

#include "WED_PreviewLayer.h"

#include "ILibrarian.h"
#include "AptDefs.h"
#include "CompGeomUtils.h"
#include "GISUtils.h"
#include "MathUtils.h"
#include "MatrixUtils.h"
#include "TexUtils.h"
#include "ObjDraw.h"
#include "XObjDefs.h"
#include "XESConstants.h"

#include "GUI_Resources.h"
#include "GUI_GraphState.h"
#include "GUI_DrawUtils.h"

#include "WED_ResourceMgr.h"
#include "WED_LibraryMgr.h"
#include "WED_TexMgr.h"
#include "WED_UIDefs.h"
#include "WED_DrawUtils.h"
#include "WED_EnumSystem.h"
#include "WED_MapZoomerNew.h"
#include "WED_ToolUtils.h"
#include "WED_Sign_Editor.h"


#include "WED_AirportBeacon.h"
#include "WED_AirportChain.h"
#include "WED_AirportNode.h"
#include "WED_AirportSign.h"
#include "WED_ForestPlacement.h"
#include "WED_FacadePlacement.h"
#include "WED_DrapedOrthophoto.h"
#include "WED_Runway.h"
#include "WED_Sealane.h"
#include "WED_Helipad.h"
#include "WED_LinePlacement.h"
#include "WED_ObjPlacement.h"
#include "WED_RoadEdge.h"
#include "WED_PolygonPlacement.h"
#include "WED_StringPlacement.h"
#include "WED_AutogenPlacement.h"
#include "WED_Taxiway.h"
#include "WED_TruckParkingLocation.h"
#include "WED_LightFixture.h"
#include "WED_Windsock.h"

#if APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

/***************************************************************************************************************************************************
 * MISC DRAWING UTILS
 ***************************************************************************************************************************************************/

inline void setup_transformation(double heading, double scale_s, double scale_t, const Point2& origin, WED_MapZoomerNew * z)
{
		GLdouble	m1[16] = { 1.0,		0.0,	0.0,	 0.0,
								0.0,	1.0,	0.0,	 0.0,
								0.0,	0.0,	1.0,	 0.0,
								0.0,	0.0,	0.0,	 1.0 };

		double ppm = z->GetPPM();

//		for (int n = 0; n < 16; ++n)
//			m1[n] /= ppm;
		m1[0] /= ppm * scale_s;
		m1[5] /= ppm * scale_t;
		applyRotation(m1, heading, 0, 0, 1);

		applyTranslation(m1, -origin.x_, -origin.y_ , 0);

		double	proj_tex_s[4], proj_tex_t[4];
		proj_tex_s[0] = m1[0 ];
		proj_tex_s[1] = m1[4 ];
		proj_tex_s[2] = m1[8 ];
		proj_tex_s[3] = m1[12];
		proj_tex_t[0] = m1[1 ];
		proj_tex_t[1] = m1[5 ];
		proj_tex_t[2] = m1[9 ];
		proj_tex_t[3] = m1[13];

		glEnable(GL_TEXTURE_GEN_S);	glTexGeni(GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);	glTexGendv(GL_S,GL_OBJECT_PLANE,proj_tex_s);
		glEnable(GL_TEXTURE_GEN_T);	glTexGeni(GL_T,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);	glTexGendv(GL_T,GL_OBJECT_PLANE,proj_tex_t);
}

static void kill_transform(void)
{
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
}

static Point2 some_nearby_fixed_loc(WED_MapZoomerNew * z)
{
	Point2 pt = z->PixelToLL(Point2());  // some arbitrary point near the visible map
	pt.x_ = round(pt.x());               // This makes the point 'essentially' fixed when zooming/panning around in the 3D view
	pt.y_ = round(pt.y());               // but at the same time is close enough so the 32b floats on the GPU give accurate UV coordinates
	return z->LLToPixel(pt);
}

static bool setup_taxi_texture(int surface_code, double heading, const Point2& centroid, GUI_GraphState * g, WED_MapZoomerNew * z, float alpha)
{
	int tex_id = 0;
	switch(surface_code)
	{
		case shoulder_Asphalt:
		case surf_Asphalt:	tex_id = GUI_GetTextureResource("asphalt.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case shoulder_Concrete:
		case surf_Concrete:	tex_id = GUI_GetTextureResource("concrete.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);break;
		case surf_Grass:	tex_id = GUI_GetTextureResource("grass.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Dirt:		tex_id = GUI_GetTextureResource("dirt.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Gravel:	tex_id = GUI_GetTextureResource("gravel.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Lake:		tex_id = GUI_GetTextureResource("lake.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Water:	tex_id = GUI_GetTextureResource("water.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Snow:		tex_id = GUI_GetTextureResource("snow.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Trans:
		case shoulder_None:
		default: return false;
	}
	if (!tex_id)
	{
		g->SetState(false,0,false,true,true,false,false);
		glColor4f(0.5,0.5,0.5,alpha);
		return true;
	}
	else
	{
		g->SetState(false,1,false,true,true,false,false);
		glColor4f(1,1,1,alpha);
		g->BindTex(tex_id,0);
		setup_transformation(heading, 6.25, 6.25, centroid, z);
		return true;
	}
}

static bool setup_pol_texture(ITexMgr * tman, const pol_info_t& pol, double heading, bool no_proj, const Point2& centroid, GUI_GraphState * g,
							WED_MapZoomerNew * z, float alpha, bool isAbsPath = true)
{
	TexRef	ref = tman->LookupTexture(pol.base_tex.c_str(),true, pol.wrap ? (tex_Compress_Ok|tex_Wrap|tex_Always_Pad) : tex_Compress_Ok|tex_Always_Pad);
	if(ref == NULL) return false;
	int tex_id = tman->GetTexID(ref);

	if (tex_id)
	{
		g->SetState(false,1,false,!pol.kill_alpha,!pol.kill_alpha,false,false);
		glColor4f(1,1,1,alpha);
		g->BindTex(tex_id,0);

		if(no_proj)
		{
			glDisable(GL_TEXTURE_GEN_S);
			glDisable(GL_TEXTURE_GEN_T);
		}
		else
			setup_transformation(heading, pol.proj_s, pol.proj_t, centroid, z);
	}
	else
	{
		g->SetState(false,0,false,true,true,false,false);
		glColor4f(0.5,0.5,0.5,alpha);
		return true;
	}

	return true;
}

static bool setup_taxi_texture(int surface_code, double heading, const Point2& centroid, GUI_GraphState * g, WED_MapZoomerNew * z, float alpha,
		IResolver * resolver)
{
	if (surface_code >= shoulder_Asphalt_1 && surface_code <= shoulder_Concrete_8)
		surface_code -= shoulder_Asphalt_1 - surf_Asphalt_1;

	if(surface_code != surf_Trans && surface_code != shoulder_None)
	{
		WED_LibraryMgr * lmgr = WED_GetLibraryMgr(resolver);
		string resource;
		if (lmgr->GetSurfVpath(surface_code, resource))
		{
			WED_ResourceMgr* rmgr = WED_GetResourceMgr(resolver);
				ITexMgr* tman = WED_GetTexMgr(resolver);
				const pol_info_t* pol_info;

				if (rmgr->GetPol(resource, pol_info))
					if (setup_pol_texture(tman, *pol_info, heading, false, centroid, g, z, alpha))
						return true;
		}
	}

	if (surface_code < surf_Concrete_1) surface_code = surf_Asphalt;
	else if (surface_code < surf_Grass) surface_code = surf_Concrete;
	int tex_id = 0;

	switch(surface_code)
	{
		case surf_Asphalt:	tex_id = GUI_GetTextureResource("asphalt.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Concrete:	tex_id = GUI_GetTextureResource("concrete.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);break;
		case surf_Grass:	tex_id = GUI_GetTextureResource("grass.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Dirt:		tex_id = GUI_GetTextureResource("dirt.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Gravel:	tex_id = GUI_GetTextureResource("gravel.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Lake:		tex_id = GUI_GetTextureResource("lake.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Water:	tex_id = GUI_GetTextureResource("water.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Snow:		tex_id = GUI_GetTextureResource("snow.png",tex_Wrap+tex_Linear+tex_Mipmap,NULL);	break;
		case surf_Trans:
		default: return false;
	}
	if (!tex_id)
	{
		g->SetState(false,0,false,true,true,false,false);
		glColor4f(0.5,0.5,0.5,alpha);
		return true;
	}
	else
	{
		g->SetState(false,1,false,true,true,false,false);
		glColor4f(1,1,1,alpha);
		g->BindTex(tex_id,0);
		setup_transformation(heading, 6.25, 6.25, centroid, z);
		return true;
	}
}

struct	Obj_DrawStruct {
	GUI_GraphState *	g;
	int					tex;
	int					drp;
};

void Obj_SetupPoly(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->SetTexUnits(1);
	glColor3f(1,1,1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}
void Obj_SetupLine(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->SetTexUnits(0);
	glColor3f(1,1,1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}
void Obj_SetupLight(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->SetTexUnits(0);
	glColor3f(1,1,1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}
void Obj_SetupMovie(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->SetTexUnits(0);
	glColor3f(1,1,1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}
void Obj_SetupPanel(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->SetTexUnits(0);
	glColor3f(1,1,1);
}

void Obj_TexCoord(const float * st, void * ref)
{
	glTexCoord2fv(st);
}

void Obj_TexCoordPointer(int size, unsigned long type, long stride, const void * pointer, void * ref)
{
	glTexCoordPointer(size, type, stride, pointer);
}

float Obj_GetAnimParam(const char * string, float v1, float v2, void * ref)
{
	return v1;
}

void Obj_SetDraped(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->BindTex(d->drp,0);
}

void Obj_SetNoDraped(void * ref)
{
	Obj_DrawStruct * d= (Obj_DrawStruct*) ref;
	d->g->BindTex(d->tex,0);
}

static ObjDrawFuncs10_t kFuncs  = { Obj_SetupPoly, Obj_SetupLine, Obj_SetupLight, Obj_SetupMovie, Obj_SetupPanel, Obj_TexCoord, Obj_TexCoordPointer, Obj_GetAnimParam, Obj_SetDraped, Obj_SetNoDraped };

void draw_obj_at_ll(ITexMgr * tman, const XObj8 * o, const Point2& loc, float agl, float r, GUI_GraphState * g, WED_MapZoomerNew * zoomer, 
					float(*anim_cb)(const char * string, float v1, float v2, void * ref) = Obj_GetAnimParam)
{
	if (!o) return;

	ObjDrawFuncs10_t draw_funcs = { Obj_SetupPoly, Obj_SetupLine, Obj_SetupLight, Obj_SetupMovie, Obj_SetupPanel, Obj_TexCoord,
								Obj_TexCoordPointer, anim_cb, Obj_SetDraped, Obj_SetNoDraped };

	TexRef	ref = tman->LookupTexture(o->texture.c_str() ,true, tex_Wrap|tex_Compress_Ok|tex_Always_Pad);
	TexRef	ref2 = o->texture_draped.empty() ? ref : tman->LookupTexture(o->texture_draped.c_str() ,true, tex_Wrap|tex_Compress_Ok|tex_Always_Pad);
	int id1 = ref  ? tman->GetTexID(ref ) : 0;
	int id2 = ref2 ? tman->GetTexID(ref2) : 0;
	g->SetTexUnits(1);
	if(id1)g->BindTex(id1,0);
	Point2 l = zoomer->LLToPixel(loc);
	float ppm = zoomer->GetPPM();

	glMatrixMode(GL_MODELVIEW);
	zoomer->PushMatrix();
	zoomer->Translatef(l.x(), l.y(), agl * ppm);
	zoomer->Scalef(ppm,ppm,ppm);
	zoomer->Rotatef(90, 1,0,0);
	zoomer->Rotatef(r, 0,-1,0);
	Obj_DrawStruct ds = { g, id1, id2 };
	ObjDraw8(*o, 0, &draw_funcs, &ds);
	zoomer->PopMatrix();
}

void draw_obj_at_xyz(ITexMgr * tman, const XObj8 * o, double x, double y, double z, float heading, GUI_GraphState * g)
{
	if (!o) return;
	TexRef	ref = tman->LookupTexture(o->texture.c_str() ,true, tex_Wrap|tex_Compress_Ok|tex_Always_Pad);
	TexRef	ref2 = o->texture_draped.empty() ? ref : tman->LookupTexture(o->texture_draped.c_str() ,true, tex_Wrap|tex_Compress_Ok|tex_Always_Pad);
	int id1 = ref  ? tman->GetTexID(ref ) : 0;
	int id2 = ref2 ? tman->GetTexID(ref2) : 0;
	g->SetTexUnits(1);
	if(id1)g->BindTex(id1,0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glTranslatef(x,y,z);
	glRotatef(heading, 0, -1, 0);
	Obj_DrawStruct ds = { g, id1, id2 };
	ObjDraw8(*o, 0, &kFuncs, &ds);
	glPopMatrix();
}

void draw_agp_at_xyz(ITexMgr * tman, const agp_t * agp, double x, double y, double z, float height, float heading, GUI_GraphState * g, int tile_idx)
{
	if (!agp) return;

	TexRef	ref = tman->LookupTexture(agp->base_tex.c_str(), true, tex_Linear | tex_Mipmap | tex_Compress_Ok | tex_Always_Pad);
	int id1 = ref ? tman->GetTexID(ref) : 0;
	if (id1) g->BindTex(id1, 0);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glTranslatef(x, y, z);
	glRotatef(heading, 0, -1, 0);
	glColor3f(1, 1, 1);
	auto ti = agp->tiles[tile_idx];
	if (!ti.tile.empty() && !agp->hide_tiles)
	{
		glDisable(GL_CULL_FACE);
		glBegin(GL_TRIANGLE_FAN);
		for (int n = 0; n < ti.tile.size(); n += 4)
		{
			glTexCoord2f(ti.tile[n + 2], ti.tile[n + 3]);
			glVertex3f(ti.tile[n], 0, -ti.tile[n + 1]);
		}
		glEnd();
		glEnable(GL_CULL_FACE);
	}
	for (auto& o : ti.objs)
		if (o.scp_step > 0.0)
		{
			if (height > o.scp_min)
				height = min(o.scp_max - o.scp_min, floor((height - o.scp_min) / o.scp_step) * o.scp_step);
			else
				height = 0.0;
			draw_obj_at_xyz(tman, o.obj, o.x, height, -o.y, o.r, g);
		}
		else
			draw_obj_at_xyz(tman, o.obj, o.x, o.z, -o.y, o.r, g);

	for (auto& f : ti.facs)
		draw_facade(tman, nullptr, f.name, *(f.fac), f.locs, f.walls, f.height, g, true);
	glPopMatrix();
}

void draw_agp_at_ll(ITexMgr * tman, const agp_t * agp, const Point2& loc, float height, float heading, GUI_GraphState * g, WED_MapZoomerNew * zoomer, int preview_level)
{
	if (!agp) return;
	Point2 pix = zoomer->LLToPixel(loc);
	float ppm = zoomer->GetPPM();

	TexRef	ref = tman->LookupTexture(agp->base_tex.c_str(), true, tex_Linear | tex_Mipmap | tex_Compress_Ok | tex_Always_Pad);
	int id1 = ref ? tman->GetTexID(ref) : 0;
	if (id1) g->BindTex(id1, 0);

	glMatrixMode(GL_MODELVIEW);
	zoomer->PushMatrix();
	zoomer->Translatef(pix.x(), pix.y(), 0);
	zoomer->Scalef(ppm, ppm, ppm);
	zoomer->Rotatef(90, 1, 0, 0);
	zoomer->Rotatef(heading, 0, -1, 0);
	glColor3f(1, 1, 1);
	auto ti = agp->tiles.front();
	if (!ti.tile.empty() && !agp->hide_tiles)
	{
		glDisable(GL_CULL_FACE);
		glBegin(GL_TRIANGLE_FAN);
		for (int n = 0; n < ti.tile.size(); n += 4)
		{
			glTexCoord2f(ti.tile[n + 2], ti.tile[n + 3]);
			glVertex3f(ti.tile[n], 0, -ti.tile[n + 1]);
		}
		glEnd();
		glEnable(GL_CULL_FACE);
	}
	for (auto& o : ti.objs)
	{
		if ((o.show_lo + o.show_hi) / 2 <= preview_level)
		if (ppm * max(o.obj->xyz_max[0] - o.obj->xyz_min[0], o.obj->xyz_max[2] - o.obj->xyz_min[2]) > MIN_PIXELS_PREVIEW)
		{
			if (o.scp_step > 0.0)
			{
				if (height > o.scp_min)
					height = min(o.scp_max - o.scp_min, floor((height - o.scp_min) / o.scp_step) * o.scp_step);
				else
					height = 0.0;
				draw_obj_at_xyz(tman, o.obj, o.x, height, -o.y, o.r, g);
			}
			else
				draw_obj_at_xyz(tman, o.obj, o.x, o.z, -o.y, o.r, g);
		}
	}
	for (auto& f : ti.facs)
		draw_facade(tman, nullptr, f.name, *(f.fac), f.locs, f.walls, f.height, g, true, ppm);
	zoomer->PopMatrix();
}

// Given a group name and an offset, this comes up with the total layer number...

const struct { const char * name; int group_lo;  int group_hi; }	kGroupNames[] = {
	"terrain",		group_Terrain,			group_Terrain,
	"beaches",		group_Beaches,			group_Beaches,
	"unpaved_taxiways",	group_UnpavedTaxiwaysBegin,		group_UnpavedTaxiwaysEnd,
	"unpaved_runways",	group_UnpavedTaxiwaysBegin,		group_UnpavedTaxiwaysEnd,
	"shoulders",	group_ShouldersBegin,	group_ShouldersEnd,
	"taxiways",		group_TaxiwaysBegin,	group_TaxiwaysEnd,
	"runways",		group_RunwaysBegin,		group_RunwaysEnd,
	"markings",		group_Markings,			group_Markings,
	"airports",		group_AirportsBegin,	group_AirportsEnd,
	"footprints",	group_Footprints,		group_Footprints,
	"roads",		group_Roads,			group_Roads,
	"objects",		group_Objects,			group_Objects,
	"light_objects",group_LightObjects,		group_LightObjects,
	NULL,			0,						0
};


int layer_group_for_string(const char * s, int o, int def)
{
	int n = 0;
	while(kGroupNames[n].name)
	{
		if(strcasecmp(s,kGroupNames[n].name) == 0)
			return (o < 0) ? (kGroupNames[n].group_lo + o) : (kGroupNames[n].group_hi + o);
		++n;
	}
	return def;
}

/***************************************************************************************************************************************************
 * DRAW ITEMS FOR SORT
 ***************************************************************************************************************************************************/


struct sort_item_by_layer {	bool operator()(WED_PreviewItem * lhs, WED_PreviewItem * rhs) const { return lhs->get_layer() < rhs->get_layer(); } };


static double PixelSize(const WED_GISPolygon * poly, double featureSizeMeters, const WED_MapZoomerNew * zoomer)
{
	Bbox2 bb;
	poly->GetBounds(gis_Geo, bb);
	return zoomer->PixelSize(bb, featureSizeMeters);
}

static double PixelSize(const WED_GISChain * chain, double featureSizeMeters, const WED_MapZoomerNew * zoomer)
{
	Bbox2 bb;
	chain->GetBounds(gis_Geo, bb);
	return zoomer->PixelSize(bb, featureSizeMeters);
}

static double PixelSize(const WED_GISEdge * edge, double featureSizeMeters, const WED_MapZoomerNew * zoomer)
{
	Bbox2 bb;
	edge->GetBounds(gis_Geo, bb);
	return zoomer->PixelSize(bb, featureSizeMeters);
}

static double PixelSize(const WED_GISPoint * point, double diameterMeters, const WED_MapZoomerNew * zoomer)
{
	Point2 ll;
	point->GetLocation(gis_Geo, ll);
	return zoomer->PixelSize(ll, diameterMeters);
}

struct	preview_runway : public WED_PreviewItem {
	WED_Runway * rwy;
	int			 do_shoulders;
	IResolver * res;
	preview_runway(WED_Runway * r, int l, int is_shoulders, IResolver * re) : WED_PreviewItem(l), rwy(r), do_shoulders(is_shoulders), res(re) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		Point2 	corners[4], shoulders[8], blas1[4], blas2[4];
		bool	has_shoulders, has_blas1, has_blas2;

		// First, transform our geometry.
						rwy->GetCorners(gis_Geo,corners);			zoomer->LLToPixelv(corners, corners, 4);
		if (has_blas1 = rwy->GetCornersBlas1(blas1))				zoomer->LLToPixelv(blas1, blas1, 4);
		if (has_blas2 = rwy->GetCornersBlas2(blas2))				zoomer->LLToPixelv(blas2, blas2, 4);
		if (has_shoulders = rwy->GetCornersShoulders(shoulders))	zoomer->LLToPixelv(shoulders, shoulders, 8);

		if (mPavementAlpha > 0.0f)
		{
			// "Solid" geometry.
			if(!do_shoulders)
			if (setup_taxi_texture(rwy->GetSurface(),rwy->GetHeading(), zoomer->LLToPixel(rwy->GetCenter()), g,zoomer, mPavementAlpha, res))
			{
											glShape2v(GL_QUADS, corners, 4);
				if (has_blas1)				glShape2v(GL_QUADS, blas1,4);
				if (has_blas2)				glShape2v(GL_QUADS, blas2,4);
			}
			if(do_shoulders)
			if (setup_taxi_texture(rwy->GetShoulder(),rwy->GetHeading(), zoomer->LLToPixel(rwy->GetCenter()), g,zoomer, mPavementAlpha, res))
			{
				if (has_shoulders)			glShape2v(GL_QUADS, shoulders, 8);
			}
			kill_transform();
			g->SetState(false,0,false, true,true, false,false);
		}
		double z = zoomer->GetPPM();
		if (z > 0.2)                     // draw some well know sign and light positions
		{
			AptRunway_t info;
			rwy->Export(info);
			if(info.has_distance_remaining)
			{
				glColor3ub(25,25,25);
				for(int dir = 0 ; dir <= 1; dir++)
				{
					Point2 lpos = corners[2*dir];
					Vector2 direction(corners[2*dir], corners[1+2*dir]);
					double rwy_len = direction.normalize();
					direction *= z * 1000*FT_TO_MTR;
					Vector2 offset = direction.perpendicular_ccw() * 15.0/1000;
					Point2 rpos = corners[3-2*dir] - offset;
					lpos += offset;
					int num_signs = rwy_len / (z * 1000*FT_TO_MTR);

					double sign_hdg = RAD_TO_DEG * atan2(direction.x(),direction.y());
					for(int n = 0; n < num_signs; n++)
					{
						lpos += direction;
						rpos += direction;
						GUI_PlotIcon(g,"map_taxisign.png",lpos.x(),lpos.y(),sign_hdg, max(0.4, z * 0.05));
						GUI_PlotIcon(g,"map_taxisign.png",rpos.x(),rpos.y(),sign_hdg, max(0.4, z * 0.05));
					}
				}
			}
			for(int dir = 0 ; dir <= 1; dir++)
				if(info.app_light_code[dir])
				{
					glColor4ub(255,255,255,128);

					double spacing = 200*FT_TO_MTR;
					double length = 1400*FT_TO_MTR;
					if(info.app_light_code[dir] == apt_app_ALSFI || info.app_light_code[dir] == apt_app_ALSFII ||
						info.app_light_code[dir] == apt_app_MALSR || info.app_light_code[dir] == apt_app_SSALR)
					{
						length = 2400*FT_TO_MTR;
						if(info.app_light_code[dir] == apt_app_ALSFI || info.app_light_code[dir] == apt_app_ALSFII)
							spacing = 100*FT_TO_MTR;
					}
					Point2 rwy_end = Segment2(corners[3-2*dir],corners[2*dir]).midpoint(0.5);  // runway end position
					Vector2 rwy_dir(corners[1+2*dir], corners[2*dir]);
					rwy_dir.normalize();
					Point2 lpos = rwy_end - rwy_dir * z * info.disp_mtr[dir];           // appr lights start position is at threshold
					Vector2 rbar_dir = rwy_dir.perpendicular_ccw();
					rbar_dir *= z * 8.0;   						                             // 8.0m spacing of roll bar lights
					Vector2 vec_lgts = rwy_dir * z * spacing;
					int num_lgts = length / spacing;
					double sign_hdg = RAD_TO_DEG * atan2(rwy_dir.x(),rwy_dir.y());

					if(info.app_light_code[dir] <= apt_app_MALS)    // 1000' roll bar
					{
						Vector2 dir2(rwy_dir);
						dir2 *= z * 1000*FT_TO_MTR;
						Point2 rollbar = lpos + dir2;

						rollbar -= rbar_dir * 2;
						for(int n = 0; n < 5; n++)
						{
							if(n != 2)
								GUI_PlotIcon(g,"map_light.png",rollbar.x(),rollbar.y(),sign_hdg, max(0.3, z * 0.05));
							rollbar += rbar_dir;
						}
					}
					for(int n = 0; n < num_lgts; n++)
					{
						lpos += vec_lgts;
						GUI_PlotIcon(g,"map_light.png",lpos.x(),lpos.y(),sign_hdg, max(0.3, z * 0.05));
					}
				}
			g->SetState(false,0,false, true,true, false,false);
			
			if(gExportTarget >= wet_xplane_1200)
				for(int dir = 0 ; dir <= 1; dir++)
					if(info.skid_len[dir] > 0.0 && info.skids[dir] > 0.0)
					{
						Point2 skids[4];
						Vector2 direction(corners[0], corners[1]);
						Vector2 width(corners[1], corners[2]);
						width *= 0.25;
						
						double skid_ends[2];
						skid_ends[dir] = 0.1;
						skid_ends[1-dir] = 0.5 + 0.3 * (1.0 - doblim(info.skid_len[dir],0,1));
					
						skids[0] = corners[0] + width + direction * skid_ends[0];
						skids[1] = corners[1] + width - direction * skid_ends[1];
						skids[2] = corners[2] - width - direction * skid_ends[1];
						skids[3] = corners[3] - width + direction * skid_ends[0];
						
						glColor4f(0,0,0,0.1);
						glShape2v(GL_QUADS, skids, 4);
					}	
		}
	}
};

struct	preview_helipad : public WED_PreviewItem {
	WED_Helipad * heli;
	IResolver * res;
	preview_helipad(WED_Helipad * h, int l, IResolver * r) : WED_PreviewItem(l), heli(h), res(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		if (mPavementAlpha > 0.0f)
		{
			Point2 corners[4];
			heli->GetCorners(gis_Geo, corners);
			zoomer->LLToPixelv(corners, corners, 4);
			setup_taxi_texture(heli->GetSurface(), heli->GetHeading(), corners[0], g, zoomer, mPavementAlpha, res);
			glShape2v(GL_QUADS, corners, 4);
			kill_transform();

		}
	}
};

struct	preview_sealane : public WED_PreviewItem {
	WED_Sealane * sea;
	preview_sealane(WED_Sealane * s, int l) : WED_PreviewItem(l), sea(s) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		Point2 corners[4];
		sea->GetCorners(gis_Geo,corners);
		zoomer->LLToPixelv(corners, corners, 4);

		if (mPavementAlpha > 0.0f)
		{
			GLfloat storage[4];
			g->SetState(false,0,false, true,true, false,false);
			glColor4fv(WED_Color_RGBA_Alpha(wed_Surface_Water,mPavementAlpha, storage));
			glShape2v(GL_QUADS, corners, 4);
		}

	}
};


struct	preview_polygon : public WED_PreviewItem {
	WED_GISPolygon * pol;
 	bool has_uv;
	preview_polygon(WED_GISPolygon * p, int l, bool uv) : WED_PreviewItem(l), pol(p), has_uv(uv) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		vector<Point2>	pts;
		vector<int>		hole_starts;

		PointSequenceToVector(pol->GetOuterRing(), zoomer, pts, has_uv);
		int n = pol->GetNumHoles();
		for (int i = 0; i < n; ++i)
		{
			hole_starts.push_back(pts.size());
			PointSequenceToVector(pol->GetNthHole(i), zoomer, pts, has_uv);
		}
		if (!pts.empty())
		{
			glFrontFace(GL_CCW);
			glPolygon2(pts, has_uv, hole_starts, false);
			glFrontFace(GL_CW);
		}
	}
};

struct	preview_taxiway : public preview_polygon {
	WED_Taxiway * taxi;
	IResolver * res;
	preview_taxiway(WED_Taxiway * t, int l, IResolver * r) : preview_polygon(t, l, false), taxi(t), res(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		// I tried "LODing" out the solid pavement, but the margin between when the pavement can disappear and when the whole
		// airport can is tiny...most pavement is, while visually insignificant, still sprawling, so a bbox-sizes test is poor.
		// Any other test is too expensive, and for the small pavement squares that would get wiped out, the cost of drawing them
		// is negligable anyway.

		Point2 centroid;
		taxi->GetOuterRing()->GetNthPoint(0)->GetLocation(gis_Geo, centroid);
		centroid = zoomer->LLToPixel(centroid);

		if (setup_taxi_texture(taxi->GetSurface(), taxi->GetHeading(), centroid, g, zoomer, mPavementAlpha, res))
		{
			preview_polygon::preview_polygon::draw_it(zoomer,g,mPavementAlpha);
		}
		kill_transform();
	}
};

struct	preview_forest : public preview_polygon {
	WED_ForestPlacement * fst;
	preview_forest(WED_ForestPlacement * f, int l) : preview_polygon(f,l,false), fst(f) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		g->SetState(false,0,false,false,false,false,false);
		glColor3f(
			interp(0,0.1,1,0.0,fst->GetDensity()),
			interp(0,0.5,1,0.3,fst->GetDensity()),
			interp(0,0.1,1,0.0,fst->GetDensity()));

		if(fst->GetFillMode() == dsf_fill_area)
			preview_polygon::draw_it(zoomer,g,mPavementAlpha);
		else if(fst->GetFillMode() == dsf_fill_line)
		{
			IGISPointSequence * ps = fst->GetOuterRing();
			for(int i = 0; i < ps->GetNumSides(); ++i)
			{
				vector<Point2>	pts;
				SideToPoints(ps,i,zoomer, pts);
				glLineWidth(5);
				glShape2v(GL_LINES/*GL_LINE_STRIP*/, &*pts.begin(), pts.size());
				glLineWidth(1);

			}
		}
	}
};

static void draw_line_preview(const vector<Point2>& pts, const lin_info_t& linfo, int l, double PPM)
{
	double half_width =  (linfo.s2[l]-linfo.s1[l]) / 2.0 * linfo.scale_s * PPM;
	double offset     = ((linfo.s2[l]+linfo.s1[l]) / 2.0 - linfo.sm[l]) * linfo.scale_s * PPM;
	double uv_dt      =  (linfo.s2[l]-linfo.s1[l]) / 2.0 * linfo.scale_s / linfo.scale_t; // correction factor for 'slanted' texture ends
	double uv_t2      = 0.0;                                                              // accumulator for texture t, so each starts where the previous ended
	bool is_ring = pts.front() == pts.back();

	double startcap_t = 0.0;
	double endcap_t = 0.0;
	int start_of_endcap = pts.size();
	double endcap_frac_t = 0.0;

	if(!is_ring)
	{
		if(linfo.start_caps.size() > l)
			startcap_t = linfo.start_caps[l].t2 - linfo.start_caps[l].t1;

		if(linfo.end_caps.size() > l)
		{
			endcap_t = linfo.end_caps[l].t2 - linfo.end_caps[l].t1;
			start_of_endcap = pts.size()-2;
			bool once = true;
			while(start_of_endcap > 0 || once)
			{
				once = false;
				double prev_t = endcap_frac_t;
				endcap_frac_t += sqrt(Segment2(pts[start_of_endcap], pts[start_of_endcap+1]).squared_length()) / PPM / linfo.scale_t;
				if(endcap_frac_t > endcap_t)
				{
					endcap_frac_t = endcap_t - prev_t;
					break;
				}
				start_of_endcap--;
			}
		}
	}

	Vector2	dir2(pts[1],pts[0]);
	dir2.normalize();
	if(is_ring)
	{
		Vector2 dir_last(pts[0],pts[pts.size()-2]);
		dir_last.normalize();
		dir2 = (dir2 + dir_last) / (1.0 + dir_last.dot(dir2));
	}
	dir2 = dir2.perpendicular_ccw();   // direction perpendicular to previous segment


	for (int j = 0; j < pts.size()-1; ++j)
	{
		Vector2	dir1(dir2);
		Vector2 dir = Vector2(pts[j+1],pts[j]);
		double len = dir.normalize();        // direction of this segment
		if(j < pts.size()-2+is_ring)
		{
			int n = j < pts.size()-2 ? j+2 : 1;
			Vector2 dir3(pts[n],pts[j+1]);
			dir3.normalize();
			dir2 = (dir + dir3) / (1.0 + dir.dot(dir3));
		}
		else dir2 = dir;
		dir2 = dir2.perpendicular_ccw();

		double uv_t1(uv_t2);
		uv_t2 += len / PPM / linfo.scale_t;
		double d1 = uv_dt * dir.dot(dir1);
		double d2 = uv_dt * dir.dot(dir2);

		Point2 start_left (pts[j]   + dir1 * (offset - half_width));
		Point2 start_right(pts[j]   + dir1 * (offset + half_width));
		Point2 end_left   (pts[j+1] + dir2 * (offset - half_width));
		Point2 end_right  (pts[j+1] + dir2 * (offset + half_width));

		if(startcap_t > 0.0)
		{
			double cap_len_t = linfo.start_caps[l].t2 - linfo.start_caps[l].t1;
			double t = min(startcap_t, uv_t2 - uv_t1);
			glBegin(GL_QUADS);
				glTexCoord2f(linfo.start_caps[l].s1,linfo.start_caps[l].t2-startcap_t); glVertex2(start_left);
				glTexCoord2f(linfo.start_caps[l].s2,linfo.start_caps[l].t2-startcap_t); glVertex2(start_right);
				start_left  = Segment2(start_left, end_left).midpoint(t/(uv_t2 - uv_t1));
				start_right = Segment2(start_right, end_right).midpoint(t/(uv_t2 - uv_t1));
				startcap_t -= t;
				glTexCoord2f(linfo.start_caps[l].s2,linfo.start_caps[l].t2-startcap_t); glVertex2(start_right);
				glTexCoord2f(linfo.start_caps[l].s1,linfo.start_caps[l].t2-startcap_t); glVertex2(start_left);
			glEnd();
			if(startcap_t > 0.0) continue;
			uv_t1 = 0.0;
			uv_t2 -= cap_len_t;
		}

		if(j >= start_of_endcap)
		{
			endcap_frac_t = min(endcap_frac_t,uv_t2 - uv_t1);
			glBegin(GL_QUADS);
				glTexCoord2f(linfo.end_caps[l].s2,linfo.end_caps[l].t2-endcap_t + endcap_frac_t); glVertex2(end_right);
				glTexCoord2f(linfo.end_caps[l].s1,linfo.end_caps[l].t2-endcap_t + endcap_frac_t); glVertex2(end_left);
				end_left  = Segment2(end_left, start_left).midpoint(endcap_frac_t/(uv_t2 - uv_t1));
				end_right = Segment2(end_right, start_right).midpoint(endcap_frac_t/(uv_t2 - uv_t1));
				glTexCoord2f(linfo.end_caps[l].s1,linfo.end_caps[l].t2-endcap_t); glVertex2(end_left);
				glTexCoord2f(linfo.end_caps[l].s2,linfo.end_caps[l].t2-endcap_t); glVertex2(end_right);
			glEnd();
			endcap_t -= endcap_frac_t;
			endcap_frac_t = 1.0;           // cram as much endcap as it gets into the next segment
			if(j > start_of_endcap) continue;
		}

		if(j == pts.size()-2 && linfo.align > 0) uv_t2 = round_by_parts(uv_t2 - (linfo.end_caps.size() > l ? linfo.end_caps[l].t2-linfo.end_caps[l].t1 : 0.0), linfo.align);

		glBegin(GL_QUADS);
			glTexCoord2f(linfo.s1[l],uv_t1 + d1); glVertex2(start_left);
			glTexCoord2f(linfo.s2[l],uv_t1 - d1); glVertex2(start_right);
			glTexCoord2f(linfo.s2[l],uv_t2 - d2); glVertex2(end_right);
			glTexCoord2f(linfo.s1[l],uv_t2 + d2); glVertex2(end_left);
		glEnd();

	}
}

struct	preview_line : WED_PreviewItem {
	WED_LinePlacement * lin;
	IResolver * resolver;
	preview_line(WED_LinePlacement * ln, int l, IResolver * r) : WED_PreviewItem(l), lin(ln), resolver(r) {}
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		string vpath;
		const lin_info_t * linfo;
		lin->GetResource(vpath);
		if (!rmgr->GetLin(vpath,linfo)) return;

		ITexMgr *	tman = WED_GetTexMgr(resolver);
		TexRef tref = tman->LookupTexture(linfo->base_tex.c_str(),true,tex_Compress_Ok);
		int tex_id = 0;
		if(tref) tex_id = tman->GetTexID(tref);

		if(!tex_id)
			return;

		g->SetState(false,1,false,true,true,false,false);
		g->BindTex(tex_id,0);
		glColor3f(1,1,1);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		IGISPointSequence * ps = SAFE_CAST(IGISPointSequence,lin);
		if(ps)
		{
			glFrontFace(GL_CCW);
			for (int l = 0; l < linfo->s1.size(); ++l)
			{
				vector<Point2>	pts;
				PointSequenceToVector(ps, zoomer, pts, false, true);
				draw_line_preview(pts, *linfo, l, zoomer->GetPPM());
			}
			glFrontFace(GL_CW);
		}
	}
};

static void draw_string_preview(const vector<Point2>& pts, double& d0, double ds, const str_info_t& sinfo, WED_MapZoomerNew * zoomer,
	GUI_GraphState * g, ITexMgr * tman, const XObj8 * obj)
{
	double ppm = zoomer->GetPPM();

	// strings, like taxiway perimeter lights - can be very big - and lights only get visible when zoomed in very close and are still small.
	// So it almost certain - the vast majority will be _far_ off screen. So lets not care about their size in screenspace , offset etc
	// just cull the ones *very* far off screen.

	double E, W, N, S;
	zoomer->GetPixelBounds(W, S, E, N);
	double tmp = E - W;
	E += tmp; W -= tmp;
	tmp = N - S;
	N += tmp; S -= tmp;

	for (int j = 0; j < pts.size()-1; ++j)
	{
		Vector2 dir = Vector2(pts[j],pts[j+1]);
		double len_m = sqrt(dir.squared_length()) / ppm;

		if (ds-d0 > len_m)
		{
			d0 += len_m;
		}
		else
		{
			double hdg = VectorMeters2NorthHeading(pts[j], pts[j], dir) + sinfo.rotation;
			Vector2 off = dir.perpendicular_cw();
			off.normalize();
			off *= sinfo.offset * ppm;

			double d1 = ds - d0;
			double x;
			double left_after =  modf((len_m - d1) / ds, &x) * ds;
			int obj_this_seg = x;

			Point2 cur_pos(pts[j]);
			if(d0 > 0.0)
				cur_pos += dir * (d1 / len_m);
			else
				obj_this_seg++;

			while(obj_this_seg >= 0)
			{
				if (cur_pos.x() < E && cur_pos.x() > W && cur_pos.y() > S && cur_pos.y() < N)
					draw_obj_at_ll(tman, obj, zoomer->PixelToLL(cur_pos+off), 0.0, hdg, g, zoomer);
				cur_pos += dir * (ds / len_m);
				obj_this_seg--;
			}
			d0 = left_after;
		}
	}
}

struct	preview_string : WED_PreviewItem {
	WED_StringPlacement * str;
	IResolver * resolver;
	preview_string(WED_StringPlacement * st, int l, IResolver * r) : WED_PreviewItem(l), str(st), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		string vpath;
		const str_info_t * sinfo;
		str->GetResource(vpath);
		if (!rmgr->GetStr(vpath,sinfo)) return;

		IGISPointSequence * ps = SAFE_CAST(IGISPointSequence,str);
		if(ps && sinfo->objs.size())
		{
			const XObj8 * o;
			if(rmgr->GetObjRelative(sinfo->objs.front(),vpath,o))
			{
				float real_radius=pythag(
						o->xyz_max[0]- o->xyz_min[0],
						o->xyz_max[2]- o->xyz_min[2]);

				if(PixelSize(str, real_radius, zoomer) > MIN_PIXELS_PREVIEW)             // cutoff size for real preview
				{
					ITexMgr * tman = WED_GetTexMgr(resolver);
					g->SetState(false, 1, false, true, true, true, true);
					glColor3f(1,1,1);

					double ds = str->GetSpacing();
					double d0 = ds * 0.5;

					for(int i = 0; i < ps->GetNumSides(); ++i)
					{
						vector<Point2>	pts;
						SideToPoints(ps, i, zoomer, pts);
						draw_string_preview(pts, d0, ds, *sinfo, zoomer, g, tman, o);
					}
				}
			}
		}
	}
};


struct	preview_airportlines : WED_PreviewItem {
	IGISPointSequence * ps;
	IResolver * res;

	preview_airportlines(IGISPointSequence * ips, int l, IResolver * r) : WED_PreviewItem(l), ps(ips), res(r) {}
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
//		if(zoomer->GetPPM() * 0.3 < MIN_PIXELS_PREVIEW) return;      // cutoff size for real preview, average line width is 0.3m
//		IGISPointSequence * ps = SAFE_CAST(IGISPointSequence,chn);
//		if(!ps) return;

		glFrontFace(GL_CCW);
		int i = 0;
		while (i < ps->GetNumSides())
		{
			set<int> attrs;
			WED_AirportNode * apt_node = dynamic_cast<WED_AirportNode*>(ps->GetNthPoint(i));
			if (apt_node) apt_node->GetAttributes(attrs);

			int t = 0;
			for(set<int>::const_iterator a = attrs.begin(); a != attrs.end(); ++a)
			{
				int n = ENUM_Export(*a);
				if(n < 100)
				{
					t = n;
					break;
				}
			}
			string vpath;
			const lin_info_t * linfo = nullptr;
			int tex_id = 0;
			WED_ResourceMgr * rmgr = WED_GetResourceMgr(res);
			ITexMgr         * tman = WED_GetTexMgr(res);
			WED_LibraryMgr  * lmgr = WED_GetLibraryMgr(res);

			if (lmgr->GetLineVpath(t, vpath))
				if (rmgr->GetLin(vpath, linfo))
				{
					TexRef tref = tman->LookupTexture(linfo->base_tex.c_str(),true,tex_Compress_Ok);
					if(tref) tex_id = tman->GetTexID(tref);
				}

			if(tex_id)
			{
				vector<Point2> pts;

				g->SetState(false,1,false,true,true,false,false);
				g->BindTex(tex_id,0);
				glColor3f(1,1,1);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

				for ( ; i < ps->GetNumSides(); ++i)
				{
					if (pts.size()) pts.pop_back();
					SideToPoints(ps, i, zoomer, pts);

					if(i < ps->GetNumSides()-1)
					{
						apt_node = dynamic_cast<WED_AirportNode*>(ps->GetNthPoint(i+1));
						if (apt_node) apt_node->GetAttributes(attrs);
						int tn = 0;
						for(set<int>::const_iterator a = attrs.begin(); a != attrs.end(); ++a)
						{
							int n = ENUM_Export(*a);
							if (n < 100)
							{
								tn = n;
								break;
							}
						}
						if (tn != t) { ++i; break; }           // stop, as next segment will need different line type;
					}
				}

				for (int l = 0; l < linfo->s1.size(); ++l)
				{
					draw_line_preview(pts, *linfo, l, zoomer->GetPPM());
				}
			}
			else
				++i; // in case we cang get the attributes, skip to next node. If we dont, we'll loop indefinitely;
		}
		glFrontFace(GL_CW);
	}
};


struct	preview_airportlights : WED_PreviewItem {
	IGISPointSequence * ps;
	IResolver * res;

	preview_airportlights(IGISPointSequence * ips, int l, IResolver * r) : WED_PreviewItem(l), ps(ips), res(r) {}
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(res);
		WED_LibraryMgr  * lmgr = WED_GetLibraryMgr(res);
		ITexMgr         * tman = WED_GetTexMgr(res);

		int i = 0;
		while (i < ps->GetNumSides())
		{
			set<int> attrs;
			WED_AirportNode * apt_node = dynamic_cast<WED_AirportNode*>(ps->GetNthPoint(i));
			if (apt_node) apt_node->GetAttributes(attrs);

			int t = 0;
			for(set<int>::const_iterator a = attrs.begin(); a != attrs.end(); ++a)
			{
				int n = ENUM_Export(*a);
				if(n > 100 && n < 200)
				{
					t = n;
					break;
				}
			}
			string vpath;
			const str_info_t * sinfo;
			int tex_id = 0;
			if (t && lmgr->GetLineVpath(t, vpath) && rmgr->GetStr(vpath, sinfo))
			{
				vector<Point2> pts;
				double ds = 8.0;                     // default spacing, e.g. taxiline center lights
				if(t == apt_light_taxi_edge || t == apt_light_bounary) ds = 20.0;          // twy edge lights
				if(t == apt_light_hold_short || t == apt_light_hold_short_flash) ds = 2.0;  // hold lights
				double d0 = ds * 0.5;

				g->SetState(false,1,false,true,true,false,false);
				glColor3f(1,1,1);

				for ( ; i < ps->GetNumSides(); ++i)
				{
					if (pts.size()) pts.pop_back();
					SideToPoints(ps, i, zoomer, pts);

					if(i < ps->GetNumSides()-1)
					{
						apt_node = dynamic_cast<WED_AirportNode*>(ps->GetNthPoint(i+1));
						if (apt_node) apt_node->GetAttributes(attrs);
						int tn = 0;
						for(set<int>::const_iterator a = attrs.begin(); a != attrs.end(); ++a)
						{
							int n = ENUM_Export(*a);
							if(n > 100 && n < 200)
							{
								tn = n;
								break;
							}
						}
						if (tn != t) { ++i; break; }           // stop, as next segment will need different line type;
					}
				}
				const XObj8 * obj;
				if(rmgr->GetObjRelative(sinfo->objs.front(), vpath, obj))
					draw_string_preview(pts, d0, ds, *sinfo, zoomer, g, tman, obj);
			}
			else
				++i; // in case we can't get the attributes, skip to next node. If we dont, we'll loop indefinitely;
		}
	}
};


struct	preview_facade : public preview_polygon {
	WED_FacadePlacement * fac;
	IResolver * resolver;
	preview_facade(WED_FacadePlacement * f, int l, IResolver * r) : preview_polygon(f,l,false), fac(f), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		IGISPointSequence * ps = fac->GetOuterRing();
		glColor4f(1,1,1,1);

		g->SetState(false, 0, false, true, true, true, true);

		if(1) // fac->HasCustomWalls())
		{
			WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
			ITexMgr * tman = WED_GetTexMgr(resolver);
			Polygon2 pts;
			vector<int> choices;

			int n = ps->GetNumSides();
			pts.reserve(n);
			choices.reserve(n);

			Bbox2 bounds;
			fac->GetBounds(gis_Geo, bounds);
			CoordTranslator2 tr;
			CreateTranslatorForBounds(bounds, tr);

			string vpath;
			fac->GetResource(vpath);
			const fac_info_t * info;

			if (rmgr->GetFac(vpath, info))
			for(int i = 0; i < n; ++i)
			{
				static Bezier2		b;
				ps->GetSide(gis_Geo, i, b);

				if (i == n-2 && fac->HasDockingCabin())
				{
					auto my_tun = info->tunnels[0];
					Bezier2 bp;
					ps->GetSide(gis_Param, i, bp);

					for(auto& t : info->tunnels)
						if (t.idx == bp.p1.x())
						{
							my_tun = t;
							break;
						}

					static Point2 pt;
					ps->GetNthPoint(i + 2)->GetLocation(gis_Geo, pt);

					static float extension_min, extension_max;
					auto cbk = [](const char* dref, float v1, float v2, void* ref) -> float
					{
						float retval;
						if (strcmp(dref, "sim/graphics/animation/jetways/jw_tunnel_extension") == 0)
						{
							retval = LonLatDistMeters(b.p1, b.p2);
							extension_min = v1;
							extension_max = v2;
						}
						else if (strcmp(dref, "sim/graphics/animation/jetways/jw_cabin_rotation") == 0)
						{
							retval = -VectorDegs2NorthHeading(b.p1, b.p1, Vector2(b.p1, b.p2)) + VectorDegs2NorthHeading(b.p2, b.p2, Vector2(b.p2, pt));
							retval = fltwrap(retval, -180, 180);
						}
						else // if (strcmp(dref, "sim/graphics/animation/jetways/jw_base_rotation") == 0)
							retval = 0.0;
						return fltlim(retval, v1, v2);
					};

					if (my_tun.o)
						draw_obj_at_ll(tman, my_tun.o, b.p1, 0, VectorDegs2NorthHeading(b.p1, b.p1, Vector2(b.p1, b.p2)), g, zoomer, cbk);

					g->SetState(false, 0, false, true, true, false, false);
					glColor4f(1, 0, 0, 0.2);

					Point2	b1 = zoomer->LLToPixel(b.p1);
					Point2  b2 = zoomer->LLToPixel(b.p2);
					Vector2 dir(b1, b2);
					dir.normalize();
					dir *= zoomer->GetPPM();
					b1 += dir.perpendicular_ccw() * 2.5;         // place the 'serviced area' indication about at the cabin baffle location

					glBegin(GL_TRIANGLE_FAN);
						dir.rotate_by_degrees(-15);
						glVertex2(b1 + dir * extension_max);
						glVertex2(b1 + dir * extension_min);
						const int stepsize = 10;
						const int arc_angle = 45 + 15;
						for (int i = 0; i < arc_angle; i += stepsize)
						{
							dir.rotate_by_degrees(stepsize);
							glVertex2(b1 + dir * extension_min);
						}
						glVertex2(b1 + dir * extension_max);
						for (int i = 0; i < arc_angle; i += stepsize)
						{
							dir.rotate_by_degrees(-stepsize);
							glVertex2(b1 + dir * extension_max);
						}
					glEnd();
					g->EnableDepth(true, true);

				}
				if (i > n-2 && fac->HasDockingCabin())
					continue;

//				Vector2 v(VectorLLToMeters(ref_pt, Vector2(ref_pt,b.p1)));
				Point2 v = tr.Forward(b.p1);
				// The facade preview code uses -Z / north facing coordinates, same a the OBJ8's.
				// So we invert the y coordinates here, which will in 3D space be the Z coordinates.

				pts.push_back(Point2(v.x(), -v.y()));

				if(i == n-1 && !ps->IsClosed())
				{
					// we count on LTO to optimize this seriously, to remove all those redundant cos(ref_pt.y) calculations.
//					v = VectorLLToMeters(ref_pt, Vector2(ref_pt,b.p2));
					v = tr.Forward(b.p2);
					pts.push_back(Point2(v.x(), -v.y()));
				}

				if(fac->HasCustomWalls())
				{
					ps->GetSide(gis_Param, i, b);
					choices.push_back(b.p1.x());
				}
				else
					choices.push_back(0);   // we skip the clever geometry dependent wall auto-wall selection that XP does. Sorry.

				if(i == n-1 && !ps->IsClosed())
					choices.push_back(0);
			}

			Bbox2 bb_geo;
			fac->GetBounds(gis_Geo, bb_geo);

			g->SetState(false,0,false,true,true,true,true);

			glMatrixMode(GL_MODELVIEW);
			zoomer->PushMatrix();
			Point2 l = zoomer->LLToPixel(bounds.p1);
			zoomer->Translatef(l.x(),l.y(),0.0);
			float ppm = zoomer->GetPPM();
			zoomer->Scalef(ppm,ppm,ppm);
			zoomer->Rotatef(90, 1,0,0);

			if(rmgr->GetFac(vpath, info))
				draw_facade(tman, rmgr, vpath, *info, pts, choices, fac->GetHeight(), g, true, 0.7 * zoomer->PixelSize(bb_geo, 1.0));
			zoomer->PopMatrix();
		}

	}
};

struct	preview_pol : public preview_polygon {
	WED_PolygonPlacement * pol;
	IResolver * resolver;
	preview_pol(WED_PolygonPlacement * p, int l, IResolver * r) : preview_polygon(p,l,false), pol(p), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		string vpath;
		const pol_info_t * pol_info;

		pol->GetResource(vpath);
		if(rmgr->GetPol(vpath,pol_info))
		{
			setup_pol_texture(tman, *pol_info, pol->GetHeading(), false, some_nearby_fixed_loc(zoomer), g, zoomer, mPavementAlpha);
			preview_polygon::draw_it(zoomer, g, mPavementAlpha);
			kill_transform();
		}
	}
};

struct	preview_autogen: public preview_polygon {
	WED_AutogenPlacement * ags;
	IResolver * resolver;
	preview_autogen(WED_AutogenPlacement * a, int l, IResolver * r) : preview_polygon(a,l,false), ags(a), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		IGISPointSequence * ps = ags->GetOuterRing();
		int tile_width = min(zoomer->GetPPM() * 20.0, 10.0);
        g->SetState(false,0,false,true,true,false,false);
		vector<Point2>	pts;
        if(tile_width > 0)
        {
			glLineWidth(tile_width);
			glColor4f(1, 1, 0, .3);
			int n = ps->GetNumSides();
			if(ags->IsAGBlock())          // cross with same orientation as first segment to indicate block alignment
			{
				Bezier2		b;
				ps->GetSide(gis_Geo, 0, b);
				b.p1 = zoomer->LLToPixel(b.p1);
				b.p2 = zoomer->LLToPixel(b.p2);
				Vector2	dir(b.p1, b.p2);
				dir *= 0.2;
				Bbox2 		box;
				ags->GetBounds(gis_Geo, box);
				Point2 center(zoomer->LLToPixel(box.centroid()));
				pts.reserve(4);
				pts.push_back(center + dir);
				pts.push_back(center - dir);
				dir = dir.perpendicular_cw();
				pts.push_back(center + dir);
				pts.push_back(center - dir);

				glShape2v(GL_LINES, pts.data(), pts.size());
			}
			for(int i = 0; i < n; ++i)
			{
				pts.clear();
				SideToPoints(ps,i,zoomer, pts);

				Bezier2		bp;
				ps->GetSide(gis_Param,i,bp);
				bool spawning = bp.p1.x();

				if(spawning)
					glShapeOffset2v(GL_LINES, pts.data(), pts.size(), 1 + 0.5 * tile_width);
			}
			glLineWidth(1);
		}
		glColor4f(1, 1, 0, .2);
		preview_polygon::draw_it(zoomer, g, mPavementAlpha);
	}
};

struct	preview_ortho : public preview_polygon {
	WED_DrapedOrthophoto * orth;
	IResolver * resolver;
	preview_ortho(WED_DrapedOrthophoto * o, int l, IResolver * r) : preview_polygon(o,l,true), orth(o), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);


		//If this ortho is new
		if(orth->IsNew() == true)
		{
			string rpath;
			orth->GetResource(rpath);
			TexRef	tref = tman->LookupTexture(rpath.c_str(), false, tex_Compress_Ok|tex_Linear);
			if(tref == NULL) return;
			if(int tex_id = tman->GetTexID(tref))
			{
				g->SetState(false,1,false,true,true,false,false);
				glColor4f(1,1,1,1);
				g->BindTex(tex_id,0);
			}
		}
		else
		{
			string vpath;
			const pol_info_t * pol_info;
			orth->GetResource(vpath);
			if(!rmgr->GetPol(vpath,pol_info)) return;
			setup_pol_texture(tman, *pol_info, 0.0, true, Point2(), g, zoomer, mPavementAlpha);
		}
		preview_polygon::draw_it(zoomer,g,mPavementAlpha);
		kill_transform();
	}
};

struct	preview_object : public WED_PreviewItem {
	WED_ObjPlacement * obj;
	int	preview_level;
	IResolver * resolver;
	preview_object(WED_ObjPlacement * o, int l, int pl, IResolver * r) : WED_PreviewItem(l), obj(o), resolver(r), preview_level(pl) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *		tman = WED_GetTexMgr(resolver);
		ILibrarian *	lmgr = WED_GetLibrarian(resolver);
		string			vpath;
		const XObj8 *	o;
		const agp_t *	agp;
		Point2			loc;

		obj->GetResource(vpath);
		obj->GetLocation(gis_Geo, loc);

		g->SetState(false, 1, false, true, true, true, true);
		glColor4f(1, 1, 1, 1);

		float agl = obj->HasCustomMSL() > 1 ? obj->GetCustomMSL() : 0.0;

		if (rmgr->GetObj(vpath, o))
		{
			draw_obj_at_ll(tman, o, loc, agl, obj->GetHeading() + zoomer->GetRotation(loc), g, zoomer);
//			draw_obj_at_ll(tman, o, loc, agl, obj->GetHeading(), g, zoomer);
		}
		else if (rmgr->GetAGP(vpath, agp))
			draw_agp_at_ll(tman, agp, loc, agl, obj->GetHeading(), g, zoomer, preview_level);
		else
		{
			loc = zoomer->LLToPixel(loc);
			glColor3f(1,0,0);
			GUI_PlotIcon(g,"map_missing_obj.png", loc.x(),loc.y(), 0, 1.0);
		}
	}
};

struct	preview_taxisign : public WED_PreviewItem {
	WED_AirportSign * ts;
	IResolver * resolver;
	preview_taxisign(WED_AirportSign * s, int l, IResolver * r) : WED_PreviewItem(l), ts(s), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		ITexMgr *	tman = WED_GetTexMgr(resolver);

		Point2 loc;
		double hdg;
		int letters;
		string name;

		ts->GetLocation(gis_Geo,loc);
		hdg = ts->GetHeading();
		ts->GetName(name);

		double sign_scale;
		switch(ts->GetHeight())
		{
			case size_SmallRemaining:
			case size_SmallTaxi:   sign_scale = 0.010; break;
			case size_MediumTaxi:  sign_scale = 0.013; break;
			default:               sign_scale = 0.016;
		}
		g->SetState(false,0,false,false,true,true,true);
//		g->EnableDepth(true, true);
		glColor3f(0.4,0.3,0.1);

		glMatrixMode(GL_MODELVIEW);
		zoomer->PushMatrix();

		double ppm = zoomer->GetPPM() * sign_scale;
		Point2 l = zoomer->LLToPixel(loc);
		zoomer->Translatef(l.x(), l.y(), ppm * 10);
		zoomer->Scalef(ppm, ppm, ppm);
		zoomer->Rotatef(hdg, 0, 0, -1);

		sign_data tsign;
		tsign.from_code(name);

		const int w = max(tsign.calc_width(0), tsign.calc_width(1)) / 2;
		const int d =  6;
		const int h = 55;

		glEnable(GL_NORMALIZE);
		glBegin(GL_TRIANGLE_FAN);
			glVertex3i(-w,  d,  0);  glNormal3i(0,h,d);
			glVertex3i( w,  d,  0);
			glVertex3i( w,  0,  h);
			glVertex3i(-w,  0,  h);
		glEnd();
		glColor3f(0.15, 0.15, 0.15);
		glBegin(GL_TRIANGLES);
			glVertex3i( w,  d,  0);  glNormal3i(1,0,0);
			glVertex3i( w, -d,  0);
			glVertex3i( w,  0,  h);
		glEnd();

		zoomer->Rotatef(180, 0, 0, -1);

		glBegin(GL_TRIANGLE_FAN);
			glVertex3i(-w,  d,  0);  glNormal3i(0,h,d);
			glVertex3i( w,  d,  0);
			glVertex3i( w,  0,  h);
			glVertex3i(-w,  0,  h);
		glEnd();
		glBegin(GL_TRIANGLES);
			glVertex3i( w,  d,  0);  glNormal3i(1,0,0);
			glVertex3i( w, -d,  0);
			glVertex3i( w,  0,  h);
		glEnd();
		glDisable(GL_NORMALIZE);

		zoomer->PopMatrix();
	}
};


struct	preview_windsock : public WED_PreviewItem {
	WED_Windsock * ws;
	IResolver * resolver;
	preview_windsock(WED_Windsock * w, int l, IResolver * r) : WED_PreviewItem(l), ws(w), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		ILibrarian * lmgr = WED_GetLibrarian(resolver);

		glColor3f(1,1,1);
		Point2 loc;
		ws->GetLocation(gis_Geo,loc);
		const XObj8 * o = NULL;

		if(rmgr->GetObj("lib/airport/landscape/windsock.obj",o))
		{
			g->SetState(false,1,false,false,true,true,true);
			glColor3f(1,1,1);
			draw_obj_at_ll(tman, o, loc, 0.0, 120.0, g, zoomer);
		}
	}
};

struct	preview_beacon : public WED_PreviewItem {
	WED_AirportBeacon * bcn;
	IResolver * resolver;
	preview_beacon(WED_AirportBeacon * b, int l, IResolver * r) : WED_PreviewItem(l), bcn(b), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		ILibrarian * lmgr = WED_GetLibrarian(resolver);
		const char * vpath;

		glColor3f(1,1,1);
		Point2 loc;
		bcn->GetLocation(gis_Geo,loc);
		const XObj8 * o = NULL;

		switch(bcn->GetKind())
		{
			case beacon_Seaport:         vpath = "lib/airport/beacons/beacon_heliport.obj"; break;
			case beacon_Heliport:        vpath = "lib/airport/beacons/beacon_seaport.obj"; break;
			case beacon_MilitaryAirport: vpath = "lib/airport/beacons/beacon_mil.obj"; break;
			default /*beacon_Airport*/ : vpath = "lib/airport/beacons/beacon_airport_big.obj";
		}

		if(rmgr->GetObj(vpath, o))
		{
			g->SetState(false,1,false,false,true,true,true);
			glColor3f(1,1,1);
			draw_obj_at_ll(tman, o, loc, 0.0, 0.0, g, zoomer);
		}
	}
};

struct	preview_truck : public WED_PreviewItem {
	WED_TruckParkingLocation * trk;
	IResolver * resolver;
	preview_truck(WED_TruckParkingLocation * o, int l, IResolver * r) : WED_PreviewItem(l), trk(o), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		ILibrarian * lmgr = WED_GetLibrarian(resolver);
		string vpath1, vpath2;

		vpath1 = trk->GetTruckCustom();
		if(vpath1.empty())
			switch(trk->GetTruckType()) 
			{
			case atc_ServiceTruck_Baggage_Loader:		vpath1 = "lib/airport/vehicles/baggage_handling/belt_loader.obj";break;
			case atc_ServiceTruck_Baggage_Train:		vpath1 = "lib/airport/vehicles/baggage_handling/tractor.obj";
														vpath2 = "lib/airport/vehicles/baggage_handling/bag_cart.obj";	break;
			case atc_ServiceTruck_Crew_Limo:
			case atc_ServiceTruck_Crew_Car:				vpath1 = "lib/airport/vehicles/servicing/crew_car.obj";			break;
			case atc_ServiceTruck_Crew_Ferrari:			vpath1 = "lib/airport/vehicles/servicing/crew_ferrari.obj";		break;
			case atc_ServiceTruck_Food:					vpath1 = "lib/airport/vehicles/servicing/catering_truck.obj";	break;
			case atc_ServiceTruck_FuelTruck_Jet:		vpath1 = "lib/airport/vehicles/servicing/fuel_truck_large.obj";	break;
			case atc_ServiceTruck_FuelTruck_Liner:		vpath1 = "lib/airport/vehicles/fuel/hyd_disp_truck.obj";		break;
			case atc_ServiceTruck_FuelTruck_Prop:		vpath1 = "lib/airport/vehicles/servicing/fuel_truck_small.obj";	break;
			case atc_ServiceTruck_Ground_Power_Unit:	vpath1 = "lib/airport/vehicles/baggage_handling/tractor.obj";
														vpath2 = "lib/airport/vehicles/servicing/GPU.obj";				break;
			case atc_ServiceTruck_Pushback:				vpath1 = "lib/airport/vehicles/pushback/tug.obj";				break;
			}

		const XObj8 * o1 = NULL, * o2 = NULL;
		if(!vpath1.empty() && rmgr->GetObj(vpath1,o1))
		{
			g->SetState(false,1,false,true,true,true,true);
			glColor3f(1,1,1);
			Point2 loc;
			trk->GetLocation(gis_Geo,loc);
			double trk_heading = trk->GetHeading();
			draw_obj_at_ll(tman, o1, loc, 0.0, trk_heading, g, zoomer);

			if(trk->GetTruckType() == atc_ServiceTruck_Baggage_Train)
			{
				rmgr->GetObj(vpath2,o2);
				if(o2)
				{
					double gap = 3.899;
					Vector2 dirv(sin(trk_heading * DEG_TO_RAD),
								 cos(trk_heading * DEG_TO_RAD));
					Vector2 llv = VectorMetersToLL(loc, dirv);

					for(int c = 0; c < trk->GetNumberOfCars(); ++c)
					{
						loc -= (llv * gap);
						draw_obj_at_ll(tman, o2, loc, 0.0, trk_heading, g, zoomer);
						gap = 3.598;
					}
				}
			}
			if(trk->GetTruckType() == atc_ServiceTruck_Ground_Power_Unit)
			{
				rmgr->GetObj(vpath2,o2);
				if(o2)
				{
					double gap = 4.247;
					Vector2 dirv(sin(trk_heading * DEG_TO_RAD),
								 cos(trk_heading * DEG_TO_RAD));
					Vector2 llv = VectorMetersToLL(loc, dirv);

					loc -= (llv * gap);
					draw_obj_at_ll(tman, o2, loc, 0.0, trk_heading, g, zoomer);
				}
			}
		}
		else
		{
			Point2 l;
			trk->GetLocation(gis_Geo,l);
			l = zoomer->LLToPixel(l);
			glColor3f(1,0,0);
			GUI_PlotIcon(g,"map_missing_obj.png", l.x(),l.y(),0,1.0);
		}
	}
};

struct	preview_light : public WED_PreviewItem {
	WED_LightFixture * lgt;
	IResolver * resolver;
	preview_light(WED_LightFixture * o, int l, IResolver * r) : WED_PreviewItem(l), lgt(o), resolver(r) { }
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		ILibrarian * lmgr = WED_GetLibrarian(resolver);
		string vpath;
		AptLight_t light;
		lgt->Export(light);

		switch(light.light_code)
		{
			case apt_gls_vasi:          vpath = "lib/airport/lights/slow/VASI.obj";break;
			case apt_gls_vasi_tricolor: vpath = "lib/airport/lights/slow/VASI3.obj";break;
			case apt_gls_apapi_left:
			case apt_gls_apapi_right:
			case apt_gls_papi_left:
			case apt_gls_papi_right:
			case apt_gls_papi_20:  vpath = "lib/airport/lights/slow/PAPI.obj";	break;
			case apt_gls_wigwag:   vpath = "lib/airport/lights/slow/rway_guard.obj"; break;
		}

		const XObj8 * o = NULL;
		if(!vpath.empty() && rmgr->GetObj(vpath,o))
		{
			g->SetState(false,1,false,true,true,true,true);
			glColor3f(1,1,1);

			bool is_apapi = false;
			switch(light.light_code)
			{
				case apt_gls_vasi:
				{
					Vector2 dirv(0,75);
					dirv.rotate_by_degrees(-light.heading);
					dirv = VectorMetersToLL(light.location,dirv);

					light.location -= dirv;
					draw_obj_at_ll(tman, o, light.location, 0.0, light.heading, g, zoomer);
					light.location += dirv * 2.0;
					draw_obj_at_ll(tman, o, light.location, 0.0, light.heading, g, zoomer);
					break;
				}
				case apt_gls_apapi_left:
				case apt_gls_apapi_right:
					is_apapi = true;
				case apt_gls_papi_left:
				case apt_gls_papi_right:
				case apt_gls_papi_20:
				{
					Vector2 dirv(8,0);
					dirv.rotate_by_degrees(-light.heading);
					dirv = VectorMetersToLL(light.location,dirv);

					light.location -= dirv * (is_apapi ? 0.5 : 1.5);
					for(int n = 0; n < (is_apapi ? 2 : 4); n++)
					{
						draw_obj_at_ll(tman, o, light.location, 0.0, light.heading, g, zoomer);
						light.location += dirv;
					}
					break;
				}
				default:
					draw_obj_at_ll(tman, o, light.location, 0.0, light.heading, g, zoomer);
			}

		}
	}
};

struct	preview_road : WED_PreviewItem {
	WED_RoadEdge * road;
	IResolver * resolver;
	preview_road(WED_RoadEdge * ro, int l, IResolver * r) : WED_PreviewItem(l), road(ro), resolver(r) {}
	virtual void draw_it(WED_MapZoomerNew * zoomer, GUI_GraphState * g, float mPavementAlpha)
	{
		WED_ResourceMgr * rmgr = WED_GetResourceMgr(resolver);
		string vpath;
		road->GetResource(vpath);
		const road_info_t * rds;
		if(!rmgr->GetRoad(vpath,rds)) return;

		int sub_type = road->GetSubtype();
		auto vroads_i = rds->vroad_types.find(sub_type);
		if(vroads_i == rds->vroad_types.end()) return;
		auto roads_i = rds->road_types.find(vroads_i->second.rd_type);
		if(roads_i == rds->road_types.end()) return;
		auto& rd = roads_i->second;
		ITexMgr *	tman = WED_GetTexMgr(resolver);
		if (rd.tex_idx >= rds->textures.size()) return;
		TexRef tref = tman->LookupTexture(rds->textures[rd.tex_idx].c_str(),true,tex_Wrap+tex_Mipmap+tex_Linear);

		int tex_id = 0;
		if(tref) tex_id = tman->GetTexID(tref);

		IGISPointSequence * ps = SAFE_CAST(IGISPointSequence,road);
		auto PPM = zoomer->GetPPM();

		if(ps)
		{
			if(PixelSize(road, rd.width, zoomer) < 2*MIN_PIXELS_PREVIEW || !tex_id)             // cutoff size for real preview
			{
				g->SetState(false,0,false,false,false,false,false);
				glColor4f(0.3, 0.3, 0.3, mPavementAlpha);

				for(int i = 0; i < road->GetNumSides(); ++i)
				{
					vector<Point2>	pts;
					SideToPoints(ps,i,zoomer, pts);
					glLineWidth(5);
					glShape2v(GL_LINES, &*pts.begin(), pts.size());
					glLineWidth(1);
				}
			}
			else
			{
				g->SetState(false,1,false,true,true,false,false);
				g->BindTex(tex_id,0);
				glColor3f(1,1,1);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				for (const auto& s : rd.segs)
				{
					vector<Point2>	pts;
					PointSequenceToVector(ps, zoomer, pts, false, true);

					double left  = s.left  * PPM;
					double right = s.right * PPM;
					double t = 0.0;                                                              // accumulator for texture t, so each starts where the previous ended

					Vector2	dir(pts[1],pts[0]);               // direction of this segment
					double len = dir.normalize();
					Vector2 perp = dir.perpendicular_ccw();   // direction perpendicular

					glBegin(GL_TRIANGLE_STRIP);
						glTexCoord2f(s.s_right,t); glVertex2(pts[0] + perp * right);
						glTexCoord2f(s.s_left, t); glVertex2(pts[0] + perp * left);

						for (int j = 1; j < pts.size(); ++j)
						{
							t += len / (rd.length * PPM);
							Vector2 dir_next;
							if(j < pts.size()-1)
							{
								dir_next = Vector2(pts[j+1],pts[j]);
								len = dir_next.normalize();
								perp = (dir + dir_next) / (1.0 + dir.dot(dir_next));
							}
							else
								perp = dir;
							perp = perp.perpendicular_ccw();

							glTexCoord2f(s.s_right, t); glVertex2(pts[j] + perp * right);
							glTexCoord2f(s.s_left,  t); glVertex2(pts[j] + perp * left);
							dir = dir_next;
						}
					glEnd();
				}
			}
		}
	}
};

/***************************************************************************************************************************************************
 * DRAWING OBJECT
 ***************************************************************************************************************************************************/

WED_PreviewLayer::WED_PreviewLayer(GUI_Pane * host, WED_MapZoomerNew * zoomer, IResolver * resolver) :
	WED_MapLayer(host, zoomer, resolver),
	mPavementAlpha(1.0f),
	mObjDensity(6),
	mRunwayLayer(group_RunwaysBegin),
	mTaxiLayer(group_TaxiwaysBegin),
	mShoulderLayer(group_ShouldersBegin)
{
}

WED_PreviewLayer::~WED_PreviewLayer()
{
}

void		WED_PreviewLayer::GetCaps						(bool& draw_ent_v, bool& draw_ent_s, bool& cares_about_sel, bool& wants_clicks)
{
	draw_ent_v = true;
	draw_ent_s = false;
	cares_about_sel = false;
	wants_clicks = false;
}

bool		WED_PreviewLayer::DrawEntityVisualization		(bool inCurrent, IGISEntity * entity, GUI_GraphState * g, int selected)
{
	const char *	sub_class	= entity->GetGISSubtype();

	/******************************************************************************************************************************
	 * RUNWAYS, HELIPADS, SEALANES, TAIXWAYS, AND OTHER AIRPORT-RELATED GOO
	 ******************************************************************************************************************************/

	if (sub_class == WED_Runway::sClass)
	{
		WED_Runway * rwy = SAFE_CAST(WED_Runway,entity);
		if(rwy)
		{
			mPreviewItems.push_back(new preview_runway(rwy, mRunwayLayer++ - (rwy->GetSurface() >= surf_Grass ?
				 group_RunwaysBegin - group_UnpavedRunwaysBegin : 0), 0, GetResolver()));

			mPreviewItems.push_back(new preview_runway(rwy, mShoulderLayer++, 1, GetResolver()));
		}
	}
	else if (sub_class == WED_Helipad::sClass)
	{
		WED_Helipad * heli = SAFE_CAST(WED_Helipad,entity);
		if(heli)	mPreviewItems.push_back(new preview_helipad(heli,mRunwayLayer++,GetResolver()));
	}
	else if (sub_class == WED_Sealane::sClass)
	{
		WED_Sealane * sea = SAFE_CAST(WED_Sealane,entity);
		if(sea)		mPreviewItems.push_back(new preview_sealane(sea,mRunwayLayer++));
	}
	else if (sub_class == WED_Taxiway::sClass)
	{
		WED_Taxiway * taxi = SAFE_CAST(WED_Taxiway,entity);
		if(taxi)
		{
			mPreviewItems.push_back(new preview_taxiway(taxi,mTaxiLayer++ - (taxi->GetSurface() >= surf_Grass ?
				group_TaxiwaysBegin - group_UnpavedTaxiwaysBegin : 0) ,GetResolver()));

// f'd up - its culling by taxiway polygon size and not by gis chain line width. And thats after all the dynamic casting, boundig box pulling and all ...oh my.

			if(PixelSize(taxi, 0.4, GetZoomer()) > mOptions.minLineThicknessPixels)        // there can be so many, make visibility decision here already for performance
			{
				IGISPointSequence * ps = taxi->GetOuterRing();
				mPreviewItems.push_back(new preview_airportlines(ps, group_Markings, GetResolver()));
				mPreviewItems.push_back(new preview_airportlights(ps, group_Objects, GetResolver()));

				int n = taxi->GetNumHoles();
				for (int i = 0; i < n; ++i)
				{
					IGISPointSequence * ps = taxi->GetNthHole(i);
					mPreviewItems.push_back(new preview_airportlines(ps, group_Markings, GetResolver()));
					mPreviewItems.push_back(new preview_airportlights(ps, group_Objects, GetResolver()));
				}
			}
		}
	}
	/******************************************************************************************************************************
	 * POLYGON & LINE PREVIEW: forests, facades, polygons (ortho and landuse)
	 ******************************************************************************************************************************/

	else if (sub_class == WED_PolygonPlacement::sClass)
	{
		if(auto pol = dynamic_cast<WED_PolygonPlacement*>(entity))
		{
			Bbox2 b;
			pol->GetBounds(gis_Geo, b);
			if (GetZoomer()->PixelSize(b) > MIN_PIXELS_PREVIEW)
			{
				string vpath;
				const pol_info_t* pol_info;
				int lg = group_TaxiwaysBegin;
				WED_ResourceMgr* rmgr = WED_GetResourceMgr(GetResolver());
				pol->GetResource(vpath);
				if (!vpath.empty() && rmgr->GetPol(vpath, pol_info) && !pol_info->group.empty())
					lg = layer_group_for_string(pol_info->group.c_str(), pol_info->group_offset, lg);
				mPreviewItems.push_back(new preview_pol(pol, lg, GetResolver()));
			}
		}
	}
	else if (sub_class == WED_DrapedOrthophoto::sClass)
	{
		if(auto orth = dynamic_cast<WED_DrapedOrthophoto*>(entity))
		{
			Bbox2 b;
			orth->GetBounds(gis_Geo, b);
			if (GetZoomer()->PixelSize(b) > MIN_PIXELS_PREVIEW)
			{
				string vpath;
					const pol_info_t* pol_info;
					int lg = group_TaxiwaysBegin;
					WED_ResourceMgr* rmgr = WED_GetResourceMgr(GetResolver());

					orth->GetResource(vpath);
				if (!vpath.empty() && rmgr->GetPol(vpath, pol_info) && !pol_info->group.empty())
					lg = layer_group_for_string(pol_info->group.c_str(), pol_info->group_offset, lg);
				mPreviewItems.push_back(new preview_ortho(orth, lg, GetResolver()));
			}
		}
	}
	else if (sub_class == WED_FacadePlacement::sClass)
	{
		auto fac = dynamic_cast<WED_FacadePlacement*>(entity);
		if(fac && fac->GetShowLevel() <= mObjDensity)
			mPreviewItems.push_back(new preview_facade(fac,group_Objects, GetResolver()));
	}
	else if (sub_class == WED_ForestPlacement::sClass)
	{
		if (auto forst = dynamic_cast<WED_ForestPlacement*>(entity))
		{
			Bbox2 b;
			forst->GetBounds(gis_Geo, b);
			if (GetZoomer()->PixelSize(b) > MIN_PIXELS_PREVIEW)
				mPreviewItems.push_back(new preview_forest(forst, group_Footprints));
		}
	}
	else if(sub_class == WED_LinePlacement::sClass)
	{
		if(auto line = dynamic_cast<WED_LinePlacement*>(entity))
		{
			string vpath;
			const lin_info_t* lin_info;
			int lg = group_Markings;
			double lwidth = 0.4;
			WED_ResourceMgr* rmgr = WED_GetResourceMgr(GetResolver());

			line->GetResource(vpath);
			if (!vpath.empty() && rmgr->GetLin(vpath, lin_info))
			{
				lg = layer_group_for_string(lin_info->group.c_str(), lin_info->group_offset, lg);
				lwidth = max(0.4, lin_info->eff_width * 0.5);
			}
			// criteria matches where mRealLines disappear in StructureLayer
			if(PixelSize(line, lwidth, GetZoomer()) > mOptions.minLineThicknessPixels)
				mPreviewItems.push_back(new preview_line(line, lg, GetResolver()));
		}
	}
	else if(sub_class == WED_AirportChain::sClass)
	{
		if(auto chn = dynamic_cast<WED_AirportChain*>(entity))
			// criteria matches where mRealLines disappear in StructureLayer
			if(PixelSize(chn, 0.4, GetZoomer()) > mOptions.minLineThicknessPixels)
			{
				mPreviewItems.push_back(new preview_airportlines(chn, group_Markings, GetResolver()));
				mPreviewItems.push_back(new preview_airportlights(chn, group_Objects, GetResolver()));
			}
	}
	else if(sub_class == WED_StringPlacement::sClass)
	{
		if(auto str = dynamic_cast<WED_StringPlacement*>(entity))
			mPreviewItems.push_back(new preview_string(str, group_Objects, GetResolver()));
	}
	else if (sub_class == WED_AutogenPlacement::sClass)
	{
		if (auto ags = dynamic_cast<WED_AutogenPlacement*>(entity))
		{
			Bbox2 b;
			ags->GetBounds(gis_Geo, b);
			if (GetZoomer()->PixelSize(b) > MIN_PIXELS_PREVIEW)
				mPreviewItems.push_back(new preview_autogen(ags, group_Objects, GetResolver()));
		}
	}
	else if (sub_class == WED_RoadEdge::sClass)
	{
	if (auto rd = dynamic_cast<WED_RoadEdge*>(entity))
		mPreviewItems.push_back(new preview_road(rd, group_Roads, GetResolver()));
	}

	/******************************************************************************************************************************
	 * OBJECT preview
	 ******************************************************************************************************************************/

	else if (sub_class == WED_ObjPlacement::sClass)
	{
		if(auto obj = dynamic_cast<WED_ObjPlacement*>(entity))
			if(obj->GetShowLevel() <= mObjDensity)
				if (PixelSize(obj, 2 * obj->GetVisibleMeters(), GetZoomer()) > MIN_PIXELS_PREVIEW)
					mPreviewItems.push_back(new preview_object(obj,group_Objects, mObjDensity, GetResolver()));
	}
	else if (sub_class == WED_TruckParkingLocation::sClass)
	{
		if(auto trk = dynamic_cast<WED_TruckParkingLocation*>(entity))
			if (PixelSize(trk, 5.0, GetZoomer()) > MIN_PIXELS_PREVIEW)
				mPreviewItems.push_back(new preview_truck(trk, group_Objects, GetResolver()));
	}
	else if (sub_class == WED_LightFixture::sClass)
	{
		if(auto lgt = dynamic_cast<WED_LightFixture*>(entity))
			if (PixelSize(lgt, 1.0, GetZoomer()) > MIN_PIXELS_PREVIEW)
				mPreviewItems.push_back(new preview_light(lgt, group_Objects, GetResolver()));
	}
	else if (sub_class == WED_Windsock::sClass)
	{
		if (auto ws = dynamic_cast<WED_Windsock*>(entity))
			mPreviewItems.push_back(new preview_windsock(ws, group_Objects, GetResolver()));
	}
	else if (sub_class == WED_AirportBeacon::sClass)
	{
		if (auto bcn = dynamic_cast<WED_AirportBeacon*>(entity))
			mPreviewItems.push_back(new preview_beacon(bcn, group_Objects, GetResolver()));
	}
	else if (sub_class == WED_AirportSign::sClass)
	{
		if (auto tsign = dynamic_cast<WED_AirportSign*>(entity))
			if (PixelSize(tsign, 1.0, GetZoomer()) > MIN_PIXELS_PREVIEW)
				mPreviewItems.push_back(new preview_taxisign(tsign, group_Objects, GetResolver()));
	}
	return true;
}

void		WED_PreviewLayer::DrawVisualization			(bool inCurent, GUI_GraphState * g)
{
	// This is called after per-entity visualization; we have one preview item for everything we need.
	// sort, draw, nuke 'em.

	sort(mPreviewItems.begin(),mPreviewItems.end(),sort_item_by_layer());
	for(vector<WED_PreviewItem *>::iterator i = mPreviewItems.begin(); i != mPreviewItems.end(); ++i)
	{
		(*i)->draw_it(GetZoomer(), g, mPavementAlpha);
		delete *i;
	}
	mPreviewItems.clear();
	mRunwayLayer=	group_RunwaysBegin;
	mTaxiLayer=		group_TaxiwaysBegin;
	mShoulderLayer=	group_ShouldersBegin;
}

void		WED_PreviewLayer::SetPavementTransparency(float alpha)
{
	mPavementAlpha = alpha;
	GetHost()->Refresh();
}

float		WED_PreviewLayer::GetPavementTransparency(void) const
{
	return mPavementAlpha;
}

void		WED_PreviewLayer::SetObjDensity(int d)
{
	mObjDensity = d;
	GetHost()->Refresh();
}

int			WED_PreviewLayer::GetObjDensity(void) const
{
	return mObjDensity;
}

void		WED_PreviewLayer::SetOptions(const Options& options)
{
	mOptions = options;
}
