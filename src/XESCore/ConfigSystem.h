/*
 * Copyright (c) 2004, Laminar Research.
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
#ifndef CONFIGSYSTEM_H
#define CONFIGSYSTEM_H

struct	RGBColor_t {
	float		rgb[3];
};


// Prototype for processing functions.  You are returned a tokenized line with
// the parsing token as the first item.  Return true if successful.
typedef bool (* ProcessConfigString_f)(const std::vector<std::string>& inTokenLine, void * inRef);

// Call this routine to register a line handler.  It returns true if successful, false
// if not.  The only typical reason for failure is that the token is already in use.
bool	RegisterLineHandler(
					const char * 			inToken,
					ProcessConfigString_f 	inHandler,
					void * 					inRef);

// Locate a config file by name.  (Handles CFM weirdness nicely.)
std::string	FindConfigFile(const char * inFileName);

// Call this routine to parse a file.  Returns true if successful; it is halted if
// (1) an I/O error occurs, (2) one of the line handlers returns false, indicating
// a line error, or (3) an unknown parsing token is found.
bool	LoadConfigFile(const char * inFilename);
bool	LoadConfigFileFullPath(const char * inFilename);

// Same as above, except the config file is only loaded the first time
// this is called.  This is useful for lazy on-demand loading of prefs files.
bool	LoadConfigFileOnce(const char * inFilename);

void	DebugPrintTokens(const std::vector<std::string>& tokens);

// A few useful parsers
int					TokenizeInt(const std::string&);
float				TokenizeFloat(const std::string&);
float				TokenizeFloatWithEnum(const std::string&);
bool				TokenizeColor(const std::string&, RGBColor_t&);
bool				TokenizeEnum(const std::string& token, int& slot, const char * errMsg);
bool				TokenizeEnumSet(const std::string& tokens, std::set<int>& slots);

// Format is:
// i - int
// f - float
// F - float, but enums are legal and translated.
// c - color
// e - enum
// s - STL string
// t = char **
// S - enum std::set
// P - Point2, splatted
//   - skip
int				TokenizeLine(const std::vector<std::string>& tokens, const char * fmt, ...);

#endif

