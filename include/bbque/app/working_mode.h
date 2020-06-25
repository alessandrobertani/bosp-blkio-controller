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

#ifndef BBQUE_WORKING_MODE_H_
#define BBQUE_WORKING_MODE_H_

#include <map>

#include "bbque/app/working_mode_status.h"
#include "bbque/res/bitset.h"
#include "bbque/res/resource_assignment.h"
#include "bbque/utils/logging/logger.h"

#define AWM_NAMESPACE "bq.awm"

namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque {
namespace app {

/**
 * @class WorkingMode
 *
 * @brief Collect information about a specific running mode of an
 * Application/EXC
 *
 * Each Application's Recipe must include a set of WorkingMode definitions.
 * An "Application Working Mode" (AWM) is characterized by a set of resource
 * usage requested and a "value", which is a Quality of Service indicator
 */
class WorkingMode : public WorkingModeStatusIF
{
public:

	/**
	 * @brief Default constructor
	 */
	WorkingMode();

	/**
	 * @brief Constructor with parameters
	 * @param id Working mode ID
	 * @param name Working mode descripting name
	 * @param value The QoS value read from the recipe
	 */
	explicit WorkingMode(
			int8_t id,
			std::string const & name,
			float value,
			SchedPtr_t owner = nullptr);

	/**
	 * @brief Default destructor
	 */
	~WorkingMode();

	/**
	 * @see WorkingModeStatusIF
	 */
	inline std::string const & Name() const
	{
		return name;
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	inline int32_t Id() const
	{
		return id;
	}

	/**
	 * @brief Set the working mode name
	 * @param wm_name The name
	 */
	inline void SetName(std::string const & wm_name)
	{
		name = wm_name;
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	inline SchedPtr_t const & Owner() const
	{
		return owner;
	}

	/**
	 * @brief Set the application owning the AWM
	 * @param papp Application descriptor pointer
	 */
	inline void SetOwner(SchedPtr_t const & papp)
	{
		owner = papp;
	}

	/**
	 * @brief Check if the AWM is hidden
	 *
	 * The AWM can be hidden if the current hardware configuration cannot
	 * accommodate the resource requirements included. This happens for example
	 * if the recipe has been profiled on a platform of the same family of the
	 * current one, but with a larger availability of resources, or whenever
	 * the amount of available resources changes at run-time, due to hardware
	 * faults or low-level power/thermal policies. This means that the AWM
	 * must not be taken into account by the scheduling policy.
	 *
	 * @return true if the AWM is hidden, false otherwise.
	 */
	inline bool Disabled() const
	{
		return disabled;
	}

	/**
	 * @brief Return the value specified in the recipe
	 */
	inline uint32_t RecipeValue() const
	{
		return value.recipe;
	}

	/**
	 * @brief Return the configuration time specified in the recipe
	 */
	inline uint32_t RecipeConfigTime() const
	{
		return config_time.recipe;
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	inline float Value() const
	{
		return value.normal;
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	inline float ConfigTime() const
	{
		return config_time.normal;
	}

	/**
	 * @brief Provide an ID-string supporting log messages readability
	 */
	const char * StrId() const
	{
		return str_id;
	}

	/**
	 * @brief Set the QoS value specified in the recipe
	 *
	 * The value is viewed as a kind of satisfaction level, from the user
	 * perspective, about the execution of the application.
	 *
	 * @param r_value The QoS value of the working mode
	 */
	inline void SetRecipeValue(float r_value)
	{
		// Value must be positive
		if (r_value < 0) {
			value.recipe = 0.0;
			return;
		}
		value.recipe = r_value;
	}

	/**
	 * @brief Set the configuration time specified in the recipe
	 *
	 * @param r_time The configuration time of the working mode
	 */
	inline void SetRecipeConfigTime(uint32_t r_time)
	{
		if (r_time > 0)
			config_time.recipe = r_time;
	}

	/**
	 * @brief Set the normalized QoS value
	 *
	 * The value is viewed as a kind of satisfaction level, from the user
	 * perspective, about the execution of the application.
	 *
	 * @param n_value The normalized QoS value of the working mode. It must
	 * belong to range [0, 1].
	 */
	inline void SetNormalValue(float n_value)
	{
		// Normalized value must be in [0, 1]
		if ((n_value < 0.0) || (n_value > 1.0)) {
			logger->Error("SetNormalValue: value not normalized (v = %2.2f)",
				n_value);
			value.normal = 0.0;
			return;
		}
		value.normal = n_value;
	}

	/**
	 * @brief Set the normalized configuration time
	 *
	 * @param norm_time The normalized configuration time of the working mode. It must
	 * belong to range [0, 1].
	 */
	inline void SetNormalConfigTime(float norm_time)
	{
		// Normalized value must be in [0, 1]
		if ((norm_time < 0.0) || (norm_time > 1.0)) {
			logger->Error("SetNormalConfigTime: time not normalized (v = %2.2f)", norm_time);
			config_time.normal = 0.0;
			return;
		}
		config_time.normal = norm_time;
	}

	/**
	 * @brief Validate the resource requests according to the current HW
	 * platform configuration/status
	 *
	 * Some HW architectures can be released in several different platform
	 * versions, providing variable amount of resources. Moreover, the
	 * availability of resources can vary at run-time due to faults or
	 * low level power/thermal policies. To support such a dynamic at
	 * run-time it is useful to hide the AWM including a resource requirement
	 * that cannot be satisfied, according to its current total availability.
	 *
	 * This method performs this check, setting the AWM to "hidden", in case
	 * the condition above is true. Hidden AWMs must not be taken into account
	 * by the scheduling policy
	 */
	ExitCode_t Validate();


	/******************************************************************************
	 * Resource binding
	 *******************************************************************************/

	/**
	 * @brief Add a resource usage request
	 *
	 * This method should be mainly called by the recipe loader or the
	 * scheduling policy.
	 *
	 * @param str_path Resource path
	 * @param amount The usage amount
	 * @param split_policy How to split the amount among the bound resources
	 *
	 * @return nullptr if the resource cannot be found in the system. A
	 * shared pointer to the new ResourceAssignment object if the
	 * request has been correctly added.
	 */
	br::ResourceAssignmentPtr_t AddResourceRequest(
						std::string const & str_path,
						uint64_t amount,
						br::ResourceAssignment::Policy split_policy =
						br::ResourceAssignment::Policy::SEQUENTIAL);

	/**
	 * @brief Get a previously added resource request
	 *
	 * @param str_path Resource path in string format
	 * @return nullptr if the resource request cannot be found.
	 * shared pointer to a ResourceAssignment object.
	 */
	br::ResourceAssignmentPtr_t GetResourceRequest(
						std::string const & str_path);

	/**
	 * @brief Get a previously added resource request
	 *
	 * @param resource_path Resource path in pointer to object format
	 * @return nullptr if the resource request cannot be found.
	 * shared pointer to a ResourceAssignment object.
	 */
	br::ResourceAssignmentPtr_t GetResourceRequest(
						br::ResourcePathPtr_t resource_path);

	/**
	 * @see WorkingModeStatusIF
	 */
	br::ResourceAssignmentMap_t const & GetResourceRequests() const
	{
		return resources.requested;
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	size_t NumberOfResourceRequests() const
	{
		return resources.requested.size();
	}

	/**
	 * @brief Wipe out the list of resource requests
	 */
	void ClearResourceRequests()
	{
		resources.requested.clear();
		this->ClearSchedResourceBinding();
		this->ClearResourceBinding();
	}

	/**
	 * @brief Get the amount of requested resource
	 *
	 * @param resource_path Resource path in pointer to object format
	 * @return the amount
	 */
	uint64_t GetRequestedAmount(br::ResourcePathPtr_t resource_path) const;

	/**
	 * @brief Bind resource assignments to system resources descriptors
	 *
	 * Resource paths taken from the recipes use IDs that do not care about
	 * the real system resource IDs registered by BarbequeRTRM. Thus a binding
	 * must be solved in order to make the request of resources satisfiable.
	 *
	 * The function member takes the resource type we want to bind (i.e.
	 * "CPU"), its source ID number (as specified in the recipe), and the
	 * destination ID related to the system resource to bind. Thus it builds
	 * a UsagesMap, wherein the ResourcePath objects, featuring the resource
	 * type specified, have the destination ID number in place of the source
	 * ID.
	 *
	 * @param r_type The type of resource to bind
	 * @param source_id Recipe resource name ID
	 * @param out_id System resource name ID
	 * @param b_refn [optional] Reference number of an already started binding
	 * @param filter_rtype [optional] Second level resource type
	 * @param filter_mask  [optional] Binding mask for the second level
	 * resource type
	 *
	 * @return The reference number of the binding performed, for continuing
	 * with further binding actions
	 *
	 * @note Use R_ID_ANY if you want to bind the resource without care
	 * about its ID.
	 */
	int32_t BindResource(
			br::ResourceType r_type,
			BBQUE_RID_TYPE source_id,
			BBQUE_RID_TYPE out_id,
			int32_t prev_refn = -1,
			br::ResourceType filter_rtype =
			br::ResourceType::UNDEFINED,
			br::ResourceBitset * filter_mask = nullptr);

	/**
	 * @brief Bind resource assignments to system resources descriptors
	 *
	 * @param resource_path The resource path (mixed or template) to bind
	 * @param filter_mask Binding mask with the resource id numbers
	 * @param prev_refn  Reference number of an already started binding
	 *
	 * @return The reference number of the binding performed, for continuing
	 * with further binding actions
	 */
	int32_t BindResource(
			br::ResourcePathPtr_t resource_path,
			br::ResourceBitset const & filter_mask,
			int32_t prev_refn = -1);

	/**
	 * @brief Add missing resource requests to a resource assignment map
	 *
	 * @param bound_map The resource assignment map (already bound) with
	 * missing requests
	 * @param r_type The type of the resource for which a request adding
	 * may be needed
	 *
	 * @return The number of missing resources added
	 */
	uint32_t AddMissingResourceRequests(
					br::ResourceAssignmentMapPtr_t bound_map,
					br::ResourceType r_type);

	/**
	 * @brief Retrieve the source and the destination map of resource
	 * assignments
	 *
	 * @param out_map The map to fill with bound resource assignments
	 * @param prev_refn The reference number to retrieve later this binding
	 *
	 * @return A pointer to a source map for resource requests or
	 * previously bound resource requests
	 */
	br::ResourceAssignmentMap_t * GetSourceAndOutBindingMaps(
								br::ResourceAssignmentMapPtr_t & out_map,
								int32_t prev_refn);

	/**
	 * @brief Save a resource assignment map aming the bindings
	 *
	 * @param bound_map The resource assignment map
	 * @param prev_refn The reference number to retrieve later this binding
	 *
	 * @return The reference number of the binding
	 */
	int32_t StoreBinding(br::ResourceAssignmentMapPtr_t, int32_t prev_refn);

	/**
	 * @see WorkingModeStatusIF
	 */
	br::ResourceAssignmentMapPtr_t GetSchedResourceBinding(uint32_t b_refn) const;

	/**
	 * @brief Set the resource binding to schedule
	 *
	 * This binds the map of resource assignments pointed by
	 * "resources.sched_bindings" to the WorkingMode.
	 * The map will contain Usage objects specifying the the amount of
	 * resource requested (value) and a list of system resource descriptors
	 * to which bind the request.
	 *
	 * This method is invoked during the scheduling step to track the set of
	 * resources to acquire at the end of the synchronization step.
	 *
	 * @param b_id The scheduling binding id to set ready for synchronization
	 * (optional)
	 *
	 * @return WM_SUCCESS, or WM_RSRC_MISS_BIND if some bindings are missing
	 */
	ExitCode_t SetResourceBinding(
				br::RViewToken_t status_view,
				uint32_t b_refn = 0);

	/**
	 * @brief Get the map of scheduled resource assignments
	 *
	 * This method returns the map of the resource assignments built through the
	 * mandatory resource binding, in charge of the scheduling policy.
	 *
	 * It is called by the ResourceAccounter to scroll through the list of
	 * resources bound to the working mode assigned.
	 *
	 * @return A shared pointer to a map of Usage objects
	 */
	br::ResourceAssignmentMapPtr_t GetResourceBinding() const
	{
		return resources.sync_bindings;
	}

	/**
	 * @brief Update resource binding masks
	 *
	 * This method must be called by the ResourceAccouter to notify that the
	 * synchronization of the assigned resources has been successfully
	 * performed. Therefore the AWM can update the resource binding masks such
	 * that they can be correctly retrieved by the PlatformProxy.
	 *
	 * @param status_view The resource state view according to which update
	 * @param update_changed if true update alse the "changed" flags
	 */
	void UpdateBindingInfo(br::RViewToken_t status_view, bool update_changed = false);

	/**
	 * @brief Clear the scheduled resource binding
	 *
	 * The method reverts the effects of SetResourceBinding()
	 */
	void ClearResourceBinding();

	/**
	 * @see WorkingModeConfIF
	 */
	void ClearSchedResourceBinding()
	{
		resources.sched_bindings.clear();
	}

	/**
	 * @see WorkingModeStatusIF
	 */
	br::ResourceBitset BindingSet(const br::ResourceType & r_type) const;

	/**
	 * @see WorkingModeStatusIF
	 */
	br::ResourceBitset BindingSetPrev(const br::ResourceType & r_type) const;

	/**
	 * @see WorkingModeStatusIF
	 */
	bool BindingChanged(const br::ResourceType & r_type) const;


	/******************************************************************************
	 * Scheduling information
	 *******************************************************************************/

	/**
	 * @brief Increment the scheduling counter
	 */
	void IncSchedulingCount()
	{
		sched_count++;
		logger->Debug("Selection count: %d", sched_count);
	}

	/**
	 * @brief Increment the scheduling counter
	 */
	uint32_t GetSchedulingCount()
	{
		return sched_count;
	}

	/******************************************************************************
	 * Runtime profiling data
	 *******************************************************************************/

	/**
	 * @brief Collect the runtime profiling data from the RTLib
	 */
	struct RuntimeProfiling_t
	{
		uint32_t exec_time;
		uint32_t exec_time_tot;
		uint32_t mem_time;
		uint32_t mem_time_tot;
		uint32_t sync_time;
	};

	/**
	 * @brief Set the time spent executing on a computing device
	 * @param exec_time Value coming from RTLib
	 */
	inline void SetRuntimeProfExecTime(uint32_t exec_time)
	{
		rt_prof.exec_time = exec_time;
		rt_prof.exec_time_tot += exec_time;
		logger->Debug("Execution time: {sample = %d, tot = %d} ns",
			rt_prof.exec_time, rt_prof.exec_time_tot);
	}

	/**
	 * @brief Set the time spent in performing memory operations
	 * @param exec_time Value coming from RTLib
	 */
	inline void SetRuntimeProfMemTime(uint32_t mem_time)
	{
		rt_prof.mem_time = mem_time;
		rt_prof.mem_time_tot += mem_time;
		logger->Debug("Memory time: {sample = %d, tot = %d} ns",
			rt_prof.mem_time, rt_prof.mem_time_tot);
	}

	/**
	 * @brief Set the latest synchronization latency
	 * @param exec_time Value coming from RTLib
	 */
	inline void SetRuntimeProfSyncTime(uint32_t sync_time)
	{
		rt_prof.sync_time = sync_time;
		logger->Debug("Synchronization time: %d", rt_prof.sync_time);
	}

	/**
	 * @brief Get the collection runtime profiling data
	 */
	inline RuntimeProfiling_t const & GetProfilingData()
	{
		return rt_prof;
	}

private:

	/**
	 * @class BindingInfo
	 *
	 * Store binding information, i.e., on which system resources IDs the
	 * resource (type) has been bound by the scheduling policy
	 */
	class BindingInfo
	{
	public:

		BindingInfo()
		{
			changed = false;
		}

		virtual ~BindingInfo() { };

		/**
		 * @brief Set current resource binding set
		 */
		inline void SetCurrentSet(br::ResourceBitset const & _curr)
		{
			prev = curr;
			curr = _curr;
			changed = curr != prev;
		}

		/**
		 * @brief Restore the previous resource binding set
		 */
		inline void RestorePreviousSet()
		{
			curr = prev;
			changed = false;
		}

		/**
		 * @brief Get the current resource binding set
		 */
		inline br::ResourceBitset CurrentSet() const
		{
			return curr;
		}

		/**
		 * @brief Get the previous resource binding set
		 */
		inline br::ResourceBitset PreviousSet() const
		{
			return prev;
		}

		/**
		 * @brief Check if the binding set has changed
		 */
		inline bool IsChanged() const
		{
			return changed;
		}

	private:
		/** Save the previous set of clusters bound */
		br::ResourceBitset prev;
		/** The current set of clusters bound */
		br::ResourceBitset curr;
		/** True if current set differs from previous */
		bool changed;
	};

	/** The logger used by the application manager */
	std::shared_ptr<bu::Logger> logger;

	/**
	 * A pointer to the Application descriptor containing the
	 * current working mode
	 */
	SchedPtr_t owner;

	/** A numerical ID  */
	int8_t id = 0;

	/** A descriptive name */
	std::string name = "UNDEF";

	/** Logger messages ID string */
	char str_id[22];

	/**
	 * Whether the AWM includes resource requirements that cannot be
	 * satisfied by the current hardware platform/configuration it can be
	 * flagged as hidden. The idea is to support a dynamic reconfiguration
	 * of the underlying hardware such that some AWMs could be dynamically
	 * taken into account or not at run-time.
	 */
	bool disabled;

	/**
	 * @struct ValueAttribute_t
	 *
	 * Store information regarding the value of the AWM
	 */
	struct ValueAttribute_t
	{
		/** The QoS value associated to the working mode as specified in the
		 * recipe */
		uint32_t recipe;
		/** The normalized QoS value associated to the working mode */
		float normal;

	} value;

	/**
	 * @struct ConfigTimeAttribute_t
	 *
	 * Store information regarding the configuration time of the AWM
	 */
	struct ConfigTimeAttribute_t
	{
		/** The time (in milliseconds) profiled at design time and specified
		 * in the recipe */
		uint32_t recipe;
		/** The time normalized according to the other AWMs in the same recipe */
		float normal;
		/**
		 * The time (avg) estimated at run-time during the application
		 * execution.
		 * NOTE: Currently unused attribute
		 */
		uint32_t runtime;

	} config_time;

	/**
	 * @brief Store profiling information collected at runtime
	 */
	RuntimeProfiling_t rt_prof;

	/**
	 * @brief Keep track of how many times the application has been
	 * scheduled according to the current working mode
	 */
	uint32_t sched_count = 0;

	/**
	 * @struct ResourceUsagesInfo
	 *
	 * Store information about the resource requests specified in the recipe
	 * and the bindings built by the scheduling policy
	 */
	struct ResourcesInfo
	{
		/**
		 * The map of resources usages from the recipe
		 */
		br::ResourceAssignmentMap_t requested;
		/**
		 * The temporary map of resource bindings. This is built by the
		 * BindResource calls
		 */
		std::vector<br::ResourceAssignmentMapPtr_t> sched_bindings;
		/**
		 * The map of the resource bindings allocated for the working mode.
		 * This is set by SetResourceBinding() as a commit of the
		 * bindings performed, reasonably by the scheduling policy.
		 */
		br::ResourceAssignmentMapPtr_t sync_bindings;
		/**
		 * Keep track of the reference number of the scheduling binding map to
		 * synchronize
		 */
		size_t sync_refn;
		/**
		 *Info regarding bindings per resource
		 */
		std::map<br::ResourceType, BindingInfo> binding_masks;

	} resources;

};

} // namespace app

} // namespace bbque

#endif // BBQUE_WORKING_MODE_H_
