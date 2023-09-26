//
//  WED_RoadNode.hpp
//  SceneryTools_xcode6
//
//  Created by Ben Supnik on 12/15/15.
//
//

#ifndef WED_RoadNode_h
#define WED_RoadNode_h

#if ROAD_EDITING
#include "WED_GISPoint.h"
// This trivial point forms the nodes of road networks.  They don't have properties now but
// we need a special class so that the network editing code can know what types are "part of the network".

class WED_RoadNode : public WED_GISPoint
{

    DECLARE_PERSISTENT(WED_RoadNode)

public:
    virtual const char* HumanReadableType(void) const
    {
        return "Road Network Node";
    }

    // from WED_GISPoint
    virtual void Rescale(GISLayer_t l, const Bbox2& old_bounds, const Bbox2& new_bounds);
    virtual void Rotate(GISLayer_t l, const Point2& center, double angle);
};
#endif

#endif /* WED_RoadNode_h */
