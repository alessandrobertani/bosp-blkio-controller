/*
 * Copyright (C) 2012  Politecnico di Milano
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BBQUE_RESOURCE_ACCOUNTER_H_
#define BBQUE_RESOURCE_ACCOUNTER_H_

#include <map>
#include <set>

#include "bbque/resource_accounter_conf.h"
#include "bbque/config.h"
#include "bbque/configuration_manager.h"
#include "bbque/command_manager.h"

#include "bbque/res/resource_utils.h"
#include "bbque/res/resource_tree.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/utility.h"
#include "bbque/cpp11/thread.h"
#include "bbque/cpp11/condition_variable.h"

#define RESOURCE_ACCOUNTER_NAMESPACE "bq.ra"

#define SYNC_RVIEW_PATH       "ra.sync."
#define PREFIX_PATH           "sys"
#define TOKEN_PATH_MAX_LEN    30

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque
{

using AppAssignmentsMap_t = std::map<AppUid_t, br::ResourceAssignmentMapPtr_t>;
using AppAssignmentsMapPtr_t = std::shared_ptr<AppAssignmentsMap_t>;
using AppAssignmentsViewsMap_t = std::map<br::RViewToken_t, AppAssignmentsMapPtr_t>;
using ResourceSet_t = std::set<br::ResourcePtr_t>;
using ResourceSetPtr_t = std::shared_ptr<ResourceSet_t>;
using ResourceViewsMap_t = std::map<br::RViewToken_t, ResourceSetPtr_t>;

class ApplicationManager;

/**
 * @class ResourceAccounter
 * @ingroup sec07_ra
 *
 * @brief Provides functions to register the system resources, and query
 * their status
 */
class ResourceAccounter: public ResourceAccounterConfIF, CommandHandler
{

public:

	/**
	 * @brief The states of the module
	 */
	enum class State
	{
	        /** Information not ready for query and accounting */
	        NOT_READY,
	        /** Information ready for query and accounting */
	        READY,
	        /** A synchronization step is in progress */
	        SYNC
	};

	/**
	 * @brief Return the instance of the ResourceAccounter
	 */
	static ResourceAccounter & GetInstance();

	/**
	 * @brief Called when all the hardware resources have been registered and
	 * the platform is ready
	 */
	void SetPlatformReady();

	/**
	 * @brief Called when there are updates in the hardware resources and thus
	 * the platform cannot be considered ready
	 */
	void SetPlatformNotReady();

	/**
	 * @brief Wait for the platform synchronization to finish
	 */
	void WaitForPlatformReady();

	/**
	 * @brief Destructor
	 */
	virtual ~ResourceAccounter();

	/**
	 * @brief Total amount of the given registered resource
	 */
	uint64_t Total(std::string const & path);

	uint64_t Total(br::ResourcePtrList_t & resources_list) const;

	uint64_t Total(
	        br::ResourcePathPtr_t resource_path_ptr, PathClass_t rpc = EXACT) const;

	/**
	 * @brief Available amount of the given registered resource
	 */
	uint64_t Available(
	        std::string const & path,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t());

	uint64_t Available(
	        br::ResourcePtrList_t & resources_list,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) const;

	uint64_t Available(
	        br::ResourcePathPtr_t resource_path_ptr, PathClass_t rpc = EXACT,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) const;

	/**
	 * @brief Used/assigned amount of the given registered resource
	 */
	uint64_t Used(
	        std::string const & path,
	        br::RViewToken_t status_view = 0);

	uint64_t Used(
	        br::ResourcePtrList_t & resources_list,
	        br::RViewToken_t status_view = 0) const;

	uint64_t Used(
	        br::ResourcePathPtr_t resource_path_ptr,
	        PathClass_t rpc = EXACT,
	        br::RViewToken_t status_view = 0) const;

	/**
	 * @brief Amount of resource used by a given application/process
	 */
	uint64_t UsedBy(
	        std::string const & path,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view = 0);

	uint64_t UsedBy(
	        br::ResourcePtrList_t & resources_list,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view = 0) const;

	uint64_t UsedBy(
	        br::ResourcePathPtr_t resource_path_ptr,
	        ba::SchedPtr_t papp,
	        PathClass_t rpc = EXACT,
	        br::RViewToken_t status_view = 0) const;


	/**
	 * @see ResourceAccounterStatusIF
	 */
	uint64_t Unreserved(std::string const & path);

	uint64_t Unreserved(br::ResourcePtrList_t & resources_list) const;

	uint64_t Unreserved(br::ResourcePathPtr_t resource_path_ptr) const;

	/**
	 * @see ResourceAccounterStatusIF
	 */
	uint16_t Count(br::ResourcePathPtr_t resource_path_ptr) const;

	uint16_t CountPerType(br::ResourceType type) const;

	uint16_t CountTypes() const {
		return r_ids_per_type.size();
	}

	/**
	 * @see ResourceAccounterStatusIF
	 */
	std::map<br::ResourceType, std::set<BBQUE_RID_TYPE>> const & GetTypes() const {
		return r_ids_per_type;
	}

	/**
	 * @see ResourceAccounterStatusIF
	 */
	br::ResourcePtr_t GetResource(std::string const & path);

	br::ResourcePtr_t GetResource(br::ResourcePathPtr_t resource_path_ptr) const;

	/**
	 * @see ResourceAccounterStatusIF
	 */
	br::ResourcePtrList_t GetResources(std::string const & path);

	br::ResourcePtrList_t GetResources(br::ResourcePathPtr_t resource_path_ptr) const;

	/**
	 * @see ResourceAccounterStatusIF
	 */
	bool ExistResource(std::string const & path);

	bool ExistResource(br::ResourcePathPtr_t resource_path_ptr) const;

	std::set<br::ResourcePtr_t> GetResourceSet() {
		return resource_set;
	}

	/**
	 * @see ResourceAccounterStatusIF
	 */
	br::ResourcePathPtr_t const GetPath(std::string const & path_str);

	/**
	 * @brief Get the cumulative amount of resource usage
	 *
	 * @param assign_map A map of Usage pointers
	 * @param papp The application requiring resource assignments
	 * @param status_view The token referencing the resource state view
	 * @param r_type The type of resource to query
	 * @param r_scope_type The scope under which consider the resource
	 * @param r_scope_id  [optional] The ID of the resource scope type
	 *
	 * @return The amount of resource usage
	 */
	uint64_t GetAssignedAmount(
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view,
	        br::ResourceType r_type,
	        br::ResourceType r_scope_type =
	                br::ResourceType::UNDEFINED,
	        BBQUE_RID_TYPE r_scope_id = R_ID_ANY);

	uint64_t GetAssignedAmount(
	        br::ResourceAssignmentMap_t const & assign_map,
	        ba::SchedPtr_t papp,
	        br::RViewToken_t status_view,
	        br::ResourceType r_type,
	        br::ResourceType r_scope_type =
	                br::ResourceType::UNDEFINED,
	        BBQUE_RID_TYPE r_scope_id = R_ID_ANY) const;

	/**
	 * @brief Get the cumulative amount of resource usage
	 *
	 * This version should be used in case of query on a unbound resource
	 * usages map.
	 *
	 * @param assign_map A map of Usage pointers
	 * @param r_type The type of resource to query
	 * @param r_scope_type The scope under which consider the resource
	 * @param r_scope_id  [optional] The ID of the resoruce scope type
	 *
	 * @return The amount of resource usage
	 */
	uint64_t GetAssignedAmount(
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        br::ResourceType r_type,
	        br::ResourceType r_scope_type =
	                br::ResourceType::UNDEFINED,
	        BBQUE_RID_TYPE r_scope_id = R_ID_ANY) const {
		return GetAssignedAmount(
		               *(assign_map.get()),
		               r_type,
		               r_scope_type,
		               r_scope_id);
	}

	uint64_t GetAssignedAmount(
	        br::ResourceAssignmentMap_t const & assign_map,
	        br::ResourceType r_type,
	        br::ResourceType r_scope_type =
	                br::ResourceType::UNDEFINED,
	        BBQUE_RID_TYPE r_scope_id = R_ID_ANY) const;

	/**
	 * @brief Show the system resources status
	 *
	 * This is an utility function for debug purpose that print out all the
	 * resources path and values about usage and total amount.
	 *
	 * @param status_view Token of the resources state view
	 * @param verbose print in INFO log level is true, while false in DEBUG
	 */
	void PrintStatusReport(
	        br::RViewToken_t status_view = 0, bool verbose = false) const;

	/**
	 * @brief Print details about how resource usage is partitioned among
	 * applications/EXCs
	 *
	 * @param resource_ptr The resource description pointer
	 * @param percent Resource assignment as percentage
	 * @param status_view The token referencing the resource state view
	 * @param verbose print in INFO log level is true, while false in DEBUG
	 */
	void PrintAppDetails(
	        br::ResourcePtr_t resource_ptr, bool percent,
	        br::RViewToken_t status_view, bool verbose) const;

	/**
	 * @brief Print the number of registered resources for each type
	 */
	void PrintCountPerType() const;

	/**
	 * @brief A prefix path for recipe validation
	 */
	br::ResourcePath const & GetPrefixPath() const;

	/**
	 * @brief Register a resource
	 *
	 * Setup informations about a resource installed into the system.
	 * Resources can be system-wide or placed on the platform. A resource is
	 * identified by its path, which also provides information about
	 * architectural hierarchies of the platform.
	 *
	 * @param path Resource path in char string format
	 * @param units Units for the amount value (i.e. "1", "Kbps", "Mb", ...)
	 * @param amount The total amount available
	 *
	 * @return RA_SUCCESS if the resource has been successfully registered.
	 * RA_ERR_MISS_PATH if the path string is empty. RA_ERR_MEM if the
	 * resource descriptor cannot be allocated.
	 */
	br::ResourcePtr_t RegisterResource(
	        std::string const & path,
	        std::string const & units,
	        uint64_t amount);

	/**
	 * @brief Update the total amount of the specified resource
	 *
	 * @param path Resource path in char string format
	 * @param units Units for the amount value (i.e. "1", "Kbps", "Mb", ...)
	 * @param amount The total amount available
	 */
	ExitCode_t UpdateResource(
	        std::string const & path,
	        std::string const & units,
	        uint64_t amount);

	/**
	 * @brief Book e a set of resources
	 *
	 * The method first check that the application doesn't hold another
	 * resource set, then check thier availability, and finally reserves, for
	 * each resource in the usages map specified, the required quantity.
	 *
	 * @param papp The application requiring resource assignments
	 * @param assign_map Map of Usage objects
	 * @param status_view The token referencing the resource state view
	 * @param do_check If true the controls upon set validity and resources
	 * availability are enabled
	 *
	 * @return RA_SUCCESS if the operation has been successfully performed.
	 * RA_ERR_MISS_APP if the application descriptor is null.
	 * RA_ERR_MISS_USAGES if the resource assignments map is empty.
	 * RA_ERR_MISS_VIEW if the resource state view referenced by the given
	 * token cannot be retrieved.
	 * RA_ERR_USAGE_EXC if the resource set required is not completely
	 * available.
	 */
	ExitCode_t BookResources(
	        ba::SchedPtr_t papp,
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        br::RViewToken_t status_view = 0);

	/**
	 * @brief Release the resources
	 *
	 * The method is typically called when an application stops running
	 * (exit or is killed) and releases all the resource assignments.
	 * It lookups the current set of resource assignments of the application and
	 * release it all.
	 *
	 * @param papp The application holding the resources
	 * @param status_view The token referencing the resource state view
	 */
	void ReleaseResources(
	        ba::SchedPtr_t papp, br::RViewToken_t status_view = 0);


	/**
	 * @brief Define the amount of resource to be reserved
	 *
	 * A certain amount of resources could be dynamically reserved at
	 * run-time in order to avoid their allocation to demanding
	 * applications.
	 *
	 * Resources reservation could be conveniently used to both block
	 * access to resources temporarely not available as well as implement
	 * run-time resources management policies which, for examples, target
	 * thermal leveling issues.
	 *
	 * If a resource path template is specified, the specified amout of
	 * resource is reserved for each resource matching the template.
	 *
	 * @param path Resource path
	 * @param amount The amount of resource to reserve, 0 for note
	 *
	 * @return RA_SUCCESS if the reservation has been completed correctly,
	 * RA_FAILED otherwise.
	 */
	ExitCode_t ReserveResources(
	        br::ResourcePathPtr_t resource_path_ptr, uint64_t amount);

	ExitCode_t ReserveResources(
	        std::string const & path, uint64_t amount);

	/**
	 * @brief Set the resource to a virtual offline status
	 *
	 * The resource will not be available for any accounting
	 *
	 * @param path Resource path in char string format
	 * @return RA_SUCCESS for succesfull call, RA_FAILED if the path
	 * does not refer to any valid resource
	 */
	ExitCode_t SetOffline(std::string const & path);

	ExitCode_t SetOffline(br::ResourcePathPtr_t path);

	/**
	 * @brief Set the resource (back) to a (virtual) online status
	 *
	 * The resource will become available again
	 *
	 * @param path Resource path in char string format
	 * @return RA_SUCCESS for succesfull call, RA_FAILED if the path
	 * does not refer to any valid resource
	 */
	ExitCode_t SetOnline(std::string const & path);

	ExitCode_t SetOnline(br::ResourcePathPtr_t path);

	/**
	 * @brief Check if a given resource is offline
	 *
	 * @param path Resource path pointer
	 * @return true if offline, false otherwise
	 */
	bool IsOffline(br::ResourcePathPtr_t path) const;

	bool IsOffline(std::string const & path);

	/**
	 * @brief Check if resources are being reshuffled
	 *
	 * Resources reshuffling happens when two resources bindings are not
	 * the same, i.e. different kind or amount of resources.
	 *
	 * @param current_map the "current" resources bindings
	 * @param next_map the "next" resources bindings
	 *
	 * @return true when resources are being reshuffled
	 */
	bool IsReshuffling(
	        br::ResourceAssignmentMapPtr_t const & current_map,
	        br::ResourceAssignmentMapPtr_t const & next_map);


	/**
	 * @see ResourceAccounterConfIF
	 */
	ExitCode_t GetView(std::string const & who_req, br::RViewToken_t & tok);

	/**
	 * @see ResourceAccounterConfIF
	 */
	ExitCode_t PutView(br::RViewToken_t tok);

	/**
	 * @brief Get the system resource state view
	 *
	 * @return The token of the system view
	 */
	br::RViewToken_t GetSystemView() const {
		return sys_view_token;
	}

	/**
	 * @brief Get the synchronization resource state view
	 *
	 * @return The token of the synchronization state view
	 */
	br::RViewToken_t GetSyncView() const {
		return sync_ssn.view;
	}

	/**
	 * @brief Get the scheduled resource state view
	 *
	 * @return The token of the scheduled view
	 */
	br::RViewToken_t GetScheduledView() const {
		return sch_view_token;
	}

	/**
	 * @brief Set the scheduled resource state view
	 *
	 * @return The token of the system view
	 */
	void SetScheduledView(br::RViewToken_t svt);

	/**
	 * @brief Start a synchronized mode session
	 *
	 * Once a scheduling/resource allocation has been performed we need to
	 * make the changes effective, by updating the system resources state.
	 * For doing that a "synchronized mode session" must be started. This
	 * method open the session and init a new resource view by computing the
	 * resource accounting info of the Applications/ExC having a "RUNNING"
	 * scheduling state (the ones that continue running without
	 * reconfiguration or migrations).
	 *
	 * @return @see ExitCode_t
	 */
	ExitCode_t SyncStart();

	/**
	 * @brief Acquire resources for an Application/ExC
	 *
	 * If the sync session is not open does nothing. Otherwise it does
	 * resource booking using the state view allocated for the session.
	 * The resource set is retrieved from the "next AWM".
	 *
	 * @param papp Application/ExC acquiring the resources
	 *
	 * @return @see ExitCode_t
	 */
	ExitCode_t SyncAcquireResources(ba::SchedPtr_t const & papp);

	/**
	 * @brief Abort a synchronized mode session
	 *
	 * Changes are trashed away. The resource bookings performed in the
	 * session are canceled.
	 */
	void SyncAbort();

	/**
	 * @brief Commit a synchronized mode session
	 *
	 * Changes are made effective. Resources must be allocated accordingly to
	 * the state view built during in the session.
	 *
	 * @return @see ExitCode_t
	 */
	ExitCode_t SyncCommit();

	/**
	 * @brief Wait for the synchronization completion
	 *
	 * This member function is useful whenever a thread accesses a data
	 * structure that may be modified during a synchronization phase
	 */
	void SyncWait();


	/**
	 * @see CommandHandler
	 */
	int CommandsCb(int argc, char * argv[]);

	/**
	 * @brief Handler for the shell command that allows to redefine the total
	 * amount of resource that can be allocated.
	 *
	 * @param r_path The resource path (char string)
	 * @param value  The new total amount
	 *
	 * @return 0 if success, a positive integer value otherwise
	 */
	int SetResourceTotalHandler(char * r_path, char * value);


#ifdef CONFIG_BBQUE_PM

	/**********************************************************************
	 * POWER MANAGEMENT 		                                      *
	 **********************************************************************/

	/**
	 * @brief Specify a new power management configuration for a specific
	 * resource
	 *
	 * @param resource The descriptor (pointer) of the resource to configure
	 * @param config The power management configuration to apply
	 */
	void EnqueueResourceToPowerManage(
	        br::ResourcePtr_t resource, br::Resource::PowerSettings config);

	/**
	 * @brief Dequeue the first resource to which apply a pending power
	 * configuration
	 *
	 * @return A resource descriptor (pointer)
	 */
	br::ResourcePtr_t DequeueResourceToPowerManage();

	/**
	 * @brief Turn on all the resources previously put offline
	 */
	void RestoreResourcesToPowerOn();

#endif // CONFIG_BBQUE_PM

private:

	/**
	 * @brief This is used for selecting the state attribute to return from
	 * QueryStatus
	 */
	enum QueryOption_t {
	        /** Amount of resource available */
	        RA_AVAIL = 0,
	        /** Amount of resource used */
	        RA_USED,
	        /** Total amount of not reserved resource */
	        RA_UNRESERVED,
	        /** Total amount of resource */
	        RA_TOTAL,
	        /** Amount of resource used by the given application */
	        RA_USED_BY
	};

	/**
	 * @struct SyncSession_t
	 * @brief Store info about a synchronization session
	 */
	struct SyncSession_t {
		/** Token for the temporary resource view */
		br::RViewToken_t view;
		/** Count the number of session elapsed */
		uint32_t count;
	} sync_ssn;

	/** The logger used by the resource accounter */
	std::unique_ptr<bu::Logger> logger;

	/** The Application Manager component */
	bbque::ApplicationManager & am;

	/** The Command Manager component */
	CommandManager & cm;

	/** The Configuration Manager */
	ConfigurationManager & fm;


	/** Mutex protecting Resource Accounter status */
	std::mutex status_mtx;

	/** Conditional variable for status synchronization */
	std::condition_variable status_cv;

	/** This contain the status of the Resource Accounter */
	State status;


	/** The tree of all the resources in the system.*/
	br::ResourceTree resources;

	/** The resource paths registered (strings and objects) */
	std::map<std::string, br::ResourcePathPtr_t> r_paths;

	/** The resource paths registered (strings and objects) */
	std::set<br::ResourcePtr_t> resource_set;

	/** Id numbers set for each type of registered resource */
	std::map<br::ResourceType, std::set<BBQUE_RID_TYPE>> r_ids_per_type;

#ifdef CONFIG_BBQUE_PM

	/** Resources to power on */
	std::map<br::ResourcePathPtr_t, br::ResourcePtr_t> resources_to_power_on;

	/** Resources for which a new power management configuration has been specified */
	std::map<br::ResourcePathPtr_t, br::ResourcePtr_t> resources_to_power_manage;
#endif

	/** Resource path (pointer) referencing the prefix */
	br::ResourcePathPtr_t r_prefix_path;

	/** Keep track of the max length between resources path string */
	uint8_t path_max_len = 0;


	/**
	 * Map containing the pointers to the map of resource assignments specified in
	 * the current working modes of each application. The key is the view
	 * token. For each view an application can hold just one set of resource
	 * usages.
	 */
	AppAssignmentsViewsMap_t assign_per_views;

	/**
	 * Keep track of the resources allocated for each view. This data
	 * structure is needed to supports easily a view deletion or to set a view
	 * as the new system state.
	 */
	ResourceViewsMap_t rsrc_per_views;

	/**
	 * Pointer (shared) to the map of applications resource assignments, currently
	 * describing the resources system state (default view).
	 */
	AppAssignmentsMapPtr_t sys_assign_view;

	/**
	 * The token referencing the system resources state (default view).
	 */
	br::RViewToken_t sys_view_token;

	/**
	 * @brief Token referencing the view on scheduled resources
	 *
	 * When a new scheduling has been selected, this is the token referencing
	 * the corresponding view on resources state. When that schedule has been
	 * committed, i.e. resources usage synchronized, this has the same value
	 * of sys_view_token.
	 */
	br::RViewToken_t sch_view_token = 0;

	/**
	 * Default constructor
	 */
	ResourceAccounter();

	/**
	 * @brief Set the status to READY
	 */
	void SetState(State _s) {
		std::unique_lock<std::mutex> status_ul(status_mtx);
		status = _s;
		logger->Debug("SetState: => %d", static_cast<int>(_s));
		status_cv.notify_all();
	}

	/**
	 * @brief Wrap the class of resource path on resource tree matching flags
	 *
	 * @param rpc The resource path class
	 *
	 * @return A resource tree matching flag
	 */
	uint16_t RTFlags(PathClass_t rpc) const {
		switch (rpc) {
		case EXACT:
			return RT_MATCH_FIRST;
		case MIXED:
			return RT_MATCH_MIXED;
		case TEMPLATE:
			return RT_MATCH_TYPE;
		default:
			return 0;
		}
	}

	/**
	 * @brief Thread unsafe version of @ref GetView
	 */
	ExitCode_t _GetView(std::string const & who_req, br::RViewToken_t & tok);

	/**
	 * @brief Thread unsafe version of @ref SetView
	 */
	br::RViewToken_t _SetView(br::RViewToken_t tok);

	/**
	 * @brief Thread unsafe version of @ref PutView
	 */
	ExitCode_t _PutView(br::RViewToken_t tok);


	/**
	 * @brief Get a list of resource descriptor
	 *
	 * The list is retrieved depending on the resource path class specified.
	 * If this is "undefined" it will be up to the function to understand the
	 * class of the resource path, by invoking function member GetResources.
	 * Otherwise, whenever the path class is given, the ResourceTree find_list
	 * member function is called with the suitable matching flags.
	 *
	 * @param resource_path_ptr The resource path referencing the resources
	 * @param rpc The token referencing the resource state view
	 *
	 * @return A list of pointers (shared) to resource descriptors
	 */
	br::ResourcePtrList_t GetList(
	        br::ResourcePathPtr_t resource_path_ptr,
	        PathClass_t rpc = EXACT) const;

	/**
	 * @brief Return a state parameter (availability, resources used, total
	 * amount) for the resource.
	 *
	 * @param resources_list A list of descriptors of resources of the same type
	 * @param q_opt Resource state attribute requested (@see QueryOption_t)
	 * @param status_view The token referencing the resource state view
	 * @param papp The application interested in the query
	 *
	 * @return The value of the attribute request
	 */
	uint64_t QueryStatus(
	        br::ResourcePtrList_t const & resources_list,
	        QueryOption_t q_opt, br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) const;

	/**
	 * @brief Check the resource availability for a whole set
	 *
	 * @param assign_map A map of Usage objects to check
	 * @param status_view The token referencing the resource state view
	 * @param papp The application interested in the query
	 * @return RA_SUCCESS if all the resources are availables,
	 * RA_ERR_USAGE_EXC otherwise.
	 */
	ExitCode_t CheckAvailability(
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        br::RViewToken_t status_view = 0,
	        ba::SchedPtr_t papp = ba::SchedPtr_t()) const;

	/**
	 * @brief Get a pointer to the map of applications resource assignments
	 *
	 * Each application (or better, "execution context") can hold just one set
	 * of resource assignments. It's the one defined through the working mode
	 * scheduled. Such assertion is valid inside the scope of the resources
	 * state view referenced by the token.
	 *
	 * @param status_view The token referencing the resource state view
	 * @param apps_assign The map of applications resource assignments to get
	 * @return RA_SUCCESS if the map is found. RA_ERR_MISS_VIEW if the token
	 * doesn't match any state view.
	 */
	ExitCode_t GetAppAssignmentsByView(
	        br::RViewToken_t status_view, AppAssignmentsMapPtr_t & apps_assign);

	/**
	 * @brief Book e a set of resources (not thread-safe)
	 *
	 * The method reserves for each resource in the assign_map map specified the
	 * required quantity.
	 *
	 * @param papp The application requiring resource assignments
	 * @param assign_map Map of Usage objects
	 * @param status_view The token referencing the resource state view
	 * @param do_check If true the controls upon set validity and resources
	 * availability are enabled
	 *
	 * @return RA_SUCCESS if the operation has been successfully performed.
	 * RA_ERR_MISS_APP if the application descriptor is null.
	 * RA_ERR_MISS_USAGES if the resource assignments map is empty.
	 * RA_ERR_MISS_VIEW if the resource state view referenced by the given
	 * token cannot be retrieved.
	 * RA_ERR_USAGE_EXC if the resource set required is not completely
	 * available.
	 */
	ExitCode_t _BookResources(
	        ba::SchedPtr_t papp,
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        br::RViewToken_t status_view = 0);

	/**
	 * @brief Increment the resource assignments counts
	 *
	 * Each time an application acquires a set of resources (specified in the
	 * working mode scheduled), the counts of resources used must be increased
	 *
	 * @param assign_map Map of next resource assignments
	 * @param app The application acquiring the resources
	 * @param status_view The token referencing the resource state view
	 *
	 * @return RA_SUCCESS for successful return.
	 * RA_ERR_MISS_VIEW if the resource state view provided is missing.
	 * RA_ERR_MISS_APP if the application to schedule cannot be retrieved.
	 * RA_ERR_APP_USAGES if the application is already scheduled.
	 */
	ExitCode_t IncBookingCounts(
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        ba::SchedPtr_t const & papp,
	        br::RViewToken_t status_view = 0);

	/**
	 * @brief Book a single resource
	 *
	 * Divide the usage amount between the resources referenced in the "binds"
	 * list.
	 *
	 * @param papp The Application/ExC using the resource
	 * @param r_assign Usage object
	 * @param status_view The token referencing the resource state view
	 * @param rsrc_set The set of systems resources in use
	 *
	 * @return RA_ERR_USAGE_EXC if the usage required overcome the
	 * availability. RA_SUCCESS otherwise.
	 */
	ExitCode_t DoResourceBooking(
	        ba::SchedPtr_t const & papp,
	        br::ResourceAssignmentPtr_t & r_assign,
	        br::RViewToken_t status_view,
	        ResourceSetPtr_t & rsrc_set);

	/**
	 * @brief Release the resources
	 *
	 * This is the synchronization session mutex free verios of the dual
	 * public method ReleaseResources
	 *
	 * @see ReleaseResources
	 */
	void _ReleaseResources(ba::SchedPtr_t papp, br::RViewToken_t status_view = 0);

	/**
	 * @brief Allocate a quota of resource in the scheduling case
	 *
	 * Allocate a quota of the required resource in a single resource binding
	 * taken from the "binds" list of the Usage associated.
	 *
	 * @param papp The Application/ExC using the resource
	 * @param rsrc The resource descriptor of the resource binding
	 * @param requested The amount of resource required
	 * @param status_view The token referencing the resource state view
	 */
	void SchedResourceBooking(
	        ba::SchedPtr_t const & papp,
	        br::ResourcePtr_t & rsrc,
	        br::RViewToken_t status_view,
	        uint64_t & requested,
	        uint64_t per_rsrc_allocated = 0);

	/**
	 * @brief Allocate a quota of resource in the synchronization case
	 *
	 * Allocate a quota of the required resource in a single resource binding
	 * taken checking the assigments done by the scheduler. To retrieve this
	 * information, the scheduled view is properly queried.
	 *
	 * @param papp The Application/ExC using the resource
	 * @param rsrc The resource descriptor of the resource binding
	 * @param requested The amount of resource required
	 */
	void SyncResourceBooking(
	        ba::SchedPtr_t const & papp,
	        br::ResourcePtr_t & rsrc,
	        uint64_t & requested);

	/**
	 * @brief Decrement the resource assignments counts
	 *
	 * Each time an application releases a set of resources the counts of
	 * resources used must be decreased.
	 *
	 * @param assign_map Map of current resource assignments
	 * @param app The application releasing the resources
	 * @param status_view The token referencing the resource state view
	 */
	void DecBookingCounts(
	        br::ResourceAssignmentMapPtr_t const & assign_map,
	        ba::SchedPtr_t const & app,
	        br::RViewToken_t status_view = 0);

	/**
	 * @brief Unbook a single resource
	 *
	 * Remove the amount of usage from the resources referenced in the "binds"
	 * list.
	 *
	 * @param papp The Application/ExC using the resource
	 * @param r_assign Usage object
	 * @param status_view The token referencing the resource state view
	 * @param rsrc_set The set of systems resources in use
	 *
	 * @return RA_SUCCESS for successful return.
	 * RA_ERR_MISS_VIEW if the resource state view provided is missing.
	 * RA_ERR_MISS_APP if the application to schedule cannot be retrieved.
	 * RA_ERR_APP_USAGES if the application is already scheduled.
	 */
	ExitCode_t UndoResourceBooking(
	        ba::SchedPtr_t const & papp,
	        br::ResourceAssignmentPtr_t & r_assign,
	        br::RViewToken_t status_view,
	        ResourceSetPtr_t & rsrc_set);

	/**
	 * @brief Init the synchronized mode session
	 *
	 * This inititalizes the sync session view by adding the resource assignments
	 * of the RUNNING Applications/ExC. Thus the ones that will not be
	 * reconfigured or migrated.
	 *
	 * @return RA_ERR_SYNC_INIT if the something goes wrong in the assignment
	 * resources to the previuosly running applications/EXC.
	 */
	ExitCode_t SyncInit();

	/**
	 * @brief Clean closure of a synchronization session
	 */
	ExitCode_t SyncFinalize();

	/**
	 * @brief Thread-safe checking of sychronization step in progress
	 *
	 * @return true if the synchronization of the resource accounting is in
	 * progress, false otherwise
	 */
	bool Synching() {
		std::unique_lock<std::mutex> status_ul(status_mtx);
		return (status == State::SYNC);
	}

	/**
	 * @brief Thread-unsafe checking of sychronization step in progress
	 *
	 * @return true if the synchronization of the resource accounting is in
	 * progress, false otherwise
	 */
	bool _Synching() {
		return (status == State::SYNC);
	}

	/**
	 * @brief Set a view as the new resources state of the system
	 *
	 * Set a new system state view means that for each resource used in that
	 * view, such view becomes the default one.
	 * This is called once a new scheduling has performed, Applications/ExC
	 * must be syncronized, and thus system resources state  must be update
	 *
	 * @param tok The token used as reference to the resources view.
	 * @return The token referencing the system state view.
	 */
	br::RViewToken_t SetView(br::RViewToken_t status_view);

};

}   // namespace bbque

#endif // BBQUE_RESOURCE_ACCOUNTER_H_
