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

#include "GUI_Clipboard.h"

#if APL
#include "ObjCUtils.h"
#elif IBM
#include <Windows.h>
#include "GUI_Unicode.h"
#endif
#if LIN
#include "FL/Fl.H"
#include "AssertUtils.h"
#endif

#if APL || LIN
typedef std::string GUI_CIT; // Clipboard Internal Type
#elif IBM
typedef CLIPFORMAT GUI_CIT; // Clipboard Internal Type
#endif

#if LIN
#if FL_PATCH_VERSION > 3
#define mTOTAL_FLTK_CLIPFORMATS 2
#else
#define mTOTAL_FLTK_CLIPFORMATS 1
static const char* const m_fl_clipboard_plain_text = "text/plain";
#endif

static std::string get_nth_clipboard_format(int n)
{
#if FL_PATCH_VERSION > 3
    if (n == 0)
        return Fl::clipboard_plain_text;
    if (n == 1)
        return Fl::clipboard_image;
#else
    if (n == 0)
        return m_fl_clipboard_plain_text;
#endif
    return NULL;
}

static bool gClipboardRecieved = 0;

int Get_ClipboardRecieved()
{
    return gClipboardRecieved;
}

void Set_ClipboardRecieved(int s)
{
    gClipboardRecieved = s;
}

#endif

enum
{
    gui_Clip_Text = 0,
    gui_First_Private
};

static std::vector<std::string> sClipStrings;
static std::vector<GUI_CIT> sCITs;

//---------------------------------------------------------------------------------------------------------
// TYPE MANAGEMENT
//---------------------------------------------------------------------------------------------------------

static int GUI2CIT(GUI_ClipType in_t, GUI_CIT& out_t)
{
    if (in_t >= 0 && in_t < sCITs.size())
    {
        out_t = sCITs[in_t];
        return 1;
    }
    return 0;
}

static int CIT2GUI(GUI_CIT in_t, GUI_ClipType& out_t)
{
    for (out_t = 0; out_t < sCITs.size(); ++out_t)
        if (sCITs[out_t] == in_t)
            return 1;
    return 0;
}

void GUI_InitClipboard(void)
{
#if APL
    sCITs.push_back(get_pasteboard_text_type());
#elif IBM
    sCITs.push_back(CF_UNICODETEXT);
#elif LIN
#if FL_PATCH_VERSION > 3
    sCITs.push_back(Fl::clipboard_plain_text);
#else
    sCITs.push_back(m_fl_clipboard_plain_text);
#endif
#endif
    sClipStrings.push_back("text");
}

GUI_ClipType GUI_RegisterPrivateClipType(const char* clip_type)
{
    for (int n = 0; n < sClipStrings.size(); ++n)
    {
        if (strcmp(sClipStrings[n].c_str(), clip_type) == 0)
            return n;
    }

    sClipStrings.push_back(clip_type);
#if APL || LIN
    std::string full_type = "com.laminar.";
    full_type += clip_type;
    sCITs.push_back(full_type);
#elif IBM
    sCITs.push_back(CF_PRIVATEFIRST + sCITs.size() - gui_First_Private);
#endif
    return sCITs.size() - 1;
}

GUI_ClipType GUI_GetTextClipType(void)
{
    return gui_Clip_Text;
}

#if APL
void GUI_GetMacNativeDragTypeList(std::vector<std::string>& out_types)
{
    out_types = sCITs;
}
#endif

//---------------------------------------------------------------------------------------------------------
// DATA MANAGEMENT
//---------------------------------------------------------------------------------------------------------

#pragma mark -

bool GUI_Clipboard_HasClipType(GUI_ClipType inType)
{
#if APL
    return clipboard_has_type(sCITs[inType].c_str());
#elif IBM
    return (IsClipboardFormatAvailable(sCITs[inType]));
#elif LIN
    // TODO:mroe  unfortunatly that does not work , likly because we are in the menu-special-grab state
    // return = Fl::clipboard_contains(sCITs[inType].c_str());
    // However , we check this later in code again;
    return 1;
#endif
}

void GUI_Clipboard_GetTypes(std::vector<GUI_ClipType>& outTypes)
{
    outTypes.clear();
#if APL
    int total = count_clipboard_formats();
    for (int n = 0; n < total; ++n)
    {
        GUI_CIT raw_type = get_nth_clipboard_format(n);
        GUI_ClipType ct;
        if (CIT2GUI(raw_type, ct))
            outTypes.push_back(ct);
    }
#elif IBM
    int total = CountClipboardFormats();
    for (int n = 0; n < total; ++n)
    {
        GUI_CIT raw_type = EnumClipboardFormats(n);
        GUI_ClipType ct;
        if (CIT2GUI(raw_type, ct))
            outTypes.push_back(ct);
    }
#elif LIN
    for (int n = 0; n < mTOTAL_FLTK_CLIPFORMATS; ++n)
    {
        GUI_CIT raw_type = get_nth_clipboard_format(n);
        GUI_ClipType ct;
        if (CIT2GUI(raw_type, ct))
            outTypes.push_back(ct);
    }
#endif
}

#if IBM
struct StOpenClipboard
{
    StOpenClipboard()
    {
        is_open = OpenClipboard(NULL);
    }
    ~StOpenClipboard()
    {
        if (is_open)
            CloseClipboard();
    }
    bool operator()(void) const
    {
        return is_open;
    }
    bool is_open;
};

struct StGlobalBlock
{
    StGlobalBlock(int mem)
    {
        handle = GlobalAlloc(GMEM_MOVEABLE, mem);
    }
    ~StGlobalBlock()
    {
        if (handle)
            GlobalFree(handle);
    }
    HGLOBAL operator()(void) const
    {
        return handle;
    }
    void release(void)
    {
        handle = NULL;
    }
    HGLOBAL handle;
};

struct StGlobalLock
{
    StGlobalLock(HGLOBAL h)
    {
        handle = h;
        ptr = GlobalLock(handle);
    }
    ~StGlobalLock()
    {
        if (ptr)
            GlobalUnlock(handle);
    }
    void* operator()(void) const
    {
        return ptr;
    }
    HGLOBAL handle;
    void* ptr;
};
#endif

int GUI_Clipboard_GetSize(GUI_ClipType inType)
{
#if APL
    return get_clipboard_data_size(sCITs[inType].c_str());
#elif IBM

    HGLOBAL hglb;
    if (!IsClipboardFormatAvailable(sCITs[inType]))
        return 0;

    StOpenClipboard open_it;
    if (!open_it())
        return 0;

    hglb = GetClipboardData(sCITs[inType]);
    if (hglb == NULL)
        return 0;

    int sz = GlobalSize(hglb);
    return sz;

#elif LIN
// printf("start paste typ: %s wnd: %p\n",sCITs[inType].c_str(),Fl::focus());
#if FL_PATCH_VERSION > 3
    if (strcmp(sCITs[inType].c_str(), Fl::clipboard_plain_text) != 0)
        return 0;
#else
    if (strcmp(sCITs[inType].c_str(), m_fl_clipboard_plain_text) != 0)
        return 0;
#endif
    Set_ClipboardRecieved(false);
    Fl::paste(*Fl::focus(), 1);
    // TODO:mroe --> revisit
    // to not loop forever if things going wrong  Key_up brings us back for now
    // wait for the PASTE event roundtrip
    while (Fl::event() != FL_KEYUP)
    {
        Fl::wait();
        if (Get_ClipboardRecieved())
            return Fl::event_length();
    }
    return 0;
#endif
}

bool GUI_Clipboard_GetData(GUI_ClipType inType, int size, void* ptr)
{
#if APL
    int amt = copy_data_of_type(sCITs[inType].c_str(), ptr, size);
    return amt == size;

#elif IBM

    HGLOBAL hglb;
    if (!IsClipboardFormatAvailable(sCITs[inType]))
        return false;

    StOpenClipboard open_it;
    if (!open_it())
        return false;

    hglb = GetClipboardData(sCITs[inType]);
    if (hglb == NULL)
        return false;

    if (GlobalSize(hglb) != size)
        return false;

    StGlobalLock lock_it(hglb);

    if (!lock_it())
        return false;

    memcpy(ptr, lock_it(), size);
    return true;

#elif LIN
    DebugAssert(size == Fl::event_length());
    // TODO:mroe: this needs some more care
    memcpy(ptr, Fl::event_text(), size);
    // printf("clipboard get data txt:%s wnd: %p\n", Fl::event_text(),Fl::focus());
    Set_ClipboardRecieved(false);
    return true;

#endif
}
bool GUI_Clipboard_SetData(int type_count, GUI_ClipType inTypes[], int sizes[], const void* ptrs[])
{
#if APL
    clear_clipboard();

    for (int n = 0; n < type_count; ++n)
        add_data_of_type(sCITs[inTypes[n]].c_str(), ptrs[n], sizes[n]);
    return true;
#elif IBM
    StOpenClipboard open_it;
    if (!open_it())
        return false;
    if (!EmptyClipboard())
        return false;

    for (int n = 0; n < type_count; ++n)
    {
        StGlobalBlock block(sizes[n]);
        if (!block())
            return false;

        {
            StGlobalLock lock_it(block());
            if (!lock_it())
                return false;
            memcpy(lock_it(), ptrs[n], sizes[n]);
        }

        if (SetClipboardData(sCITs[inTypes[n]], block()) == NULL)
            return false;

        // Ben says: ownership rules of the block are as follows:
        // - function fails...we still own the handle.
        // - function succeeds...we own the handle IF it is a private scrap, but NOT if it is public.
        // So for successful public scrap std::set, release the block so we don't double-deallocate
        if (inTypes[n] < gui_First_Private)
            block.release();
    }
    return true;

#elif LIN
    for (int n = 0; n < type_count; ++n)
    {
#if FL_PATCH_VERSION > 3
        Fl::copy((const char*)ptrs[n], sizes[n], 1, sCITs[inTypes[n]].c_str());
#else
        Fl::copy((const char*)ptrs[n], sizes[n], 1);
#endif
    }

    return true;
#endif
}

//---------------------------------------------------------------------------------------------------------
// CONVENIENCE ROUTINES
//---------------------------------------------------------------------------------------------------------

bool GUI_GetTextFromClipboard(std::string& outText)
{
    GUI_ClipType text = GUI_GetTextClipType();
    if (!GUI_Clipboard_HasClipType(text))
        return false;
    int sz = GUI_Clipboard_GetSize(text);
    if (sz <= 0)
        return false;
    std::vector<char> buf(sz);
    if (!GUI_Clipboard_GetData(text, sz, &*buf.begin()))
        return false;
#if APL || LIN
    outText = std::string(buf.begin(), buf.begin() + sz);
#elif IBM
    const UTF16* p = (const UTF16*)&buf[0];
    string_utf16 str16(p, p + sz / 2 - 1);
    outText = convert_utf16_to_str(str16);
#endif
    return true;
}

bool GUI_SetTextToClipboard(const std::string& inText)
{
    GUI_ClipType text = GUI_GetTextClipType();
    const void* ptr;
#if APL || LIN
    ptr = inText.c_str();
    int sz = inText.size();
#elif IBM
    string_utf16 str16 = convert_str_to_utf16(inText);
    ptr = (const void*)&str16[0];
    int sz = 2 * (inText.size() + 1);
#endif

    return GUI_Clipboard_SetData(1, &text, &sz, &ptr);
}

//---------------------------------------------------------------------------------------------------------
// DRAG & DROP -- WINDOWS
//---------------------------------------------------------------------------------------------------------

#if IBM

// Enumerating the viable drag & drop formats on Windows is done via a separate COM object.  Because it is
// an std::iterator and has "state" and can be  cloned, we must actually use a real object.  Annyoing!

class GUI_SimpleEnumFORMATETC : public IEnumFORMATETC
{
public:
    GUI_SimpleEnumFORMATETC(GUI_SimpleDataObject* parent);
    GUI_SimpleEnumFORMATETC(const GUI_SimpleEnumFORMATETC& me);

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    STDMETHOD(Next)(ULONG count, FORMATETC* formats, ULONG* out_count);
    STDMETHOD(Skip)(ULONG count);
    STDMETHOD(Reset)(void);
    STDMETHOD(Clone)(IEnumFORMATETC** pp_obj);

private:
    ULONG mRefCount;
    ULONG mIndex; // Position in our iteration
    std::vector<GUI_ClipType>
        mTypes; // Note - we pre-copy our types to a std::vector.  Easier and puts them in a random-accessible container
};

GUI_OLE_Adapter::GUI_OLE_Adapter(IDataObject* data_obj)
{
    mObject = data_obj;
    mObject->AddRef();
}

GUI_OLE_Adapter::~GUI_OLE_Adapter()
{
    mObject->Release();
}

int GUI_OLE_Adapter::CountItems(void)
{
    return 1;
}

bool GUI_OLE_Adapter::NthItemHasClipType(int n, GUI_ClipType ct)
{
    FORMATETC format;
    if (n != 0)
        return false;
    if (!GUI2CIT(ct, format.cfFormat))
        return false;
    format.ptd = NULL;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    return mObject->QueryGetData(&format) == S_OK;
}

int GUI_OLE_Adapter::GetNthItemSize(int n, GUI_ClipType ct)
{
    FORMATETC format;
    STGMEDIUM medium;
    if (n != 0)
        return false;
    if (!GUI2CIT(ct, format.cfFormat))
        return false;
    format.ptd = NULL;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    if (mObject->GetData(&format, &medium) != S_OK)
        return 0;

    int block_size = GlobalSize(medium.hGlobal);

    ReleaseStgMedium(&medium);
    return block_size;
}

bool GUI_OLE_Adapter::GetNthItemData(int n, GUI_ClipType ct, int size, void* ptr)
{
    FORMATETC format;
    STGMEDIUM medium;
    if (n != 0)
        return false;
    if (!GUI2CIT(ct, format.cfFormat))
        return false;
    format.ptd = NULL;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;
    format.tymed = TYMED_HGLOBAL;
    if (mObject->GetData(&format, &medium) != S_OK)
        return 0;

    if (GlobalSize(medium.hGlobal) != size)
    {
        ReleaseStgMedium(&medium);
        return false;
    }

    {
        StGlobalLock lock_it(medium.hGlobal);
        if (!lock_it())
        {
            ReleaseStgMedium(&medium);
            return false;
        }

        memcpy(ptr, lock_it(), size);
    }
    ReleaseStgMedium(&medium);
    return true;
}

GUI_SimpleDataObject::GUI_SimpleDataObject(int type_count, GUI_ClipType inTypes[], int sizes[], const void* ptrs[],
                                           GUI_GetData_f get_data_func, void* ref)
    : mRefCount(1), mFetchFunc(get_data_func), mFetchRef(ref)
{
    for (int n = 0; n < type_count; ++n)
    {
        if (ptrs[n] == NULL)
            mData[inTypes[n]] = std::vector<char>();
        else
            mData[inTypes[n]] = std::vector<char>((const char*)ptrs[n], (const char*)ptrs[n] + sizes[n]);
    }
}

STDMETHODIMP GUI_SimpleDataObject::QueryInterface(REFIID riid, void** ppvOut)
{
    *ppvOut = NULL;

    if (IsEqualIID(riid, IID_IUnknown))
        *ppvOut = this;
    else if (IsEqualIID(riid, IID_IDataObject))
        *ppvOut = (IDataObject*)this;

    if (*ppvOut)
    {
        (*(LPUNKNOWN*)ppvOut)->AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) GUI_SimpleDataObject::AddRef()
{
    return ++mRefCount;
}

STDMETHODIMP_(ULONG) GUI_SimpleDataObject::Release()
{
    if (--mRefCount == 0)
    {
        delete this;
        return 0;
    }
    return mRefCount;
}

// GetData copies the data from the format to the medium.  We allocate storage and our caller
// deallocates it.  So the only thing that can go wrong is they want something we don't do,
// e.g. a weird data format or weird storage format.
STDMETHODIMP GUI_SimpleDataObject::GetData(FORMATETC* format, STGMEDIUM* medium)
{
    GUI_ClipType desired_type;
    if (!CIT2GUI(format->cfFormat, desired_type))
        return DV_E_FORMATETC;
    if (mData.count(desired_type) == 0)
        return DV_E_FORMATETC;
    if ((format->tymed & TYMED_HGLOBAL) == 0)
        return DV_E_TYMED;

    if (mData[desired_type].empty())
    {
        if (mFetchFunc == NULL)
            return E_UNEXPECTED;

        const void* start_p;
        const void* end_p;

        GUI_FreeFunc_f free_it = mFetchFunc(desired_type, &start_p, &end_p, mFetchRef);
        if (start_p == NULL)
            return E_OUTOFMEMORY;

        std::vector<char> buf((const char*)start_p, (const char*)end_p);
        mData[desired_type].swap(buf);
        if (free_it)
            free_it(start_p, mFetchRef);
    }

    medium->tymed = TYMED_HGLOBAL;

    StGlobalBlock new_block(mData[desired_type].size());
    if (!new_block())
        return E_OUTOFMEMORY;

    {
        StGlobalLock lock_it(new_block());
        if (!lock_it())
            return E_OUTOFMEMORY;
        memcpy(lock_it(), &*mData[desired_type].begin(), mData[desired_type].size());
    }

    medium->hGlobal = new_block();
    medium->pUnkForRelease = NULL; // This tells the caller to use the standard release (GlobalFree) on our handle.
    new_block.release();

    return S_OK;
}

// This copies our data into a medium that is totally pre-allocated.  I can't imagine this would work even remotely
// well...the caller must pre-allocate the handle to the exact right size.  So...pre-flight and then just use the
// handle if we have it.
STDMETHODIMP GUI_SimpleDataObject::GetDataHere(FORMATETC* format, STGMEDIUM* medium)
{
    GUI_ClipType desired_type;
    if (!CIT2GUI(format->cfFormat, desired_type))
        return DV_E_FORMATETC;
    if (mData.count(desired_type) == 0)
        return DV_E_FORMATETC;
    if ((format->tymed & TYMED_HGLOBAL) == 0)
        return DV_E_TYMED;
    if (medium->tymed != TYMED_HGLOBAL)
        return DV_E_TYMED;

    if (mData[desired_type].empty())
    {
        if (mFetchFunc == NULL)
            return E_UNEXPECTED;

        const void* start_p;
        const void* end_p;

        GUI_FreeFunc_f free_it = mFetchFunc(desired_type, &start_p, &end_p, mFetchRef);
        if (start_p == NULL)
            return E_OUTOFMEMORY;

        std::vector<char> buf((const char*)start_p, (const char*)end_p);
        mData[desired_type].swap(buf);
        if (free_it)
            free_it(start_p, mFetchRef);
    }

    if (medium->hGlobal == NULL)
        return E_INVALIDARG;
    if (GlobalSize(medium->hGlobal) != mData[desired_type].size())
        return STG_E_MEDIUMFULL;

    {
        StGlobalLock lock_it(medium->hGlobal);
        if (!lock_it())
            return E_OUTOFMEMORY;
        memcpy(lock_it(), &*mData[desired_type].begin(), mData[desired_type].size());
    }

    // Ben says: docs on GetDataHere says we must fill this out...strange, but necessary.
    medium->pUnkForRelease = NULL;
    return S_OK;
}

// This is the equivalent of "do you have this format" - we just do preflighting.
STDMETHODIMP GUI_SimpleDataObject::QueryGetData(FORMATETC* format)
{
    GUI_ClipType desired_type;
    if (!CIT2GUI(format->cfFormat, desired_type))
        return DV_E_FORMATETC;
    if (mData.count(desired_type) == 0)
        return DV_E_FORMATETC;
    if ((format->tymed & TYMED_HGLOBAL) == 0)
        return DV_E_TYMED;
    return S_OK;
}

// I still can't say I fully understand what this is for ... somehow it is used by calling code to analyze the
// various conversion options.  Bottom line is returning what we got with no ptd is fair game per the MS docs
// for trivial clients.  (Of course we provide exactly one rendering per clipboard-type.)
STDMETHODIMP GUI_SimpleDataObject::GetCanonicalFormatEtc(FORMATETC* format_in, FORMATETC* format_out)
{
    GUI_ClipType desired_type;
    if (!CIT2GUI(format_in->cfFormat, desired_type))
        return DV_E_FORMATETC;
    if (mData.count(desired_type) == 0)
        return DV_E_FORMATETC;

    memcpy(format_out, format_in, sizeof(FORMATETC));
    format_out->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP GUI_SimpleDataObject::SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease)
{
    return E_NOTIMPL;
}

STDMETHODIMP GUI_SimpleDataObject::EnumFormatEtc(DWORD direction, IEnumFORMATETC** ppEnumObj)
{
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    *ppEnumObj = new GUI_SimpleEnumFORMATETC(this);
    return S_OK;
}

STDMETHODIMP GUI_SimpleDataObject::DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink,
                                           DWORD* pdwConnection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP GUI_SimpleDataObject::DUnadvise(DWORD dwConnection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}
STDMETHODIMP GUI_SimpleDataObject::EnumDAdvise(IEnumSTATDATA** ppenumAdvise)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

GUI_SimpleEnumFORMATETC::GUI_SimpleEnumFORMATETC(GUI_SimpleDataObject* parent) : mRefCount(1), mIndex(0)
{
    for (std::map<GUI_ClipType, std::vector<char>>::iterator i = parent->mData.begin(); i != parent->mData.end(); ++i)
        mTypes.push_back(i->first);
}

GUI_SimpleEnumFORMATETC::GUI_SimpleEnumFORMATETC(const GUI_SimpleEnumFORMATETC& rhs)
    : mRefCount(1), mIndex(rhs.mIndex), mTypes(rhs.mTypes)
{
}

STDMETHODIMP GUI_SimpleEnumFORMATETC::QueryInterface(REFIID riid, void** ppvOut)
{
    *ppvOut = NULL;

    if (IsEqualIID(riid, IID_IUnknown))
        *ppvOut = this;
    else if (IsEqualIID(riid, IID_IEnumFORMATETC))
        *ppvOut = (IEnumFORMATETC*)this;

    if (*ppvOut)
    {
        (*(LPUNKNOWN*)ppvOut)->AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) GUI_SimpleEnumFORMATETC::AddRef()
{
    return ++mRefCount;
}

STDMETHODIMP_(ULONG) GUI_SimpleEnumFORMATETC::Release()
{
    if (--mRefCount == 0)
    {
        delete this;
        return 0;
    }
    return mRefCount;
}

STDMETHODIMP GUI_SimpleEnumFORMATETC::Next(ULONG count, FORMATETC* formats, ULONG* out_count)
{
    if (mIndex >= mTypes.size())
    {
        if (out_count)
            *out_count = NULL;
        return S_FALSE;
    }
    ULONG remaining = mTypes.size() - mIndex;
    int to_fetch = std::min(count, remaining);

    for (int n = 0; n < to_fetch; ++n)
    {
        GUI2CIT(mTypes[n + mIndex], formats[n].cfFormat);
        formats[n].ptd = NULL;
        formats[n].dwAspect = DVASPECT_CONTENT;
        formats[n].lindex = -1;
        formats[n].tymed = TYMED_HGLOBAL;
    }

    mIndex += to_fetch;
    if (out_count)
        *out_count = to_fetch;
    return (count == to_fetch) ? S_OK : S_FALSE;
}

STDMETHODIMP GUI_SimpleEnumFORMATETC::Skip(ULONG count)
{
    if (mIndex >= mTypes.size())
    {
        return S_FALSE;
    }
    ULONG remaining = mTypes.size() - mIndex;
    int to_fetch = std::min(count, remaining);
    mIndex += to_fetch;
    return (count == to_fetch) ? S_OK : S_FALSE;
}

STDMETHODIMP GUI_SimpleEnumFORMATETC::Reset(void)
{
    mIndex = 0;
    return S_OK;
}

STDMETHODIMP GUI_SimpleEnumFORMATETC::Clone(IEnumFORMATETC** pp_obj)
{
    *pp_obj = new GUI_SimpleEnumFORMATETC(*this);
    return S_OK;
}

#endif /* IBM */

//---------------------------------------------------------------------------------------------------------
// DRAG & DROP -- MAC
//---------------------------------------------------------------------------------------------------------

#if APL

// The GUI_DragMgr_Adapter is a simple adapter class from the drag mgr API to the
// GUI abstract class.  IT's almost a 1:1 coding of the API.
GUI_DragMgr_Adapter::GUI_DragMgr_Adapter(void* data_obj) : mObject(data_obj)
{
}

GUI_DragMgr_Adapter::~GUI_DragMgr_Adapter()
{
}

int GUI_DragMgr_Adapter::CountItems(void)
{
    return count_drag_items(mObject);
}

bool GUI_DragMgr_Adapter::NthItemHasClipType(int n, GUI_ClipType ct)
{
    return nth_drag_item_has_type(mObject, n, sCITs[ct].c_str());
}

int GUI_DragMgr_Adapter::GetNthItemSize(int n, GUI_ClipType ct)
{
    return nth_drag_item_get_size(mObject, n, sCITs[ct].c_str());
}

bool GUI_DragMgr_Adapter::GetNthItemData(int n, GUI_ClipType ct, int size, void* ptr)
{
    return nth_drag_item_get_data(mObject, n, sCITs[ct].c_str(), size, ptr) == size;
}

void* GUI_LoadOneSimpleDrag(int inTypeCount, GUI_ClipType inTypes[], int sizes[], const void* ptrs[],
                            const int bounds[4])
{
    std::vector<const char*> raw_types;

    for (int i = 0; i < inTypeCount; ++i)
    {
        raw_types.push_back(sCITs[inTypes[i]].c_str());
    }

    return create_drag_item_with_data(inTypeCount, &raw_types[0], sizes, ptrs, bounds);
}

#endif /* APL */

//---------------------------------------------------------------------------------------------------------
// DRAG & DROP -- LIN
//---------------------------------------------------------------------------------------------------------

#if LIN

// mroe: implementation of a adapter like MAC version
GUI_DragData_Adapter::GUI_DragData_Adapter(void* data_obj) : mObject(data_obj)
{
}

GUI_DragData_Adapter::~GUI_DragData_Adapter()
{
}

int GUI_DragData_Adapter::CountItems(void)
{
    // TODO:mroe CountItems not implemented yet;
    // only a workaround while develop WED;
    return 1;
}

bool GUI_DragData_Adapter::NthItemHasClipType(int n, GUI_ClipType ct)
{
    // TODO:mroe NthItemHasClipType not implemented yet ;
    // only a workaround while develop WED;
    return true;
}

int GUI_DragData_Adapter::GetNthItemSize(int n, GUI_ClipType ct)
{
    // TODO:mroe GetNthItemSize not implemented yet ;
    return 0;
}

bool GUI_DragData_Adapter::GetNthItemData(int n, GUI_ClipType ct, int size, void* ptr)
{
    // TODO:mroe GetNthItemData not implemented yet ;
    return false;
}

#endif /* LIN */
