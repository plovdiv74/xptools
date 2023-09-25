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
#include "ObjTables.h"
#include "ConfigSystem.h"
#include "DEMTables.h"
#include "EnumSystem.h"
#include "ParamDefs.h"

RepTable						gRepTable;
RepFeatureIndex					gRepFeatureIndex;
FeatureInfoTable				gFeatures;
//FeatureToRepTable				gFeatureToRep;
//std::set<int>						gFeatureAsFacade;

static std::set<int>					sKnownFeatures;
static std::set<int>					sFeatureObjs;

//RepAreaIndex					gFacadeAreaIndex;
//RepAreaIndex					gObjectAreaIndex;
RepUsageTable					gRepUsage;
int								gRepUsageTotal = 0;
RepTableTerrainIndex			gRepTableTerrainIndex;

std::string							gObjPlacementFile;
std::string							gObjLibPrefix;

static int ObjScheduleJump(int height)
{
	// This is the "Obj Jump schedule" - it indicates the increments between successive objects.
	if (height >= 200) return height+50;		//   0-100 meters: 10 meter jumps.
	if (height >= 100) return height+20; 	// 100-200 meters: 20 meter jumps
					   return height+10; 	// 200+    meters: 50 meter jumps
}

bool	ReadPrefixLine(const vector<std::string>& tokens, void * ref)
{
	gObjLibPrefix = tokens[1];
	return true;
}

bool	ReadRepLine(const vector<std::string>& tokens, void * ref)
{
	RepInfo_t	info;
	int row_num;
	if (tokens[0] == "OBJ_PROP")
	{
		if (TokenizeLine(tokens, " eefffiie",
			&info.feature, &info.terrain,

//			&info.temp_min, &info.temp_max,
//			&info.rain_min, &info.rain_max,
//			&info.slope_min, &info.slope_max,

//			&info.urban_dense_min, &info.urban_dense_max,
//			&info.urban_radial_min, &info.urban_radial_max,
//			&info.urban_trans_min, &info.urban_trans_max,

//			&info.freq, &info.max_num,
			&info.width_min,
			&info.depth_min,
			&info.height_max,
			&info.road,
			&info.fill,
			&info.obj_name) != 9) return false;

		info.obj_type = rep_Obj;
		info.width_max = info.width_min;
		info.depth_max = info.depth_min;
		info.height_min = 0;
		row_num = gRepTable.size();
		gRepTable.push_back(info);

		if (info.feature != NO_VALUE)	sFeatureObjs.insert(info.obj_name);

		if (gRepFeatureIndex.count(info.obj_name) > 0)
		{
			RepInfo_t& master(gRepTable[gRepFeatureIndex[info.obj_name]]);
//			if (master.freq      != info.freq		)	printf("WARNING: inconsistent frequency for object %s\n", FetchTokenString(info.obj_name));
//			if (master.max_num   != info.max_num	)	printf("WARNING: inconsistent max num for object %s\n", FetchTokenString(info.obj_name));
			if (master.width_min != info.width_min	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
			if (master.width_max != info.width_max	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
			if (master.height_min != info.height_min)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
			if (master.height_max != info.height_max)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
			if (master.depth_min != info.depth_min	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));
			if (master.depth_max != info.depth_max	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));

			if (master.obj_type != info.obj_type	)	printf("WARNING: inconsistent type for object %s\n", FetchTokenString(info.obj_name));
		} else
			gRepFeatureIndex[info.obj_name] = row_num;
	}
	else if (tokens[0] == "OBS_PROP")
	{
		std::string base_name;
		int		height_min, height_max;
		if (TokenizeLine(tokens, " eeffiiiis",
			&info.feature, &info.terrain,

//			&info.temp_min, &info.temp_max,
//			&info.rain_min, &info.rain_max,
//			&info.slope_min, &info.slope_max,

//			&info.urban_dense_min, &info.urban_dense_max,
//			&info.urban_radial_min, &info.urban_radial_max,
//			&info.urban_trans_min, &info.urban_trans_max,

//			&info.freq, &info.max_num,
			&info.width_min,
			&info.depth_min,
			&height_min, &height_max,
			&info.road,
			&info.fill,
			&base_name) != 10) return false;

		info.obj_type = rep_Obj;
		info.width_max = info.width_min;
		info.depth_max = info.depth_min;

		if (height_min % 10) printf("WARNING: object %s min height %d not multiple of 10 meters.\n", base_name.c_str(), height_min);
		if (height_max % 10) printf("WARNING: object %s max height %d not multiple of 10 meters.\n", base_name.c_str(), height_max);

		vector<int>	heights;
		int h;
		for (h = height_min; h <= height_max; h = ObjScheduleJump(h))
			heights.push_back(h);

		for (vector<int>::reverse_iterator riter = heights.rbegin(); riter != heights.rend(); ++riter)
		{
			h = *riter;
			info.height_max = h;
			info.height_min = 0;
			char	obj_name[256];
			sprintf(obj_name,"%s%d", base_name.c_str(), h);
			info.obj_name = LookupTokenCreate(obj_name);
			row_num = gRepTable.size();
			gRepTable.push_back(info);

			if (info.feature != NO_VALUE)	sFeatureObjs.insert(info.obj_name);

			if (gRepFeatureIndex.count(info.obj_name) > 0)
			{
				RepInfo_t& master(gRepTable[gRepFeatureIndex[info.obj_name]]);
//				if (master.freq      != info.freq		)	printf("WARNING: inconsistent frequency for object %s\n", FetchTokenString(info.obj_name));
//				if (master.max_num   != info.max_num	)	printf("WARNING: inconsistent max num for object %s\n", FetchTokenString(info.obj_name));
				if (master.width_min != info.width_min	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
				if (master.width_max != info.width_max	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
				if (master.height_min != info.height_min)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
				if (master.height_max != info.height_max)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
				if (master.depth_min != info.depth_min	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));
				if (master.depth_max != info.depth_max	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));

				if (master.obj_type != info.obj_type	)	printf("WARNING: inconsistent type for object %s\n", FetchTokenString(info.obj_name));
			} else
				gRepFeatureIndex[info.obj_name] = row_num;
		}
	}
	else
	{
		if (TokenizeLine(tokens, " eeffffffiie",
			&info.feature, &info.terrain,

//			&info.temp_min, &info.temp_max,
//			&info.rain_min, &info.rain_max,
//			&info.slope_min, &info.slope_max,

//			&info.urban_dense_min, &info.urban_dense_max,
//			&info.urban_radial_min, &info.urban_radial_max,
//			&info.urban_trans_min, &info.urban_trans_max,

//			&info.freq, &info.max_num,
			&info.width_min, &info.width_max,
			&info.depth_min, &info.depth_max,
			&info.height_min, &info.height_max,
			&info.road,
			&info.fill,
			&info.obj_name) != 12) return false;

		info.obj_type = rep_Fac;
		row_num = gRepTable.size();
		gRepTable.push_back(info);

		if (info.feature != NO_VALUE)	sFeatureObjs.insert(info.obj_name);

		if (gRepFeatureIndex.count(info.obj_name) > 0)
		{
			RepInfo_t& master(gRepTable[gRepFeatureIndex[info.obj_name]]);
//			if (master.freq      != info.freq		)	printf("WARNING: inconsistent frequency for object %s\n", FetchTokenString(info.obj_name));
//			if (master.max_num   != info.max_num	)	printf("WARNING: inconsistent max num for object %s\n", FetchTokenString(info.obj_name));
			if (master.width_min != info.width_min	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
			if (master.width_max != info.width_max	)	printf("WARNING: inconsistent width for object %s\n", FetchTokenString(info.obj_name));
			if (master.height_min != info.height_min)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
			if (master.height_max != info.height_max)	printf("WARNING: inconsistent height for object %s\n", FetchTokenString(info.obj_name));
			if (master.depth_min != info.depth_min	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));
			if (master.depth_max != info.depth_max	)	printf("WARNING: inconsistent depth for object %s\n", FetchTokenString(info.obj_name));

			if (master.obj_type != info.obj_type	)	printf("WARNING: inconsistent type for object %s\n", FetchTokenString(info.obj_name));
		} else
			gRepFeatureIndex[info.obj_name] = row_num;

	}


	if (info.feature != NO_VALUE)	sKnownFeatures.insert(info.feature);

	return true;
}

bool	ReadFeatureProps(const std::vector<std::string>& tokens, void * ref)
{
	FeatureInfo info;
	int	key;
	if (TokenizeLine(tokens, " efe", &key, &info.property_value, &info.terrain_type) != 4)
		return false;
	if (gFeatures.find(key) != gFeatures.end())
		printf("WARNING: duplicate key %s", tokens[1].c_str());
	gFeatures[key] = info;
	return true;
}

/*
bool	ReadFeatureToRep(const vector<string>& tokens, void * ref)
{
	FeatureToRep_t i;
	int k;
	if (TokenizeLine(tokens, " eefffi", &k, &i.rep_type,
		&i.min_urban, &i.max_urban, &i.area_density, &i.area_max) != 7) return false;

	gFeatureToRep.insert(FeatureToRepTable::value_type(k, i));
	return true;
}
*/
void	LoadObjTables(void)
{
	gRepTable.clear();
	gRepFeatureIndex.clear();
	gFeatures.clear();
	sKnownFeatures.clear();
	sFeatureObjs.clear();
//	gFacadeAreaIndex.clear();
//	gObjectAreaIndex.clear();
//	gRepUsage.clear();

	RegisterLineHandler("OBJ_PROP", ReadRepLine, NULL);
	RegisterLineHandler("OBJ_PREFIX", ReadPrefixLine, NULL);
	RegisterLineHandler("OBS_PROP", ReadRepLine, NULL);
	RegisterLineHandler("FAC_PROP", ReadRepLine, NULL);
	RegisterLineHandler("FEAT_PROP", ReadFeatureProps, NULL);
//	RegisterLineHandler("FEAT_2_OBJ", ReadFeatureToRep, NULL);
	if (gObjPlacementFile.empty())	LoadConfigFile("obj_properties.txt");
	else							LoadConfigFileFullPath(gObjPlacementFile.c_str());
	LoadConfigFile("feat_properties.txt");
//	LoadConfigFile("feat_2_obj.txt");

//	for (FeatureToRepTable::iterator i = gFeatureToRep.begin(); i != gFeatureToRep.end(); ++i)
//	{
//		if (!gRepTable[gRepFeatureIndex[i->second.rep_type]].fac_name.empty())
//			gFeatureAsFacade.insert(i->first);
//	}

	std::hash_map<int, int>	mins, maxs;
	for (int n = 0; n < gRepTable.size(); ++n)
	{
		int terrain = gRepTable[n].terrain;
		if (mins.count(terrain)==0)		mins[terrain] = n;
		else							mins[terrain] = min(mins[terrain], n);

										maxs[terrain] = max(maxs[terrain], n+1);
	}

	for (std::hash_map<int, int>::iterator range = mins.begin(); range != mins.end(); ++range)
	{
		int terrain = range->first;
		int ilow = range->second;
		int ihi = maxs[terrain];
		gRepTableTerrainIndex[terrain] = std::pair<int,int>(ilow, ihi);
	}
}

/************************************************************************************************
 * DATABASE OPERATIONS
 ************************************************************************************************/

#define RANGE_RULE(x)		(rec.x ## _min == rec.x ## _max || (rec.x ## _min <= x && x <= rec.x ## _max))

int	QueryUsableFacsBySize(
					// Rule inputs!
					int				feature,
					int				terrain,

//					float			temp,
//					float			rain,
//					float			slope,
//					float			urban_dense,
//					float			urban_radial,
//					float			urban_trans,

					float			inLongSide,
					float			inShortSide,
					float			inTargetHeight,

//					bool			inLimitUsage,	// True if we DO want to apply freq rule limits.
					int *			outResults,
					int				inMaxResults)
{
	int 						ret = 0;
	std::pair<int,int>				range = gRepTableTerrainIndex[terrain];
	for (int row = range.first; row < range.second; ++row)
	{
		RepInfo_t& rec = gRepTable[row];

		// Evaluate this choice
		if (rec.obj_type == rep_Fac)
//		if ((rec.max_num == 0 || rec.max_num > gRepUsage[rec.obj_name]) &&
//			(rec.freq == 0.0 || (rec.freq * (float) gRepUsageTotal >= gRepUsage[rec.obj_name])) &&

			// Enum rules
		if ((rec.feature == feature) &&
			(rec.terrain == NO_VALUE || rec.terrain == terrain) &&
			// Range Rules
//			RANGE_RULE(temp) &&
//			RANGE_RULE(slope) &&
//			RANGE_RULE(rain) &&
//			RANGE_RULE(urban_dense) &&
//			RANGE_RULE(urban_radial) &&
//			RANGE_RULE(urban_trans) &&

			(inLongSide >= rec.width_min && inLongSide <= rec.width_max) &&			// FACADES: the width range limits the 'big' side, the
			(inShortSide >= rec.depth_min && inShortSide <= rec.depth_max) &&		// depth range limits the 'small' side.  We must know this - we are making a facade.

			(inTargetHeight >= rec.height_min && inTargetHeight <= rec.height_max))
		{
			outResults[ret] = row;
			++ret;
			if (ret >= inMaxResults)
				return ret;
		}
	}
	return ret;
}


int QueryUsableObjsBySize(
					// Rule inputs!
					int				feature,
					int				terrain,

//					float			temp,
//					float			rain,
//					float			slope,
//					float			urban_dense,
//					float			urban_radial,
//					float			urban_trans,

					float			inWidth,
					float			inDepth,
					float			inHeightMax,	// If min = max, we want an exact height!

					int				road,
					int				fill,

//					bool			inLimitUsage,	// True if we DO want to apply freq rule limits.
					int *			outResults,

					int				inMaxResults)
{
	int 						ret = 0;

	// Objects are sorted by size.  We know by definition that an
	// object won't fit in a block to osmsall for it.  So we use
	// area as a quick-eval to skip past the most high priorty but
	// hugest objects.

	// Note that we cannot use side length as a heueristic
	// for placement.  Consider an antenna...the end of the antenna
	// is a TINY side the length that the road is wide.  But
	// since the antenna is in the smack middle of the facade, it
	// is conceivable that a huge object could fit there.

	std::pair<int,int>				range = gRepTableTerrainIndex[terrain];
	for (int row = range.first; row < range.second; ++row)
	{
		RepInfo_t& rec = gRepTable[row];

		// Evaluate this choice
		if (rec.obj_type == rep_Obj)
//		if ((rec.max_num == 0 || rec.max_num > gRepUsage[rec.obj_name]) &&
//			(rec.freq == 0.0 || (rec.freq * (float) gRepUsageTotal >= gRepUsage[rec.obj_name])) &&

			// Enum rules
		if ((rec.feature == feature) &&
			(rec.terrain == NO_VALUE || rec.terrain == terrain) &&
			// Range Rules
//			RANGE_RULE(slope) &&
//			RANGE_RULE(temp) &&
//			RANGE_RULE(rain) &&
//			RANGE_RULE(urban_dense) &&
//			RANGE_RULE(urban_radial) &&
//			RANGE_RULE(urban_trans) &&
			// Obj Rules
			(inWidth == -1 || (inWidth >= rec.width_max)) &&					// FOR OBJECTS: give an object if (1) we have NO idea how big this slot is (try 'em all)
			(inDepth == -1 || (inDepth >= rec.depth_max)) &&					// or if the lot is at least as bigger than the obj

			(inHeightMax >= rec.height_max) &&				// For objs - obj height less than max!

			(!fill || rec.fill) &&
			(!road || rec.road))
		{
			outResults[ret] = row;
			++ret;
			if (ret >= inMaxResults)
				return ret;
		}
	}
	return ret;
}

void IncrementRepUsage(int inRep)
{
	gRepUsage[inRep]++;
	gRepUsageTotal++;
}

void ResetUsages(void)
{
	gRepUsage.clear();
	gRepUsageTotal = 0;
}

bool IsWellKnownFeature(int inFeat)
{
	return sKnownFeatures.count(inFeat);
}

bool IsFeatureObject(int inName)
{
	return sFeatureObjs.count(inName);
}

void CheckObjTable(void)
{
	for (int n = 0; n < gRepTable.size(); ++n)
		if (gRepTable[n].terrain != NO_VALUE && gNaturalTerrainInfo.count(gRepTable[n].terrain) == 0)
			printf("WARNING: object %s references unknown terrain %s\n",FetchTokenString(gRepTable[n].obj_name), FetchTokenString(gRepTable[n].terrain));
}

void GetObjTerrainTypes(std::set<int>& outTypes)
{
	for (int n = 0; n < gRepTable.size(); ++n)
	if (gRepTable[n].terrain != NO_VALUE)
		outTypes.insert(gRepTable[n].terrain);
}
