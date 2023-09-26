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

#ifndef WED_RoadEdge_H
#define WED_RoadEdge_H

#if ROAD_EDITING

#include "WED_GISEdge.h"
#include "IHasResource.h"

struct road_info_t;

class WED_RoadEdge : public WED_GISEdge, public IHasResource
{

    DECLARE_PERSISTENT(WED_RoadEdge)

public:
    int GetStartLayer(void) const
    {
        return start_layer.value;
    }
    void SetStartLayer(int l)
    {
        start_layer = l;
    }
    int GetEndLayer(void) const
    {
        return end_layer.value;
    }
    void SetEndLayer(int l)
    {
        end_layer = l;
    }
    int GetSubtype(void) const
    {
        return subtype.value;
    }
    void SetSubtype(int s)
    {
        subtype = s;
    }
    bool IsValidSubtype(void) const;
    bool HasWires(void) const;
    std::pair<double, double> GetWidth(void) const;

    void GetResource(std::string& r) const
    {
        r = resource.value;
    }
    void SetResource(const std::string& r)
    {
        resource = r;
    }

    bool IsOneway(void) const;

    const char* HumanReadableType(void) const
    {
        return "Road";
    }

    void GetNthProperty(int n, PropertyVal_t& val) const;
    void SetNthProperty(int n, const PropertyVal_t& val);
    void GetNthPropertyDict(int n, PropertyDict_t& dict) const;
    void GetNthPropertyDictItem(int n, int e, std::string& item) const;
    void GetNthPropertyInfo(int n, PropertyInfo_t& info) const;

protected:
    bool CanBeCurved() const
    {
        return true;
    }

private:
    const road_info_t* get_valid_road_info(void) const;

    WED_PropStringText resource;
    WED_PropIntText start_layer;
    WED_PropIntText end_layer;
    WED_PropIntText subtype;
};

#endif

#endif /* WED_RoadEdge_H */
