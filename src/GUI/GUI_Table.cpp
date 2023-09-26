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

#include "GUI_Table.h"
#include "GUI_Messages.h"

// TODO -- visibility culling?

#if APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#define AUTOSCROLL_DIST 15

/*
    ALIGNMENT CACHING:

    The table is a self-managed scroller: that is, it scrolls by tracking the virtual location of its cells, not by
    simply moving the whole pane around relative to the parent.  (In this sense, the pane bounds are the visible bounds,
    NOT the logical bounds).

    This means that when the content changes, we would need to recalculate the relationship between the lgoical and
   visible bounds.  But...we defer this until someone asks...this helps us avoid doing computation when our content
   changes but we are hidden.  Once we are shown our parent scroller pane will ask where we are, and at that point we do
   the computation.

*/

static bool ClipTo(int pane[4], int cell[4])
{
    int clip[4] = {max(pane[0], cell[0]), max(pane[1], cell[1]), min(pane[2], cell[2]), min(pane[3], cell[3])};

    int w = clip[2] - clip[0];
    int h = clip[3] - clip[1];
    if (w <= 0 || h <= 0)
        return false;

    glScissor(clip[0], clip[1], w, h);
    return true;
}

/************************************************************************************************************
 * MAIN TABLE
 ************************************************************************************************************/

GUI_Table::GUI_Table(int fill_right)
    : mGeometry(NULL), mContent(NULL), mScrollH(0), mScrollV(0),       // hack to start things scrolled up!
      mDragX(-1), mDragY(-1), mExtendSide(fill_right), mAligned(false) // hack - start things scrolled up!
{
}

GUI_Table::~GUI_Table()
{
}

void GUI_Table::SetGeometry(GUI_TableGeometry* inGeometry)
{
    mGeometry = inGeometry;
}

void GUI_Table::SetContent(GUI_TableContent* inContent)
{
    mContent = inContent;
}

void GUI_Table::Draw(GUI_GraphState* state)
{
    if (mGeometry == NULL)
        return;
    if (mContent == NULL)
        return;
    AlignContents();
    glPushAttrib(GL_SCISSOR_BIT);
    glEnable(GL_SCISSOR_TEST);

    int me[4];
    GetVisibleBounds(me);

    int cells[4], cellbounds[4];
    if (CalcVisibleCells(cells))
        for (int y = cells[1]; y < cells[3]; ++y)
            for (int x = cells[0]; x < cells[2]; ++x)
            {
                if (CalcCellBounds(x, y, cellbounds))
                    if (ClipTo(me, cellbounds))
                        mContent->CellDraw(cellbounds, x, y, state);
            }
    glPopAttrib();
}

void GUI_Table::RevealCol(int x)
{
    AlignContents();
    int cell_bounds[4], pane_bounds[6];
    if (!CalcCellBounds(x, 0, cell_bounds))
        return;

    GetBounds(pane_bounds);
    pane_bounds[4] = pane_bounds[2] - pane_bounds[0];
    pane_bounds[5] = pane_bounds[3] - pane_bounds[1];

    if (cell_bounds[0] < pane_bounds[0])
    {
        ScrollH(GetScrollH() + max(cell_bounds[0] - pane_bounds[0], min(0, cell_bounds[2] - pane_bounds[2])));
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
    else if (cell_bounds[2] > pane_bounds[2])
    {
        ScrollH(GetScrollH() + min(cell_bounds[2] - pane_bounds[2], max(0, cell_bounds[0] - pane_bounds[0])));
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
}

void GUI_Table::RevealRow(int y)
{
    AlignContents();
    int cell_bounds[4], pane_bounds[6];
    if (!CalcCellBounds(0, y, cell_bounds))
        return;

    GetBounds(pane_bounds);
    pane_bounds[4] = pane_bounds[2] - pane_bounds[0];
    pane_bounds[5] = pane_bounds[3] - pane_bounds[1];

    if (cell_bounds[1] < pane_bounds[1])
    {
        ScrollV(GetScrollV() + max(cell_bounds[1] - pane_bounds[1], min(0, cell_bounds[3] - pane_bounds[3])));
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
    else if (cell_bounds[3] > pane_bounds[3])
    {
        ScrollV(GetScrollV() + min(cell_bounds[3] - pane_bounds[3], max(0, cell_bounds[1] - pane_bounds[1])));
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
}

void GUI_Table::RevealCell(int x, int y)
{
    AlignContents();
    int cell_bounds[4], pane_bounds[6];
    if (!CalcCellBounds(x, y, cell_bounds))
        return;

    GetBounds(pane_bounds);
    pane_bounds[4] = pane_bounds[2] - pane_bounds[0];
    pane_bounds[5] = pane_bounds[3] - pane_bounds[1];

    int old_h = GetScrollH();
    int old_v = GetScrollV();

    if (cell_bounds[0] < pane_bounds[0])
        ScrollH(GetScrollH() + max(cell_bounds[0] - pane_bounds[0], min(0, cell_bounds[2] - pane_bounds[2])));
    else if (cell_bounds[2] > pane_bounds[2])
        ScrollH(GetScrollH() + min(cell_bounds[2] - pane_bounds[2], max(0, cell_bounds[0] - pane_bounds[0])));

    if (cell_bounds[1] < pane_bounds[1])
        ScrollV(GetScrollV() + max(cell_bounds[1] - pane_bounds[1], min(0, cell_bounds[3] - pane_bounds[3])));
    else if (cell_bounds[3] > pane_bounds[3])
        ScrollV(GetScrollV() + min(cell_bounds[3] - pane_bounds[3], max(0, cell_bounds[1] - pane_bounds[1])));

    if (old_h != GetScrollH() || old_v != GetScrollV())
    {
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
}

int GUI_Table::MouseDown(int x, int y, int button)
{
    if (button == 1)
        return 0;
    if (mGeometry == NULL)
        return 0;
    if (mContent == NULL)
        return 0;
    AlignContents();
    mClickCellX = MouseToCellX(x);
    mClickCellY = MouseToCellY(y);
    if (mClickCellX >= 0 && mClickCellX < mGeometry->GetColCount() && mClickCellY >= 0 &&
        mClickCellY < mGeometry->GetRowCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(mClickCellX, mClickCellY, cellbounds))
            if (mContent->CellMouseDown(cellbounds, mClickCellX, mClickCellY, x, y, button, this->GetModifiersNow(),
                                        mLocked))
                return 1;
    }
    return 0;
}

void GUI_Table::MouseDrag(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mContent == NULL)
        return;
    AlignContents();
    if (!mLocked)
    {
#if BENTODO
        auto scroll ? clamp this to keep all hell from breaking loose ? ?
#endif
                                                                        mClickCellX = MouseToCellX(x);
        mClickCellY = MouseToCellY(y);
    }
    int cellbounds[4];
    if (CalcCellBounds(mClickCellX, mClickCellY, cellbounds))
        mContent->CellMouseDrag(cellbounds, mClickCellX, mClickCellY, x, y, button);
}

void GUI_Table::MouseUp(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mContent == NULL)
        return;
    AlignContents();
    if (!mLocked)
    {
        mClickCellX = MouseToCellX(x);
        mClickCellY = MouseToCellY(y);
    }
    int cellbounds[4];
    if (CalcCellBounds(mClickCellX, mClickCellY, cellbounds))
        mContent->CellMouseUp(cellbounds, mClickCellX, mClickCellY, x, y, button);
}

int GUI_Table::TrapNotify(int x, int y, int button)
{
    int b[4];
    GetVisibleBounds(b);
    if (x < b[0] || x > b[2] || y < b[1] || y > b[3])
    {
        if (mContent)
            mContent->KillEditing(true);
        return 0;
    }
    return 1;
}

int GUI_Table::GetCursor(int x, int y)
{
    if (mGeometry == NULL)
        return gui_Cursor_None;
    if (mContent == NULL)
        return gui_Cursor_None;
    AlignContents();
    int cx = MouseToCellX(x);
    int cy = MouseToCellY(y);
    if (cx >= 0 && cx < mGeometry->GetColCount() && cy >= 0 && cy < mGeometry->GetRowCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(cx, cy, cellbounds))
            return mContent->CellGetCursor(cellbounds, cx, cy, x, y);
    }
    return gui_Cursor_None;
}

int GUI_Table::GetHelpTip(int x, int y, int tip_bounds[4], std::string& tip)
{
    if (mGeometry == NULL)
        return 0;
    if (mContent == NULL)
        return 0;
    AlignContents();
    int mx = MouseToCellX(x);
    int my = MouseToCellY(y);
    if (mx >= 0 && mx < mGeometry->GetColCount() && my >= 0 && my < mGeometry->GetRowCount())
    {
        if (CalcCellBounds(mx, my, tip_bounds))
            return mContent->CellGetHelpTip(tip_bounds, mx, my, x, y, tip);
    }
    return 0;
}

GUI_DragOperation GUI_Table::DragEnter(int x, int y, GUI_DragData* drag, GUI_DragOperation allowed,
                                       GUI_DragOperation recommended)
{
    if (mGeometry == NULL)
        return gui_Drag_None;
    if (mContent == NULL)
        return gui_Drag_None;
    AlignContents();

    mDragX = MouseToCellX(x);
    mDragY = MouseToCellY(y);
    if (mDragX >= 0 && mDragX < mGeometry->GetColCount() && mDragY >= 0 && mDragY < mGeometry->GetRowCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(mDragX, mDragY, cellbounds))
            return mContent->CellDragEnter(cellbounds, mDragX, mDragY, x, y, drag, allowed, recommended);
    }
    mDragX = -1;
    mDragY = -1;
    return gui_Drag_None;
}

GUI_DragOperation GUI_Table::DragOver(int x, int y, GUI_DragData* drag, GUI_DragOperation allowed,
                                      GUI_DragOperation recommended)
{
    if (mGeometry == NULL)
        return gui_Drag_None;
    if (mContent == NULL)
        return gui_Drag_None;
    AlignContents();

    int new_x = MouseToCellX(x);
    int new_y = MouseToCellY(y);
    int cellbounds[4];

    if (new_x < 0 || new_x >= mGeometry->GetColCount() || new_y < 0 || new_y >= mGeometry->GetRowCount())
        new_x = new_y = -1;

    if (new_x != mDragX || new_y != mDragY)
    {
        if (mDragX != -1 && mDragY != -1)
        {
            if (CalcCellBounds(mDragX, mDragY, cellbounds))
                mContent->CellDragLeave(cellbounds, mDragX, mDragY);
        }
        mDragX = new_x;
        mDragY = new_y;
        if (mDragX != -1 && mDragY != -1)
        {
            if (CalcCellBounds(mDragX, mDragY, cellbounds))
                return mContent->CellDragEnter(cellbounds, mDragX, mDragY, x, y, drag, allowed, recommended);
        }
    }
    else
    {
        if (mDragX != -1 && mDragY != -1)
        {
            if (CalcCellBounds(mDragX, mDragY, cellbounds))
                return mContent->CellDragWithin(cellbounds, mDragX, mDragY, x, y, drag, allowed, recommended);
        }
    }
    mDragX = mDragY = -1;
    return gui_Drag_None;
}

void GUI_Table::DragScroll(int x, int y)
{
    AlignContents();
    int me[4];
    GetBounds(me);
    float total[4], vis[4];
    GetScrollBounds(total, vis);

    int old_h = GetScrollH();
    int old_v = GetScrollV();

    int max_left = max(vis[0] - total[0], 0.0f);
    int max_right = max(total[2] - vis[2], 0.0f);
    int max_bottom = max(vis[1] - total[1], 0.0f);
    int max_top = max(total[3] - vis[3], 0.0f);

    int speed_left = me[0] - x + AUTOSCROLL_DIST;
    int speed_right = x - me[2] + AUTOSCROLL_DIST;
    int speed_bottom = me[1] - y + AUTOSCROLL_DIST;
    int speed_top = y - me[3] + AUTOSCROLL_DIST;

    speed_left = min(AUTOSCROLL_DIST, max(0, speed_left));
    speed_right = min(AUTOSCROLL_DIST, max(0, speed_right));
    speed_bottom = min(AUTOSCROLL_DIST, max(0, speed_bottom));
    speed_top = min(AUTOSCROLL_DIST, max(0, speed_top));

    ScrollH(GetScrollH() - min(speed_left, max_left));
    ScrollH(GetScrollH() + min(speed_right, max_right));
    ScrollV(GetScrollV() - min(speed_bottom, max_bottom));
    ScrollV(GetScrollV() + min(speed_top, max_top));

    if (old_h != GetScrollH() || old_v != GetScrollV())
    {
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
    }
}

void GUI_Table::DragLeave(void)
{
    if (mGeometry == NULL)
        return;
    if (mContent == NULL)
        return;
    AlignContents();

    int cellbounds[4];
    if (mDragX != -1 && mDragY != -1)
    {
        if (CalcCellBounds(mDragX, mDragY, cellbounds))
            mContent->CellDragLeave(cellbounds, mDragX, mDragY);
    }
    mDragX = -1;
    mDragY = -1;
}

GUI_DragOperation GUI_Table::Drop(int x, int y, GUI_DragData* drag, GUI_DragOperation allowed,
                                  GUI_DragOperation recommended)
{
    if (mGeometry == NULL)
        return gui_Drag_None;
    if (mContent == NULL)
        return gui_Drag_None;
    AlignContents();

    int new_x = MouseToCellX(x);
    int new_y = MouseToCellY(y);
    int cellbounds[4];

    if (new_x >= 0 && new_x < mGeometry->GetColCount() && new_y >= 0 && new_y < mGeometry->GetRowCount())
        if (CalcCellBounds(mDragX, mDragY, cellbounds))
            return mContent->CellDrop(cellbounds, new_x, new_y, x, y, drag, allowed, recommended);
    return gui_Drag_None;
}

void GUI_Table::SetBounds(int x1, int y1, int x2, int y2)
{
    int b[4];
    GetBounds(b);
    int new_height = y2 - y1;
    int old_height = b[3] - b[1];
    int delta_y = new_height - old_height;
    ScrollV(GetScrollV() - delta_y);
    GUI_Pane::SetBounds(x1, y1, x2, y2);
    mAligned = false;
}

void GUI_Table::SetBounds(int inBounds[4])
{
    int b[4];
    GetBounds(b);
    int new_height = inBounds[3] - inBounds[1];
    int old_height = b[3] - b[1];
    int delta_y = new_height - old_height;
    ScrollV(GetScrollV() - delta_y);
    GUI_Pane::SetBounds(inBounds);
    mAligned = false;
}

void GUI_Table::AlignContents(void)
{
    if (mAligned)
        return;
    mAligned = true;
    float vis[4], total[4];

    GetScrollBounds(total, vis);
    if (total[1] > vis[1])
        ScrollV(GetScrollV() - (vis[1] - total[1]));

    GetScrollBounds(total, vis);
    if (total[3] < vis[3])
        ScrollV(GetScrollV() - (vis[3] - total[3]));

    GetScrollBounds(total, vis);
    if (total[2] < vis[2])
        ScrollH(GetScrollH() - (vis[2] - total[2]));

    GetScrollBounds(total, vis);
    if (total[0] > vis[0])
        ScrollH(GetScrollH() - (vis[0] - total[0]));
}

void GUI_Table::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    switch (inMsg)
    {
    case GUI_TABLE_SHAPE_RESIZED:
    case GUI_TABLE_CONTENT_RESIZED:
        // would be nice to do this:
        //	int b[4];
        //	GetBounds(b);
        //	int new_height = inBounds[3] - inBounds[1];
        //	int old_height = b[3] - b[1];
        //	int delta_y = new_height - old_height;
        //	GetScrollV() -= delta_y;
        mAligned = false;
        if (mContent)
            mContent->KillEditing(false);
        BroadcastMessage(GUI_SCROLL_CONTENT_SIZE_CHANGED, 0);
        Refresh();
        break;
    case GUI_TABLE_CONTENT_CHANGED:
        Refresh();
        break;
    }
}

int GUI_Table::GetScrollH(void)
{
    return mScrollH;
}

int GUI_Table::GetScrollV(void)
{
    return mGeometry->GetCellTop(mGeometry->GetRowCount() - 1) - mScrollV;
}

void GUI_Table::GetScrollBounds(float outTotalBounds[4], float outVisibleBounds[4])
{
    AlignContents();
    int b[4];
    GetBounds(b);

    outVisibleBounds[0] = b[0];
    outVisibleBounds[1] = b[1];
    outVisibleBounds[2] = b[2];
    outVisibleBounds[3] = b[3];

    if (mGeometry == NULL)
    {
        outTotalBounds[0] = b[0];
        outTotalBounds[1] = b[1];
        outTotalBounds[2] = b[2];
        outTotalBounds[3] = b[3];
        return;
    }

    int rc = mGeometry->GetRowCount();
    int cc = mGeometry->GetColCount();

    outTotalBounds[0] = b[0] - GetScrollH();
    outTotalBounds[1] = b[1] - GetScrollV();
    outTotalBounds[2] = b[0] - GetScrollH() + (cc ? mGeometry->GetCellRight(cc - 1) : 0);
    outTotalBounds[3] = b[1] - GetScrollV() + (rc ? mGeometry->GetCellTop(rc - 1) : 0);
}

void GUI_Table::ScrollH(float xOffset)
{
    if (mContent)
        mContent->KillEditing(true);
    mScrollH = xOffset;
}

void GUI_Table::ScrollV(float yOffset)
{
    if (mContent)
        mContent->KillEditing(true);
    mScrollV = mGeometry->GetCellTop(mGeometry->GetRowCount() - 1) - yOffset;
}

int GUI_Table::MouseToCellX(int x)
{
    if (mGeometry == NULL)
        return -1;
    AlignContents();
    int b[4];
    GetBounds(b);
    return mGeometry->ColForX(x - b[0] + GetScrollH());
}

int GUI_Table::MouseToCellY(int y)
{
    if (mGeometry == NULL)
        return -1;
    AlignContents();
    int b[4];
    GetBounds(b);
    return mGeometry->RowForY(y - b[1] + GetScrollV());
}

int GUI_Table::CalcVisibleCells(int bounds[4])
{
    if (mGeometry == NULL)
        return 0;
    int b[4];
    int l[4];
    GetBounds(b);
    AlignContents();

    int xc = mGeometry->GetColCount();
    int yc = mGeometry->GetRowCount();
    if (xc == 0 || yc == 0)
        return 0;

    l[0] = GetScrollH();
    l[1] = GetScrollV();
    l[2] = GetScrollH() + b[2] - b[0];
    l[3] = GetScrollV() + b[3] - b[1];

    bounds[0] = mGeometry->ColForX(l[0]);
    bounds[1] = mGeometry->RowForY(l[1]);
    bounds[2] = mGeometry->ColForX(l[2] - 1) + 1;
    bounds[3] = mGeometry->RowForY(l[3] - 1) + 1;

    if (bounds[0] < 0)
        bounds[0] = 0;
    if (bounds[1] < 0)
        bounds[1] = 0;
    if (bounds[2] < 0)
        bounds[2] = 0;
    if (bounds[3] < 0)
        bounds[3] = 0;
    if (bounds[0] >= xc)
        bounds[0] = xc - 1;
    if (bounds[1] >= yc)
        bounds[1] = yc - 1;
    if (bounds[2] > xc)
        bounds[2] = xc;
    if (bounds[3] > yc)
        bounds[3] = yc;

    if (bounds[0] >= bounds[2] || bounds[1] >= bounds[3])
        return 0;

    return 1;
}

int GUI_Table::CalcCellBounds(int x, int y, int bounds[4])
{
    if (mGeometry == NULL)
        return 0;
    AlignContents();
    int b[4];
    GetBounds(b);

    bounds[0] = mGeometry->GetCellLeft(x) + b[0] - GetScrollH();
    bounds[1] = mGeometry->GetCellBottom(y) + b[1] - GetScrollV();
    bounds[2] = mGeometry->GetCellRight(x) + b[0] - GetScrollH();
    bounds[3] = mGeometry->GetCellTop(y) + b[1] - GetScrollV();

    if (mExtendSide)
        if (x == mGeometry->GetColCount() - 1 && bounds[2] < b[2])
            bounds[2] = b[2];

    return 1;
}

void GUI_Table::SizeShowAll(void)
{
    if (!mGeometry)
        return;
    AlignContents();
    int b[4];
    GetBounds(b);
    b[2] = b[0] + mGeometry->GetCellLeft(mGeometry->GetColCount()) - mGeometry->GetCellLeft(0);
    b[3] = b[1] + mGeometry->GetCellBottom(mGeometry->GetRowCount()) - mGeometry->GetCellBottom(0);
    SetBounds(b);
}

/************************************************************************************************************
 * HEADER
 ************************************************************************************************************/
#pragma mark -

GUI_Header::GUI_Header(int fill_right) : mGeometry(NULL), mHeader(NULL), mTable(NULL), mExtendSide(fill_right)
{
}

GUI_Header::~GUI_Header()
{
}

void GUI_Header::SetGeometry(GUI_TableGeometry* inGeometry)
{
    mGeometry = inGeometry;
}

void GUI_Header::SetHeader(GUI_TableHeader* inHeader)
{
    mHeader = inHeader;
}

void GUI_Header::SetTable(GUI_Table* inTable)
{
    mTable = inTable;
}

void GUI_Header::Draw(GUI_GraphState* state)
{
    if (mGeometry == NULL)
        return;
    if (mHeader == NULL)
        return;

    int me[4];
    GetVisibleBounds(me);

    glPushAttrib(GL_SCISSOR_BIT);
    glEnable(GL_SCISSOR_TEST);

    int cells[2], cellbounds[4];
    if (CalcVisibleCells(cells))
        for (int x = cells[0]; x < cells[1]; ++x)
        {
            if (CalcCellBounds(x, cellbounds))
                if (ClipTo(me, cellbounds))
                    mHeader->HeadDraw(cellbounds, x, state);
        }

    glPopAttrib();
}

int GUI_Header::MouseDown(int x, int y, int button)
{
    if (mGeometry == NULL)
        return 0;
    if (mHeader == NULL)
        return 0;
    mClickCellX = MouseToCellX(x);
    if (mClickCellX >= 0 && mClickCellX < mGeometry->GetColCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(mClickCellX, cellbounds))
            if (mHeader->HeadMouseDown(cellbounds, mClickCellX, x, y, button, this->GetModifiersNow(), mLocked))
                return 1;
    }
    return 0;
}

void GUI_Header::MouseDrag(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mHeader == NULL)
        return;
    int cellbounds[4];
    if (!mLocked)
        mClickCellX = MouseToCellX(x);

    if (CalcCellBounds(mClickCellX, cellbounds))
        mHeader->HeadMouseDrag(cellbounds, mClickCellX, x, y, button);
}

void GUI_Header::MouseUp(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mHeader == NULL)
        return;
    int cellbounds[4];
    if (!mLocked)
        mClickCellX = MouseToCellX(x);
    if (CalcCellBounds(mClickCellX, cellbounds))
        mHeader->HeadMouseUp(cellbounds, mClickCellX, x, y, button);
}

int GUI_Header::GetCursor(int x, int y)
{
    if (mGeometry == NULL)
        return gui_Cursor_None;
    if (mHeader == NULL)
        return gui_Cursor_None;
    int cx = MouseToCellX(x);
    if (cx >= 0 && cx < mGeometry->GetColCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(cx, cellbounds))
            return mHeader->HeadGetCursor(cellbounds, cx, x, y);
    }
    return gui_Cursor_None;
}

int GUI_Header::GetHelpTip(int x, int y, int tip_bounds[4], std::string& tip)
{
    if (mGeometry == NULL)
        return 0;
    if (mHeader == NULL)
        return 0;
    int cx = MouseToCellX(x);
    if (cx >= 0 && cx < mGeometry->GetColCount())
    {
        if (CalcCellBounds(cx, tip_bounds))
            return mHeader->HeadGetHelpTip(tip_bounds, cx, x, y, tip);
    }
    return 0;
}

void GUI_Header::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    switch (inMsg)
    {
    case GUI_TABLE_SHAPE_RESIZED:
    case GUI_TABLE_CONTENT_RESIZED:
        Refresh();
        break;
    }
}
int GUI_Header::MouseToCellX(int x)
{
    if (mGeometry == NULL)
        return -1;
    if (mTable == NULL)
        return -1;
    int b[4];
    GetBounds(b);
    return mGeometry->ColForX(x - b[0] + mTable->GetScrollH());
}

int GUI_Header::CalcVisibleCells(int bounds[2])
{
    if (mGeometry == NULL)
        return 0;
    if (mTable == NULL)
        return 0;

    int xc = mGeometry->GetColCount();
    if (xc == 0)
        return 0;

    int b[4];
    int l[2];
    GetBounds(b);
    l[0] = mTable->GetScrollH();
    l[1] = mTable->GetScrollH() + b[2] - b[0];

    bounds[0] = mGeometry->ColForX(l[0]);
    bounds[1] = mGeometry->ColForX(l[1] - 1) + 1;

    if (bounds[0] < 0)
        bounds[0] = 0;
    if (bounds[1] < 0)
        bounds[1] = 0;
    if (bounds[0] >= xc)
        bounds[0] = xc - 1;
    if (bounds[1] > xc)
        bounds[1] = xc;

    if (bounds[0] >= bounds[1])
        return 0;
    return 1;
}

int GUI_Header::CalcCellBounds(int x, int bounds[4])
{
    if (mGeometry == NULL)
        return 0;
    int b[4];
    GetBounds(b);

    bounds[0] = mGeometry->GetCellLeft(x) + b[0] - mTable->GetScrollH();
    bounds[1] = b[1];
    bounds[2] = mGeometry->GetCellRight(x) + b[0] - mTable->GetScrollH();
    bounds[3] = b[3];

    if (mExtendSide)
        if (x == mGeometry->GetColCount() - 1 && bounds[2] < b[2])
            bounds[2] = b[2];

    return 1;
}

/************************************************************************************************************
 * SIDE
 ************************************************************************************************************/
#pragma mark -

GUI_Side::GUI_Side() : mGeometry(NULL), mSide(NULL), mTable(NULL), mWideY(-1)
{
}

GUI_Side::~GUI_Side()
{
}

void GUI_Side::SetGeometry(GUI_TableGeometry* inGeometry)
{
    mGeometry = inGeometry;
}

void GUI_Side::SetSide(GUI_TableSide* inSide)
{
    mSide = inSide;
}

void GUI_Side::SetTable(GUI_Table* inTable)
{
    mTable = inTable;
}

void GUI_Side::Draw(GUI_GraphState* state)
{
    if (mGeometry == NULL)
        return;
    if (mSide == NULL)
        return;

    int me[4];
    GetVisibleBounds(me);

    glPushAttrib(GL_SCISSOR_BIT);
    glEnable(GL_SCISSOR_TEST);

    int cells[2], cellbounds[4];
    if (CalcVisibleCells(cells))
    {
        for (int y = cells[0]; y < cells[1]; ++y)
        {
            if (CalcCellBounds(y, cellbounds))
            {
                if (y == mWideY) // make cell wider to show full text
                {
                    cellbounds[2] += 80;
                    me[2] += 80;
                }
                if (ClipTo(me, cellbounds))
                    mSide->SideDraw(cellbounds, y, state);
            }
        }
    }
    glPopAttrib();
}

int GUI_Side::MouseDown(int x, int y, int button)
{
    if (mGeometry == NULL)
        return 0;
    if (mSide == NULL)
        return 0;

    mClickCellY = MouseToCellY(y);

    if (mClickCellY >= 0 && mClickCellY < mGeometry->GetRowCount())
    {
        mWideY = mClickCellY;
        Refresh();

        int cellbounds[4];
        if (CalcCellBounds(mClickCellY, cellbounds))
            if (mSide->SideMouseDown(cellbounds, mClickCellY, x, y, button, this->GetModifiersNow(), mLocked))
                return 1;
    }
    return 0;
}

void GUI_Side::MouseDrag(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mSide == NULL)
        return;

    if (mWideY >= 0)
    {
        mWideY = -1;
        Refresh();
    }

    if (!mLocked)
        mClickCellY = MouseToCellY(y);

    int cellbounds[4];
    if (CalcCellBounds(mClickCellY, cellbounds))
        mSide->SideMouseDrag(cellbounds, mClickCellY, x, y, button);
}

void GUI_Side::MouseUp(int x, int y, int button)
{
    if (mGeometry == NULL)
        return;
    if (mSide == NULL)
        return;

    if (mWideY >= 0)
    {
        mWideY = -1;
        Refresh();
    }

    int cellbounds[4];
    if (!mLocked)
        mClickCellY = MouseToCellY(y);
    if (CalcCellBounds(mClickCellY, cellbounds))
        mSide->SideMouseUp(cellbounds, mClickCellY, x, y, button);
}

int GUI_Side::GetCursor(int x, int y)
{
    if (mGeometry == NULL)
        return gui_Cursor_None;
    if (mSide == NULL)
        return gui_Cursor_None;
    int cy = MouseToCellY(x);
    if (cy >= 0 && cy < mGeometry->GetRowCount())
    {
        int cellbounds[4];
        if (CalcCellBounds(cy, cellbounds))
            return mSide->SideGetCursor(cellbounds, cy, x, y);
    }
    return gui_Cursor_None;
}

int GUI_Side::GetHelpTip(int x, int y, int tip_bounds[4], std::string& tip)
{
    if (mGeometry == NULL)
        return 0;
    if (mSide == NULL)
        return 0;
    int cy = MouseToCellY(y);
    if (cy >= 0 && cy < mGeometry->GetRowCount())
    {
        if (CalcCellBounds(cy, tip_bounds))
            return mSide->SideGetHelpTip(tip_bounds, cy, x, y, tip);
    }
    return 0;
}

void GUI_Side::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    switch (inMsg)
    {
    case GUI_TABLE_SHAPE_RESIZED:
    case GUI_TABLE_CONTENT_RESIZED:
        Refresh();
        break;
    }
}
int GUI_Side::MouseToCellY(int y)
{
    if (mGeometry == NULL)
        return -1;
    if (mTable == NULL)
        return -1;
    int b[4];
    GetBounds(b);
    return mGeometry->RowForY(y - b[1] + mTable->GetScrollV());
}

int GUI_Side::CalcVisibleCells(int bounds[2])
{
    if (mGeometry == NULL)
        return 0;
    if (mTable == NULL)
        return 0;

    int yc = mGeometry->GetRowCount();
    if (yc == 0)
        return 0;

    int b[4];
    int t[2];
    GetBounds(b);
    t[0] = mTable->GetScrollV();
    t[1] = mTable->GetScrollV() + b[3] - b[1];

    bounds[0] = mGeometry->RowForY(t[0]);
    bounds[1] = mGeometry->RowForY(t[1] - 1) + 1;

    if (bounds[0] < 0)
        bounds[0] = 0;
    if (bounds[1] < 0)
        bounds[1] = 0;
    if (bounds[0] >= yc)
        bounds[0] = yc - 1;
    if (bounds[1] > yc)
        bounds[1] = yc;

    if (bounds[0] >= bounds[1])
        return 0;

    return 1;
}

int GUI_Side::CalcCellBounds(int y, int bounds[4])
{
    if (mGeometry == NULL)
        return 0;
    int b[4];
    GetBounds(b);

    bounds[0] = b[0];
    bounds[1] = mGeometry->GetCellBottom(y) + b[1] - mTable->GetScrollV();
    bounds[2] = b[2];
    bounds[3] = mGeometry->GetCellTop(y) + b[1] - mTable->GetScrollV();

    return 1;
}
