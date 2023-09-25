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

#ifndef WED_PROPERTYHELPER_H
#define WED_PROPERTYHELPER_H

/*	WED_PropertyHelper - THEORY OF OPERATION

	IPropertyObject provides an interface for a class to describe and I/O it's own data.  But...implementing that a hundred times over
	for each object would grow old fast.

	WED_PropertyHelper is an implementation that uses objects wrapped around member vars to simplify building up objects quickly.

	As a side note besides providing prop interfaces, it provides a way to stream properties to IODef reader/writers.  This is used to
	save undo work in WED_thing.
*/

#include "IPropertyObject.h"
#include "WED_XMLReader.h"
#include "WED_Globals.h"

class	WED_PropertyHelper;
class	IOWriter;
class	IOReader;
class	WED_XMLElement;

/* memory size optimization for LARGE datasets, e.g. all 38,000+ Global Airports:

   These macros create a *single* std::string containing the properties WED name and both XML names,
   saving 2 pointers in each property item.
   Every WED_Thing has on average 10 WED_Properties, the Global Airports have as of mid 2019 ~14 million WED_Things,
   so this adds up to over 2 GB of memory for just these two extra pointers saved, some 30% of total memory.

   There is no compile time switch to turn this trickery off and return to using full pointers again.
*/

#define XML_Name(x,y) x "\0" y
#define PROP_Name(wed_name, xml_name) wed_name "\0" xml_name, sizeof(wed_name) + 256 * (strlen(xml_name)+1+sizeof(wed_name))

/* more memory size and access time optimization for LARGE datasets, e.g. all 38,000+ Global Airports:

   And the STL container std::vector<WED_PropertyItem *> is responsible for a good chunk of all that pain,
   as the pointer array is on the heap, requireing a second memory access to resolve. With large
   data structures, pretty much every memory access is a cache miss. So making this a memory-local
   fixed array gains speed in every read/write to any property.

   All WED_PropertyItems are part of the same class, max WED_PropertyHelper memory size is ~2kb
   Due to alignof(class) == 8 that relative distance can be encoded with just 1 byte.

   So the std::vector<class *> is reduced a memory-local char[]

   This saves another 24% of total memory and 5% CPU time on load, save and export.

   The maximum number of properties for any WEDEntity is 31 right now (Runways), so we define the
   byte array to have a rounded up 32 entrie

   Set below to 0 to disable this trickery and use a plain std::vector<void *> again and also make mParent
   pointing from each property back to the WED_Thing a full pointer and not just an 8 bit relative pointer.

   Using some more clever std::vector like LLVM's SmallVector or boosts small_vector would eliminate the
   need for a fixed array here. In that case - an inline size of 16 elements would suffice to hold 
   the relaive offsets for all entities except runways - only those have more properties. So that saves 
   even more memory and reduced any need in the future to re-adjust this fixed array for increased couunts !
   There are a LOT less runways than vertices and other entities - so loosing some optimizations for that
   entity type is not causing any notable drawbacks.
*/

#define PROP_PTR_OPT 32

class	WED_PropertyItem {
public:
	WED_PropertyItem(WED_PropertyHelper * parent, const char * title, int offset);

	virtual void		GetPropertyInfo(PropertyInfo_t& info)=0;
	virtual	void		GetPropertyDict(PropertyDict_t& dict)=0;
	virtual	void		GetPropertyDictItem(int e, std::string& item)=0;
	virtual void		GetProperty(PropertyVal_t& val) const=0;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent)=0;
	virtual	void 		ReadFrom(IOReader * reader)=0;
	virtual	void 		WriteTo(IOWriter * writer)=0;
	virtual	void		ToXML(WED_XMLElement * parent)=0;
	virtual	bool		WantsElement(WED_XMLReader * reader, const char * name) { return false; }
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value)=0;

	const char *		GetWedName(void) const;
protected:
	WED_PropertyHelper* GetParent(void) const;
	const char *		GetXmlName(void) const;
	const char *		GetXmlAttrName(void) const;
private:
#if PROP_PTR_OPT
	#define PTR_CLR(x)  (x & (1ULL << 45) - 1ULL)
	/*
	Unfortunately we need 2 more bits as we have 'unused' bits in the 47 bit pointers. So we clear two more bits, but need
	to put them back later to restore the exact pointer.
	Under Linux and OSX - that's easy: All constants are at the bottom of the 47bit virtual address space - so UNLESS the
	pointer is refering to the heap - those other more significant bits are all zero.
	But under windows - the constants are mapped at the very top, i.e. right below 0x7FFFFFFFFFFF. So we look at the
	45th bit - the highest one we didn't clobber and copy that to the 46 and 47th bits. This restores the exact pointer
	if pointimng to either in the top 32TB OR bottom 32TB of the 128TB / 47 bit virtual address space.
	*/
	#define PTR_FIX(x)  (PTR_CLR(x) | (x & (1ULL << 44)) << 1 | (x & (1ULL << 44)) << 2)
	uintptr_t			mTitle;      // this now holds THREE tightly packed offsets in its 19 MSBits to save even more memory
  #if DEV
	WED_PropertyHelper*	mParent;     // only to verify the relative pointer calculated from the 8 bits is identical to the real pointer
  #endif
#else
	/*
	So called "64bit" processors actually only have 48bit virtual address space - a concession to the structures and speed of the
	virtual memory mapping hardware. So for the next decade or so, the top 17 bits of all pointers to anywhere in the userspace of
	any x86-64 application are zero. 17 bits ? Yep - all existing OS use the MSB to distinguish between kernel and user address
	space - so no legal pointer can ever have the MSB be 1. And due to the way the 47 bits are required to be sign-extended, the
	effective user space goes from 0x0000 0000 0000 0000 to 0x0000 7FFF FFFF FFFF. So that is why we can safely abuse the top
	17bit to store other information, as long as we zero those bits out before the pointer is actually used.
	*/
	#define PTR_CLR(x) (x & ((1ULL << 47) - 1ULL))
	uintptr_t			mTitle;      // the two MSBytes hold offsets to make the const char * point to the 2nd and 3rd word in the std::string
	WED_PropertyHelper* mParent;
#endif
};

#if PROP_PTR_OPT
class relPtr {
public:
						relPtr() : mItemsCount(0) {};
	WED_PropertyItem *	operator [] (int n) const;
	int					size(void) const { return mItemsCount; }
	void 				push_back(WED_PropertyItem * ptr);
private:
#pragma pack (push)
#pragma pack (1)
	unsigned char 		mItemsCount;
	unsigned char 		mItemsOffs[PROP_PTR_OPT];
#pragma pack (pop)
};
#endif

class WED_PropertyHelper : public WED_XMLHandler, public IPropertyObject {
public:

	virtual	int		FindProperty(const char * in_prop) const;
	virtual	int		CountProperties(void) const;
	virtual	void		GetNthPropertyInfo(int n, PropertyInfo_t& info) const;
	virtual	void		GetNthPropertyDict(int n, PropertyDict_t& dict) const;
	virtual	void		GetNthPropertyDictItem(int n, int e, std::string& item) const;
	virtual	void		GetNthProperty(int n, PropertyVal_t& val) const;
	virtual	void		SetNthProperty(int n, const PropertyVal_t& val);
	virtual	void		DeleteNthProperty(int n) { };

	virtual	void		PropEditCallback(int before)=0;
	virtual	int					CountSubs(void)=0;
	virtual	IPropertyObject *	GetNthSub(int n)=0;

	// Utility to help manage streaming
				void 		ReadPropsFrom(IOReader * reader);
				void 		WritePropsTo(IOWriter * writer);
				void		PropsToXML(WED_XMLElement * parent);

	virtual	void		StartElement(
								WED_XMLReader * reader,
								const XML_Char *	name,
								const XML_Char **	atts);
	virtual	void		EndElement(void);
	virtual	void		PopHandler(void);

	// This is virtual so remappers like WED_Runway can "fix" the results
	virtual	int		PropertyItemNumber(const WED_PropertyItem * item) const;

#if PROP_PTR_OPT
	relPtr				mItems;
#else
	std::vector<WED_PropertyItem *>		mItems;
#endif

	friend class		WED_PropertyItem;
};

// ------------------------------ A LIBRARY OF HANDY MEMBER VARIABLES ------------------------------------

// An integer value entered as text.
class	WED_PropIntText : public WED_PropertyItem {
public:

	int				value;
	int				mDigits;

							operator int&() { return value; }
							operator int() const { return value; }
	WED_PropIntText& operator=(int v);

	WED_PropIntText(WED_PropertyHelper * parent, const char * title, int offset, int initial, int digits) :
		WED_PropertyItem(parent, title, offset), value(initial), mDigits(digits) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);

};

// A true-false value, stored as an int, but edited as a check-box.
class	WED_PropBoolText : public WED_PropertyItem {
public:

	int				value;

							operator int&() { return value; }
							operator int() const { return value; }
	WED_PropBoolText& operator=(int v);

	WED_PropBoolText(WED_PropertyHelper * parent, const char * title, int offset, int initial) :
		WED_PropertyItem(parent, title, offset), value(initial) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// A double value edited as text.
class	WED_PropDoubleText : public WED_PropertyItem {
public:

	double			value;

#pragma pack (push)
#pragma pack (1)
	char			mDigits;
	char			mDecimals;
	char 			mUnit[6];  // this can be non-zero terminated if desired unit text is 6 chars (or longer, but its truncated then)
#pragma pack (pop)

							operator double&() { return value; }
							operator double() const { return value; }
	WED_PropDoubleText&	operator=(double v);

	WED_PropDoubleText(WED_PropertyHelper * parent, const char * title, int offset, double initial, int digits, int decimals, const char * unit = "") :
		WED_PropertyItem(parent, title, offset), mDigits(digits), mDecimals(decimals), value(initial) { strncpy(mUnit,unit,6); }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

class	WED_PropFrequencyText : public WED_PropDoubleText {
public:
	WED_PropFrequencyText(WED_PropertyHelper * parent, const char * title, int offset, double initial, int digits, int decimals)
		: WED_PropDoubleText(parent, title, offset, initial, digits, decimals) { AssignFrom1Khz(GetAs1Khz()); }

	WED_PropFrequencyText& operator=(double v) { WED_PropDoubleText::operator=(v); return *this; }

	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);

	int		GetAs1Khz(void) const;
	void	AssignFrom1Khz(int freq_1khz);

};

// A double value edited as text.  Stored in meters, but displayed in feet or meters, depending on UI settings.
class	WED_PropDoubleTextMeters : public WED_PropDoubleText {
public:
	WED_PropDoubleTextMeters(WED_PropertyHelper * parent, const char * title, int offset, double initial, int digits, int decimals) :
		WED_PropDoubleText(parent, title, offset, initial, digits, decimals) { }

	WED_PropDoubleTextMeters& operator=(double v) { WED_PropDoubleText::operator=(v); return *this; }

	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual void		GetPropertyInfo(PropertyInfo_t& info);
};

// A std::string, edited as text.
class	WED_PropStringText : public WED_PropertyItem {
public:

	std::string			value;

							operator std::string&() { return value; }
							operator std::string() const { return value; }
	WED_PropStringText&	operator=(const std::string& v);

	WED_PropStringText(WED_PropertyHelper * parent, const char * title, int offset, const std::string& initial) :
		WED_PropertyItem(parent, title, offset), value(initial) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// A file path, saved as an STL std::string, edited by the file-open dialog box.
class	WED_PropFileText : public WED_PropertyItem {
public:

	std::string			value;

						operator std::string&() { return value; }
						operator std::string() const { return value; }
	WED_PropFileText& operator=(const std::string& v);

	WED_PropFileText(WED_PropertyHelper * parent, const char * title, int offset, const std::string& initial) :
		WED_PropertyItem(parent, title, offset), value(initial) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// An enumerated item.  Stored as an int, edited as a popup menu.  Property knows the "domain" the enum belongs to.
class	WED_PropIntEnum : public WED_PropertyItem {
public:

	int			value;
	int			domain;

						operator int&() { return value; }
						operator int() const { return value; }
	WED_PropIntEnum& operator=(int v);

	WED_PropIntEnum(WED_PropertyHelper * parent, const char * title, int offset, int idomain, int initial) :
		WED_PropertyItem(parent, title, offset), value(initial), domain(idomain) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// A std::set of enumerated items.  Stored as an STL std::set of int values, edited as a multi-check popup.  We store the domain.
// Exclusive?  While the data model is always a std::set, the exclusive flag enforces "pick at most 1" behavior in the UI (e.g. pick a new value deselects the old) - some users like that sometimes.
// In exclusive a user CAN pick no enums at all.  (Set enums usually don't have a "none" enum value.)
class	WED_PropIntEnumSet : public WED_PropertyItem, public WED_XMLHandler {
public:

	std::set<int>	value;
	int			domain;
	int			exclusive;

						operator std::set<int>&() { return value; }
						operator std::set<int>() const { return value; }
	WED_PropIntEnumSet& operator=(const std::set<int>& v);

	WED_PropIntEnumSet& operator+=(const int v)
	{ if(value.count(v) == 0)
		{ if (GetParent()) GetParent()->PropEditCallback(1);
			value.insert(v);
			if (GetParent()) GetParent()->PropEditCallback(0);
		}
		return *this;
	}
	WED_PropIntEnumSet(WED_PropertyHelper * parent, const char * title, int offset, int idomain, int iexclusive) :
		WED_PropertyItem(parent, title, offset), domain(idomain), exclusive(iexclusive) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
	virtual	bool		WantsElement(WED_XMLReader * reader, const char * name);

	virtual void		StartElement(
								WED_XMLReader * reader,
								const XML_Char *	name,
								const XML_Char **	atts);
	virtual	void		EndElement(void);
	virtual	void		PopHandler(void);

};

// Set of enums stored as a bit-field.  The export values for the enum domain must be a bitfield.
// This is:
// - Stored as a std::set<int> internally.
// - Almost always saved/restored as a bit-field.
// - Edited as a popup with multiple checks.
class	WED_PropIntEnumBitfield : public WED_PropertyItem {
public:

	std::set<int>		value;
	int			domain;
	int			can_be_none;

						operator std::set<int>&() { return value; }
						operator std::set<int>() const { return value; }
	WED_PropIntEnumBitfield& operator=(const std::set<int>& v);

	WED_PropIntEnumBitfield(WED_PropertyHelper * parent, const char * title, int offset, int idomain, int be_none) :
		WED_PropertyItem(parent, title, offset), domain(idomain), can_be_none(be_none) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);

};


// VIRTUAL ITEM: A FILTERED display.
// This item doesn't REALLY create data - it provides a filtered view of another enum std::set, showing only the enums within a given range.
// This is used to take ALL taxiway attributes and show only lights or only lines.
class	WED_PropIntEnumSetFilter : public WED_PropertyItem {
public:

	const char *		host;

	short					minv;
	short					maxv;
	bool					exclusive;

	WED_PropIntEnumSetFilter(WED_PropertyHelper * parent, const char * title, int offset, const char * ihost, int iminv, int imaxv, int iexclusive) :
		WED_PropertyItem(parent, title, offset), host(ihost), minv(iminv), maxv(imaxv), exclusive(iexclusive) { }

	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// VIRTUAL ITEM: a UNION display.  Property helpers can contain "sub" property helpers.  For the WED hierarchy, each hierarchy item (WED_Thing) is a
// property helper (with properties inside it) and the sub-items in the hierarchy are the sub-helpers.  Thus a property item's parent (the "helper" sub-class)
// gives access to sub-items.  This filter looks at all enums on all children and unions them.
// We use this to let a user edit the marking attributes of all lines by editing the taxiway itself.
class	WED_PropIntEnumSetUnion : public WED_PropertyItem {
public:

	const char *		host;
	int					exclusive;

	WED_PropIntEnumSetUnion(WED_PropertyHelper * parent, const char * title, int offset, const char * ihost, int iexclusive) :
		WED_PropertyItem(parent, title, offset), host(ihost), exclusive(iexclusive) { }
	virtual void		GetPropertyInfo(PropertyInfo_t& info);
	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual	void		GetPropertyDictItem(int e, std::string& item);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
	virtual	void 		ReadFrom(IOReader * reader);
	virtual	void 		WriteTo(IOWriter * writer);
	virtual	void		ToXML(WED_XMLElement * parent);
	virtual	bool		WantsAttribute(const char * ele, const char * att_name, const char * att_value);
};

// VIRTUAL ITEM: A FILTERED matrix display.
// This item doesn't REALLY create data - it provides a filtered view of another enum std::set, showing only the enums within a given range.
// This is used to take ALL taxiway attributes and show only lights or only lines.

class	WED_PropIntEnumSetFilterVal : public WED_PropIntEnumSetFilter {
public:

	WED_PropIntEnumSetFilterVal(WED_PropertyHelper * parent, const char * title, int offset, const char * ihost, int iminv, int imaxv, int iexclusive) :
		WED_PropIntEnumSetFilter(parent, title, offset, ihost, iminv, imaxv, iexclusive) { }

	virtual	void		GetPropertyDict(PropertyDict_t& dict);
	virtual void		GetProperty(PropertyVal_t& val) const;
	virtual void		SetProperty(const PropertyVal_t& val, WED_PropertyHelper * parent);
};

#endif
