#define DBG_LIN_COLOR 1,0,1,1,0,1

#if DEV
#define DEBUG_VIS_LINES 1
#endif

#include "WED_Validate.h"
#include "WED_ValidateATCRunwayChecks.h"
#include "WED_EnumSystem.h"

#include "WED_Airport.h"
#include "WED_ATCFlow.h"
#include "WED_ATCRunwayUse.h"
#include "WED_PolygonPlacement.h"
#include "WED_Runway.h"
#include "WED_TaxiRoute.h"
#include "WED_RampPosition.h"
#include "WED_RoadEdge.h"

#include "WED_ResourceMgr.h"
#include "WED_HierarchyUtils.h"
#include "CompGeomUtils.h"
#include "GISUtils.h"
#include "WED_PreviewLayer.h"

#include <sstream>

typedef std::vector<WED_ATCRunwayUse*>  ATCRunwayUseVec_t;

//We're just using WED_GISPoint because old WED and airports
typedef std::vector<WED_GISPoint*>      TaxiRouteNodeVec_t;
typedef std::vector<RunwayInfo>         RunwayInfoVec_t;
typedef std::vector<TaxiRouteInfo>      TaxiRouteInfoVec_t;

//Collects 'potentially active' runways.
// - any runway that is referenced in at least one flow AND there is at least one runway segement taxi route on it
// - if no flows are defined, all runways are considered active
// - if no taxiway std::vector is passed, being mentioned in a flow is sufficient to consider it active
static RunwayInfoVec_t CollectPotentiallyActiveRunways( const TaxiRouteInfoVec_t& all_taxiroutes,
														const RunwayInfoVec_t& all_runways_info,
														const FlowVec_t& flows,
														ATCRunwayUseVec_t& use_rules,
														validation_error_vector& msgs,
														WED_Airport* apt)
{
	//Find all potentially active runways:
	//0 flows means treat all runways as potentially active
	//>1 means find all runways mentioned, ignoring duplicates
	RunwayVec_t potentially_active_runways;
	RunwayInfoVec_t runway_info_vec;

	if(flows.size() == 0)
	{
		runway_info_vec = all_runways_info;
	}
	else
	{
		for(auto f : flows)
			CollectRecursive(f, back_inserter(use_rules), WED_ATCRunwayUse::sClass);

		//For all runways in the airport
		for(auto& runway_itr : all_runways_info)
		{
			//Search through all runway uses, testing if the runway has a match AND at least one taxiroutes associated with it
			for(ATCRunwayUseVec_t::const_iterator use_itr = use_rules.begin(); use_itr != use_rules.end(); ++use_itr)
			{
				AptRunwayRule_t runway_rule;
				(*use_itr)->Export(runway_rule);

				//Compare the name of the runway mentioned by the taxiway to the runway
				std::string runway_name_p1 = runway_itr.name.substr(0,runway_itr.name.find_first_of('/'));
				std::string runway_name_p2 = runway_itr.name.substr(runway_itr.name.find_first_of('/')+1);

				if( runway_rule.runway == runway_name_p1 ||
					runway_rule.runway == runway_name_p2)
				{
					if (all_taxiroutes.empty())
					{
						// if no taxiroutes specified, being mentioned in a flow is sufficient to be considered active
						runway_info_vec.push_back(runway_itr);
					}
					else
					{
						// check that there is at least one taxi route associated with it
						for(const auto& taxiroute : all_taxiroutes)
						{
							std::string taxiroute_name = ENUM_Desc(taxiroute.ptr->GetRunway());

							if(runway_itr.name == taxiroute_name || ( taxiroute_name[0] = '0' && runway_itr.name == taxiroute_name.substr(1) ))
							{
								runway_info_vec.push_back(runway_itr);
								break; //exit all_taxiroutes loop
							}
						}
					}
					break; //exit use_rules loop
				}
			}
		}
	}
	return runway_info_vec;
}

//Returns a std::vector of TaxiRouteInfos whose name matches the given runway
static TaxiRouteInfoVec_t FilterMatchingRunways( const RunwayInfo& runway_info,
												 const TaxiRouteInfoVec_t& all_taxiroutes)
{
	TaxiRouteInfoVec_t matching_taxiroutes;
	for(auto& taxiroute_itr : all_taxiroutes)
	{
		if(taxiroute_itr.ptr->IsRunway())
		{
			std::string taxiroute_name = ENUM_Desc(taxiroute_itr.ptr->GetRunway());
			if(runway_info.name == taxiroute_name)
			{
				matching_taxiroutes.push_back(taxiroute_itr);
			}
		}
	}

	return matching_taxiroutes;
}

static void AssignRunwayUse( RunwayInfo& runway_info,
							  const ATCRunwayUseVec_t& all_use_rules)
{
	if(all_use_rules.empty() == true)
	{
		runway_info.runway_ops[0] = 0x01 | 0x02; //Mark for arrivals and departures
		runway_info.runway_ops[1] = 0x01 | 0x02;
		return;
	}

	//The case of "This airport has no flows" gets implicitly taken care of here by the fact that use_rules will be 0,
	//and the fact that we assign this in ???

	for(ATCRunwayUseVec_t::const_iterator use_itr = all_use_rules.begin(); use_itr != all_use_rules.end(); ++use_itr)
	{
		AptRunwayRule_t apt_runway_rule;
		(*use_itr)->Export(apt_runway_rule);

		if(runway_info.runway_numbers[0] == ENUM_LookupDesc(ATCRunwayOneway, apt_runway_rule.runway.c_str()))
		{
			runway_info.runway_ops[0] |= apt_runway_rule.operations;
		}
		else if(runway_info.runway_numbers[1] == ENUM_LookupDesc(ATCRunwayOneway, apt_runway_rule.runway.c_str()))
		{
			runway_info.runway_ops[1] |= apt_runway_rule.operations;
		}
	}
}

//!!IMPORTANT: These methods return true if they pass without error, false if there was an error!!
//--Centerline Checks----------------------------------------------------------
static bool AllTaxiRouteNodesInRunway( const RunwayInfo& runway_info,
									   const TaxiRouteInfoVec_t& matching_taxiroutes,
									   validation_error_vector& msgs,
									   WED_Airport* apt)
{
	int original_num_errors = msgs.size();
	Polygon2 runway_hit_box(runway_info.corners_geo);
	Vector2 dir_ext = runway_info.dir_vec_1m *2.0;       // extend runway in longitudinal direction by 2m

	runway_hit_box[0] -= dir_ext;
	runway_hit_box[1] += dir_ext;
	runway_hit_box[2] += dir_ext;
	runway_hit_box[3] -= dir_ext;

	for(auto& tr : matching_taxiroutes)
	{
		if(runway_hit_box.inside(tr.segment_geo.p1) == false)
		{
			std::string node_name;
			tr.nodes[0]->GetName(node_name);
			std::string msg = "Taxiroute node " + node_name + " is out of runway " + runway_info.name + "'s bounds";
			msgs.push_back(validation_error_t(msg, err_atcrwy_taxi_route_node_out_of_bounds, tr.nodes[0],apt));
		}
		if(runway_hit_box.inside(tr.segment_geo.p2) == false)
		{
			std::string node_name;
			tr.nodes[1]->GetName(node_name);
			std::string msg = "Taxiroute node " + node_name + " is out of runway " + runway_info.name + "'s bounds";
			msgs.push_back(validation_error_t(msg, err_atcrwy_taxi_route_node_out_of_bounds, tr.nodes[1],apt));
		}
	}

#if DEBUG_VIS_LINES
#if DEBUG_VIS_LINES < 2
    if (msgs.size() - original_num_errors != 0)
#endif
    {
        debug_mesh_segment(runway_hit_box.side(0), DBG_LIN_COLOR); //left side
        debug_mesh_segment(runway_hit_box.side(1), DBG_LIN_COLOR); //top side
        debug_mesh_segment(runway_hit_box.side(2), DBG_LIN_COLOR); //right side
        debug_mesh_segment(runway_hit_box.side(3), DBG_LIN_COLOR); //bottom side
	}
#endif
	return msgs.size() - original_num_errors == 0 ? true : false;
}

//True for all hidden and all not WED_Entity
static bool is_hidden(const WED_Thing* node)
{
	const WED_Entity* ent = dynamic_cast<const WED_Entity*>(node);
	DebugAssert(ent != NULL);

	if (ent != NULL)
	{
		return static_cast<bool>(ent->GetHidden());
	}

	return true;
}

static std::vector<WED_TaxiRoute *> get_all_visible_viewers(const WED_GISPoint* node)
{
	std::set<WED_Thing *> viewers;
	node->GetAllViewers(viewers);

	std::vector<WED_TaxiRoute *> taxi_routes;
	for (auto it : viewers)
	{
		auto tr = dynamic_cast<WED_TaxiRoute *>(it);
		if (tr && !tr->GetHidden())
			taxi_routes.push_back(tr);
	}
	return taxi_routes;
}

static bool TaxiRouteCenterlineCheck( const RunwayInfo& runway_info,
			 						  const TaxiRouteInfoVec_t& matching_taxiroutes,
			 						  validation_error_vector& msgs,
			 						  WED_Airport* apt)
{
	int original_num_errors = msgs.size();
	for(auto& tr : matching_taxiroutes)
	{
		double METERS_TO_CENTER_THRESHOLD = 5.0;
		double p1_to_center_dist = sqrt(runway_info.centerline_m.squared_distance_supporting_line(tr.segment_m.p1));
		double p2_to_center_dist = sqrt(runway_info.centerline_m.squared_distance_supporting_line(tr.segment_m.p2));

		if( p1_to_center_dist > METERS_TO_CENTER_THRESHOLD ||
			p2_to_center_dist > METERS_TO_CENTER_THRESHOLD)
		{
			std::string msg = "Taxi route segment for runway " + tr.name + " is not on the center line";
			msgs.push_back(validation_error_t(msg, err_atcrwy_centerline_taxiroute_segment_off_center, tr.ptr,apt));
		}
	}

	return msgs.size() - original_num_errors == 0 ? true : false;
}

static std::vector<WED_TaxiRoute *> filter_viewers_by_is_runway(const WED_GISPoint* node, const std::string& runway_name)
{
	std::vector<WED_TaxiRoute *> matching_routes;
	std::vector<WED_TaxiRoute *> node_viewers = get_all_visible_viewers(node);

	for(auto itr : node_viewers)
	{
		std::string name;
		itr->GetName(name);

		if(itr->IsRunway() && name == runway_name)
		{
			matching_routes.push_back(itr);
		}
	}
	return matching_routes;
}

//Checks that a Runway's taxi route has two nodes with a valence of one, and no nodes with a valence of > 2
static bool RunwaysTaxiRouteValencesCheck (const RunwayInfo& runway_info,
										   const TaxiRouteNodeVec_t& all_matching_nodes, //All nodes from taxiroutes matching the runway, these will come in sorted
										   WED_TaxiRoute*& out_start_taxiroute, //Out parameter, one of the ends of the taxiroute
										   validation_error_vector& msgs,
										   WED_Airport* apt)
{
	int original_num_errors = msgs.size();
	int num_valence_of_1 = 0; //Aka, the tips of the runway, should end up being 2
	out_start_taxiroute = NULL;

	//Valence allowed per runway enum = 2
	for(TaxiRouteNodeVec_t::const_iterator node_itr = all_matching_nodes.begin(); node_itr != all_matching_nodes.end(); ++node_itr)
	{
		int node_valence = std::count(all_matching_nodes.begin(), all_matching_nodes.end(), *node_itr);

		if(node_valence == 1)
		{
			if(num_valence_of_1 < 2)
			{
				if(out_start_taxiroute == NULL)
				{
					auto xxx = filter_viewers_by_is_runway(*node_itr,runway_info.name);
					if(xxx.size())
						out_start_taxiroute = xxx.front();
				}
				++num_valence_of_1;
			}
			else
			{
				msgs.push_back(validation_error_t("Runway " + runway_info.name + "'s taxi route is not continuous", err_atcrwy_connectivity_not_continous, *node_itr,apt));
			}
		}
		else if(node_valence >= 3)
		{
			std::string node_name;
			(*node_itr)->GetName(node_name);

			msgs.push_back(validation_error_t("Runway " + runway_info.name + "'s taxi route is split " + std::to_string(node_valence)+ " ways at taxi route node " + node_name, err_atcrwy_connectivity_n_split, *node_itr,apt));
		}
	}

	if(num_valence_of_1 == 0 && all_matching_nodes.size() > 0)
	{
		msgs.push_back(validation_error_t("Runway " + runway_info.name + "'s taxi route forms a loop", err_atcrwy_connectivity_forms_loop, runway_info.runway_ptr,apt));
	}
	return msgs.size() - original_num_errors == 0 ? true : false;
}

static int get_node_valence(const WED_GISPoint* node)
{
	return get_all_visible_viewers(node).size();
}

static WED_GISPoint* get_next_node(const WED_GISPoint* current_node,
							const TaxiRouteInfo& next_taxiroute)
{
	WED_GISPoint* next = NULL;
	if(next_taxiroute.nodes[0] == current_node)
	{
		next = next_taxiroute.nodes[1]; //Going backwards choose next-1 | 0--next--1-->0--current--1-->
	}
	else
	{
		next = next_taxiroute.nodes[0]; //Going forwards, choose next-0 | 0--current--1-->0--next--1-->
	}

	if(next == NULL)
	{
		return NULL;
	}
	else if(get_node_valence(next) == 1)
	{
		return NULL; //We don't want to travel there next, its time to end
	}
	//Will we have somewhere to go next?
	else if(filter_viewers_by_is_runway(next, next_taxiroute.name).size() == 0)
	{
		return NULL;
	}
	else
	{
		return next;
	}
}

static WED_TaxiRoute* get_next_taxiroute(const WED_GISPoint* current_node,
										 const TaxiRouteInfo& current_taxiroute)
{
	TaxiRouteVec_t viewers = filter_viewers_by_is_runway(current_node, current_taxiroute.name); //The taxiroute name should equal the runway name
	DebugAssert(viewers.size() == 1 || viewers.size() == 2);

	if(viewers.size() == 2)
		return current_taxiroute.ptr == viewers[0] ? viewers[1] : viewers[0];
	else
		return current_taxiroute.ptr == viewers[0] ? NULL : viewers[0];
}

//returns std::pair<is_target_of_current,is_target_of_next>
static std::pair<bool,bool> get_taxiroute_relationship(const WED_GISPoint* current_node,
												  const TaxiRouteInfo& current_taxiroute,
												  const TaxiRouteInfo& next_taxiroute)
{
	//std::pair<is_target_of_current,is_target_of_next>
	std::pair<bool,bool> taxiroute_relationship;

	//See relationship chart in TaxiRouteSquishedZCheck
	taxiroute_relationship.first  = current_taxiroute.nodes[1] == current_node;
	taxiroute_relationship.second = next_taxiroute.nodes[1]    == current_node;

	return taxiroute_relationship;
}

static bool TaxiRouteSquishedZCheck( const RunwayInfo& runway_info,
									 const TaxiRouteInfo& start_taxiroute, //One of the ends of this chain of taxi routes
									 CoordTranslator2 translator,
									 validation_error_vector& msgs,
									 WED_Airport* apt)
{
	// We know all the nodes are within threshold of the center and within bounds, the segments are parallel enough,
	// the route is a complete chain with no 3+-way splits. Now: do any of the segments make TWO complete 180 unexpectedly?
	// Granted, this is an EXTREMELY rare case, because unless there are taxiway exits forking off each of those sharp turns
	// this would be caught by the generic "sudden sharp turn" test that is run on ALL taxiroutes

	WED_GISPoint* current_node = NULL;
	TaxiRouteInfo current_taxiroute(start_taxiroute);

	if(filter_viewers_by_is_runway(start_taxiroute.nodes[0], runway_info.name).size() == 2)
		current_node = start_taxiroute.nodes[0]; // We'll be moving backwards through the route, <----0--------1-->
	else
		current_node = start_taxiroute.nodes[1]; // 0---->1---->

	//while we have not run out of nodes to traverse
	while(current_node != NULL)
	{
//printf("squishedZ iter\n");
		WED_TaxiRoute* next_route = get_next_taxiroute(current_node,current_taxiroute);
		if(next_route == NULL)
		{
			break;
		}

		TaxiRouteInfo next_taxiroute(next_route,translator);

		std::pair<bool,bool> relationship = get_taxiroute_relationship(current_node,current_taxiroute,next_taxiroute);
		WED_GISPoint* next_node = get_next_node(current_node,next_taxiroute);

		Vector2 taxiroute_vec_1 = Vector2(current_taxiroute.segment_m.p1, current_taxiroute.segment_m.p2);
		taxiroute_vec_1.normalize();
		double dot_runway_route_1 = runway_info.dir_1m.dot(taxiroute_vec_1);

		Vector2 taxiroute_vec_2 = Vector2(next_taxiroute.segment_m.p1, next_taxiroute.segment_m.p2);
		taxiroute_vec_2.normalize();
		double dot_runway_route_2 = runway_info.dir_1m.dot(taxiroute_vec_2);

		// Given a runway [<--------------------] where this side is this source
        //
		//    r_1        | o is target        | o is source
		//r_2            |--------------------------------------  |a|b
		//o is target    |[--2-->o<--1--] = - |[--2-->o--1-->] = +|-+-
		//o is source    |[<--2--o<--1--] = + |[<--2--o--1-->] = -|c|d
		//
		//The o represents the common, current, node
		//Because by this point we've passed the ParallelCheck all we need to do is check positive or negative

		int expected_sign = 0;

		bool first_is_target  = relationship.first;
		bool second_is_target = relationship.second;

		if((first_is_target == true  && second_is_target == true) ||
		   (first_is_target == false && second_is_target == false))
		{
			expected_sign = -1; //a and d
		}
		else
		{
			expected_sign = 1;//b and c
		}

		//Take the dot product between the two runway_routes
		double dot_product = taxiroute_vec_1.dot(taxiroute_vec_2);

		int real_sign = dot_product > 0 ? 1 : -1;
		if( real_sign != expected_sign)
		{
			TaxiRouteVec_t problem_children;
			problem_children.push_back(current_taxiroute.ptr);
			problem_children.push_back(next_taxiroute.ptr);

			msgs.push_back(validation_error_t( "Taxi routes " + current_taxiroute.name + " and " + next_taxiroute.name + " are making a turn that is too tight for aircraft to follow.",
											   err_atcrwy_centerline_too_sharp_turn,
											   problem_children,
											   apt));

			return false; //The next test will just be redundant so we'll end here
		}

		current_taxiroute = next_taxiroute;
		current_node = next_node;
	}

	return true;
}

static bool FullyConnectedNetworkCheck( const TaxiRouteVec_t& all_taxiroutes,      // All the taxiroutes in the airport, for EnsureRunwayTaxirouteValences
										validation_error_vector& msgs,
										WED_Airport * apt)
{
	int original_num_errors = msgs.size();

	std::set<WED_Thing *> to_visit, all_routes;
	std::vector<std::set<WED_Thing *> > networks;

	for(auto tr : all_taxiroutes)
		all_routes.insert(tr);

	while(!all_routes.empty())
	{
		to_visit.clear();
		networks.push_back(to_visit);
		to_visit.insert(*all_routes.begin());

		while(!to_visit.empty())
		{
			WED_Thing * i = *to_visit.begin();
			to_visit.erase(to_visit.begin());
			networks.back().insert(i);
			all_routes.erase(i);

			int ns = i->CountSources();
			for(int s = 0; s < ns; ++s)
			{
				WED_Thing * src = i->GetNthSource(s);
				std::set<WED_Thing *>	viewers;
				src->GetAllViewers(viewers);
				for(auto v : viewers)
					if(all_routes.count(v))	to_visit.insert(v);
			}
		}
	}

	int largest_nw(0);
	std::set<WED_Thing *> * largest_nw_set(nullptr);

	for(auto& nw : networks)
	{
		if(nw.size() > largest_nw)
		{
			largest_nw_set = &nw;
			largest_nw = nw.size();
		}
	}

	for(auto& nw : networks)
	{
		if(&nw != largest_nw_set)
		{
			std::string msg;
			(*nw.begin())->GetName(msg);
			if(nw.size() == 1)
				msg = "Taxi Edge " + msg;
			else
				msg = "A std::set of " + std::to_string(nw.size()) + " Taxi Edges";
			msg += " is not connected to the remainder of the taxi network.";
			msgs.push_back(validation_error_t(msg, err_atc_taxi_routes_not_connected, nw ,apt));
		}
	}
	return msgs.size() - original_num_errors == 0 ? true : false;
}


//All checks that require knowledge of taxiroute connectivity checks
static bool DoTaxiRouteConnectivityChecks( const RunwayInfo& runway_info,
										   const TaxiRouteInfoVec_t& all_taxiroutes,      // All the taxiroutes in the airport, for EnsureRunwayTaxirouteValences
										   const TaxiRouteInfoVec_t& matching_taxiroutes, // Only the taxiroutes which match the runway in runway_info
										   CoordTranslator2 translator,
										   validation_error_vector& msgs,
										   WED_Airport * apt)
{
	int original_num_errors = msgs.size();

	TaxiRouteNodeVec_t matching_nodes;
	for (auto& itr : matching_taxiroutes)
	{
		matching_nodes.push_back(itr.nodes[0]);
		matching_nodes.push_back(itr.nodes[1]);
	}

	sort(matching_nodes.begin(),matching_nodes.end());

	WED_TaxiRoute* out_start_taxiroute = NULL;
	if(RunwaysTaxiRouteValencesCheck(runway_info, matching_nodes, out_start_taxiroute, msgs, apt))
	{
		//The algorithm requires there to be atleast 2 taxiroutes
		if(all_taxiroutes.size() >= 2 && out_start_taxiroute != NULL)
		{
			TaxiRouteSquishedZCheck(runway_info, TaxiRouteInfo(out_start_taxiroute,translator), translator, msgs, apt);
		}
	}

	return msgs.size() - original_num_errors == 0 ? true : false;
}

static bool TaxiRouteParallelCheck( const RunwayInfo& runway_info,
								    const TaxiRouteInfoVec_t& matching_routes,
								    validation_error_vector& msgs,
								    WED_Airport* apt)
{
	int original_num_errors = msgs.size();

	//The first matching taxiroute chooses the direction, the rest must match
	for(auto& tr : matching_routes)
	{
		Vector2 taxiroute_vec = Vector2(tr.segment_m.p1,tr.segment_m.p2);
		taxiroute_vec.normalize();

		double dot_product = fabs(runway_info.dir_1m.dot(taxiroute_vec));
		double ANGLE_THRESHOLD = 0.995;
		if(dot_product < ANGLE_THRESHOLD)
		{
			std::string msg = "Taxi route segment " + tr.name + " is not parallel to the runway's " + runway_info.name + "'s center line.";
			msgs.push_back(validation_error_t(msg, err_atcrwy_centerline_not_parallel_centerline, tr.ptr, apt));
		}
	}

	return msgs.size() - original_num_errors == 0 ? true : false;
}

static bool RunwayHasCorrectCoverage( const RunwayInfo& runway_info,
									  const TaxiRouteInfoVec_t& all_taxiroutes,
									  validation_error_vector& msgs,
									  WED_Airport* apt)
{
	// In other tests run before this the routes were collected to all be runway tagged, on the runway and connected
	// to each other in a single line. So no further checking is needed, just add them up ...

	double length_accumulator = 0.0;
	for(auto& tr : all_taxiroutes)
		length_accumulator += sqrt(tr.segment_m.squared_length());

	const double COVERAGE_THRESHOLD = runway_info.runway_ptr->GetLength() * 0.8;

	if (length_accumulator < COVERAGE_THRESHOLD)
	{
		std::string msg = "Taxi route for runway " + runway_info.name + " does not span enough runway";
		msgs.push_back(validation_error_t(msg, err_atcrwy_taxi_route_does_not_span_enough_rwy,  runway_info.runway_ptr,apt));
		return false;
	}

	return true;
}
//-----------------------------------------------------------------------------

//--Hot zone checks------------------------------------------------------------

static bool FindIfMarked( const int runway_number,        //enum from ATCRunwayOneway
						  const TaxiRouteInfo& taxiroute, //The taxiroute to check if it is marked properly
						  const std::set<std::string>& hot_set,     //The std::set of hot_listed_runways
						  const std::string& op_type,          //String for the description
						  validation_error_vector& msgs,  //WED_Valiation messages
						  WED_Airport* apt) //Airport to pass into msgs
{

    bool found_marked = false;
	for(std::set<std::string>::const_iterator hot_set_itr = hot_set.begin();
		hot_set_itr != hot_set.end();
		++hot_set_itr)
	{
		if(runway_number == ENUM_LookupDesc(ATCRunwayOneway,(*hot_set_itr).c_str()))
		{
			found_marked = true;
		}
	}

	if(found_marked == false)
	{
		stringstream ss;
		ss  << "Taxi route "
			<< taxiroute.name
			<< " is too close to runway "
			<< ENUM_Desc(runway_number)
			<< " and now must be marked active for runway "
			<< ENUM_Desc(runway_number)
			<< " "
			<< op_type;

		msgs.push_back(validation_error_t(
			ss.str(),
			err_atcrwy_hotzone_taxi_route_too_close,
			taxiroute.ptr,
			apt));
	}
	return !found_marked;
}

//Returns polygon in lat/lon
static Polygon2 MakeHotZoneHitBox( const RunwayInfo& runway_info, // The relevant runway info
								   int runway_number,             // The runway number we're doing
								   bool make_arrival)             // add arrival protection area, but *only if* runway is used for arrivals
{
	if((runway_info.IsHotForArrival(runway_number) == false &&
		runway_info.IsHotForDeparture(runway_number) == false) ||
		runway_number == atc_Runway_None)
	{
		return Polygon2(0);
	}
	bool paved = runway_info.runway_ptr->GetSurface() < surf_Grass;

	//Unfortunatly due to the messy real world we must have unrealistically low thresholds to avoid edge case after edge case for very airport
	//that doesn't play by the rules, was grandfathered in, or was built on Mt. Doom and needs to avoid the volcanic dust clouds. You know, the usual.
	double HITZONE_OVERFLY_THRESHOLD_M = 100.00;
	if(!paved)
		HITZONE_OVERFLY_THRESHOLD_M = 50.0; // reduced arrival end clearance requirements for these

	double HITZONE_WIDTH_THRESHOLD_M = 30.0;  // Width of Runway Protection Zone beyond runway width, each side
	if (!paved || runway_info.runway_ptr->GetLength() < 1500.0 || runway_info.runway_ptr->GetWidth() < 20.0)
		HITZONE_WIDTH_THRESHOLD_M = 10.0;	// good guess, FAA advisory 150' from CL is impractically wide for many small airfields

	Polygon2 runway_hit_box(runway_info.corners_geo);
	/*         top   ^
	                 |
	                 runway_info.dir_vec_1m
	            1    |
	        1_______2!----> runway_info.width_vec_1m
		   |    |    |
		0  |    |    | 2
		   |    |    |
		   0---------3
		        3
		      bottom
	*/
	Vector2 width_ext = runway_info.width_vec_1m * HITZONE_WIDTH_THRESHOLD_M;

	runway_hit_box[0] -= width_ext;
	runway_hit_box[1] -= width_ext;
	runway_hit_box[2] += width_ext;
	runway_hit_box[3] += width_ext;

	Vector2 len_ext;

	if(runway_number <= atc_18R)
	{
		if(runway_info.IsHotForArrival(runway_number) == true && make_arrival == true)
		{
			HITZONE_OVERFLY_THRESHOLD_M = max(HITZONE_OVERFLY_THRESHOLD_M - runway_info.runway_ptr->GetDisp1(), 0.0);
			//arrival_side is bottom_side;
			runway_hit_box[0] -= runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
			runway_hit_box[3] -= runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
		}

		if(runway_info.IsHotForDeparture(runway_number) == true && make_arrival == false)
		{
			//departure_side is top_side;
			runway_hit_box[1] += runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
			runway_hit_box[2] += runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
		}
	}
	else
	{
		if(runway_info.IsHotForArrival(runway_number) == true  && make_arrival == true)
		{
			HITZONE_OVERFLY_THRESHOLD_M = max(HITZONE_OVERFLY_THRESHOLD_M - runway_info.runway_ptr->GetDisp2(), 0.0);
			//arrival_side is top_side;
			runway_hit_box[1] += runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
			runway_hit_box[2] += runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
		}

		if(runway_info.IsHotForDeparture(runway_number) == true  && make_arrival == false)
		{
			//departure_side is bottom_side;
			runway_hit_box[0] -= runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
			runway_hit_box[3] -= runway_info.dir_vec_1m * HITZONE_OVERFLY_THRESHOLD_M;
		}
	}

	return runway_hit_box;
}

static bool DoHotZoneChecks( const RunwayInfo& runway_info,
							 const TaxiRouteInfoVec_t& all_taxiroutes,
							const std::vector<WED_RampPosition*>& ramps,
							 validation_error_vector& msgs,
							 WED_Airport* apt)
{
	int original_num_errors = msgs.size();
	std::set<WED_RampPosition *> ramps_near_rwy;

	for (int runway_side = 0; runway_side < 2; ++runway_side)
	{
		int runway_number = runway_info.runway_numbers[runway_side];

		for (int make_arrival = 0; make_arrival < 2; make_arrival++)
		{
			//Make the hitbox based on the runway and which side (low/high) you're currently on and if you need to be making arrival or departure
			Polygon2 hit_box = MakeHotZoneHitBox(runway_info, runway_number, (bool)make_arrival);
			bool hitbox_error = false;

			if(hit_box.empty()) continue;

			for (auto r : ramps)
			{
				if (r->GetType() != atc_Ramp_Misc)
				{
					Point2 pt[4];
					r->GetTips(pt);
					for(int i = 0; i <4; ++i)
						if (hit_box.inside(pt[i]))
						{
							ramps_near_rwy.insert(r);
							hitbox_error = true;
#if DEBUG_VIS_LINES
							debug_mesh_line(pt[0],pt[2], DBG_LIN_COLOR);
							debug_mesh_line(pt[1],pt[3], DBG_LIN_COLOR);
#endif
							break;
						}
				}
			}

			for(auto& taxiroute_itr : all_taxiroutes)
			{
				// even if its not intersecting the box - it could be completely inside
				if(hit_box.intersects(taxiroute_itr.segment_geo) || hit_box.inside(taxiroute_itr.segment_geo.p1))
				{
					if(runway_info.IsHotForArrival(runway_number) && static_cast<bool>(make_arrival) == true )
					{
						hitbox_error |= FindIfMarked(runway_number, taxiroute_itr, taxiroute_itr.hot_arrivals, "arrivals", msgs, apt);
					}

					if(runway_info.IsHotForDeparture(runway_number) && static_cast<bool>(make_arrival) == false)
					{
						hitbox_error |= FindIfMarked(runway_number, taxiroute_itr, taxiroute_itr.hot_departures, "departures", msgs, apt);
					}
				}
			}
#if DEBUG_VIS_LINES
#if DEBUG_VIS_LINES < 2
			if (hitbox_error)
#endif
			{
				debug_mesh_segment(hit_box.side(0), DBG_LIN_COLOR); //left side
				debug_mesh_segment(hit_box.side(1), DBG_LIN_COLOR); //top side
				debug_mesh_segment(hit_box.side(2), DBG_LIN_COLOR); //right side
				debug_mesh_segment(hit_box.side(3), DBG_LIN_COLOR); //bottom side
			}
#endif
		}
	}
	if(ramps_near_rwy.size())
		msgs.push_back(validation_error_t("Only Ramp Starts of type=misc are allowed near runways", err_ramp_only_misc_starts_in_hotzones,
																		ramps_near_rwy, apt));
	return msgs.size() - original_num_errors == 0 ? true : false;
}

// flag all ground traffic routes that cross a runways hitbox

static void AnyTruckRouteNearRunway( const RunwayInfo& runway_info,
							 const TaxiRouteInfoVec_t& all_routes, const std::vector<WED_RoadEdge*>& roads,
							 validation_error_vector& msgs, WED_Airport* apt)
{
	Polygon2 runway_hit_box(runway_info.corners_geo);

	Vector2 side_ext = runway_info.width_vec_1m * (5.0 + 0.5 * runway_info.runway_ptr->GetWidth());  // to centerline of road. Edge-2-edge spacing is 5-10m less, depending on road width.
	Vector2 len_ext  = runway_info.dir_vec_1m  * (runway_info.runway_ptr->GetLength() > 1500.0 ? 60.0 : 30.0);  // to go around end of runway
	runway_hit_box[0] -= len_ext + side_ext;
	runway_hit_box[1] += len_ext - side_ext;
	runway_hit_box[2] += len_ext + side_ext;
	runway_hit_box[3] -= len_ext - side_ext;

	std::set<WED_TaxiRoute*> close_routes;
	for(auto& route_itr : all_routes)
		if(runway_hit_box.intersects(route_itr.segment_geo) || runway_hit_box.inside(route_itr.segment_geo.p1))
			close_routes.insert(route_itr.ptr);

	std::set<WED_RoadEdge*> close_roads;
	for(auto road_itr : roads)
	{
		Bezier2 b;
		Segment2 s;
		for(int i = road_itr->GetNumSides() -1; i >= 0; --i)
		{
			road_itr->GetSide(gis_Geo, i, b);
			s = b.as_segment();
			if(runway_hit_box.intersects(s) || runway_hit_box.inside(s.p1))
			{
				close_roads.insert(road_itr);
				break;
			}
		}
	}

    if (close_routes.size() || close_roads.size())
    {
		if(close_routes.size())
		{
			std::string msg = "Ground Vehicle Route too close to runway " + runway_info.name;
			msgs.push_back(validation_error_t(msg, err_atcrwy_truck_route_too_close_to_runway, close_routes, apt));
		}
		if(close_roads.size())
			msgs.push_back(validation_error_t("Road too close to runway", err_atcrwy_truck_route_too_close_to_runway, close_roads, apt));

#if DEBUG_VIS_LINES == 2
	}
	{
#elif DEBUG_VIS_LINES
        debug_mesh_segment(runway_hit_box.side(0), DBG_LIN_COLOR); //left side
        debug_mesh_segment(runway_hit_box.side(1), DBG_LIN_COLOR); //top side
        debug_mesh_segment(runway_hit_box.side(2), DBG_LIN_COLOR); //right side
        debug_mesh_segment(runway_hit_box.side(3), DBG_LIN_COLOR); //bottom side
#endif
	}
}


static void	AnyPolgonsOnRunway( const RunwayInfo& runway_info,	 const std::vector<WED_PolygonPlacement *>& all_polygons,
							 validation_error_vector& msgs, WED_Airport* apt, WED_ResourceMgr * rmgr)
{

	Polygon2 runway_hit_box(runway_info.corners_geo);

	Vector2 side_ext = runway_info.width_vec_1m * -1.0;  // Allow any polygon to overlap runway by 1m on all sides
	Vector2 len_ext  = runway_info.dir_vec_1m   * -1.0;

	runway_hit_box[0] -= len_ext + side_ext;
	runway_hit_box[1] += len_ext - side_ext;
	runway_hit_box[2] += len_ext + side_ext;
	runway_hit_box[3] -= len_ext - side_ext;

	Bbox2	runway_bounds;
	runway_info.runway_ptr->GetBounds(gis_Geo,runway_bounds);

	for(auto pp : all_polygons)
	{
		if(pp->Cull(runway_bounds))
		{
			std::string vpath;
			const pol_info_t * pol_info;

			int lg = group_TaxiwaysBegin;
			pp->GetResource(vpath);
			if(!vpath.empty() && rmgr->GetPol(vpath,pol_info) && !pol_info->group.empty())
				lg = layer_group_for_string(pol_info->group.c_str(),pol_info->group_offset, lg);

			if(lg <= group_RunwaysEnd ) break;  // don't worry about polygons drawn underneath the runway

			bool isOnRunway = pp->Overlaps(gis_Geo, runway_hit_box);

			if (isOnRunway)
			{
				std::string msg ;
				pp->GetName(msg);
				msg = "The gateway discourages user created runway markings. DrapedPolygon '" + msg + "' intersects with runway " + runway_info.name;
				msgs.push_back(validation_error_t(msg, warn_atcrwy_marking, pp, apt));
			}
		}
	}
}

static void TJunctionCrossingTest(const TaxiRouteInfoVec_t& all_taxiroutes, validation_error_vector& msgs, WED_Airport * apt)
{
	/*For each edge A
		for each OTHER edge B

		If A and B intersect, do not mark them as a T - the intersection test will pick this up and we don't want to have double errors on a single user problem.
		else If A and B share a common vertex, do not mark them as a T junction - this is legal. (Do this by comparing the Point2, not the IGISPoint *.If A and B have separate exactly on top of each other nodes, the duplicate nodes check will find this, and again we don't want to squawk twice on one user error.

			for each end B(B src and B dst)
				if end has a valence of 1
					if the distance between A and the end node you are testing is < M meters
						validation failure - that node is too close to a taxiway route but isn't joined.
	*/
	#define TJUNCTION_THRESHOLD_TRUCKS  5.0
	#define TJUNCTION_THRESHOLD_AC_REL  0.6

	#define SHORT_THRESHOLD_TRUCKS   5.0
	#define SHORT_THRESHOLD_AC_SM	 7.0
	#define SHORT_THRESHOLD_AC		10.0
	#define SHORT_THRESHOLD_AC_LG	20.0
	#define STR(s) #s
	
	std::set<WED_TaxiRoute *> crossing_edges, short_edgesAB, short_edgesC, short_edgesDEF, short_edgesT;
	auto grievance = gExportTarget == wet_gateway ? err_atc_taxi_short : warn_atc_taxi_short;

	for (auto tr_a = all_taxiroutes.cbegin(); tr_a != all_taxiroutes.cend(); ++tr_a)
	{
		Segment2 edge_a = tr_a->segment_m;
		double length_sq = edge_a.squared_length();
		if (tr_a->is_aircraft_route)
		{
			switch (tr_a->ptr->GetWidth())
			{
				case width_A:
				case width_B:
					if (length_sq < SHORT_THRESHOLD_AC_SM * SHORT_THRESHOLD_AC_SM)
						short_edgesAB.insert(tr_a->ptr);
					break;
				case width_C:
					if (length_sq < SHORT_THRESHOLD_AC * SHORT_THRESHOLD_AC)
						short_edgesC.insert(tr_a->ptr);
					break;
				default:
					if (length_sq < SHORT_THRESHOLD_AC_LG * SHORT_THRESHOLD_AC_LG)
						short_edgesDEF.insert(tr_a->ptr);
					break;
			}
		}
		else if (length_sq < SHORT_THRESHOLD_TRUCKS * SHORT_THRESHOLD_TRUCKS)
					short_edgesT.insert(tr_a->ptr);

		for (auto tr_b = tr_a + 1; tr_b != all_taxiroutes.end(); ++tr_b)
		{
			Segment2 edge_b = tr_b->segment_m;

			// Skip if the edges are colocated at one end, i.e. are propper merged or not so propper unmerged nodes
			// should also catch testing one edge against itself
			if (edge_a.p1 == edge_b.p1 || edge_a.p1 == edge_b.p2 ||
				edge_a.p2 == edge_b.p1 || edge_a.p2 == edge_b.p2 ) continue;

			Point2 tmp;
			if (edge_a.intersect(edge_b,tmp))
			{
				crossing_edges.insert(tr_a->ptr);
				crossing_edges.insert(tr_b->ptr);
				continue;
			}

			for (int i = 0; i < 2; i++)
			{
				// its also worth changing this to Bezier2.is_near() to prepare for future curved edges
				double dist_b_node_to_a_edge = i ? edge_a.squared_distance(edge_b.p2) : edge_a.squared_distance(edge_b.p1);
				double dist_min;
				if (tr_a->is_aircraft_route)
				{
					switch (tr_a->ptr->GetWidth())
					{
					case width_A: dist_min = 4.5 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_B: dist_min = 6.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_C: dist_min = 9.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_D: dist_min = 14.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_E: dist_min = 14.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_F: dist_min = 16.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					}
				}
				else              dist_min = TJUNCTION_THRESHOLD_TRUCKS;

				if (dist_b_node_to_a_edge < dist_min * dist_min)
				{
					std::set<WED_Thing*> node_viewers;
					tr_b->nodes[i]->GetAllViewers(node_viewers);

					if (node_viewers.size() == 1)
					{
						std::vector<WED_Thing*> problem_children;
						problem_children.push_back(tr_a->ptr);
						problem_children.push_back(tr_b->nodes[i]);

						msgs.push_back(validation_error_t("Taxi route " + tr_a->name + " is not joined to destination route.",
								err_taxi_route_not_joined_to_dest_route, problem_children, apt));
					}
				}
				double dist_a_node_to_b_edge = i ? edge_b.squared_distance(edge_a.p2) : edge_b.squared_distance(edge_a.p1);
				if (tr_b->is_aircraft_route)
				{
					switch (tr_b->ptr->GetWidth())
					{
					case width_A: dist_min = 4.5 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_B: dist_min = 6.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_C: dist_min = 9.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_D: dist_min = 14.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_E: dist_min = 14.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					case width_F: dist_min = 16.0 * TJUNCTION_THRESHOLD_AC_REL; break;
					}
				}
				else              dist_min = TJUNCTION_THRESHOLD_TRUCKS;
				if (dist_a_node_to_b_edge < dist_min * dist_min)
				{
					std::set<WED_Thing*> node_viewers;
					tr_a->nodes[i]->GetAllViewers(node_viewers);

					if (node_viewers.size() == 1)
					{
						std::vector<WED_Thing*> problem_children;
						problem_children.push_back(tr_b->ptr);
						problem_children.push_back(tr_a->nodes[i]);

						msgs.push_back(validation_error_t("Taxi route " + tr_b->name + " is not joined to a destination route.",
								err_taxi_route_not_joined_to_dest_route, problem_children, apt));
					}
				}
			}
		}
	}
	for(auto e : crossing_edges)
		msgs.push_back(validation_error_t("Airport contains crossing ATC routing lines with no node at the crossing point."
										  " Split the lines and join the nodes.", err_airport_ATC_network, e, apt));
	for (auto e : short_edgesAB)
		msgs.push_back(validation_error_t(std::string("Airport contains short (<") + std::to_string((int)SHORT_THRESHOLD_AC_SM) + "m) Taxi route segment(s).",
			grievance, e, apt));
	for (auto e : short_edgesC)
			msgs.push_back(validation_error_t(std::string("Airport contains short (<") + std::to_string((int) SHORT_THRESHOLD_AC) + "m) Taxi route segment(s).",
			grievance, e, apt));
	for (auto e : short_edgesDEF)
		msgs.push_back(validation_error_t(std::string("Airport contains short (<") + std::to_string((int) SHORT_THRESHOLD_AC_LG) + "m) Taxi route segment(s).",
			grievance, e, apt));
	for (auto e : short_edgesT)
		msgs.push_back(validation_error_t(std::string("Airport contains short (<") + std::to_string((int) SHORT_THRESHOLD_TRUCKS) + "m) Truck route segment(s).",
			grievance, e, apt));
}

static void TestInvalidHotZOneTags(const TaxiRouteInfoVec_t& taxi_routes, const std::set<int>& legal_rwy_oneway, const std::set<int>& legal_rwy_twoway,
										validation_error_vector& msgs, WED_Airport * apt)
{
	for(auto& tr : taxi_routes)
	{
		// See bug http://dev.x-plane.com/bugbase/view.php?id=602 - blank names are okay!
		//			if (name.empty() && !taxi->IsRunway())
		//			{
		//				msg = "This taxi route has no name.  All taxi routes must have a name so that ATC can give taxi instructions.";
		//			}

		if(tr.ptr->HasInvalidHotZones(legal_rwy_oneway))
		{
			msgs.push_back(validation_error_t(std::string("Taxi route '") + tr.name + "' has hot zones for runways not present at its airport.",
									err_taxi_route_has_hot_zones_not_present, tr.ptr, apt));
		}

		if(tr.ptr->IsRunway())
		{
			if (legal_rwy_twoway.count(tr.ptr->GetRunway()) == 0)
			{
				msgs.push_back(validation_error_t(std::string("Taxi route '") + tr.name + "' is std::set to a runway not present at the airport.",
					err_taxi_route_set_to_runway_not_present, tr.ptr, apt));
			}
		}
		else if(tr.hot_arrivals.size() || tr.hot_arrivals.size())
		{
			for(int i = 0; i < 2; i++)
				if(get_node_valence(tr.nodes[i]) < 2)
				{
					msgs.push_back(validation_error_t("Taxi routes with HotZone tags most be connected on both ends to other taxi routes.",
						err_taxi_route_has_hot_zones_but_not_connected, tr.nodes[i], apt));
				}
		}
	}
}

static void TwyNameCheck(const TaxiRouteInfoVec_t& all_taxiroutes_info, validation_error_vector& msgs, WED_Airport *apt)
{
	std::unordered_map<std::string, std::set<WED_TaxiRoute*> > long_named, odd_named;
	for(auto& tr : all_taxiroutes_info)
	{
		if(tr.is_aircraft_route && !tr.ptr->IsRunway())
		{
			if(tr.name.size() > 3)
				long_named[tr.name].insert(tr.ptr);
			else if(!tr.name.empty())
			{
				bool ok = isalpha(tr.name[0]);
				if(tr.name.size() > 1 && !isalnum(tr.name[1])) ok = false;
				if(tr.name.size() > 2 && !isdigit(tr.name[2])) ok = false;
				if(!ok)
					odd_named[tr.name].insert(tr.ptr);

			}
		}
	}
	for(auto t : long_named)
		msgs.push_back(validation_error_t(std::string("Taxi route '") + t.first+ "' name is unusually long, should be less than 4 characters.",
			warn_taxi_route_name_unusual, t.second, apt));

	for(auto t : odd_named)
		msgs.push_back(validation_error_t(std::string("Taxi route '") + t.first + "' name is likely wrong, should be 1-2 letters optionally followed by 1-2 digits or empty.",
			warn_taxi_route_name_unusual, t.second, apt));
}

//-----------------------------------------------------------------------------

void WED_DoATCRunwayChecks(WED_Airport& apt, validation_error_vector& msgs, const TaxiRouteVec_t& all_taxiroutes_plain,
							const RunwayVec_t& all_runways, const std::set<int>& legal_rwy_oneway, const std::set<int>& legal_rwy_twoway,
							const FlowVec_t& all_flows, WED_ResourceMgr * res_mgr, const std::vector<WED_RampPosition*>& ramps,
							const std::vector<WED_RoadEdge*>& roads)
{
	Bbox2 box;
	apt.GetBounds(gis_Geo, box);
	CoordTranslator2 translator;
	CreateTranslatorForBounds(box,translator); // equivalent to MapZoomerNew: Pre-calculates cos(lat) to covert LL->Meter with linear algebra, only

	TaxiRouteInfoVec_t	all_taxiroutes_info;
	TaxiRouteVec_t 		all_aircraftroutes_plain;
	TaxiRouteInfoVec_t	all_aircraftroutes;
	TaxiRouteInfoVec_t	all_truckroutes;

	all_taxiroutes_info.reserve(all_taxiroutes_plain.size());

	for(const auto& taxi : all_taxiroutes_plain)
	{
		TaxiRouteInfo tr_info(taxi,translator);
		all_taxiroutes_info.push_back(tr_info);
		if(tr_info.is_aircraft_route)
		{
			all_aircraftroutes.push_back(tr_info);
			all_aircraftroutes_plain.push_back(taxi);
		}
		else
			all_truckroutes.push_back(tr_info);
	}

	TJunctionCrossingTest(all_taxiroutes_info, msgs, &apt);
	TwyNameCheck(all_taxiroutes_info, msgs, &apt);

	RunwayInfoVec_t all_runways_info;
	for(const auto& rwy : all_runways)
		all_runways_info.push_back(RunwayInfo(rwy,translator));

	if(!all_aircraftroutes.empty())
	{
		if(gExportTarget == wet_xplane_900)
		{
			msgs.push_back(validation_error_t("ATC Taxi Routes are only supported in X-Plane 10 and newer.",
						err_atc_taxi_routes_only_for_gte_xp10, all_taxiroutes_plain, &apt));
			return;
		}

		ATCRunwayUseVec_t all_use_rules;
		RunwayInfoVec_t potentially_active_runways = CollectPotentiallyActiveRunways(all_aircraftroutes, all_runways_info, all_flows, all_use_rules, msgs, &apt);

		FullyConnectedNetworkCheck(all_aircraftroutes_plain, msgs, &apt);

		TestInvalidHotZOneTags(all_aircraftroutes, legal_rwy_oneway, legal_rwy_twoway, msgs, &apt);

		for(auto& runway_info : potentially_active_runways)
		{
			int original_num_errors = msgs.size();
			TaxiRouteInfoVec_t matching_taxiroutes = FilterMatchingRunways(runway_info, all_aircraftroutes);

			if (!matching_taxiroutes.empty())
			{
				if (AllTaxiRouteNodesInRunway(runway_info, matching_taxiroutes, msgs, &apt))
				{
					if (TaxiRouteParallelCheck(runway_info, matching_taxiroutes, msgs, &apt))
					{
						if (TaxiRouteCenterlineCheck(runway_info, matching_taxiroutes, msgs, &apt))
						{
							if (DoTaxiRouteConnectivityChecks(runway_info, all_aircraftroutes, matching_taxiroutes, translator, msgs, &apt))
							{
								if (RunwayHasCorrectCoverage(runway_info, matching_taxiroutes, msgs, &apt))
								{
									//Add additional checks as needed here
								}
							}
						}
					}
				}
			}
	#if DEBUG_VIS_LINES
	#if DEBUG_VIS_LINES < 2
			if (msgs.size() - original_num_errors != 0)
	#endif
			{
				debug_mesh_polygon(runway_info.corners_geo, 1, 0, 1);
				debug_mesh_segment(runway_info.centerline_geo, DBG_LIN_COLOR);
			}
	#endif
			AssignRunwayUse(runway_info, all_use_rules);
			bool passes_hotzone_checks = DoHotZoneChecks(runway_info, all_aircraftroutes, ramps, msgs, &apt);
			//Nothing to do here yet until we have more checks after this
		}
	}

	std::vector<WED_PolygonPlacement *> all_polys;
	if(gExportTarget == wet_gateway)
		CollectRecursive(&apt,back_inserter(all_polys), WED_PolygonPlacement::sClass);

	if(!all_polys.empty())
	{
		for(const auto& runway_info : all_runways_info)
			AnyPolgonsOnRunway(runway_info, all_polys, msgs, &apt, res_mgr);
	}

	if(!all_truckroutes.empty())
	{
		for(const auto& runway_info : all_runways_info)
			AnyTruckRouteNearRunway(runway_info, all_truckroutes, roads, msgs, &apt);
	}

}
