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

#ifndef GUI_APPLICATION_H
#define GUI_APPLICATION_H

#if LIN
#define POPUP_ARRAY_SIZE 80
#include <FL/Fl_Menu.H>
#include <FL/Fl_Menu_Bar.H>
#endif

#include "GUI_Commander.h"
#include "GUI_Timer.h"
#include "CmdLine.h"

class GUI_Window;

/*
	WINDOWS WARNING: MENUS

	Windows has the following limitations on the menu system:

	1. App menus cannot be created dynamically on the fly because:

	- They are replicated as each window is made, but existing windows will not receive the new additions.
	- Accelerators are only std::set up at startup (so all menus must be std::set up before app-run.

*/

class	GUI_Application : public GUI_Commander {
public:
#if LIN
	GUI_Application(int& argc, char* argv[]);
#elif APL
	GUI_Application(int argc, char const * const * argv, const char * menu_nib);	// A NIB with an app and Windows menu is passed in.
#else
	GUI_Application(const char * arg);
#endif
	virtual			~GUI_Application();

	// APPLICATION API
	void			Run(void);
	void			Quit(void);

	// MENU API
	GUI_Menu		GetMenuBar(void);
	GUI_Menu		GetPopupContainer(void);

	GUI_Menu		CreateMenu(const char * inTitle, const GUI_MenuItem_t	items[], GUI_Menu parent, int parent_item);
	void			RebuildMenu(GUI_Menu menu, const GUI_MenuItem_t	items[]);

	virtual	void	AboutBox(void)=0;
	virtual	void	Preferences(void)=0;
	virtual	bool	CanQuit(void)=0;
	virtual	void	OpenFiles(const std::vector<std::string>& inFiles)=0;

	// From GUI_Commander - app never refuses focus!
	virtual	int				AcceptTakeFocus(void) 	{ return 1; }
	virtual	int				HandleCommand(int command);
	virtual	int				CanHandleCommand(int command, std::string& ioName, int& ioCheck);


#if APL
	static void MenuUpdateCB(void * ref, int cmd, char * io_name, int * io_check, int * io_enable);
	static void TryQuitCB(void * ref);
#endif

#if LIN
	const Fl_Menu_Item * GetMenu(){return mMenu;}

	static void update_menus(const Fl_Menu_Item * menu);
	static void update_menus_cb(Fl_Widget* w,void * data);
	static int  event_dispatch_cb(int e, Fl_Window *w);
#endif

    const CmdLine args;

private:

    bool                    mDone;
    std::set<GUI_Menu>           mMenus;

#if LIN
    const Fl_Menu_Item *	mMenu;
    const Fl_Menu_Item *	mPopup;
#endif
};

extern	GUI_Application *	gApplication;

#endif


