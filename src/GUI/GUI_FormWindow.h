/*
 * Copyright (c) 2014, Laminar Research.
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

#ifndef GUI_FormWindow_H
#define GUI_FormWindow_H

#include "GUI_Window.h"
#include "GUI_Listener.h"
#include "GUI_Destroyable.h"
#include "GUI_Broadcaster.h"
/* THEORY OF OPERATION - GUI_FormWindow

GUI_FormWindow is meant to be a simple pop-up window that one can easily add GUI_Components to. See
WED_ExportDialogDialog for an example.
*/

class GUI_FormWindow : public GUI_Window, public GUI_Listener, public GUI_Destroyable
{
public:
    enum field_type
    {
        ft_single_line,
        ft_multi_line,
        ft_big,
        ft_password
    };

    GUI_FormWindow(GUI_Commander* cmdr, const std::string& title, int width, int height);
    virtual ~GUI_FormWindow();

    void Reset(const std::string& aux_label, const std::string& ok_label, const std::string& cancel_label,
               bool submit_with_return_key);

    void AddLabel(const std::string& msg);

    void AddField(int id, const std::string& label, const std::string& default_text, field_type ft = ft_single_line);

    void AddFieldNoEdit(int id, const std::string& label, const std::string& text);

    std::string GetField(int id);

    // The action preformed when the Aux button is pressed
    virtual void AuxiliaryAction(){};

    // The action preformed when the submit or "Ok" button is pressed
    virtual void Submit() = 0;

    // The action preformed when the cancle button is pressed
    virtual void Cancel() = 0;

    virtual bool Closed(void)
    {
        return true;
    }
    virtual void ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam);

    virtual int HandleKeyPress(uint32_t inKey, int inVK, GUI_KeyFlags inFlags);

private:
    int mInsertY;

    std::vector<GUI_Pane*> mParts;
    std::vector<GUI_Commander*> mFocusRing;

    // The auxiliary panel, a button that can be optionally hidden
    GUI_Pane* mAux;
    GUI_Pane* mOK;
    GUI_Pane* mCancel;

    bool mReturnSubmit;
    int mFormBounds[4];
};

#endif
