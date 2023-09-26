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

#include "WED_PackageListAdapter.h"
#include "WED_PackageMgr.h"
#include "WED_Messages.h"
#include "GUI_Messages.h"
#include "WED_Menus.h"

static int kDefCols[] = {85, 115, 300};

WED_PackageListAdapter::WED_PackageListAdapter(GUI_Commander* cmd_target)
    : GUI_SimpleTableGeometry(3, kDefCols), mSel(-1), mCmdTarget(cmd_target)

{
    gPackageMgr->AddListener(this);
}

WED_PackageListAdapter::~WED_PackageListAdapter()
{
}

void WED_PackageListAdapter::GetCellContent(int cell_x, int cell_y, GUI_CellContent& the_content)
{
    the_content.content_type = gui_Cell_EditText;
    the_content.text_val = "";
    the_content.can_disclose = 0;
    the_content.string_is_resource = 0;
    the_content.can_drag = 0;
    the_content.is_disclosed = 0;
    the_content.is_selected = cell_y == mSel;
    the_content.indent_level = 0;
    the_content.can_select = 1;
    the_content.can_edit = 0;
    the_content.can_delete = false;

    int n_pkg = gPackageMgr->CountCustomPackages() - cell_y - 1;
    switch (cell_x)
    {
    case 0:
        if (gPackageMgr->IsDisabled(n_pkg))
            the_content.text_val = "Disabled";
        break;
    case 1:
        if (gPackageMgr->HasXML(n_pkg) || gPackageMgr->HasAPT(n_pkg))
        {
            if (gPackageMgr->HasXML(n_pkg))
                the_content.text_val = "WED Airport";
            else
                the_content.text_val = "Airport";
        }
        else if (gPackageMgr->HasLibrary(n_pkg))
            the_content.text_val = "Library";
        break;
    default:
        gPackageMgr->GetNthPackageName(n_pkg, the_content.text_val);
        the_content.can_edit = mLock.count(the_content.text_val) == 0;
    }
}

void WED_PackageListAdapter::GetEnumDictionary(int cell_x, int cell_y, GUI_EnumDictionary& out_dictionary)
{
}

void WED_PackageListAdapter::AcceptEdit(int cell_x, int cell_y, const GUI_CellContent& the_content, int apply_all)
{
    gPackageMgr->RenameCustomPackage(gPackageMgr->CountCustomPackages() - cell_y - 1, the_content.text_val);
}

void WED_PackageListAdapter::ToggleDisclose(int cell_x, int cell_y)
{
}

void WED_PackageListAdapter::DoDrag(GUI_Pane* drag_emitter, int mouse_x, int mouse_y, int button, int bounds[4])
{
}

void WED_PackageListAdapter::SelectionStart(int clear)
{
    if (clear)
        mSel = -1;
}

int WED_PackageListAdapter::SelectGetExtent(int& low_x, int& low_y, int& high_x, int& high_y)
{
    low_x = high_x = 2;
    low_y = high_y = mSel;
    return mSel != -1;
}

int WED_PackageListAdapter::SelectGetLimits(int& low_x, int& low_y, int& high_x, int& high_y)
{

    low_x = low_y = 0;
    high_x = 1;
    high_y = gPackageMgr->CountCustomPackages() - 1;

    return high_y != -1;
}

void WED_PackageListAdapter::SelectRange(int start_x, int start_y, int end_x, int end_y, int is_toggle)
{
    mSel = start_y;
    BroadcastMessage(GUI_TABLE_CONTENT_CHANGED, 0);
}

void WED_PackageListAdapter::SelectionEnd(void)
{
}

int WED_PackageListAdapter::SelectDisclose(int open_it, int all)
{
    return 0;
}

int WED_PackageListAdapter::TabAdvance(int& io_x, int& io_y, int reverse, GUI_CellContent& the_content)
{
    bool need_sync = mSel == io_y;
    if (reverse < 0)
        ++io_y;
    else if (reverse > 0)
        --io_y;
    if (io_y >= gPackageMgr->CountCustomPackages())
        io_y = 0;
    if (io_y < 0)
        io_y = gPackageMgr->CountCustomPackages() - 1;
    if (need_sync)
        mSel = io_y;
    GetCellContent(io_x, io_y, the_content);
    return 1;
}

int WED_PackageListAdapter::DoubleClickCell(int cell_x, int cell_y)
{
    mCmdTarget->DispatchHandleCommand(wed_OpenPackage);
    return 1;
}

void WED_PackageListAdapter::GetLegalDropOperations(int& allow_between_col, int& allow_between_row,
                                                    int& allow_into_cell)
{
    allow_between_col = allow_between_row = allow_into_cell = 0;
}

GUI_DragOperation WED_PackageListAdapter::CanDropIntoCell(int cell_x, int cell_y, GUI_DragData* drag,
                                                          GUI_DragOperation allowed, GUI_DragOperation recommended,
                                                          int& whole_col, int& whole_row)
{
    return gui_Drag_None;
}

GUI_DragOperation WED_PackageListAdapter::CanDropBetweenColumns(int cell_x, GUI_DragData* drag,
                                                                GUI_DragOperation allowed,
                                                                GUI_DragOperation recommended)
{
    return gui_Drag_None;
}

GUI_DragOperation WED_PackageListAdapter::CanDropBetweenRows(int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                                             GUI_DragOperation recommended)
{
    return gui_Drag_None;
}

GUI_DragOperation WED_PackageListAdapter::DoDropIntoCell(int cell_x, int cell_y, GUI_DragData* drag,
                                                         GUI_DragOperation allowed, GUI_DragOperation recommended)
{
    return gui_Drag_None;
}

GUI_DragOperation WED_PackageListAdapter::DoDropBetweenColumns(int cell_x, GUI_DragData* drag,
                                                               GUI_DragOperation allowed, GUI_DragOperation recommended)
{
    return gui_Drag_None;
}

GUI_DragOperation WED_PackageListAdapter::DoDropBetweenRows(int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                                            GUI_DragOperation recommended)
{
    return gui_Drag_None;
}

#pragma mark -

int WED_PackageListAdapter::GetColCount(void)
{
    return 3;
}

int WED_PackageListAdapter::GetRowCount(void)
{
    return gPackageMgr->CountCustomPackages();
}

void WED_PackageListAdapter::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    if (inMsg == msg_SystemFolderChanged || inMsg == msg_SystemFolderUpdated)
        BroadcastMessage(GUI_TABLE_CONTENT_RESIZED, 0);
}

bool WED_PackageListAdapter::HasSelection(void)
{
    return mSel >= 0 && mSel < gPackageMgr->CountCustomPackages();
}

int WED_PackageListAdapter::GetSelection(std::string* package)
{
    if (package)
        gPackageMgr->GetNthPackageName(gPackageMgr->CountCustomPackages() - 1 - mSel, *package);
    return gPackageMgr->CountCustomPackages() - 1 - mSel;
}

void WED_PackageListAdapter::SelectPackage(int n)
{
    mSel = gPackageMgr->CountCustomPackages() - 1 - n;
    BroadcastMessage(GUI_TABLE_CONTENT_CHANGED, 0);
}

int WED_PackageListAdapter::SelectPackage(const std::string& package)
{
    std::string name;
    int cnt = gPackageMgr->CountCustomPackages();
    for (unsigned int i = 0; i < cnt; ++i)
    {
        gPackageMgr->GetNthPackageName(i, name);
        if (name == package)
        {
            SelectPackage(i);
            return i;
        }
    }

    return -1;
}

void WED_PackageListAdapter::LockPackage(const std::string& name)
{
    mLock.insert(name);
}

void WED_PackageListAdapter::UnlockPackage(const std::string& name)
{
    mLock.erase(name);
}
