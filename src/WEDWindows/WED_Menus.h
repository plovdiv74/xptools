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

#ifndef WED_MENUS_H
#define WED_MENUS_H

#include "GUI_Application.h"
#include "GUI_Menus.h"

extern	GUI_Menu	test1;
extern	GUI_Menu	sub1;

enum {

	// File Menu
	wed_NewPackage = GUI_APP_MENUS,
	wed_OpenPackage,
	wed_ChangeSystem,
	wed_Validate,
	wed_ImportApt,
	wed_ExportApt,
	wed_ExportDSF,
	wed_ImportDSF,
	// Edit Menu,
	wed_Group,
	wed_Ungroup,
	wed_Crop,
	wed_Split,
	wed_Reverse,
	wed_MoveFirst,
	wed_MovePrev,
	wed_MoveNext,
	wed_MoveLast,
	// Pavement menu
	wed_Pavement0,
	wed_Pavement25,
	wed_Pavement50,
	wed_Pavement75,
	wed_Pavement100,
	// view menu
	wed_ZoomWorld,
	wed_ZoomAll,
	wed_ZoomSelection,
	wed_UnitFeet,
	wed_UnitMeters,
	wed_ToggleLines,
	wed_ToggleVertices,
	wed_PickOverlay,
//	wed_ToggleOverlay,
	wed_ToggleWorldMap,
	wed_ToggleTerraserver,
	wed_TogglePreview,
	wed_RestorePanes,
	// Select Menu
	wed_SelectParent,
	wed_SelectChild,
	wed_SelectVertex,
	wed_SelectPoly,
	// Airport Menu
	wed_CreateApt,
	wed_EditApt,
	wed_AddATCFreq,
	// Help Menu
	wed_HelpManual,
	wed_HelpScenery
};

class	GUI_Application;

void WED_MakeMenus(GUI_Application * inApp);

#endif
