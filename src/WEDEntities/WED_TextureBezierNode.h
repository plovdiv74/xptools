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

#ifndef WED_TextureBezierNode_H
#define WED_TextureBezierNode_H

#include "WED_GISPoint_Bezier.h"

class WED_TextureBezierNode : public WED_GISPoint_Bezier
{

    DECLARE_PERSISTENT(WED_TextureBezierNode)

public:
    virtual bool HasLayer(GISLayer_t l) const;
    virtual void Rescale(GISLayer_t l, const Bbox2& old_bounds, const Bbox2& new_bounds);
    virtual void Rotate(GISLayer_t l, const Point2& center, double angle);

    virtual void SetLocation(GISLayer_t layer, const Point2& st);
    virtual void GetLocation(GISLayer_t layer, Point2& st) const;

    virtual bool GetControlHandleLo(GISLayer_t layer, Point2& p) const;
    virtual bool GetControlHandleHi(GISLayer_t layer, Point2& p) const;
    virtual void SetControlHandleLo(GISLayer_t layer, const Point2& p);
    virtual void SetControlHandleHi(GISLayer_t layer, const Point2& p);

    virtual const char* HumanReadableType(void) const
    {
        return "Curved UV-Mapped Node";
    }

private:
    WED_PropDoubleText mS;
    WED_PropDoubleText mT;
    WED_PropDoubleText mScL;
    WED_PropDoubleText mTcL;
    WED_PropDoubleText mScH;
    WED_PropDoubleText mTcH;
};

#endif /* WED_TextureBezierNode_H */
