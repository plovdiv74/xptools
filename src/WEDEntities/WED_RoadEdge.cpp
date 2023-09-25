/*
 * Copyright (c) 2010, Laminar Research.
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

#include "WED_RoadEdge.h"
#include "WED_EnumSystem.h"
#include "WED_ResourceMgr.h"
#include "WED_ToolUtils.h"
#include <sstream>

/*
√		merge commands broken, need rewrite
√		show road types in CREATE TOOL
√		option-split won't split beziers?!
√		fixed edge duplicate command

	road todos:
		exporter does not clip
	create tool
		intersect beziers
		self-overlapping doesn't work
		multi-overlaps causes @#$@ chaos
	split command
		needs to split selected edges
		needs to slit crossing edges
	vertex tool
		no way to get AT bezier curves when we don't ALREADY have handles
		(all of the handle ops from bezier vertices just don't work)
	validation
		crossing roads triggers validation fail labeled as 'atc'

*/

#if ROAD_EDITING

DEFINE_PERSISTENT(WED_RoadEdge)
TRIVIAL_COPY(WED_RoadEdge, WED_GISEdge)


WED_RoadEdge::WED_RoadEdge(WED_Archive * a, int i) : WED_GISEdge(a,i),
	start_layer(this,PROP_Name("Start Layer", XML_Name("road_edge","layer")),     0,2),
	end_layer(this,  PROP_Name("End Layer",   XML_Name("road_edge","end_layer")), 0,2),
	subtype(this,    PROP_Name("Type",        XML_Name("road_edge","sub_type")),  1,3),
	resource(this,   PROP_Name("Resource",    XML_Name("road_edge","resource")),  "")
{
}

WED_RoadEdge::~WED_RoadEdge()
{
}

bool			WED_RoadEdge::IsOneway(void) const
{
	if(auto r = get_valid_road_info())
	{
		auto vr = r->vroad_types.find(subtype.value);
		if (vr != r->vroad_types.end())
		{
			auto rd = r->road_types.find(vr->second.rd_type);
			if ( rd != r->road_types.end())
				return rd->second.oneway;
		}
	}
	return false;
}

std::pair<double, double> WED_RoadEdge::GetWidth(void) const
{
	if(auto r = get_valid_road_info())
	{
		auto vr = r->vroad_types.find(subtype.value);
		if (vr != r->vroad_types.end())
		{
			auto rd = r->road_types.find(vr->second.rd_type);
			if ( rd != r->road_types.end())
				return std::make_pair(rd->second.width, rd->second.traffic_width);
		}
	}
	return std::make_pair(0, 0);
}

void		WED_RoadEdge::GetNthPropertyInfo(int n, PropertyInfo_t& info) const
{
	WED_GISEdge::GetNthPropertyInfo(n, info);
	if(n == PropertyItemNumber(&subtype))
	{
		if(get_valid_road_info())
		{
			info.prop_kind = prop_RoadType;
//			info.prop_kind = prop_Enum;
			return;
		}
	}
}

void		WED_RoadEdge::GetNthProperty(int n, PropertyVal_t& val) const
{
	WED_GISEdge::GetNthProperty(n, val);
	if(n == PropertyItemNumber(&subtype))
	{
		if(get_valid_road_info())
		{
			val.prop_kind = prop_RoadType;
//			val.prop_kind = prop_Enum;
		}
	}
}

void		WED_RoadEdge::SetNthProperty(int n, const PropertyVal_t& val)
{
	if(n == PropertyItemNumber(&subtype))
	{
		if(get_valid_road_info())
		{
			PropertyVal_t v(val);
			v.prop_kind = prop_Int;
			WED_GISEdge::SetNthProperty(n, v);
			return;
		}
	}
	WED_GISEdge::SetNthProperty(n, val);
}


void		WED_RoadEdge::GetNthPropertyDict(int n, PropertyDict_t& dict) const
{
	dict.clear();
	if(n == PropertyItemNumber(&subtype))
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
	WED_GISEdge::GetNthPropertyDict(n, dict);
}

void		WED_RoadEdge::GetNthPropertyDictItem(int n, int e, std::string& item) const
{
	if(n == PropertyItemNumber(&subtype))
	{
		if(auto r = get_valid_road_info())
		{
			auto i = r->vroad_types.find(subtype.value);
			if (i != r->vroad_types.end())
			{
				item = i->second.description;
				return;
			}
			else
			{
				if (subtype.value == 1)
				{
					item = "None";
				}
				else
				{
					stringstream ss;
					ss << subtype.value;
					item = ss.str();
				}
				return;
			}
		}
	}
	WED_GISEdge::GetNthPropertyDictItem(n, e, item);
}

bool	WED_RoadEdge::IsValidSubtype(void) const
{
	if(auto inf = get_valid_road_info())
	{
		auto vi = inf->vroad_types.find(subtype.value);
		if(vi != inf->vroad_types.end())
			if(inf->road_types.find(vi->second.rd_type) != inf->road_types.end())
				return true;
	}
	return false;
}

bool	WED_RoadEdge::HasWires(void) const
{
	if(auto inf = get_valid_road_info())
	{
		auto vi = inf->vroad_types.find(subtype.value);
		if(vi != inf->vroad_types.end())
		{
			auto ri = inf->road_types.find(vi->second.rd_type);
			if( ri != inf->road_types.end() && ri->second.wires.size() > 0)
				return true;
		}
	}
	return false;
}

const road_info_t * WED_RoadEdge::get_valid_road_info() const
{
#if WED
	WED_ResourceMgr * rmgr = WED_GetResourceMgr(GetArchive()->GetResolver());
	const road_info_t * r;
	if(rmgr && rmgr->GetRoad(resource.value, r))
		if(r->vroad_types.size() > 0)
			return r;
#endif
	return nullptr;
}

#endif
