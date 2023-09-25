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

#ifndef WED_CreateEdgeTool_H
#define WED_CreateEdgeTool_H

// Ben says: edges are going to be a general part of future WEDs - not just for
// taxi routes but for roads!  But since WED 1.1 has NEITHER roads nor tai routes,
// kill the whole tool.  We can't make the tool sanely if there are ZERO real
// entity types it can make.

#include "WED_CreateToolBase.h"

enum CreateEdge_t {
	create_TaxiRoute = 0,
#if ROAD_EDITING
	create_Road
#endif
};

class	WED_Thing;
class	IGISEntity;
class	IGISPointSequence;
struct	road_info_t;


class WED_CreateEdgeTool : public WED_CreateToolBase {
public:

	WED_CreateEdgeTool(
									const char *		tool_name,
									GUI_Pane *			host,
									WED_MapZoomerNew *	zoomer,
									IResolver *			resolver,
									WED_Archive *		archive,
									CreateEdge_t		tool_type);

	virtual				~WED_CreateEdgeTool();

	// WED_MapToolNew
	virtual	const char *		GetStatusText(void);

	virtual	void			GetNthProperty(int n, PropertyVal_t& val) const;
	virtual	void			SetNthProperty(int n, const PropertyVal_t& val);
	virtual	void			GetNthPropertyDict(int n, PropertyDict_t& dict) const;
	virtual	void			GetNthPropertyDictItem(int n, int e, std::string& item) const;
	virtual	void			GetNthPropertyInfo(int n, PropertyInfo_t& info) const;
#if ROAD_EDITING
	void			        SetResource(const std::string& r);
#endif

private:

	WED_PropIntEnum			mVehicleClass;
	WED_PropBoolText		mOneway;
	WED_PropIntEnum			mRunway;
	WED_PropIntEnumSet		mHotDepart;
	WED_PropIntEnumSet		mHotArrive;
	WED_PropIntEnumSet		mHotILS;
	WED_PropIntEnum			mWidth;

	WED_PropStringText		mName;
	WED_PropIntText			mSlop;

#if ROAD_EDITING
	WED_PropIntText			mLayer;
	WED_PropIntText			mSubtype;
	WED_PropStringText		mResource;
#endif
	virtual	void		AcceptPath(
							const std::vector<Point2>&	pts,
							const std::vector<Point2>&	dirs_lo,
							const std::vector<Point2>&	dirs_hi,
							const std::vector<int>		has_dirs,
							const std::vector<int>		has_split,
							int						closed);
	virtual	bool		CanCreateNow(void);

			// FILTERING: we don't actually want our network-creation tools to pick up just ANY part of the airport.  Filter field (if not null) is the
			// name of the classes that we 'intersect' with.  It should be the edge class name.
			void		FindNear(   WED_Thing * host, IGISEntity * ent, const char * filter, const Point2& loc, WED_Thing *& out_thing,         double& out_dsq);
			void		FindNearP2S(WED_Thing * host, IGISEntity * ent, const char * filter, const Point2& loc, IGISPointSequence *& out_thing, double& out_dsg ,const double dst);
			WED_Thing *	GetHost(int& idx);

	const road_info_t *	get_valid_road_info(void) const;

		CreateEdge_t	mType;

};

#endif /* WED_CreateEdgeTool_H */
