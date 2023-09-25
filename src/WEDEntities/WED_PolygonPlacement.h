/*
 * Copyright (c) 2008, Laminar Research.
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

#ifndef WED_PolygonPlacement_H
#define WED_PolygonPlacement_H

#include "IHasResource.h"
#include "WED_GISPolygon.h"

class	WED_PolygonPlacement : public WED_GISPolygon, public IHasResource {

DECLARE_PERSISTENT(WED_PolygonPlacement)

public:

			double		GetHeading(void) const;
			void		SetHeading(double h);

void		GetResource(	  std::string& r) const;
void		SetResource(const std::string& r);

int			CountProperties(void) const;
int			FindProperty(const char * in_prop) const;
void		GetNthPropertyInfo(int n, PropertyInfo_t& info) const;
void		GetNthPropertyDict(int n, PropertyDict_t& dict) const;
void		GetNthPropertyDictItem(int n, int e, std::string& item) const;
void		GetNthProperty(int n, PropertyVal_t& val) const;
void		SetNthProperty(int n, const PropertyVal_t& val);


const char *	HumanReadableType(void) const { return "Draped Polygon"; }

protected:

bool		IsInteriorFilled(void) const { return true; }

private:

	WED_PropDoubleText		heading;
	WED_PropStringText		resource;

};



#endif /* WED_PolygonPlacement_H */
