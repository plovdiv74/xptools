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

#ifndef APTDEFS_H
#define APTDEFS_H

#include <vector>
#include "CompGeomDefs2.h"


enum {
	// File format record codes
	apt_airport 		= 1,
	apt_rwy_old 		= 10,			// Legacy runway/taxiway record from 810 and earlier.
	apt_tower_loc 		= 14,
	apt_startup_loc 	= 15,
	apt_seaport 		= 16,
	apt_heliport 		= 17,
	apt_beacon 			= 18,
	apt_windsock 		= 19,
	apt_freq_awos 		= 50,
	apt_freq_ctaf 		= 51,
	apt_freq_del 		= 52,
	apt_freq_gnd 		= 53,
	apt_freq_twr 		= 54,
	apt_freq_app 		= 55,
	apt_freq_dep 		= 56,
	apt_done 			= 99,
	// These records are new with X-Plane 850!
	apt_sign 			= 20,
	apt_papi 			= 21,

	apt_rwy_new 		= 100,			// These replace the old type 10 record.
	apt_sea_new 		= 101,
	apt_heli_new 		= 102,
	apt_rwy_skids		= 105,
	apt_taxi_new 		= 110,
	apt_free_chain		= 120,
	apt_boundary 		= 130,

	apt_lin_seg 		= 111,
	apt_lin_crv 		= 112,
	apt_rng_seg 		= 113,
	apt_rng_crv 		= 114,
	apt_end_seg		 	= 115,
	apt_end_crv 		= 116,

	apt_flow_def		= 1000,			// 1000 <traffic flow name, must be unique to the ICAO airport>
	apt_flow_wind		= 1001,			// 1001 <metar icao> <wind dir min> <wind dir max> <wind max speed>
	apt_flow_ceil		= 1002,			// 1002 <metar icao> <ceiling minimum>
	apt_flow_vis		= 1003,			// 1003 <metar icao> <vis minimum>
	apt_flow_time		= 1004,			// 1004 <zulu time start> <zulu time end>

	apt_freq_awos_1k	= 1050,         // XP 1130 1kHz resolution freq
	apt_freq_ctaf_1k	= 1051,
	apt_freq_del_1k 	= 1052,
	apt_freq_gnd_1k		= 1053,
	apt_freq_twr_1k		= 1054,
	apt_freq_app_1k		= 1055,
	apt_freq_dep_1k		= 1056,

	apt_flow_rwy_rule	= 1100,
	apt_flow_pattern	= 1101,
	apt_flow_rwy_rule1k	= 1110,         // XP 1130 1kHz resolution freq

	apt_taxi_header		= 1200,			// 1200 <name>
	apt_taxi_node		= 1201,			// 1201 <lat> <lon> <type> <id, 0 based sequence, ascending> <name>
	apt_taxi_edge		= 1202,			// 1202 <src> <dst> <oneway flag> <runway flag/taxi width> <name>
	apt_taxi_shape		= 1203,			// 1203 <lat> <lon>
	apt_taxi_active		= 1204,			// 1204 type|flags runway,std::list
#if HAS_CURVED_ATC_ROUTE
	apt_taxi_control	= 1205,			// 1205 <lat> <lon>
#else
	apt_taxi_control	= 1205,			// 1205 just gracefully ignore these for now. Some are already in the apt.dat
#endif
	apt_taxi_truck_edge = 1206,			// 1206 <src> <dst> <oneway flag> <name>

	apt_startup_loc_new	= 1300,			// 1300 lat lon heading misc|gate|tie_down|hangar traffic name
	apt_startup_loc_extended = 1301,	// 1301 size opertaions_type airline_list
	apt_meta_data = 1302,				// 1302 <key> <value>

	apt_truck_parking	= 1400,			// 1400 lat lon heading type cars name
	apt_truck_destination = 1401,		// 1401 lat lon heading type|type|type... name
	apt_truck_custom = 1402,			// 1402 vpath_vehicle vpath_driver

	apt_jetway = 1500,					// 1500 lat lon install_heading style_code size_code parked_tunnel_heading parked_tunnel_length parked_cab_heading
	apt_jetway_custom = 1501,			// specifies custom path/vpath to be used for last preceeding 1500 jetway

	// Surface codes
	apt_surf_none		= 0,
	apt_surf_asphalt,
	apt_surf_concrete,
	apt_surf_grass,
	apt_surf_dirt,
	apt_surf_gravel,
	apt_surf_asphalt_heli,			// these are 810 only
	apt_surf_concrete_heli,
	apt_surf_grass_heli,
	apt_surf_dirt_heli,
	apt_surf_asphalt_line,
	apt_surf_concrete_line,
	apt_surf_dry_lake,				// all versions
	apt_surf_water,
	apt_surf_ice,					// 850 only
	apt_surf_transparent,
	apt_surf_asphalt_1   = 20,      // 1200 and later
	apt_surf_asphalt_2,
	apt_surf_asphalt_3,
	apt_surf_asphalt_4,
	apt_surf_asphalt_5,
	apt_surf_asphalt_6,
	apt_surf_asphalt_7,
	apt_surf_asphalt_8,
	apt_surf_asphalt_9,
	apt_surf_asphalt_10,
	apt_surf_asphalt_11,
	apt_surf_asphalt_12,
	apt_surf_asphalt_13,
	apt_surf_asphalt_14,
	apt_surf_asphalt_15,
	apt_surf_asphalt_16,
	apt_surf_asphalt_17,
	apt_surf_asphalt_18,
	apt_surf_asphalt_19,
	apt_surf_concrete_1  = 50,
	apt_surf_concrete_2,
	apt_surf_concrete_3,
	apt_surf_concrete_4,
	apt_surf_concrete_5,
	apt_surf_concrete_6,
	apt_surf_concrete_7,
	apt_surf_concrete_8,

	// Light Fixture Codes (850)
	apt_gls_vasi			= 1,
	apt_gls_papi_left,
	apt_gls_papi_right,
	apt_gls_papi_20,
	apt_gls_vasi_tricolor,
	apt_gls_wigwag,
	apt_gls_apapi_left,
	apt_gls_apapi_right,
	// VASI codes (810)
	apt_gls_none_810 = 1,
	apt_gls_vasi_810,
	apt_gls_papi_810,
	apt_gls_papi20_810,

	// Edge Light Codes (850)
	apt_edge_none = 0,
	apt_edge_LIRL,
	apt_edge_MIRL,
	apt_edge_HIRL,
	apt_heli_edge_none = 0,
	apt_heli_edge_yellow,
	// REIL Codes (850)
	apt_reil_none = 0,
	apt_reil_omni,
	apt_reil_uni,
	apt_no_reil_but_thr,  // added for (1200)
	// Edge Light Codes (810)
	apt_edge_none_810 = 1,
	apt_edge_MIRL_810,
	apt_edge_REIL_810,
	apt_edge_CLL_810,
	apt_edge_TDZL_810,
	apt_edge_taxiway_810,

	// Approach Lights (850)
	apt_app_none = 0,
	apt_app_ALSFI,
	apt_app_ALSFII,
	apt_app_CALVERTI,
	apt_app_CALVERTII,
	apt_app_SSALR,
	apt_app_SSALF,
	apt_app_SALS,
	apt_app_MALSR,
	apt_app_MALSF,
	apt_app_MALS,
	apt_app_ODALS,
	apt_app_RAIL,
	// Approach lights (810)
	apt_app_none_810 = 1,
	apt_app_SSALS_810,
	apt_app_SALSF_810,
	apt_app_ALSFI_810,
	apt_app_ALSFII_810,
	apt_app_ODALS_810,
	apt_app_CALVERTI_810,
	apt_app_CALVERTII_810,

	// Shoulder codes
	apt_shoulder_none = 0,
	apt_shoulder_asphalt,
	apt_shoulder_concrete,
	apt_shoulder_asphalt_1 = 20,
	apt_shoulder_asphalt_2,
	apt_shoulder_asphalt_3,
	apt_shoulder_asphalt_4,
	apt_shoulder_asphalt_5,
	apt_shoulder_asphalt_6,
	apt_shoulder_asphalt_7,
	apt_shoulder_asphalt_8,
	apt_shoulder_asphalt_9,
	apt_shoulder_asphalt_10,
	apt_shoulder_asphalt_11,
	apt_shoulder_asphalt_12,
	apt_shoulder_asphalt_13,
	apt_shoulder_asphalt_14,
	apt_shoulder_asphalt_15,
	apt_shoulder_asphalt_16,
	apt_shoulder_asphalt_17,
	apt_shoulder_asphalt_18,
	apt_shoulder_asphalt_19,
	apt_shoulder_concrete_1 = 50,
	apt_shoulder_concrete_2,
	apt_shoulder_concrete_3,
	apt_shoulder_concrete_4,
	apt_shoulder_concrete_5,
	apt_shoulder_concrete_6,
	apt_shoulder_concrete_7,
	apt_shoulder_concrete_8,

	// Runway markings
	apt_mark_none = 0,
	apt_mark_visual,
	apt_mark_non_precision,
	apt_mark_precision,
	apt_mark_non_precision_UK,	// 850 only
	apt_mark_precision_UK,
	apt_mark_non_precision_EASA,// 1200 only
	apt_mark_precision_EASA,
	// Helipad Markings
	apt_mark_heli_default = 0,	// 850 only

	// Rwy Marking sizes (new in 1200)
	apt_mark_auto = 99,
	apt_mark_narrow = 0,
	apt_mark_medium,
	apt_mark_wide,

	// Airport beacons
	apt_beacon_none = 0,
	apt_beacon_airport,
	apt_beacon_seaport,
	apt_beacon_heliport,
	apt_beacon_military,

	// Sign codes
	apt_sign_small = 1,
	apt_sign_medium,
	apt_sign_large,
	apt_sign_large_distance,
	apt_sign_small_distance,

	// Sign Style
	apt_sign_style_default = 0,

	// Linear feature codes
	apt_line_none = 0,
	apt_line_solid_yellow,
	apt_line_broken_yellow,
	apt_line_double_solid_yellow,
	apt_line_runway_hold,
	apt_line_other_hold,
	apt_line_ils_hold,
	apt_line_ils_center,
	apt_line_wide_broken_yellow,
	apt_line_wide_double_broken_yellow,
	apt_line_solid_white = 20,
	apt_line_chequered_white,
	apt_line_broken_white,
	apt_line_Bsolid_yellow = 51,
	apt_line_Bbroken_yellow,
	apt_line_Bdouble_solid_yellow,
	apt_line_Brunway_hold,
	apt_line_Bother_hold,
	apt_line_Bils_hold,
	apt_line_Bils_center,
	apt_line_Bwide_broken_yellow,
	apt_line_Bwide_double_broken_yellow,
	apt_light_taxi_centerline = 101,
	apt_light_taxi_edge,
	apt_light_hold_short,
	apt_light_hold_short_flash,
	apt_light_hold_short_centerline,
	apt_light_bounary,

	// ATC Crap

	apt_pattern_left = 1,
	apt_pattern_right = 2,

	atc_traffic_heavies = 1,
	atc_traffic_jets = 2,
	atc_traffic_turbos = 4,
	atc_traffic_props = 8,
	atc_traffic_helis = 16,
	atc_traffic_fighters = 32,

	atc_traffic_all = (atc_traffic_heavies|atc_traffic_jets|atc_traffic_turbos|atc_traffic_props|atc_traffic_helis|atc_traffic_fighters),

	atc_op_arrivals = 1,
	atc_op_departures = 2,
	atc_op_all = (atc_op_arrivals | atc_op_departures),

	atc_ramp_misc = 0,
	atc_ramp_gate = 1,
	atc_ramp_tie_down = 2,
	atc_ramp_hangar = 3,

	atc_width_A = 0,
	atc_width_B = 1,
	atc_width_C = 2,
	atc_width_D = 3,
	atc_width_E = 4,
	atc_width_F = 5,

	ramp_operation_none = 0,
	ramp_operation_general_aviation = 1,
	ramp_operation_airline = 2,
	ramp_operation_cargo = 3,
	ramp_operation_military = 4,

	//First entry of the service truck types
	apt_truck_baggage_loader = 0,
	apt_truck_baggage_train,
	apt_truck_crew_car,
	apt_truck_crew_ferrari,
	apt_truck_crew_limo,
	apt_truck_fuel_jet,
	apt_truck_fuel_liner,
	apt_truck_fuel_prop,
	apt_truck_food,
	apt_truck_gpu,
	apt_truck_pushback,

	apt_truck_destination_fuel_farm = 0,
	apt_truck_destination_baggage_hall

};

inline bool apt_code_is_curve(int code) { return code == apt_lin_crv || code == apt_rng_crv || code == apt_end_crv; }
inline bool apt_code_is_end(int code) { return code == apt_end_seg || code == apt_end_crv; }
inline bool apt_code_is_ring(int code) { return code == apt_rng_seg || code == apt_rng_crv; }
inline bool apt_code_is_term(int code) { return apt_code_is_end(code) || apt_code_is_ring(code); }


struct	AptRunway_t {
	Segment2	ends;
	float		width_mtr;
	int			surf_code;
	int			shoulder_code;
	float		roughness_ratio;

	int			has_centerline;
	int			edge_light_code;
	int			has_distance_remaining;

	std::string		id[2];
	float		disp_mtr[2];
	float		blas_mtr[2];
	int			marking_code[2];
	int			app_light_code[2];
	int			has_tdzl[2];
	int			reil_code[2];

	// rowcode 105 data
	bool		has_105;
	int			mark_color;
	int			mark_size;
	float		number_size;
	float		skids[2];
	float		skid_len[2];
};
typedef std::vector<AptRunway_t>		AptRunwayVector;

struct	AptSealane_t {
	Segment2	ends;
	float		width_mtr;
	int			has_buoys;
	std::string		id[2];
};
typedef std::vector<AptSealane_t>	AptSealaneVector;

struct	AptHelipad_t {
	std::string		id;
	Point2		location;
	float		heading;
	float		length_mtr;
	float		width_mtr;
	int			surface_code;
	int			marking_code;
	int			shoulder_code;
	float		roughness_ratio;
	int			edge_light_code;
};
typedef	std::vector<AptHelipad_t>	AptHelipadVector;

struct	AptLinearSegment_t {
	int			code;
	Point2		pt;
	Point2		ctrl;
	std::set<int>	attributes;
};
typedef	std::vector<AptLinearSegment_t>		AptPolygon_t;

struct	AptTaxiway_t {
	int						surface_code;
	float					roughness_ratio;
	float					heading;
	AptPolygon_t			area;
	std::string					name;
};
typedef std::vector<AptTaxiway_t>	AptTaxiwayVector;

struct	AptBoundary_t {
	AptPolygon_t			area;
	std::string					name;
};
typedef std::vector<AptBoundary_t>	AptBoundaryVector;

struct AptMarking_t {
	AptPolygon_t			area;
	std::string					name;
};
typedef std::vector<AptMarking_t>	AptMarkingVector;

struct	AptPavement_t {
	Segment2	ends;	// Endpoint locations
	float		width_ft;	// Width in feet
	std::string		name;	// lo or taxiway letter or blank

	int			surf_code;
	int			shoulder_code;
	int			marking_code;
	float		roughness_ratio;
	int			distance_markings;

	int			blast1_ft;	// Length of blast-pads in feet.
	int			disp1_ft;
	int			vap_lites_code1;
	int			edge_lites_code1;
	int			app_lites_code1;
	int			vasi_angle1;	//  x100

	int			blast2_ft;
	int			disp2_ft;
	int			vap_lites_code2;
	int			edge_lites_code2;
	int			app_lites_code2;
	int			vasi_angle2;	// x100
};
typedef std::vector<AptPavement_t>	AptPavementVector;

struct	AptGate_t {
	Point2		location;
	float		heading;
	int			type;
	int			equipment;
	int			width;			// icao width code
	std::string		name;
	int			ramp_op_type;     // ramp operations type
	std::string		airlines;
};
typedef std::vector<AptGate_t>		AptGateVector;

struct	AptTowerPt_t {
	Point2		location;
	float		height_ft;
	int			draw_obj;		// not used in 850
	std::string		name;
};

struct	AptBeacon_t {
	Point2		location;
	int			color_code;
	std::string		name;
};

struct AptWindsock_t {
	Point2		location;
	int			lit;
	std::string		name;
};
typedef std::vector<AptWindsock_t>	AptWindsockVector;

struct	AptLight_t {
	Point2		location;
	int			light_code;
	float		heading;
	float		angle;
	std::string		name;
};
typedef std::vector<AptLight_t>		AptLightVector;

struct	AptSign_t {
	Point2		location;
	float		heading;
	int			style_code;
	int			size_code;
	std::string		text;
};
typedef std::vector<AptSign_t>		AptSignVector;

struct	AptATCFreq_t {
	int			freq;          // since WED 171 has a base of 1kHz, not 10kHz
	int			atc_type;
	std::string		name;
};
typedef std::vector<AptATCFreq_t>	AptATCFreqVector;

/************************************************************************************************************************
 * ATC INFO
 ************************************************************************************************************************/
struct AptRunwayRule_t {
	std::string			name;
	std::string			runway;
	int				operations;
	int				equipment;
	int				dep_freq;
	int				dep_heading_lo;		// lo == hi if "any" is okay.
	int				dep_heading_hi;		// This filters the use of the runway by where we are going, to keep traffic from crossing in-air.
	int				ini_heading_lo;		// This is the range of initial headings the tower can issue.
	int				ini_heading_hi;
};
typedef std::vector<AptRunwayRule_t>	AptRunwayRuleVector;

struct AptWindRule_t {
	std::string			icao;
	int				dir_lo_degs_mag;
	int				dir_hi_degs_mag;
	int				max_speed_knots;
};
typedef std::vector<AptWindRule_t>	AptWindRuleVector;

struct AptTimeRule_t {
	int				start_zulu;
	int				end_zulu;
};
typedef std::vector<AptTimeRule_t>	AptTimeRuleVector;

struct AptFlow_t {
	std::string						name;

	std::string						icao;
	int							ceiling_ft;
	float						visibility_sm;
	AptTimeRuleVector			time_rules;
	AptWindRuleVector			wind_rules;
	int							pattern_side;
	std::string						pattern_runway;
	AptRunwayRuleVector			runway_rules;
};
typedef std::vector<AptFlow_t>		AptFlowVector;

struct AptRouteNode_t {
	std::string						name;
	int							id;
	Point2						location;
};

struct AptEdgeBase_t {
	int							src;
	int							dst;
	int							oneway;
	std::vector<std::pair<Point2, bool> >	shape;			// This is pairs of shape points and curved flags - true means curve control point
												// The end points are NOT included in shape.  It is a requirement that no more
												// than 2 adjacent curve control points exist without a regular point.  There is no min/max size requirement for shape.
};

struct AptRouteEdge_t : AptEdgeBase_t {
	std::string						name;
	int							runway;
	int							width;	// icao width code
	std::set<std::string>					hot_depart;
	std::set<std::string>					hot_arrive;
	std::set<std::string>					hot_ils;

};

struct AptServiceRoadEdge_t : AptEdgeBase_t {
	std::string						name;
};

struct AptNetwork_t {
	std::string						name;
	std::vector<AptRouteNode_t>		nodes;
	std::vector<AptRouteEdge_t>		edges;
	std::vector<AptServiceRoadEdge_t>service_roads;
};

struct AptTruckParking_t {
	std::string						name;
	Point2						location;
	float						heading;
	int							parking_type;
	int							train_car_count;
	std::string						vpath;         // optional
};
typedef std::vector<AptTruckParking_t> AptTruckParkingVector;

struct AptTruckDestination_t {
	std::string						name;
	Point2						location;
	float						heading;
	std::set<int>					truck_types;
};
typedef std::vector<AptTruckDestination_t> AptTruckDestinationVector;

struct Jetway_t {
	Point2						location;
	float						install_heading;
	int							style_code;	// enum
	int							size_code;	// enum
	float						parked_tunnel_heading;
	float						parked_tunnel_length;
	float						parked_cab_heading;
	std::string						vpath;          // optional
};
typedef std::vector<Jetway_t> JetwayVector;

struct AptInfo_t {
	int					kind_code;				// Enum
	std::string				icao;
	std::string				name;
	int					elevation_ft;
	int					has_atc_twr;            // not used in 1000
	int					default_buildings;		// not used in 850
	std::vector<std::pair<std::string,std::string> > meta_data; //Contains meta data for real and synthetic properties

	AptRunwayVector		runways;				// 850 structures
	AptSealaneVector	sealanes;
	AptHelipadVector	helipads;
	AptTaxiwayVector	taxiways;
	AptBoundaryVector	boundaries;
	AptMarkingVector	lines;
	AptLightVector		lights;
	AptSignVector		signs;

	AptPavementVector	pavements;				// 810 structures
	AptGateVector		gates;					// shared structures

	AptTruckParkingVector		truck_parking;
	AptTruckDestinationVector	truck_destinations;
	JetwayVector				jetways;

	AptTowerPt_t		tower;
	AptBeacon_t			beacon;
	AptWindsockVector	windsocks;
	AptATCFreqVector	atc;

	AptFlowVector		flows;
	AptNetwork_t		taxi_route;

	Bbox2				bounds;

#if OPENGL_MAP
	struct AptLineLoop_t {
		float			rgb[3];
		Polygon2		pts;
	};
	std::vector<AptLineLoop_t>	ogl;
#endif


};

typedef std::vector<AptInfo_t>	AptVector;

typedef std::hash_multimap<int,int>	AptIndex;

#endif
