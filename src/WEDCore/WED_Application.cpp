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

#include "WED_Application.h"
#include "WED_Document.h"
#include "WED_Menus.h"
#include "WED_Url.h"
#include "WED_Messages.h"

#include "WED_AboutBox.h"
#include "WED_Colors.h"

#include "GUI_Resources.h"
#include "GUI_Fonts.h"
#include "GUI_Help.h"
#include "GUI_Packer.h"
#include "GUI_Button.h"
#include "GUI_Label.h"
#include "GUI_TextField.h"

#include "WED_Globals.h"
#if LIN
#include <FL/Fl_Tooltip.H>
#endif

static int settings_bounds[4] = {0, 0, 512, 384};

enum
{
    kMsg_Close = WED_PRIVATE_MSG_BASE
};

class RadioButton
{
public:
    RadioButton(int x0, int y0, WED_Settings* parent, const int* var, const std::string& desc, const char* text0,
                const char* text1);
    ~RadioButton(){};
};

RadioButton::RadioButton(int x0, int y0, WED_Settings* parent, const int* var, const std::string& desc,
                         const char* text0, const char* text1)
{
    const char* texture = "check_buttons.png";
    int r_yes[4] = {0, 1, 1, 3};
    int r_nil[4] = {0, 0, 1, 3};

    int h = GUI_GetImageResourceHeight(texture) * 0.4;

    float white[4] = {1, 1, 1, 1};

    int x1 = x0 + 120;

    GUI_Label* label = new GUI_Label();
    label->SetBounds(x0, y0 - h, x1, y0 + h + 2);
    label->SetColors(white);
    label->SetDescriptor(desc);
    label->SetParent(parent);
    label->Show();

    GUI_Button* btn_0 = new GUI_Button(texture, btn_Radio, r_nil, r_nil, r_yes, r_yes);
    btn_0->SetBounds(x1, y0, x1 + 220, y0 + h);
    btn_0->SetDescriptor(text0);
    btn_0->SetParent(parent);
    btn_0->Show();

    GUI_Button* btn_1 = new GUI_Button(texture, btn_Radio, r_nil, r_nil, r_yes, r_yes);
    btn_1->SetBounds(x1, y0 - h, x1 + 220, y0);
    btn_1->SetDescriptor(text1);
    btn_1->SetParent(parent);
    btn_1->Show();
    btn_1->AddListener(parent);
    btn_1->SetMsg((intptr_t)var, (intptr_t)btn_1);

    btn_0->AddRadioFriend(btn_1);
    btn_1->AddRadioFriend(btn_0);

    if (*var == 0)
        btn_0->SetValue(1.0);
    else
        btn_1->SetValue(1.0);
}

bool WED_Settings::Closed(void)
{
    this->TakeFocus(); // more: removes the focus from the edit fields if any ; stops the cursor blink timer.
    Hide();
    return false;
}

void WED_Settings::ReceiveMessage(GUI_Broadcaster* inSrc, intptr_t inMsg, intptr_t inParam)
{
    //	printf("WS Msg %p %ld %ld\n", inSrc, inMsg, inParam);

    if (inMsg == (intptr_t)&gIsFeet)
    {
        gIsFeet = ((GUI_Button*)inParam)->GetValue();
        BroadcastMessage(GUI_TABLE_CONTENT_CHANGED, 0);
        this->TakeFocus();
    }
    else if (inMsg == (intptr_t)&gInfoDMS)
    {
        gInfoDMS = ((GUI_Button*)inParam)->GetValue();
        this->TakeFocus();
    }
    else if (inMsg == (intptr_t)&gModeratorMode)
    {
        gModeratorMode = ((GUI_Button*)inParam)->GetValue();
        this->TakeFocus();
    }
    else if (inMsg == (intptr_t)&gCustomSlippyMap)
    {
        ((GUI_TextField*)inParam)->GetDescriptor(gCustomSlippyMap);
        printf("%s\n", gCustomSlippyMap.c_str());
    }
    else if (inMsg == (intptr_t)&gFontSize)
    {
        std::string new_val;
        ((GUI_TextField*)inParam)->GetDescriptor(new_val);
        gFontSize = std::max(10, std::min(18, atoi(new_val.c_str())));
        GUI_SetFontSizes(gFontSize);
#if LIN
        Fl_Tooltip::size((int)GUI_GetFontSize(font_UI_Small));
#endif
        int field_height = gFontSize + gFontSize / 2;
        int b[4];
        if (mCustom_box)
        {
            mCustom_box->GetBounds(b);
            mCustom_box->SetBounds(b[0], b[3] - field_height, b[2], b[3]);
        }
        if (mFont_box)
        {
            mFont_box->GetBounds(b);
            mFont_box->SetBounds(b[0], b[1], b[2], b[1] + field_height);
        }
    }
    else if (inMsg == (intptr_t)&gOrthoExport)
    {
        gOrthoExport = ((GUI_Button*)inParam)->GetValue();
        this->TakeFocus();
    }
    else if (inMsg == kMsg_Close)
    {
        this->TakeFocus();
        Hide();
        //		AsyncDestroy();
    }
}

WED_Settings::WED_Settings(GUI_Commander* cmdr)
    : GUI_Window("WED Preferences", xwin_style_movable | xwin_style_centered | xwin_style_popup, settings_bounds, cmdr)
{
    GUI_Packer* packer = new GUI_Packer;
    packer->SetParent(this);
    packer->Show();
    packer->SetBounds(settings_bounds);
    packer->SetBkgkndImage("about.png");

    RadioButton(220, 350, this, &gIsFeet, "Length Units", "Meters", "Feet");
    RadioButton(220, 300, this, &gInfoDMS, "Info Bar\nCoordinates", "DD.DDDDD", "DD MM SS");

    int k_yes[4] = {0, 1, 1, 3};
    int k_no[4] = {0, 2, 1, 3};

    float* white = WED_Color_RGBA(wed_Table_Text);

    GUI_Button* moderator_btn = new GUI_Button("check_buttons.png", btn_Check, k_no, k_no, k_yes, k_yes);
    moderator_btn->SetBounds(340, 255, 510, 255 + GUI_GetImageResourceHeight("check_buttons.png") / 3);
    moderator_btn->Show();
    moderator_btn->SetDescriptor("Moderator Mode");
    moderator_btn->SetParent(this);
    moderator_btn->AddListener(this);
    moderator_btn->SetMsg((intptr_t)&gModeratorMode, (intptr_t)moderator_btn);

    GUI_Button* png_btn = new GUI_Button("check_buttons.png", btn_Check, k_no, k_no, k_yes, k_yes);
    png_btn->SetBounds(340, 230, 510, 230 + GUI_GetImageResourceHeight("check_buttons.png") / 3);
    png_btn->Show();
    png_btn->SetDescriptor("Ortho's to .dds");
    png_btn->SetParent(this);
    png_btn->AddListener(this);
    png_btn->SetValue(gOrthoExport);
    png_btn->SetMsg((intptr_t)&gOrthoExport, (intptr_t)png_btn);

    int field_height = gFontSize + gFontSize / 2;

    mCustom_box = new GUI_TextField(true, this);
    GUI_Label* label = new GUI_Label();
    mCustom_box->SetMargins(3, 2, 3, 2);
    mCustom_box->SetBounds(20, 140 - field_height, 490, 140);
    label->SetBounds(20, 142, 300, 162);
    mCustom_box->SetWidth(1000);
    mCustom_box->SetParent(this);
    mCustom_box->AddListener(this);
    mCustom_box->SetKeyMsg((intptr_t)&gCustomSlippyMap, (intptr_t)mCustom_box);
    mCustom_box->SetDescriptor(gCustomSlippyMap);
    mCustom_box->Show();
    mCustom_box->SetKeyAllowed(GUI_KEY_RETURN, false);
    mCustom_box->SetKeyAllowed(GUI_VK_ESCAPE, false);
    mCustom_box->SetKeyAllowed('\\', false);
    label->SetColors(white);
    label->SetParent(this);
    label->SetDescriptor("Tile Server Custom URL");
    label->Show();

    mFont_box = new GUI_TextField(false, this);
    GUI_Label* label2 = new GUI_Label();
    mFont_box->SetMargins(3, 2, 3, 2);
    mFont_box->SetBounds(340, 190, 400, 190 + field_height);
    label2->SetBounds(220, 190, 350, 210);
    mFont_box->SetParent(this);
    mFont_box->AddListener(this);
    mFont_box->SetKeyMsg((intptr_t)&gFontSize, (intptr_t)mFont_box);
    mFont_box->SetDescriptor(std::to_string(gFontSize));
    mFont_box->Show();
    mFont_box->SetKeyAllowed(GUI_KEY_RETURN, false);
    mFont_box->SetKeyAllowed(GUI_VK_ESCAPE, false);
    mFont_box->SetKeyAllowed('\\', false);
    label2->SetColors(white);
    label2->SetParent(this);
    label2->SetDescriptor("Font Size");
    label2->Show();

    GUI_Button* close_btn = new GUI_Button("push_buttons.png", btn_Push, k_no, k_yes, k_no, k_yes);
    close_btn->SetBounds(220, 5, 290, 5 + GUI_GetImageResourceHeight("push_buttons.png") / 3);
    close_btn->Show();
    close_btn->SetDescriptor("Close");
    close_btn->SetParent(this);
    close_btn->AddListener(this);
    close_btn->SetMsg(kMsg_Close, 0);
}

#if LIN
WED_Application::WED_Application(int& argc, char* argv[])
    : GUI_Application(argc, argv),
#elif APL
WED_Application::WED_Application(int argc, char const* const* argv)
    : GUI_Application(argc, argv, "WEDMainMenu"),
#else // Windows
WED_Application::WED_Application(const char* args)
    : GUI_Application(args),
#endif
      mAboutBox(NULL), mSettingsWin(NULL)
{
}

WED_Application::~WED_Application()
{
    if (mAboutBox)
        delete mAboutBox;
    if (mSettingsWin)
        delete mSettingsWin;
}

void WED_Application::OpenFiles(const std::vector<std::string>& inFiles)
{
}

int WED_Application::HandleCommand(int command)
{
    switch (command)
    {
    case wed_ESRIUses:
        // as per https://operations.osmfoundation.org/policies/tiles/
        GUI_LaunchURL(WED_URL_ESRI_USES);
        return 1;
    case wed_OSMFixTheMap:
        // as per https://operations.osmfoundation.org/policies/tiles/
        GUI_LaunchURL(WED_URL_OSM_FIXTHEMAP);
        return 1;
    case wed_HelpScenery:
        // LR maintains a forwarding directory for all v10-class products
        // so that we can restructure our content management without breaking binary
        // apps in-field.  So...this is the perma-marker for WED 1.1 scenery help.
        GUI_LaunchURL(WED_URL_HELP_SCENERY);
        return 1;
    case wed_HelpManual: {
        // We used to have a nice PDF published with WED, but...WED is changing fast
        // and it stops going final to have to wait for doc complete.  So let's put
        // the manual online and off we go.
        //			std::string path;
        //			if (GUI_GetTempResourcePath("WEDManual.pdf",path))
        //			{
        //				GUI_LaunchURL(path.c_str());
        //			}
        GUI_LaunchURL(WED_URL_MANUAL);
    }
        return 1;
    default:
        return GUI_Application::HandleCommand(command);
    }
}

int WED_Application::CanHandleCommand(int command, std::string& ioName, int& ioCheck)
{
    switch (command)
    {
    case gui_Undo:
        ioName = "&Undo";
        return 0;
    case gui_Redo:
        ioName = "&Redo";
        return 0;
    case wed_HelpScenery:
    case wed_HelpManual:
    case wed_OSMFixTheMap:
    case wed_ESRIUses:
        return 1;
    default:
        return GUI_Application::CanHandleCommand(command, ioName, ioCheck);
    }
}

void WED_Application::AboutBox(void)
{
    if (!mAboutBox)
        mAboutBox = new WED_AboutBox(this);
    mAboutBox->Show();
}

void WED_Application::Preferences(void)
{
    if (!mSettingsWin)
        mSettingsWin = new WED_Settings(this);
    mSettingsWin->Show();
#if APL
    mSettingsWin->Refresh();
#endif
}

bool WED_Application::CanQuit(void)
{
    //	if (ConfirmMessage("Are you sure you want to quit WED", "Quit", "Cancel"))	return true;	return false;
    return WED_Document::TryCloseAll();
}
