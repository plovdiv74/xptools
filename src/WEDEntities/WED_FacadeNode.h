/* 
 * Copyright (c) 2011, Laminar Research.
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

#ifndef WED_FacadeNode_H
#define WED_FacadeNode_H

#include "WED_GISPoint_Bezier.h"

class WED_FacadeNode : public WED_GISPoint_Bezier {

DECLARE_PERSISTENT(WED_FacadeNode)

public: 

	virtual	bool	HasLayer		(GISLayer_t layer							  ) const;

	virtual	void	GetLocation		   (GISLayer_t l,      Point2& p) const;
	virtual	bool	GetControlHandleLo (GISLayer_t l,       Point2& p) const;
	virtual	bool	GetControlHandleHi (GISLayer_t l,       Point2& p) const;

	virtual void	GetNthPropertyDict(int n, PropertyDict_t& dict) const;
	virtual	void	GetNthPropertyDictItem(int n, int e, std::string& item) const;
	virtual	void	PropEditCallback(int before);
			int		GetWallType(void) const;
			void		SetWallType(int wt);

	virtual const char *	HumanReadableType(void) const { return "Facade Node"; }

private:
	
	WED_PropIntEnum		wall_type;

};


#endif /* WED_FacadeNode_H */
