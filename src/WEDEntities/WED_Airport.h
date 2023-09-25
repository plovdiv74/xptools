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

#ifndef WED_AIRPORT_H
#define WED_AIRPORT_H

#include "WED_GISComposite.h"
#include <vector>

struct	AptInfo_t;

class	WED_Airport : public WED_GISComposite {

DECLARE_PERSISTENT(WED_Airport)

public:

	void		GetICAO(std::string& icao) const;
	int			GetAirportType(void) const;
	int			GetSceneryID(void) const;
	
	void		SetSceneryID(int new_id);
	void		SetAirportType(int airport_type);
	void		SetICAO(const std::string& icao);

	typedef std::pair<std::string,std::string> meta_data_entry;

	//--Meta Data API-------------------------------
	//Adds a Meta Data Key. Collision is a replace value
	void		AddMetaDataKey(const std::string& key, const std::string& value);
	
	//Edits a given Meta Data key's value. Non-existant keys are ignored
	void		EditMetaDataKey(const std::string& key, const std::string& value);
	
	//Removes a key/value std::pair
	//void		RemoveMetaDataKey(const std::string& key);
	
	//Returns true if meta_data_vec_map contains the key
	bool		ContainsMetaDataKey(const std::string& key) const;
	bool		ContainsMetaDataKey(int meta_data_enum) const;

	//Gets the size of the Meta Data Vector
	int			CountMetaDataKeys();

	//Returns the key's value, key MUST be in the metadata std::vector already
	std::string		GetMetaDataValue(const std::string& key) const;
	std::string		GetMetaDataValue(int meta_data_enum) const;
	//----------------------------------------------

	void		Import(const AptInfo_t& info, void (* print_func)(void *, const char *, ...), void * ref);
	void		Export(		 AptInfo_t& info) const;

	// IPropertyObject
	int			FindProperty(const char * in_prop) const;
	int			CountProperties(void) const;
	void		GetNthPropertyInfo(int n, PropertyInfo_t& info) const;

	void		GetNthPropertyDict(int n, PropertyDict_t& dict) const;
	void		GetNthPropertyDictItem(int n, int e, std::string& item) const;
	
	void		GetNthProperty(int n, PropertyVal_t& val) const;
	void		SetNthProperty(int n, const PropertyVal_t& val);
	void		DeleteNthProperty(int n);

	//WED_Persistant, for Undo/Redo
	virtual	bool 			ReadFrom(IOReader * reader);
	virtual	void 			WriteTo(IOWriter * writer);

	//WED_Thing
	virtual void			AddExtraXML(WED_XMLElement * obj);
	
	//IOperation
	virtual void		StartElement(
								WED_XMLReader * reader,
								const XML_Char *	name,
								const XML_Char **	atts);
	/*virtual	void		EndElement(void);
	virtual	void		PopHandler(void);*/

	virtual const char *	HumanReadableType(void) const { return "Airport"; }

private:

	WED_PropIntEnum				airport_type;
	WED_PropDoubleTextMeters	elevation;
	WED_PropBoolText			has_atc;
	WED_PropStringText			icao;
	WED_PropBoolText			always_flatten;
	WED_PropBoolText			drive_on_left;
	WED_PropIntText				scenery_id;
	
	//A std::vector of meta data key,value entrys, all synthetic properties
	//Due to the way it is stored in XML, keys are not allowed to contain commas
	std::vector<meta_data_entry>	meta_data_vec_map;
};

#endif /* WED_AIRPORT_H */
