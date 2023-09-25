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
#ifndef NETTABLES_H
#define NETTABLES_H

#include "XESConstants.h"

/*
	Terminology note:
	
		FEATURE = an ABSTRACT road modeled on real world data, e.g. a "primary one-way limited access highway".  Comes from GIS data.
		REP = an ACTUAL specific road type, e.g. six-lane divided highway.  A translation of the GIS type based on some circumstances
		
		FEATURES can help define things like density of urban areas.
		REPS have metrics like how big they are, can they spawn buildings, etc.
*/

struct	NetFeatureInfo {
	float		density_factor;
	int			oneway_feature;
	int			is_oneway;
};
typedef std::hash_map<int, NetFeatureInfo>	NetFeatureInfoTable;
extern	NetFeatureInfoTable				gNetFeatures;

struct	NetRepInfo {
	float		semi_l;
	float		semi_r;
	inline float width() const { return semi_l + semi_r; }
	float		pad;
	float		building_percent;
//	float		max_slope;
	int			use_mode;
	int			is_oneway;	
//	int			export_type_normal;
//	int			export_type_overpass;
	int			export_type_draped;
	float		crease_angle_cos;				// Cosine of the angle, where for any sharper turn, we have a sharp cut and not a bezier curve
	float		min_defl_deg_mtr;				// Minimum turn (in degrees) for each meter of road before we say "this is straight".
												// So a 1 degree per meter turn in a highway could be a long arcing turn but a 1 deg/mtr
												// turn in a city street is probably a "notch"
	float		max_err;
};
typedef std::hash_map<int, NetRepInfo>				NetRepInfoTable;
extern 	NetRepInfoTable							gNetReps;

struct	Feature2RepInfo {
	int			feature;
	float		min_density;
	float		max_density;
	float		min_rail;
	float		max_rail;
//	std::set<int>	zoning_left;
//	std::set<int>	zoning_right;
	float		rain_min;
	float		rain_max;
	float		temp_min;
	float		temp_max;
	int			rep_type;
};
typedef std::vector<Feature2RepInfo>		Feature2RepInfoTable;
extern	Feature2RepInfoTable					gFeature2Rep;

extern std::set<int>									gPromotedZoningSet;

struct	ZoningPromote {
	int			promote_left;
	int			promote_right;
	int			promote_both;
};
typedef std::hash_map<int, ZoningPromote>			ZonePromoteTable;
extern ZonePromoteTable								gZonePromote;

typedef std::hash_map<int, int>						RoadCountryTable;
extern RoadCountryTable							gRoadCountry;

struct ForkRule {
	int			trunk;
	int			left;
	int			right;
	int			new_trunk;
	int			new_left;
	int			new_right;
};
typedef std::vector<ForkRule>						ForkRuleTable;
extern ForkRuleTable							gForkRules;

struct ChangeRule {
	int			prev;
	int			next;
	int			new_mid;
};
typedef std::vector<ChangeRule>						ChangeRuleTable;
extern ChangeRuleTable							gChangeRules;

typedef std::map<int,int>							LevelCrossingTable;
extern LevelCrossingTable						gLevelCrossings;

struct	BridgeInfo {
	int			rep_type;

	// Rulez
	float		min_length;
	float		max_length;
	float		min_seg_length;
	float		max_seg_length;
	float		min_seg_count;
	float		max_seg_count;
	float		curve_limit;	// Expressed as a DOT product (Cosine) - 0 means no limit, 1.0 means straight!

	// Splitting
	int			split_count;
	float		split_length;
	int			split_arch;

	// Geometry
	float		min_start_agl;
	float		max_start_agl;
	float		search_dist;
	float		pref_start_agl;

	float		min_center_agl;
	float		max_center_agl;
	float		height_ratio;
	float		road_slope;

	// Export to X-Plane
	int			export_type;
};
typedef	std::vector<BridgeInfo>				BridgeInfoTable;
extern	BridgeInfoTable					gBridgeInfo;

extern std::map<int,int>						gTwinRules;

void	LoadNetFeatureTables(rf_region inRegion);

//bool	IsSeparatedHighway(int feat_type);
//int		SeparatedToOneway(int feat_type);
bool	IsOneway(int rep_type);

bool	IsTwinRoads(int rep_type1, int rep_type2);

int		FindBridgeRule(int rep_type, double len, double smallest_seg, double biggest_seg, int num_segments, double curve_dot, double agl1, double agl2);

#endif
