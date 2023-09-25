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

#ifndef WED_TAXIWAY_H
#define WED_TAXIWAY_H

#include "WED_GISPolygon.h"
#include "IHasResource.h"

struct	AptTaxiway_t;

class	WED_Taxiway : public WED_GISPolygon, public IHasAttr {

DECLARE_PERSISTENT(WED_Taxiway)
public:

	void		SetSurface(int s);
	void		SetRoughness(double r);
	void		SetHeading(double h);
	double		GetHeading(void) const;

	int			GetSurface(void) const;

	void		Import(const AptTaxiway_t& x, void (* print_func)(void *, const char *, ...), void * ref);
	void		Export(		 AptTaxiway_t& x) const;

	virtual void			GetResource(std::string& r) const;
	virtual const char *	HumanReadableType(void) const { return "Taxiway"; }

	virtual		void	GetNthPropertyDict(int n, PropertyDict_t& dict) const;

protected:

	virtual	bool		IsInteriorFilled(void) const { return true; }

private:

	WED_PropIntEnum			surface;
	WED_PropDoubleText		roughness;
	WED_PropDoubleText		heading;

	WED_PropIntEnumSetUnion	lines;
	WED_PropIntEnumSetUnion	lights;

};

#endif /* WED_TAXIWAY_H */
