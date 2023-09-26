/*
 * Copyright (c) 2016, Laminar Research.
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

#ifndef WED_METADATADEFAULTS_H
#define WED_METADATADEFAULTS_H

#include "CSVParser.h"

class WED_Airport;

// Fill in an airport's meta data's missing or blank entries with defaults
// from the LR official meta data source
// Returns true if the fill was a success, false if not
bool fill_in_airport_metadata_defaults(WED_Airport& airport, const std::string& file_path);

// If you're trying to fill the metadata for a bajillion airports at a time,
// you may want to read the CSV yourself, once, and never again have to hit the disk.
bool fill_in_airport_metadata_defaults(WED_Airport& airport, const CSVParser::CSVTable& table);

// prefixed any existing country meta data with a iso 3166 country code
// tries to dereive the name from the country code meta data by matching against a list of partical strings,
// if that fails tries to dereive country from region code in either ICAO code or ICAO region meta data
// if country meta  is empty or non-existent but a clear ICAO region match exists, country meta data is added
// returns if meta dat was changed

bool add_iso3166_country_metadata(WED_Airport& airport, bool inProgress = false);

extern std::vector<std::vector<const char*>> iso3166_codes;

#endif
