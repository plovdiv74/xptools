/*
 * Copyright (c) 2012, Laminar Research.
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

#ifndef GUI_FILTERBAR_H
#define GUI_FILTERBAR_H

#include "GUI_Table.h"
#include "GUI_TextTable.h"
#include "GUI_SimpleTableGeometry.h"

#include "AssertUtils.h"

class GUI_FilterBar : public GUI_Table, public GUI_TextTableProvider, public GUI_SimpleTableGeometry
{
public:
    GUI_FilterBar(GUI_Commander* cmdr, intptr_t in_msg, intptr_t in_param, const std::string& in_label,
                  const std::string& in_def, bool in_have_enum_dict, int default_enum_val = 0);

    std::string GetText(void)
    {
        return mText;
    }
    std::string GetEnumText(void)
    {
        DebugAssert(mHaveEnumDict == true);
        return mCurEnumTxt;
    }
    int GetEnumValue(void)
    {
        DebugAssert(mHaveEnumDict == true);
        return mCurEnumVal;
    }
    // GUI_SimpleTableGeometry
    virtual int GetColCount(void);
    virtual int GetRowCount(void);

    // GUI_TextTableProvider
    virtual void GetCellContent(int cell_x, int cell_y, GUI_CellContent& the_content);
    virtual void GetEnumDictionary(int cell_x, int cell_y, GUI_EnumDictionary& out_dictionary){};
    virtual void AcceptEdit(int cell_x, int cell_y, const GUI_CellContent& the_content, int apply_all);
    virtual void ToggleDisclose(int cell_x, int cell_y)
    {
    }
    virtual void DoDeleteCell(int cell_x, int cell_y)
    {
    }
    virtual void DoDrag(GUI_Pane* drag_emitter, int mouse_x, int mouse_y, int button, int bounds[4])
    {
    }

    virtual void SelectionStart(int clear)
    {
    }
    virtual int SelectGetExtent(int& low_x, int& low_y, int& high_x, int& high_y)
    {
        return 0;
    }
    virtual int SelectGetLimits(int& low_x, int& low_y, int& high_x, int& high_y)
    {
        return 0;
    }
    virtual void SelectRange(int start_x, int start_y, int end_x, int end_y, int is_toggle)
    {
    }
    virtual void SelectionEnd(void)
    {
    }
    virtual int SelectDisclose(int open_it, int all)
    {
        return 0;
    }

    virtual int TabAdvance(int& io_x, int& io_y, int dir, GUI_CellContent& the_content)
    {
        return 0;
    }
    virtual int DoubleClickCell(int cell_x, int cell_y)
    {
        return 0;
    }

    virtual void GetLegalDropOperations(int& allow_between_col, int& allow_between_row, int& allow_into_cell)
    {
        allow_between_col = allow_between_row = allow_into_cell = 0;
    }
    virtual GUI_DragOperation CanDropIntoCell(int cell_x, int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                              GUI_DragOperation recommended, int& whole_col, int& whole_row)
    {
        return gui_Drag_None;
    }
    virtual GUI_DragOperation CanDropBetweenColumns(int cell_x, GUI_DragData* drag, GUI_DragOperation allowed,
                                                    GUI_DragOperation recommended)
    {
        return gui_Drag_None;
    }
    virtual GUI_DragOperation CanDropBetweenRows(int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                                 GUI_DragOperation recommended)
    {
        return gui_Drag_None;
    }

    virtual GUI_DragOperation DoDropIntoCell(int cell_x, int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                             GUI_DragOperation recommended)
    {
        return gui_Drag_None;
    }
    virtual GUI_DragOperation DoDropBetweenColumns(int cell_x, GUI_DragData* drag, GUI_DragOperation allowed,
                                                   GUI_DragOperation recommended)
    {
        return gui_Drag_None;
    }
    virtual GUI_DragOperation DoDropBetweenRows(int cell_y, GUI_DragData* drag, GUI_DragOperation allowed,
                                                GUI_DragOperation recommended)
    {
        return gui_Drag_None;
    }

    void ClearFilter();

protected:
    std::string GetEnumText() const
    {
        return mCurEnumTxt;
    }
    int GetEnumValue() const
    {
        return mCurEnumVal;
    }
    bool GetHaveEnumDict() const
    {
        return mHaveEnumDict;
    }

private:
    // current pack one that is selected (used for GetCellContents)
    // Data from AcceptEdit
    std::string mCurEnumTxt;
    int mCurEnumVal;

    // Decides if it should have enum dictionary
    bool mHaveEnumDict;

    // Label for the search bar
    std::string mLabel;

    // Text inside the search bar
    std::string mText;

    intptr_t mMsg;
    intptr_t mParam;
    GUI_TextTable mTextTable;
};

#endif
