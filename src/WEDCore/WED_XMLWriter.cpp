/* 
 * Copyright (c) 2011, Laminar Research.
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

#include "WED_XMLWriter.h"
#include "AssertUtils.h"
#include "GUI_Unicode.h"
/*
	PERFORMANCE NOTES:

	We could improve memory management by allocating a big shared block of memory and just pushing a ptr
	to get memory to stash strings (rather than using STL strings).
	
	If we do this we need to know when to purge out the memory...the answer:
	
	- Each time an obj starts using a block, add a ref to it.
	- Each time the obj is nuked, decrease ref count.
	- each time the current block is filled, start a new one.
	
	The result: as we run through the files and purge out the <object> blocks, 
	filled blocks will be retired for re-use.  We save a million tiny alloc/deallocs for STL strings.	
	
	2019 update: 
	
	The XML writer code is now propagating const char * rather than strings wherever possible, 
	so most of those strings are gone.
	The overhead of formatted printing ( fprintf() vs fputs() ) was removed and only std::string values are
	subjected to XML escape sequences conversion ( numbers will NEVER have non-ASCII content ), reducing
	XML write time for large files by 35%.
	
	Benchmarked throughtput is 75 sec for 14 million items (global airports) to create a 5.5GB xml file.
	
	2020 update:
	
	Half the putc() and all of the sprintf() are gone now, down to 60sec for the 5.5GB XML file now.
	The STL memory allocation for STL strings aren't much of an issue - as all but long text parameters 
	all fit into the intrinsic space inside the std::string objects itself. Together with reserve(7) for the
	attributes, which are now a std::vector - that means a single malloc for most WED_XMLElements.

	2022 update:

	Undder windows, all I/O library calls suck. Saving .xml is half as fast as under either OSX or Linux.
	So the number of I/O calls is reduced greatly by pre-assembling full lines and fwrite() once per line 
	in most cases for a few percent speedup on Linux, but double the speed on Windows.
	
	Throughput is now on all OS around 50 sec for a 5.5GB xml file.
*/

#define FIX_EMPTY 0
#define FAST_PRINTF_REPLACEMENTS 1

static void fput_indented_name(int n, FILE* fi, const char *name, bool add_slash = false) 
{
#if FAST_PRINTF_REPLACEMENTS
	char c[32];
	auto p = c;
	while (n--)
		*p++ = ' ';
	*p++ = '<';
	if (add_slash)
		*p++ = '/';
	auto l = strlen(name);
	DebugAssert(p - c + l + 3 < sizeof(c));
	memcpy(p, name, l);
	p += l;
	if (add_slash)
	{
		strcpy(p, ">\n");
		p += strlen(">\n");
	}
	fwrite(c, p - c, 1, fi);
#else
	while (n--)
		fputc(' ', fi);
	fputc('<', fi);
	if(add_slash)
		fputc('/', fi);
	fputs(name, fi);
	if (add_slash)
		fputs(">\n", fi);
#endif
}

static std::string str_escape(const std::string& str)
{
	std::string result;
	result.reserve(str.size());

	UTF8 * b = (UTF8 *) str.c_str();
	UTF8 * e = b + str.length();
	
	// This fixes a problem, but not the way I intended, and may be worth some examination.
	// WED uses UTF8.  Period.  That is all it has ever displayed sanely, and it should be the only thing
	// you can get INTO it.  (At least, on mac and windows if you get a non-ASCII char in, it DOES come in
	// as UTF8.  I assume Linux isn't the offender putting ISO-Latin-1 in.)
	//
	// But.  Names of airports come straight from apt.dat, and some users encode the apt.dat file 
	// incorrectly as ISO-Latin-1.  We really only wanted ASCII, and X-Plane never liked ISO, so who knows
	// what is up.  Anyway: it is conceivable that at run time we have ISO-Latin-1 chars that are not valid 
	// UTF8 sequences.
	// This routine writes each invalid 8-bit char as a numeric code reference into the XML.  This has the
	// wrong effect: on read-in, we interpret what WAS a byte as a Unicode char.
	// 
	// And yet, this is actually useful.  It turns out that ISO-Latin-1 128-255 are mostly mapped to
	// unicode 128-255 (but NOT UTF8 128-255, which are bit encodings).  So for example:
	//
	// User wants unicode U+00C5.  In ISO-Latin-1 we have 0xC5.  This displays as a U or something wrong in
	// WED because the char printer will "muddle through" randomly with invalid input.  But we write it out
	// as %#xC5.  On read-in, Expat thinsk this is U+00C5 and gives us the correct 2-byte sequence UTF8 valid
	// sequence 0xC3 0x85.  This is of course what the user ORIGINALLY wanted.
	 
	while(b < e)
	{
		const UTF8 * v = UTF8_ValidRange(b,e);
		while(b < v)
		{	
			switch(*b) {
			case '<':
				result += "&lt;";
				break;			
			case '>':
				result += "&gt;";
				break;
			case '"':
				result += "&quot;";
				break;
			case '&':
				result += "&amp;";
				break;
			default:
				// This is STILL not ideal - XML disallows anything above #x10FFFF or the surrogate blocks, but 
				// for now just notice that control chars are bogus.  Drop control chars, there's just no way to
				// encode them, and frankly they are silly.
				if(*b >= ' ' || *b == '\t' || *b == '\r' || *b == '\n')
					result += *b;
				break;
			}
			++b;
		}
		const UTF8 * iv = UTF8_InvalidRange(b,e);
		while(b < iv)
		{
			// No low-number chars - that blows up the reader.
			if(*b >= ' ' || *b == '\t' || *b == '\r' || *b == '\n')
			{
				char c[8];
				snprintf(c,7,"&#x%02X;",(int) *b);
				result += c;
			}
			++b;
		}
	}
	return result;
}


WED_XMLElement::WED_XMLElement(
									const char *		n,
									int					i,
									FILE *				f) : 
	file(f), indent(i), name(n), flushed(false), parent(NULL)
{
	attrs.reserve(7);
}

WED_XMLElement::~WED_XMLElement()
{
	if (!flushed)
	{
		fput_indented_name(indent, file, name);

// fprintf() is REALLY slow
// unformatted putc(), puts() speed up xml file writing by 2x
// but, especially under windows, any file I/O is SLOW
// so we pre-assemble essentially the whole line and frwite() once per line.
// this further reduces xml save time by 30%

// saving xml not ony sucks when dealing with the global airports (some 10 GB), but we like to do foreground
//  auto-save some day, so saving should really be fast

#if FAST_PRINTF_REPLACEMENTS
		char c[256];
		auto p = c;
		for (const auto& a : attrs)
		{
			*p++ = ' ';
			auto l = strlen(a.first);
			if (p - c + l + 7 > sizeof(c)) goto long_string;
			memcpy(p, a.first, l);
			p += l;

			*p++ = '=';	*p++ = '"';
			l = a.second.size();
			if (p - c + l + 7 > sizeof(c)) goto long_string;
			memcpy(p, a.second.c_str(), l);
			p += l;
			*p++ = '"';
		}

		if (children.empty())
			*p++ = '/';
		strcpy(p, ">\n");
		p += strlen(">\n");

		fwrite(c, p - c, 1, file);
		goto done;
long_string:
#endif
		for (const auto& a : attrs)
		{
			fputc(' ', file); fputs(a.first, file); fputs("=\"", file);
			fputs(a.second.c_str(), file);
			fputc('"', file);
		}
		if (children.empty())
			fputs("/>\n", file);
		else
			fputs(">\n", file);
	}
done:
	for(std::vector<WED_XMLElement *>::iterator c = children.begin(); c != children.end(); ++c)
		delete *c;

	if(!children.empty() || flushed)
		fput_indented_name(indent, file, name, true);
}

void WED_XMLElement::flush()
{
	flush_from(NULL);
}

void WED_XMLElement::flush_from(WED_XMLElement * who)
{
	if(who == NULL && children.empty())	return;

	if(parent)
		parent->flush_from(this);
	parent = NULL;

	if(!flushed)
	{
		fput_indented_name(indent, file, name);

		for(const auto& a : attrs)
		{
			fputc(' ',file); fputs(a.first,file); fputs("=\"",file);
			fputs(a.second.c_str(), file);
			fputc('"',file);
		}
		fputs(">\n",file);
	}

	DebugAssert(who == children.back() || who == NULL);

	for(std::vector<WED_XMLElement *>::iterator c = children.begin(); c != children.end(); ++c)
	if(*c != who)
		delete *c;

	children.clear();
	if(who)
		children.push_back(who);
	flushed = true;
}


void					WED_XMLElement::add_attr_int(const char * name, int value)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
#else
	DebugAssert(name && *name);	
#endif
	DebugAssert(!flushed);
//	attrs[name] = std::to_string(value); // using a map or std::to_string() is friggin slow - wanna spend 10% of the _whole_ time to save in his _one_ line ???
#if FAST_PRINTF_REPLACEMENTS
	if(value == 0)
		attrs.push_back(std::make_pair(name, std::string("0")));
	else
	{
		char c[32];             // suffcient digits to hold even -2^31
		char *p = c+sizeof(c);
		bool negative =  value < 0;
		if(negative) value = -value;

		while (value != 0)
		{
//			int remainder =  value / 10;
			int remainder =  ((long long) value * 214748365) >> 31;  // works for all 32bit ints, takes 4 CPU clocks, rather than 22 for IDIV
			int this_digit = value - 10 * remainder;
			*--p = '0' + this_digit;
			value = remainder;
		}
		if(negative) *--p = '-';
		attrs.push_back(std::make_pair(name, std::string(p, c+sizeof(c)-p)));
	}
#else
	attrs.push_back(std::make_pair(name, std::to_string(value)));
#endif
}

void					WED_XMLElement::add_attr_double(const char * name, double value, int dec)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
#else
	DebugAssert(name && *name);	
#endif
	DebugAssert(!flushed);
	if(value == 0.0)
		attrs.push_back(std::make_pair(name, std::string("0.0")));
	else
	{
		char c[32];
#if FAST_PRINTF_REPLACEMENTS
		char *p = c+sizeof(c)-dec-1;
		bool negative =  value < 0;
		if(negative) value = -value;

		char *dp = p;
		if(dec)
			*dp = '.';
		else
			p++;
		
		if(dec == 9)
			value += 0.0000000005;
		else
		{
			double add(0.5);
			for(int i = 0; i < dec; ++i)
				add *= 0.1;
			value += add;
		}

		int ivalue = value;
		value -= ivalue;

		if(ivalue == 0)
			*--p = '0';
		else
			while(ivalue != 0)
			{
				if(p == c) break;
//				int remainder =  ivalue / 10;
				int remainder =  ((unsigned) ivalue * 52429U) >> 19;   // works upto 81919, takes 4 CPU clocks, rather than 22 for IDIV
				int this_digit =  ivalue - 10 * remainder;
				*--p = '0' + this_digit;
				ivalue = remainder;
			}
		if(negative && p != c) *--p = '-';

		while(dec--)
		{
			value *= 10.0;
			int this_digit = value;
			value -= this_digit;
			*++dp = '0' + this_digit;
		}

		attrs.push_back(std::make_pair(name, std::string(p, c+sizeof(c)-p)));
#else
		snprintf(c, 31, "%.*lf",dec, value);
		attrs.push_back(std::make_pair(name, std::string(c)));
#endif
	}
}

void					WED_XMLElement::add_attr_c_str(const char * name, const char * str)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
	if(str == 0 || *str == 0) str = name;
#else
	DebugAssert(name && *name && str && *str);
#endif
	DebugAssert(!flushed);
	attrs.push_back(std::make_pair(name, str_escape(str)));
}

void					WED_XMLElement::add_attr_stl_str(const char * name, const std::string& str)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
#else
	DebugAssert(name && *name);	
#endif
	DebugAssert(!flushed);
	attrs.push_back(std::make_pair(name, str_escape(str)));
}

WED_XMLElement *		WED_XMLElement::add_sub_element(const char * name)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
#else
	DebugAssert(name && *name);	
#endif

	WED_XMLElement * child = new WED_XMLElement(name, indent + 2, file);
	children.push_back(child);
	child->parent = this;
	return child;
}

WED_XMLElement *		WED_XMLElement::add_or_find_sub_element(const char * name)
{
#if FIX_EMPTY
	if(name == 0 || *name == 0)	name = "tbd";
#else
	DebugAssert(name && *name);	
#endif

	DebugAssert(!flushed);
	std::string n(name);
	for(int i = 0; i < children.size(); ++i)
	if(n == children[i]->name)
		return children[i];
		
	WED_XMLElement * child = new WED_XMLElement(name, indent + 2, file);
	children.push_back(child);
	child->parent = this;
	return child;
}
