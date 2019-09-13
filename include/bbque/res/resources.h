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

#ifndef BBQUE_RESOURCES_H_
#define BBQUE_RESOURCES_H_

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bbque/config.h"
#include "bbque/app/application_status.h"
#include "bbque/pm/power_manager.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/timer.h"
#include "bbque/utils/stats.h"


using bbque::app::SchedPtr_t;
using bbque::app::AppUid_t;
using bbque::utils::ExtraDataContainer;
using bbque::utils::Timer;
using bbque::utils::pEma_t;

namespace bbque
{

// Forward declaration
class ResourceAccounter;


namespace res
{

// Forward declarations
class Resource;
struct ResourceState;

/** Resource state view token data type */
using RViewToken_t = size_t;

/** Shared pointer to Resource descriptor */
using ResourcePtr_t = std::shared_ptr<Resource>;

/** List of shared pointers to Resource descriptors */
using ResourcePtrList_t = std::list<ResourcePtr_t>;

/** Iterator of ResourcePtr_t list */
using ResourcePtrListIterator_t = ResourcePtrList_t::iterator;

/** Shared pointer to ResourceState object */
using ResourceStatePtr_t = std::shared_ptr<ResourceState>;

/** Map of amounts of resource used by applications. Key: Application UID */
using AppUsageQtyMap_t = std::map<AppUid_t, uint64_t>;

/** Hash map collecting the state views of a resource */
using RSHashMap_t =  std::unordered_map<RViewToken_t, ResourceStatePtr_t>;


using ResourcePathPtr_t = std::shared_ptr<ResourcePath>;

/**
 * @class ResourceState
 *
 * @brief The class keep track of the status of the resource, from the usage standpoint.
 * How many resources are used/available? Which application is using the
 * resource? How much is using it ?
 * This are the basic information we need to track upon resources.
 *
 * @note: The total amount of resource is not an information of "state".
 * Indeed a state is a dynamic concept, while the total is a static
 * information.
 */
struct ResourceState {

	/**
	 * @brief Constructor
	 */
	ResourceState():
		used(0) {
	}

	/**
	 * @brief Destructor
	 */
	~ResourceState() {
		apps.clear();
	}

	/** The amount of resource used in the system   */
	uint64_t used;

	/**
	 * Amounts of resource used by each of the applications holding the
	 * resource
	 */
	AppUsageQtyMap_t apps;

};


/**
 * @class Resource
 * @brief A generic resource descriptor
 *
 * The "Resource" is the fundamental entity for BarbequeRTRM.
 * To access a resource is a matter of using a "path". A resource path is
 * built recursively, as a sequence of resources name, in a hierarchical
 * form (@see ResourceAccounter, @see ResourceTree).
 *
 * Basically, a resource has a identifying name, a total amount value,  and a
 * state. In our design, MORE than one state.
 * The idea is to have a default state, the "real" one, and a possible set of
 * temporary states to use as "buffers". Thus each state is a different VIEW
 * of resource. This feature is particularly useful for components like the
 * Scheduler/Optimizer (see below.)
 */
class Resource: public ResourceIdentifier, public ExtraDataContainer
{

// This makes method SetTotal() accessible to RA
friend class bbque::ResourceAccounter;

public:

	enum ExitCode_t {
	        RS_SUCCESS = 0,       /** Generic success code */
	        RS_FAILED,            /** Generic failure code */
	        RS_NO_APPS,           /** Resource not used by any application */
	        RS_PWR_INFO_DISABLED  /** Required a power information not enabled */
	};

	enum ValueType {
	        INSTANT,
	        MEAN
	};

	/**
	 * @class PowerSettings
	 *
	 * @brief Power settings as of required via recipe or set by scheduling
	 * policy. This is actuated later on by the platform manager at the
	 * optimization stage.
	 *
	 * freq_governor is the specific governor to set
	 * freq_khz is the operating frequency in KHz
	 * perf_state is a number [0,inf) to indicate the operating state of the
	 * resource
	 */
	class PowerSettings
	{
	public:

		PowerSettings():
			freq_governor(""),
			freq_khz(0),
			perf_state(-1) {
		}

		PowerSettings(
		        std::string const & gov,
		        uint32_t freq,
		        int32_t pstate):

			freq_governor(gov),
			freq_khz(freq),
			perf_state(pstate) {
		}

		enum Actions {
		        TURN_ONOFF      = 1,
		        CHANGE_GOVERNOR = 2,
		        SET_FREQUENCY   = 4,
		        SET_PERF_STATE  = 8
		};

		bool operator==(const PowerSettings & other) const noexcept {
			return ((this->freq_governor.compare(other.freq_governor) == 0)
			        && this->freq_khz == other.freq_khz
			        && this->perf_state == other.perf_state);
		}

		void operator=(const PowerSettings & other) {
			SetOn(other.online);
			if (this->freq_governor.compare(other.freq_governor) != 0) {
				pending_actions |= CHANGE_GOVERNOR;
				this->freq_governor.assign(other.freq_governor);
			}
			if (this->freq_khz != other.freq_khz) {
				pending_actions |= SET_FREQUENCY;
				this->freq_khz = other.freq_khz;
			}
			if (this->perf_state != other.perf_state) {
				pending_actions |= SET_PERF_STATE;
				this->perf_state = other.perf_state;
			}
		}

		bool SetOn(bool on) {
			if (this->online != on) {
				pending_actions |= TURN_ONOFF;
				this->online = on;
				return true;
			}
			return false;
		}

		void Reset() {
			online = true;
			freq_governor.assign("");
			freq_khz = 0;
			perf_state = -1;
			pending_actions = 0;
		}


		/// Online/offline status
		bool online = true;

		/// Governor (eg. cpufreq)
		std::string freq_governor;

		/// Operating clock frequency
		uint32_t freq_khz;

		/// Operating performance state (alternative to set frequency)
		int32_t perf_state;

		/// Set of actions to be performed to update the power
		/// management settings (bitset)
		uint8_t pending_actions = 0;
	};


	/**********************************************************************
	 * GENERAL INFORMATION                                                  *
	 **********************************************************************/

	/**
	 * @brief Constructor
	 *
	 * @param type Resource type
	 * @param id Resource integer ID
	 * @param tot The total amount of resource
	 */
	Resource(ResourceType type, BBQUE_RID_TYPE id, uint64_t tot = 1);

	/**
	 * Destructor
	 */
	~Resource() {
		state_views.clear();
#ifdef CONFIG_BBQUE_PM
		pw_profile.values.clear();
#endif
	}

	/**
	 * @brief Set the resource model name
	 * e.g. The model name of a CPU ("Intel i7-2640M")
	 */
	void SetModel(std::string const & model_name) {
		model.assign(model_name);
	}

	/**
	 * @brief Get the resource model name
	 *
	 * e.g. The model name of a CPU ("Intel i7-2640M")
	 *
	 * @return A char string object reference
	 */
	std::string const & Model() const {
		return model;
	}

	/**
	 * @brief Set the resource path string
	 */
	void SetPath(ResourcePathPtr_t r_path) {
		path = r_path;
	}

	/**
	 * @brief The registered resource path string
	 * @return A string containing the resource path
	 */
	ResourcePathPtr_t Path() const {
		return path;
	}

	/**********************************************************************
	 * ACCOUNTING INFORMATION                                             *
	 **********************************************************************/

	/**
	 * @brief Resource total
	 * @return The total amount of resource
	 */
	uint64_t Total() const {
		return total;
	}

	/**
	 * @brief Amount of resource used
	 *
	 * @param view_id The token referencing the resource view
	 *
	 * @return How much resource has been allocated
	 */
	uint64_t Used(RViewToken_t view_id = 0) const;

	/**
	 * @brief Resource availability
	 *
	 * @param papp Application interested in the query. We want to include
	 * in the count the amount of resource used by this application.
	 * This could be useful when we want to check the availability with the
	 * aim of allocate the resource to the given application. If the
	 * application is using yet a certain amount of resource this quantity
	 * should be considered as "available" for this application.
	 *
	 * If the Application is not specified the method returns the amount of
	 * resource free, i.e. not allocated to any Application/EXC.
	 *
	 * @param view_id The token referencing the resource view
	 *
	 * @return How much resource is still available including the amount of
	 * resource used by the given application
	 */
	uint64_t Available(SchedPtr_t papp = SchedPtr_t(), RViewToken_t view_id = 0) const;

	/**
	 * @brief Count of applications using the resource
	 *
	 * @param view_id The token referencing the resource view
	 * @return Number of applications
	 */
	uint16_t ApplicationsCount(RViewToken_t view_id = 0) const {
		AppUsageQtyMap_t apps_map;
		return ApplicationsCount(apps_map, view_id);
	}

	/**
	 * @brief Amount of resource used by the application
	 *
	 * @param papp Application (shared pointer) using the resource
	 * @param view_id The token referencing the resource view
	 *
	 * @return The 'quota' of resource used by the application
	 */
	uint64_t ApplicationUsage(SchedPtr_t const & papp, RViewToken_t view_id = 0) const;

	/**
	 * @brief Applications using the resource
	 *
	 * @param apps_map Map of application uid and resource usage amount
	 * @param view_id The token referencing the resource view
	 * @return The number of applications
	 */
	uint16_t Applications(AppUsageQtyMap_t & apps_map, RViewToken_t view_id = 0) const {
		return ApplicationsCount(apps_map, view_id);
	}

	/**
	 * @brief Get the Uid of the n-th App/EXC using the resource
	 *
	 * @param app_uid The Uid of the n-th App/EXC using the resource
	 * @param amount This is set to the amount of resource used by the App/EXC
	 * @param nth The n-th App/EXC to find
	 * @param view_id The token referencing the resource view
	 * @return RS_SUCCESS if the App/EXC has been found, RS_NO_APPS otherwise
	 */
	ExitCode_t UsedBy(
	        AppUid_t & app_uid,
	        uint64_t & amount,
	        uint8_t nth = 0,
	        RViewToken_t view_id = 0) const;

	/**
	 * @brief The number of state views of the resource
	 * @return The size of the map
	 */
	size_t ViewCount() const {
		return state_views.size();
	}


	/**********************************************************************
	 * RUNTIME (PHYSICAL) AVAILABILITY                                    *
	 **********************************************************************/

	/**
	 * @brief Not reserved resources
	 *
	 * The amount of resources not being currently reserved, this value is
	 * equal to Total just when there are not reserved resources.
	 *
	 * @return The amount of resources not being currently reserved
	 */
	uint64_t Unreserved() const {
		return (total - reserved);
	}

	/**
	 * @brief Make unavailable a given amount of resource
	 *
	 * @return
	 */
	ExitCode_t  Reserve(uint64_t amount);

	/**
	 * @brief Amount not available, not allocable
	 *
	 * @return The not allocable amount
	 */
	uint64_t Reserved() const {
		return reserved;
	}


	/**********************************************************************
	 * POWER MANAGEMENT AND PROFILING                                     *
	 **********************************************************************/

	/**
	 * @brief Check if the resource is completely not available
	 *
	 * @return true if it is offline, false otherwise
	 */
	bool IsOffline() const {
		return !pw_config.online;
	}

	/**
	 * @brief Make the resource completely not available
	 *
	 */
	void SetOffline();

	/**
	 * @brief Resume the availability of the resource
	 */
	void SetOnline();

	/**
	 * @brief Set a new power configuration to apply
	 * later on during the optimization stage
	 */
	void SetPowerSettings(PowerSettings new_settings) {
		pw_config = new_settings;
	}

	/**
	 * @brief Get the currently set power configuration
	 *  to actuate during the optimization stage
	 */
	PowerSettings const & GetPowerSettings() const {
		return pw_config;
	}

#ifdef CONFIG_BBQUE_PM

	/**
	 * @brief Enable the collection of power-thermal status information
	 *
	 * @param samples_window For each (required) power profile information
	 * compute a mean (exponential) value over a number of samples specified in
	 * the specific position (@see PowerManager::InfoType)
	 */
	void EnablePowerProfiling(PowerManager::SamplesArray_t const & samples_window);

	/**
	 * @brief Enable the collection of power-thermal status information with
	 * the default setting specified in BBQUE_PM_DEFAULT_SAMPLES_WINSIZE
	 */
	void EnablePowerProfiling();

	/**
	 * @brief The number of samples for the computation of the mean value of
	 * the power profile information required
	 *
	 * @param i_type The power profile information required
	 *
	 * @return The number of samples
	 */
	uint GetPowerInfoSamplesWindowSize(PowerManager::InfoType i_type) const {
		return pw_profile.samples_window[int(i_type)];
	}

	/**
	 * @brief The number of power profile information required
	 */
	uint GetPowerInfoEnabledCount() const {
		return pw_profile.enabled_count;
	}

	/**
	 * @brief Update the power profile information
	 *
	 * @param i_type The power profile information to update
	 * @param sample The sample value
	 */
	void UpdatePowerInfo(PowerManager::InfoType i_type, uint32_t sample) {
		std::unique_lock<std::mutex> ul(pw_profile.mux);
		pw_profile.values[int(i_type)]->update(sample);
	}

	/**
	 * @brief Power profile information
	 *
	 * @param i_type Information type (e.g., LOAD, TEMPERATURE, FREQUENCY,...)
	 * @param v_type Specify if the value required is the instantaneous or the
	 * mean (exponential) computed on a set of samples (@see ValueType)
	 *
	 * @return The value of power profile information required
	 */
	double GetPowerInfo(PowerManager::InfoType i_type, ValueType v_type = MEAN);

#endif // CONFIG_BBQUE_PM


	/**********************************************************************
	 * RELIABILITY INFORMATION                                            *
	 **********************************************************************/

	/**
	 * @brief Update the current performance degradation
	 *
	 * @param deg_perc Percentage of performance degradation
	 */
	void UpdateDegradationPerc(uint8_t deg_perc) {
		std::unique_lock<std::mutex> ul(rb_profile.mux);
		rb_profile.degradation_perc->update(deg_perc);
	}

	/**
	 * @brief The current performance degradation (last notification)
	 *
	 * @return Current percentage of performance degradation
	 */
	uint8_t CurrentDegradationPerc() const {
		std::unique_lock<std::mutex> ul(rb_profile.mux);
		return rb_profile.degradation_perc->last_value();
	}

	/**
	 * @brief Performance degradation (exponential mean value)
	 *
	 * @return Mean percentage of performance degradation
	 */
	float MeanDegradationPerc() const {
		std::unique_lock<std::mutex> ul(rb_profile.mux);
		return rb_profile.degradation_perc->get();
	}


private:

	/**
	 * @brief The metrics to track run-time availability of a resource
	 */
	typedef struct AvailabilityProfile {
		Timer online_tmr;          /** Timer to keep track of online time;   */
		Timer offline_tmr;         /** Timer to keep track of offline time;  */
		uint64_t lastOnlineTime;   /** Last online timeframe [ms]            */
		uint64_t lastOfflineTime;  /** Last offline timeframe [ms]           */
	} AvailabilityProfile_t;

	/**
	 * @brief Information related to the power/thermal status of the resource
	 */
	typedef struct PowerProfile {
		mutable std::mutex mux;
		PowerManager::SamplesArray_t samples_window; /** Available run-time information */
		std::vector<pEma_t> values;                  /** Sampled values */
		uint enabled_count;                          /** Count of power profiling info enabled */
	} PowerProfile_t;

	/**
	 * @brief Runtime information about the reliability of the resource
	 */
	typedef struct ReliabilityProfile {
		mutable std::mutex mux;
		pEma_t degradation_perc; /** Percentage of performance degradation (stats) */
	} ReliabilityProfile_t;


	/** The total amount of resource  */
	uint64_t total;

	/** The amount of resource being reserved */
	uint64_t reserved;

	/** Resource path object (pointer) indicating the hierarchical placement  */
	ResourcePathPtr_t path;

	/** Resource name, e.g. CPU architecture name */
	std::string model;


	/** The run-time availability profile of this resource */
	AvailabilityProfile_t av_profile;

	/** Power configuration to apply for the resource assignment */
	PowerSettings pw_config;

#ifdef CONFIG_BBQUE_PM

	/** Power/thermal status (if the platform support is available) */
	PowerProfile_t pw_profile;

	PowerManager::SamplesArray_t default_samples_window =
	{ BBQUE_PM_DEFAULT_SAMPLES_WINSIZE };
#endif

	/** The run-time reliability profile of this resource */
	ReliabilityProfile_t rb_profile;

	/**
	 * Hash map with all the views of the resource.
	 * A "view" is a resource state. We can think at the hash map as a map
	 * containing the "real" state of resource, plus other "temporary" states.
	 * Such temporary states allows the Scheduler/Optimizer, i.e., to make
	 * intermediate evaluations, before commit the ultimate scheduling.
	 *
	 * Each view is identified by a "token" which is hashed in order to
	 * retrieve the ResourceState descriptor.
	 *
	 * It's up to the Resource Accounter to maintain a consistent view of the
	 * system state. Thus ResourceAccounter will manage tokens and the state
	 * views life-cycle.
	 */
	RSHashMap_t state_views;

	/**
	 * @brief Availability information initialization
	 */
	void InitProfilingInfo();

	/**
	 * @brief Set the total amount of resource
	 *
	 * @note This method acts only upon the default state view only
	 * @param tot The amount to set
	 */
	void SetTotal(uint64_t tot) {
		total = tot;
	}

	/**
	 * @brief Acquire a given amount of resource
	 *
	 * @param papp The application requiring the resource
	 * @param amount How much resource is required
	 * @param view_id The token referencing the resource view
	 * @return The amount of resource acquired if success, 0 otherwise.
	 */
	uint64_t Acquire(SchedPtr_t const & papp, uint64_t amount, RViewToken_t view_id = 0);

	/**
	 * @brief Release the resource
	 *
	 * Release the specific amount of resource used by an application
	 *
	 * @param papp The application releasing the resource
	 * @param view_id The token referencing the resource view
	 * @return The amount of resource released
	 */
	uint64_t Release(SchedPtr_t const & papp, RViewToken_t view_id);

	/**
	 * @brief Release the resource
	 *
	 * Release the specific amount of resource used by an application
	 *
	 * @param app_uid The application unique id releasing the resource
	 * @param view_id The token referencing the resource view
	 * @return The amount of resource released
	 */
	uint64_t Release(AppUid_t app_uid, RViewToken_t view_id);

	/**
	 * @brief Release the resource
	 *
	 * Release the specific amount of resource used by an application
	 *
	 * @param app_uid The application unique id releasing the resource
	 * @param view The resource status view from which releasing the resource
	 * @return The amount of resource released
	 */
	uint64_t Release(AppUid_t app_uid, ResourceStatePtr_t view);


	/**
	 * @brief Apps/EXCs using the resource
	 *
	 * @param apps_map Reference to the map of App/EXC to get
	 * @param view_id The resource state view token
	 *
	 * @return The number of Apps/EXCs using the resource, and a
	 * reference to the map
	 */
	uint16_t ApplicationsCount(AppUsageQtyMap_t & apps_map, RViewToken_t view_id = 0) const;

	/**
	 * @brief Amount of resource used by the application
	 *
	 * @param papp Application (shared pointer) using the resource
	 * @param apps_map Reference to the map of App/EXC to get
	 *
	 * @return The 'quota' of resource used by the application
	 */
	uint64_t ApplicationUsage(SchedPtr_t const & papp, AppUsageQtyMap_t & apps_map) const;


	/**
	 * @brief Get the view referenced by the token
	 *
	 * @param view_id The resource state view token
	 * @return The ResourceState fo the referenced view
	 */
	ResourceStatePtr_t GetStateView(RViewToken_t view_id) const;

	/**
	 * @brief Delete a state view
	 *
	 * If the token refers to the default view, the method returns doing
	 * nothing. This control is ahead of safety and consistency purposes.
	 * Indeed if the default view was removed, what state should be picked as
	 * the new default one?
	 * Thus, this constraint force the caller to set a new default view, before
	 * delete the current one.
	 *
	 * @param view_id The token of the view to delete
	 */
	void DeleteView(RViewToken_t view_id);
};


}   // namespace res

}   // namespace bbque

#endif // BBQUE_RESOURCES_H_
