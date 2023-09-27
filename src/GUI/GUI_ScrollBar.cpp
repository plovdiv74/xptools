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

#include "GUI_ScrollBar.h"
#include "GUI_GraphState.h"
#include "GUI_Resources.h"
#include "GUI_DrawUtils.h"

#if APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

enum
{
    sb_PartNone,
    sb_PartDownButton,
    sb_PartDownPage,
    sb_PartThumb,
    sb_PartUpPage,
    sb_PartUpButton
};

static int SB_BuildMetrix(int vertical, float M1, float M2, float MD, // Major axis - min, max, span
                          float m1, float m2, float md,               // Minor axis - min, max, span
                          float vnow,                                 // Actual scroll bar values
                          float vmin, float vmax, float vpage,
                          float& minbut, // cut from min button's end to sb
                          float& thumb1, // bottom of thumb
                          float& thumb2, // top of th umb
                          float& maxbut) // cut from sb to max button's start
{
    int metrics[2];

    GUI_GetImageResourceSize(vertical ? "scroll_btn_v.png" : "scroll_btn_h.png", metrics);
    float button_len = (vertical ? metrics[1] : metrics[0]) / 2;
    GUI_GetImageResourceSize(vertical ? "scrollbar_v.png" : "scrollbar_h.png", metrics);
    float thumb_len = (vertical ? metrics[1] : metrics[0]);

    float sbl = MD - 2.0 * button_len; // real scroll bar len - buttons
    minbut = M1 + button_len;          // subtract square buttons from ends
    maxbut = M2 - button_len;

    if (vmax <= vmin)
    {
        // special case - scroll bar is not scrollable
        return 0;
    }

    float tp = (vpage / (vpage + vmax - vmin)); // thumb percent - thumb covers what percent of track?
    if (tp > 1.0)
        tp = 1.0; // clamp for sanity - apps shouldn't even let this happen!

    float tl = sbl * tp; // thumb len is just percent of track
    if (tl < thumb_len)
        tl = thumb_len;

    float fp = sbl - tl; // free play in scroll bar - how far it moves.

    float pp = (vnow - vmin) / (vmax - vmin); // position now as percent

    float ts = fp * pp; // thumb start now as dist up sb

    thumb1 = minbut + ts; // start and end of thumb can now be calculated
    thumb2 = minbut + ts + tl;

    if (thumb1 < minbut)
        thumb1 = minbut;
    if (thumb2 > maxbut)
        thumb2 = maxbut;

    return 1;
}

GUI_ScrollBar::GUI_ScrollBar() : mClickPart(0), mInPart(0), mSlop(0.0)
{
}

GUI_ScrollBar::~GUI_ScrollBar()
{
}

int GUI_ScrollBar::GetMinorAxis(int vertical)
{
    int metrics[2];

    GUI_GetImageResourceSize(vertical ? "scroll_btn_v.png" : "scroll_btn_h.png", metrics);

    float button_len = (vertical ? metrics[0] : metrics[1]) / 2;

    return button_len;
}

int GUI_ScrollBar::MouseDown(int x, int y, int button)
{
    int b[4];
    GetBounds(b);
    float bf[6] = {
        (float)b[0], (float)b[1], (float)b[2], (float)b[3], (float)b[2] - (float)b[0], (float)b[3] - (float)b[1]};
    float vnow, vmin, vmax, vpage;
    float b1, b2, t1, t2;
    vnow = this->GetValue();
    vmin = this->GetMin();
    vmax = this->GetMax();
    vpage = this->GetPageSize();
    int alive;
    float track_coord = (bf[4] > bf[5]) ? x : y;

    if (bf[4] > bf[5])
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[0], bf[2], bf[4], bf[1], bf[3], bf[5], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);
    else
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[1], bf[3], bf[5], bf[0], bf[2], bf[4], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);

    if (!alive)
        return 1;

    if (track_coord < b1)
    {
        mClickPart = sb_PartDownButton;
        vnow = std::max(vmin, vnow - vpage * 0.1f);
        if (vnow != this->GetValue())
            this->SetValue(vnow);
    }
    else if (track_coord > b2)
    {
        mClickPart = sb_PartUpButton;
        vnow = std::min(vmax, vnow + vpage * 0.1f);
        if (vnow != this->GetValue())
            this->SetValue(vnow);
    }
    else if (track_coord < t1)
    {
        mClickPart = sb_PartDownPage;
        vnow = std::max(vmin, vnow - vpage);
        if (vnow != this->GetValue())
            this->SetValue(vnow);
    }
    else if (track_coord > t2)
    {
        mClickPart = sb_PartUpPage;
        vnow = std::min(vmax, vnow + vpage);
        if (vnow != this->GetValue())
            this->SetValue(vnow);
    }
    else
    {
        mClickPart = sb_PartThumb;
        mSlop = track_coord - t1;
    }
    mInPart = 1;
    Start(0.3);
    return 1;
}

void GUI_ScrollBar::MouseDrag(int x, int y, int button)
{
    int oip = mInPart;
    int b[4];
    GetBounds(b);
    float bf[6] = {
        (float)b[0], (float)b[1], (float)b[2], (float)b[3], (float)b[2] - (float)b[0], (float)b[3] - (float)b[1]};
    float vnow, vmin, vmax, vpage;
    float b1, b2, t1, t2;
    vnow = this->GetValue();
    vmin = this->GetMin();
    vmax = this->GetMax();
    vpage = this->GetPageSize();
    int alive;

    float track_coord = (bf[4] > bf[5]) ? x : y;

    if (bf[4] > bf[5])
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[0], bf[2], bf[4], bf[1], bf[3], bf[5], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);
    else
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[1], bf[3], bf[5], bf[0], bf[2], bf[4], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);

    mInPart = 0;
    if (!alive)
        goto done;

    // If out of box and no thumb, bail
    if (mClickPart != sb_PartThumb && (x < b[0] || y < b[1] || x > b[2] || y > b[3]))
        goto done;

    if (track_coord < b1 && mClickPart == sb_PartDownButton)
    {
        mInPart = 1;
        //		vnow = std::max(vmin, vnow-vpage*0.1f);
        //		if (vnow != this->GetValue())
        //			this->SetValue(vnow);
    }
    if (track_coord > b1 && track_coord < t1 && mClickPart == sb_PartDownPage)
    {
        mInPart = 1;
        //		vnow = std::max(vmin, vnow-vpage);
        //		if (vnow != this->GetValue())
        //			this->SetValue(vnow);
    }
    if (track_coord > t2 && track_coord < b2 && mClickPart == sb_PartUpPage)
    {
        mInPart = 1;
        //		vnow = std::min(vmax, vnow+vpage);
        //		if (vnow != this->GetValue())
        //			this->SetValue(vnow);
    }
    if (track_coord > b2 && mClickPart == sb_PartUpButton)
    {
        mInPart = 1;
        //		vnow = std::min(vmax, vnow+vpage*0.1f);
        //		if (vnow != this->GetValue())
        //			this->SetValue(vnow);
    }
    if (mClickPart == sb_PartThumb)
    {
        mInPart = 1;
        vnow = (track_coord - mSlop - b1) * (vmax - vmin) / ((b2 - b1) - (t2 - t1)) + vmin;
        vnow = std::min(vnow, vmax);
        vnow = std::max(vnow, vmin);
        if (vnow != this->GetValue())
            this->SetValue(vnow);
    }
done:
    if (mInPart != oip)
        Refresh();
}

void GUI_ScrollBar::MouseUp(int x, int y, int button)
{
    mInPart = 0;
    mClickPart = sb_PartNone;
    Refresh();
    Stop();
}

void GUI_ScrollBar::TimerFired(void)
{
    if (mInPart)
    {
        float vnow, vmin, vmax, vpage;
        vnow = this->GetValue();
        vmin = this->GetMin();
        vmax = this->GetMax();
        vpage = this->GetPageSize();

        switch (mClickPart)
        {
        case sb_PartDownButton:
            vnow = std::max(vmin, vnow - vpage * 0.1f);
            if (vnow != this->GetValue())
                this->SetValue(vnow);
            break;
        case sb_PartDownPage:
            vnow = std::max(vmin, vnow - vpage);
            if (vnow != this->GetValue())
                this->SetValue(vnow);
            break;
        case sb_PartUpPage:
            vnow = std::min(vmax, vnow + vpage);
            if (vnow != this->GetValue())
                this->SetValue(vnow);
            break;
        case sb_PartUpButton:
            vnow = std::min(vmax, vnow + vpage * 0.1f);
            if (vnow != this->GetValue())
                this->SetValue(vnow);
            break;
        }
        if (mClickPart == sb_PartDownButton || mClickPart == sb_PartUpButton)
            Start(0.05);
        else
            Start(0.1);
    }
}

void GUI_ScrollBar::Draw(GUI_GraphState* state)
{

    //	GetMouseLocNow(&x,&y);
    //	if (x > 500)
    //		return;

    int b[4];
    GetBounds(b);
    float bf[6] = {
        (float)b[0], (float)b[1], (float)b[2], (float)b[3], (float)b[2] - (float)b[0], (float)b[3] - (float)b[1]};

    float vnow, vmin, vmax, vpage;
    float b1, b2, t1, t2;

    vnow = this->GetValue();
    vmin = this->GetMin();
    vmax = this->GetMax();
    vpage = this->GetPageSize();
    int alive;

    glColor3f(1, 1, 1);
    if (bf[4] > bf[5])
    {
        // Horizontal scrollbar
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[0], bf[2], bf[4], bf[1], bf[3], bf[5], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);

        int tile_s_bar[4] = {0, 0, 1, 2};
        int bounds_bar[4] = {(int)b1, (int)bf[1], (int)b2, (int)bf[3]};
        GUI_DrawHorizontalStretch(state, "scrollbar_h.png", bounds_bar, tile_s_bar);

        if (alive)
        {
            // thumb
            int thumb_sel[4] = {0, 1, 1, 2};
            int thumb_bnd[4] = {(int)t1, (int)bf[1], (int)t2, (int)bf[3]};
            //			glColor3f(0.0, (mInPart && mClickPart == sb_PartThumb) ? 1.0 : 0.5, 0.0);
            GUI_DrawHorizontalStretch(state, "scrollbar_h.png", thumb_bnd, thumb_sel);
        }

        // Down btn
        int tile_sel[4] = {0, (mInPart && mClickPart == sb_PartDownButton) ? 1 : 0, 2, 2};
        int bounds[4] = {(int)bf[0], (int)bf[1], (int)b1, (int)bf[3]};
        //		glColor3f((mInPart && mClickPart == sb_PartDownButton) ? 0.0 : 1.0, 1.0, 1.0);
        GUI_DrawCentered(state, "scroll_btn_h.png", bounds, 0, 0, tile_sel, NULL, NULL);

        // up button
        int up_sel[4] = {1, (mInPart && mClickPart == sb_PartUpButton) ? 1 : 0, 2, 2};
        int up_bnd[4] = {(int)b2, (int)bf[1], (int)bf[2], (int)bf[3]};
        //		glColor3f((mInPart && mClickPart == sb_PartUpButton) ? 0.0 : 1.0, 1.0, 1.0);
        GUI_DrawCentered(state, "scroll_btn_h.png", up_bnd, 0, 0, up_sel, NULL, NULL);
    }
    else
    {
        // Vertical scrollbar
        alive = SB_BuildMetrix(bf[5] > bf[4], bf[1], bf[3], bf[5], bf[0], bf[2], bf[4], vnow, vmin, vmax, vpage, b1, t1,
                               t2, b2);

        int tile_s_bar[4] = {0, 0, 2, 1};
        int bounds_bar[4] = {(int)bf[0], (int)b1, (int)bf[2], (int)b2};
        GUI_DrawVerticalStretch(state, "scrollbar_v.png", bounds_bar, tile_s_bar);

        if (alive)
        {
            // thumb
            int thumb_sel[4] = {1, 0, 2, 1};
            int thumb_bnd[4] = {(int)bf[0], (int)t1, (int)bf[2], (int)t2};
            //			glColor3f(0.0, (mInPart && mClickPart == sb_PartThumb) ? 1.0 : 0.5, 0.0);
            GUI_DrawVerticalStretch(state, "scrollbar_v.png", thumb_bnd, thumb_sel);
        }

        // Down btn
        int tile_sel[4] = {(mInPart && mClickPart == sb_PartDownButton) ? 1 : 0, 0, 2, 2};
        int bounds[4] = {(int)bf[0], (int)bf[1], (int)bf[2], (int)b1};
        //		glColor3f((mInPart && mClickPart == sb_PartDownButton) ? 0.0 : 1.0, 1.0, 1.0);
        GUI_DrawCentered(state, "scroll_btn_v.png", bounds, 0, 0, tile_sel, NULL, NULL);

        // up button
        int up_sel[4] = {(mInPart && mClickPart == sb_PartUpButton) ? 1 : 0, 1, 2, 2};
        int up_bnd[4] = {(int)bf[0], (int)b2, (int)bf[2], (int)bf[3]};
        //		glColor3f((mInPart && mClickPart == sb_PartUpButton) ? 0.0 : 1.0, 1.0, 1.0);
        GUI_DrawCentered(state, "scroll_btn_v.png", up_bnd, 0, 0, up_sel, NULL, NULL);
    }
}

void GUI_ScrollBar::SetValue(float inValue)
{
    GUI_Control::SetValue(inValue);
    Refresh();
}

void GUI_ScrollBar::SetMin(float inMin)
{
    GUI_Control::SetMin(inMin);
    Refresh();
}

void GUI_ScrollBar::SetMax(float inMax)
{
    GUI_Control::SetMax(inMax);
    Refresh();
}

void GUI_ScrollBar::SetPageSize(float inPageSize)
{
    GUI_Control::SetPageSize(inPageSize);
    Refresh();
}
