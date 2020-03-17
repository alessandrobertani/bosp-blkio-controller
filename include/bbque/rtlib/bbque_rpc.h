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

#ifndef BBQUE_RPC_H_
#define BBQUE_RPC_H_

#include "bbque/rtlib.h"
#include "bbque/config.h"
#include "bbque/rtlib/rpc/rpc_messages.h"
#include "bbque/utils/stats.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/timer.h"
#include "bbque/utils/logging/logger.h"

#include "sys/times.h"

#ifdef CONFIG_BBQUE_RTLIB_PERF_SUPPORT
# include "bbque/utils/perf.h"
#endif

#ifdef CONFIG_TARGET_OPENCL
#include "bbque/rtlib/bbque_ocl_stats.h"
#endif

#include <condition_variable>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/variance.hpp>

// Define module namespace for inlined methods
#undef  MODULE_NAMESPACE
#define MODULE_NAMESPACE "rpc"

#define US_IN_A_SECOND 1e6
#define RT_CPU_TICKS_PERIOD_LEN 5

using namespace boost::accumulators;
namespace bu = bbque::utils;

namespace bbque
{
namespace rtlib
{

/**
 * @class BbqueRPC
 * @brief The class implementing the RTLib plain API
 * @ingroup rtlib_sec03_plain
 *
 * This RPC mechanism is channel agnostic (i.e. does not care how the actual
 * communication with the resource manager is performed) and defines the
 * procedures that are used to send requests to the Barbeque RTRM.  The actual
 * implementation of the communication channel is provided by class derived by
 * this one. This class provides also a factory method which allows to obtain
 * an instance to the concrete communication channel that can be selected at
 * compile time.
 */
class BbqueRPC
{

public:

	/**
	 * @brief Get a reference to the (singleton) RPC service
	 *
	 * This class is a factory of different RPC communication channels. The
	 * actual instance returned is defined at compile time by selecting the
	 * proper specialization class.
	 *
	 * @return A reference to an actual BbqueRPC implementing the compile-time
	 * selected communication channel.
	 */
	static BbqueRPC * GetInstance();

	/**
	 * @brief Get a reference to the RTLib configuration
	 *
	 * All the run-time tunable and configurable RTLib options are hosted
	 * by the struct RTLib_Conf. This call returns a pointer to this
	 * configuration, which could not be update at run-time.
	 *
	 * @return A reference to the RTLib configuration options.
	 */
	static const RTLIB_Conf_t * Configuration()
	{
		return &rtlib_configuration;
	};

	/**
	 * @brief Release the RPC channel
	 */
	virtual ~BbqueRPC(void);

	/**********************************************************************
	 * Channel Independent interface
	 **********************************************************************/

	/**
	 * @brief Notify the resource manager about application and setup cgroups
	 */
	RTLIB_ExitCode_t InitializeApplication(const char * name, pid_t restore_pid = 0);

	/**
	 * @brief Register a new execution context
	 */
	RTLIB_EXCHandler_t Register(
	        const char * exc_name,
	        const RTLIB_EXCParameters_t * exc_params);

	/**
	 * @brief Unregister an execution context
	 */
	void Unregister(const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Unregister all the execution contexts of the application
	 */
	void UnregisterAll();

	/**
	 * @brief Enable an execution context
	 */
	RTLIB_ExitCode_t Enable(const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Disable an execution context
	 */
	RTLIB_ExitCode_t Disable(const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Set constraints on the AWM choice for this EXC
	 */
	RTLIB_ExitCode_t SetAWMConstraints(
	        const RTLIB_EXCHandler_t exc_handler,
	        RTLIB_Constraint_t * awm_constraints,
	        uint8_t number_of_constraints);

	/**
	 * @brief Remove all constraints on the AWM choice for this EXC
	 */
	RTLIB_ExitCode_t ClearAWMConstraints(
	        const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Synchronization function
	 * @param exc_handler
	 * @param working_mode_params
	 * @param st
	 * @return
	 */
	RTLIB_ExitCode_t GetWorkingMode(
	        RTLIB_EXCHandler_t exc_handler,
	        RTLIB_WorkingModeParams_t * working_mode_params,
	        RTLIB_SyncType_t st);


	/***********************************************************************
	 *    Profiling Functions
	 **********************************************************************/

	/**
	 * @brief Check if it's time to send a runtime feedback to the
	 * resource manager
	 */
	RTLIB_ExitCode_t ForwardRuntimeProfile(const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief
	 * @param exc_handler
	 * @return
	 */
	RTLIB_ExitCode_t UpdateRuntimeProfile(const RTLIB_EXCHandler_t exc_handler);



	/**
	 * @brief
	 * @param msg
	 * @return
	 */
	RTLIB_ExitCode_t GetRuntimeProfile(rpc_msg_BBQ_GET_PROFILE_t & msg);

	/**
	 * @brief Start monitoring performance counters for this EXC
	 */
	void StartPCountersMonitoring(RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief
	 * @param exc_handler
	 */
	void ResetRuntimeProfileStats(RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Set an explicit goal gap value (ka explicitely ask for
	 * more/less resources to the resource manager
	 *
	 * @param exc_handler
	 * @param goal_gap_percent
	 * @return
	 */
	RTLIB_ExitCode_t SetExplicitGoalGap(
	        const RTLIB_EXCHandler_t exc_handler,
	        int goal_gap_percent);


	/*********************************************************************
	 *    cgroup functions
	 *********************************************************************/

	/**
	 * @brief
	 * @return
	 */
	const std::string GetCGroupPath_APP() const
	{
		return pathCGroup;
	}

	/**
	 * @brief
	 * @param exc_handler
	 * @return
	 */
	RTLIB_ExitCode_t SetupCGroup(const RTLIB_EXCHandler_t exc_handler);


	/**********************************************************************
	 *    Utility functions
	 *********************************************************************/

	/**
	 * @brief
	 * @return
	 */
	const char * GetCharUniqueID() const
	{
		std::stringstream ss;
		ss << std::right << std::setfill('0') << std::setw(5)
		   << std::to_string(application_pid);
		std::string unique_id_str(ss.str() + ":");
		return unique_id_str.c_str();
	}

	/**
	 * @brief
	 * @param exc_handler
	 * @return
	 */
	AppUid_t GetUniqueID(RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Get a breakdown of the allocated resources
	 * @param exc_handler
	 * @param wm
	 * @param r_type
	 * @param r_amount
	 * @return
	 */
	RTLIB_ExitCode_t GetAssignedResources(
	        RTLIB_EXCHandler_t exc_handler,
	        const RTLIB_WorkingModeParams_t * wm,
	        RTLIB_ResourceType_t r_type,
	        int32_t & r_amount);

	/**
	 * @brief
	 * @param exc_handler
	 * @param wm
	 * @param r_type
	 * @param sys_array
	 * @param array_size
	 * @return
	 */
	RTLIB_ExitCode_t GetAssignedResources(
	        RTLIB_EXCHandler_t exc_handler,
	        const RTLIB_WorkingModeParams_t * wm,
	        RTLIB_ResourceType_t r_type,
	        int32_t * sys_array,
	        uint16_t array_size);

	/**
	 * @brief
	 * @param exc_handler
	 * @param wm
	 * @param ids_vector
	 * @param vector_size
	 * @return
	 */
	RTLIB_ExitCode_t GetAffinityMask(
	        RTLIB_EXCHandler_t exc_handler,
	        const RTLIB_WorkingModeParams_t * wm,
	        int32_t * ids_vector,
	        int vector_size);

	/***********************************************************************
	 *    Cycles Per Second (CPS) Control Support
	 **********************************************************************/

	/**
	 * @brief Set the required Cycles Per Second (CPS)
	 *
	 * This allows to define the required and expected cycles rate. If at
	 * run-time the cycles execution should be faster, a properly computed
	 * delay will be inserted by the RTLib in order to get the specified
	 * rate.
	 */
	RTLIB_ExitCode_t SetCPS(RTLIB_EXCHandler_t exc_handler, float cps);

	/**
	 * @brief Get the measured Cycles Per Second (CPS) value
	 *
	 * This allows to retrive the actual measured CPS value the
	 * application is achiving at run-time.
	 *
	 * @return the measured CPS value
	 */
	float GetCPS(RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Get the measured Jobs Per Second (JPS) value
	 *
	 * @return the measured JPS value
	 */
	float GetJPS(RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Set the required Cycles Per Second goal (CPS)
	 *
	 * This allows to define the required and expected cycles rate.
	 * Conversely from "SetCPS" if the (percentage) gap between the current
	 * CPS performance and the CPS goal overpass the configured threshold, a
	 * SetGoalGap is automatically called. This relieves the application
	 * developer from the burden of explicitely sending a goal-gap at each
	 * iteration.
	 */
	RTLIB_ExitCode_t SetCPSGoal(
	        RTLIB_EXCHandler_t exc_handler,
	        float cps_min, float cps_max);

	/**
	 * @brief Set the required Jobs Per Second goal (JPS)
	 */
	RTLIB_ExitCode_t SetJPSGoal(
	        RTLIB_EXCHandler_t exc_handler,
	        float jps_min, float jps_max, int jpc);

	/**
	 * @brief Updates Jobs Per Cycle value (JPC), which is used to compute JPS
	 */
	RTLIB_ExitCode_t UpdateJPC(RTLIB_EXCHandler_t exc_handler, int jpc);

	/**
	 * @brief Set the required Cycle time [us]
	 *
	 * This allows to define the required and expected cycle time. If at
	 * run-time the cycles execution should be faster, a properly computed
	 * delay will be inserted by the RTLib in order to get the specified
	 * duration.
	 */
	RTLIB_ExitCode_t SetMinimumCycleTimeUs(
	        RTLIB_EXCHandler_t exc_handler, uint32_t max_cycle_time_us)
	{
		return SetCPS(exc_handler,
		              static_cast<float>(US_IN_A_SECOND) / max_cycle_time_us);
	}

	/**
	 * @brief Get the current execution time
	 *
	 * @return the measured execution time in milliseconds
	 */
	uint32_t GetExecutionTimeMs(RTLIB_EXCHandler_t exc_handler);


	/**********************************************************************
	 *    Pre/Post Functions
	 *********************************************************************/

	void NotifySetup(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyExit(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPreConfigure(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPostConfigure(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPreRun(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPostRun(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPreMonitor(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPostMonitor(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPreSuspend(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPostSuspend(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPreResume(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyPostResume(
	        RTLIB_EXCHandler_t exc_handler);

	void NotifyRelease(
	        RTLIB_EXCHandler_t exc_handler);

protected:

	static std::unique_ptr<bu::Logger> logger;

	typedef struct PerfEventAttr {
#ifdef CONFIG_BBQUE_RTLIB_PERF_SUPPORT
		perf_type_id type;
#endif
		uint64_t config;
	} PerfEventAttr_t;

	typedef PerfEventAttr_t * pPerfEventAttr_t;

	typedef std::map<int, pPerfEventAttr_t> PerfRegisteredEventsMap_t;

	typedef std::pair<int, pPerfEventAttr_t> PerfRegisteredEventsMapEntry_t;


	typedef	struct PerfEventStats {

		/** Per AWM perf counter value */
		uint64_t value;

		/** Per AWM perf counter enable time */
		uint64_t time_enabled;

		/** Per AWM perf counter running time */
		uint64_t time_running;

		/** Perf counter attrs */
		pPerfEventAttr_t pattr;

		/** Perf counter ID */
		int id;

		/** The statistics collected for this PRE */
		accumulator_set < uint32_t,
		                stats<tag::min, tag::max, tag::variance >> perf_samples;
	} PerfEventStats_t;

	struct CpuUsageStats {
		bool reset_timestamp = true;
		struct tms time_sample;
		clock_t prev_time, prev_tms_stime, prev_tms_utime, curr_time;
	};

	typedef std::shared_ptr<PerfEventStats_t> pPerfEventStats_t;

	typedef std::map<int, pPerfEventStats_t> PerfEventStatsMap_t;

	typedef std::pair<int, pPerfEventStats_t> PerfEventStatsMapEntry_t;

	typedef std::multimap<uint8_t, pPerfEventStats_t> PerfEventStatsMapByConf_t;

	typedef std::pair<uint8_t, pPerfEventStats_t> PerfEventStatsMapByConfEntry_t;

	/**
	 * @brief Statistics on AWM usage
	 */
	typedef struct AwmStats {

		/** Count of times this AWM has been in used */
		uint32_t number_of_uses;

		/** The time [ms] spent on processing into this AWM */
		uint32_t time_spent_processing;

		/** The time [ms] spent on monitoring this AWM */
		uint32_t time_spent_monitoring;

		/** The time [ms] spent on configuring this AWM */
		uint32_t time_spent_configuring;

		/** Statistics on AWM cycles */
		accumulator_set < double,
		                stats<tag::min, tag::max, tag::variance >> cycle_samples;

		/** Statistics on ReConfiguration Overheads */
		accumulator_set < double,
		                stats<tag::min, tag::max, tag::variance >> config_samples;

		/** Statistics on Monitoring Overheads */
		accumulator_set < double,
		                stats<tag::min, tag::max, tag::variance >> monitor_samples;

#ifdef CONFIG_BBQUE_RTLIB_PERF_SUPPORT

		/** Map of registered Perf counters */
		PerfEventStatsMap_t events_map;

		/** Map registered Perf counters to their type */
		PerfEventStatsMapByConf_t events_conf_map;

#endif // CONFIG_BBQUE_RTLIB_PERF_SUPPORT

#ifdef CONFIG_TARGET_OPENCL
		/** Map of OpenCL profiling info */
		OclEventsStatsMap_t ocl_events_map;
#endif // CONFIG_TARGET_OPENCL

		/** The mutex protecting concurrent access to statistical data */
		std::mutex stats_mutex;

		AwmStats() :
			number_of_uses(0),
			time_spent_processing(0),
			time_spent_monitoring(0),
			time_spent_configuring(0) {};

	} AwmStats_t;


	/**
	 * @brief A pointer to AWM statistics
	 */
	typedef std::shared_ptr<AwmStats_t> pAwmStats_t;

	/**
	 * @brief Map AWMs ID into its statistics
	 */
	typedef std::map<uint8_t, pAwmStats_t> AwmStatsMap_t;

	/**
	 * @brief A pointer to the system resources
	 */
	typedef std::shared_ptr<RTLIB_SystemResources_t> pSystemResources_t;

	/**
	 * @brief Map systemid - system resources
	 */
	typedef std::map<uint16_t, pSystemResources_t> SysResMap_t;

	/**
	 * @class RegisteredExecutionContext
	 *
	 * @brief Run-time information of the Execution context
	 */
	typedef struct RegisteredExecutionContext {

		/** The Execution Context data */
		RTLIB_EXCParameters_t parameters;

		/** The name of this Execuion Context */
		std::string name;

		/** The RTLIB assigned ID for this Execution Context */
		uint8_t id;

		/** The PID of the control thread managing this EXC */
		pid_t control_thread_pid = 0;

#ifdef CONFIG_BBQUE_RTLIB_CGROUPS_SUPPORT
		/** The path of the CGroup for this EXC */
		std::string cgroup_path;
#endif
#define EXC_FLAGS_AWM_VALID      0x01 ///< The EXC has been assigned a valid AWM
#define EXC_FLAGS_AWM_WAITING    0x02 ///< The EXC is waiting for a valid AWM
#define EXC_FLAGS_AWM_ASSIGNED   0x04 ///< The EXC is waiting for a valid AWM
#define EXC_FLAGS_EXC_SYNC       0x08 ///< The EXC entered Sync Mode
#define EXC_FLAGS_EXC_SYNC_DONE  0x10 ///< The EXC exited Sync Mode
#define EXC_FLAGS_EXC_REGISTERED 0x20 ///< The EXC is registered
#define EXC_FLAGS_EXC_ENABLED    0x40 ///< The EXC is enabled
#define EXC_FLAGS_EXC_BLOCKED    0x80 ///< The EXC is blocked

		/** A set of flags to define the state of this EXC */
		uint8_t flags = 0x00;

		/** The last required synchronization action */
		RTLIB_ExitCode_t event = RTLIB_OK;

		/** The ID of the assigned AWM (if valid) */
		int8_t current_awm_id = 0;

		/** Resource allocation for each system **/
		SysResMap_t resource_assignment;

		/** The mutex protecting access to this structure */
		std::mutex exc_mutex;

		/** The conditional variable to be notified on changes for
		 * this EXC */
		std::condition_variable exc_condition_variable;

		/** The High-Resolution timer used for profiling */
		bu::Timer execution_timer;

		/** The time [ms] latency to start the first execution */
		uint32_t starting_time_ms   = 0;

		/** The time [ms] spent on waiting for an AWM being assigned */
		uint32_t blocked_time_ms    = 0;

		/** The time [ms] spent on reconfigurations */
		uint32_t config_time_ms     = 0;

		/** The time [ms] spent on processing */
		uint32_t processing_time_ms = 0;

#ifdef CONFIG_BBQUE_RTLIB_PERF_SUPPORT

		/** Performance counters */
		bu::Perf perf;

		/** Map of registered Perf counter IDs */
		PerfRegisteredEventsMap_t events_map;

#endif // CONFIG_BBQUE_RTLIB_PERF_SUPPORT

		/** Overall cycles for this EXC */
		uint64_t cycles_count = 0;

		/** Statistics on AWM's of this EXC */
		AwmStatsMap_t awm_stats;

		/** Statistics of currently selected AWM */
		pAwmStats_t current_awm_stats;

#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION

		struct CGroupBudgetInfo {
			float cpu_budget_isolation = 0.0;
			float cpu_budget_shared = 0.0;
			std::string memory_limit_bytes;
			std::string cpuset_cpus_isolation;
			std::string cpuset_cpus_global;
			std::string cpuset_mems;
			std::vector<int32_t> cpu_global_ids;
			std::vector<int32_t> cpu_isolation_ids;
		} cg_budget;

		struct CGroupAllocationInfo {
			float cpu_budget = 0.0;
			std::string memory_limit_bytes;
			std::string cpuset_cpus;
			std::string cpuset_mems;
			std::vector<int32_t> cpu_affinity_mask;
		} cg_current_allocation;
#endif
		struct RT_Profile {
			float cpu_goal_gap = 0.0f;
			bool rtp_forward = false;
		} runtime_profiling;

		double mon_tstart = 0; // [ms] at the last monitoring start time

		/** CPS performance monitoring/control */
		// [ms] at the last cycle start time
		double 	 cycle_start_time_ms         = 0.0;

		// [Hz] the minimum cycle time in milliseconds
		float  	 cycle_time_enforced_ms      = 0.0;

		// [Hz] the minimum required CPS
		float  	 cps_goal_min                = 0.0;

		// [Hz] the maximum required CPS
		float  	 cps_goal_max                = 0.0;

		// [Hz] the required maximum CPS
		float  	 cps_max_allowed             = 0.0;

		// [us] time spent sleeping to enforce maximum CPS
		uint16_t cps_enforcing_sleep_time_ms = 0;

		// Current number of processed Jobs per Cycle
		int      jpc                         = 1;

		// Moving Statistics for cycle times (user-side):
		// 		onRun + onMonitor + ForceCPS sleep
		bu::StatsAnalysis cycletime_stats_user;
		// Moving Statistics for cycle times (bbque-side):
		// 		onRun + onMonitor
		bu::StatsAnalysis cycletime_stats_system;
		double last_cycletime_ms = 0.0;

		// Applications can explicitely ask for a runtime profile
		// notification
		bool 	explicit_ggap_assertion = false;
		float 	explicit_ggap_value = 0.0;

		// Once a runtime profile has been forwarded to bbque, there is
		// no need to send another one before a reconfiguration happens
		int waiting_sync_timeout_ms = 0;
		bool is_waiting_for_sync = false;

		/** Cycle of the last goal-gap assertion */
		CpuUsageStats cpu_usage_info;
		bu::StatsAnalysis cpu_usage_stats;

		RegisteredExecutionContext(const char * _name, uint8_t id) :
			name(_name), id(id)
		{
			//cycletime_stats_system.EnablePhaseDetection();
			//cycletime_stats_user.EnablePhaseDetection();
			//cpu_usage_stats.EnablePhaseDetection();
		}

		~RegisteredExecutionContext()
		{
			awm_stats.clear();
			current_awm_stats = pAwmStats_t();
		}

	} RegisteredExecutionContext_t;

	typedef std::shared_ptr<RegisteredExecutionContext_t> pRegisteredEXC_t;

	//--- AWM Validity
	bool isAwmValid(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_AWM_VALID);
	}
	void setAwmValid(pRegisteredEXC_t exc) const
	{
		logger->Debug("AWM  <= Valid [%d:%s:%d]",
		              exc->id, exc->name.c_str(), exc->current_awm_id);
		exc->flags |= EXC_FLAGS_AWM_VALID;
	}
	void clearAwmValid(pRegisteredEXC_t exc) const
	{
		logger->Debug("AWM  <= Invalid [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_AWM_VALID;
	}

	//--- AWM Wait
	bool isAwmWaiting(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_AWM_WAITING);
	}

	void setAwmWaiting(pRegisteredEXC_t exc)
	{
		logger->Debug("AWM  <= Waiting [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_AWM_WAITING;
	}

	void clearAwmWaiting(pRegisteredEXC_t exc)
	{
		logger->Debug("AWM  <= NOT Waiting [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_AWM_WAITING;
	}

	//--- AWM Assignment
	bool isAwmAssigned(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_AWM_ASSIGNED);
	}

	void setAwmAssigned(pRegisteredEXC_t exc)
	{
		logger->Debug("AWM  <= Assigned [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_AWM_ASSIGNED;
	}
	void clearAwmAssigned(pRegisteredEXC_t exc)
	{
		logger->Debug("AWM  <= NOT Assigned [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_AWM_ASSIGNED;
	}

	//--- Sync Mode Status
	bool isSyncMode(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_EXC_SYNC);
	}

	void setSyncMode(pRegisteredEXC_t exc)
	{
		logger->Debug("SYNC <= Enter [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_EXC_SYNC;
	}

	void clearSyncMode(pRegisteredEXC_t exc)
	{
		logger->Debug("SYNC <= Exit [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_EXC_SYNC;
	}

	//--- Sync Done
	bool isSyncDone(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_EXC_SYNC_DONE);
	}

	void setSyncDone(pRegisteredEXC_t exc)
	{
		logger->Debug("SYNC <= Done [%d:%s:%d]",
		              exc->id, exc->name.c_str(), exc->current_awm_id);
		exc->flags |= EXC_FLAGS_EXC_SYNC_DONE;
	}

	void clearSyncDone(pRegisteredEXC_t exc)
	{
		logger->Debug("SYNC <= Pending [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_EXC_SYNC_DONE;
	}

	//--- EXC Registration status
	bool isRegistered(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_EXC_REGISTERED);
	}

	void setRegistered(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= Registered [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_EXC_REGISTERED;
	}

	void clearRegistered(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= Unregistered [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_EXC_REGISTERED;
	}

	//--- EXC Enable status
	bool isEnabled(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_EXC_ENABLED);
	}

	void setEnabled(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= Enabled [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_EXC_ENABLED;
	}

	void clearEnabled(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= Disabled [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_EXC_ENABLED;
	}

	//--- EXC Blocked status
	bool isBlocked(pRegisteredEXC_t exc) const
	{
		return (exc->flags & EXC_FLAGS_EXC_BLOCKED);
	}

	void setBlocked(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= Blocked [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags |= EXC_FLAGS_EXC_BLOCKED;
	}

	void clearBlocked(pRegisteredEXC_t exc)
	{
		logger->Debug("EXC  <= UnBlocked [%d:%s]",
		              exc->id, exc->name.c_str());
		exc->flags &= ~EXC_FLAGS_EXC_BLOCKED;
	}


#ifdef CONFIG_TARGET_OPENCL

	/**********************************************************************
	 *    OpenCL support
	 **********************************************************************/

	void OclSetDevice(uint32_t platform_id, uint8_t device_id, RTLIB_ExitCode_t status);
	void OclClearStats();
	void OclCollectStats(int8_t curr_awm_id, OclEventsStatsMap_t & ocl_events_map);
	void OclPrintCmdStats(QueueProfPtr_t, cl_command_queue);
	void OclPrintAddrStats(QueueProfPtr_t, cl_command_queue);
	void OclDumpStats(pRegisteredEXC_t exc);
	void OclDumpCmdStats(QueueProfPtr_t stPtr, cl_command_queue cmd_queue);
	void OclDumpAddrStats(QueueProfPtr_t stPtr, cl_command_queue cmd_queue);
	void OclGetRuntimeProfile(pRegisteredEXC_t exc, uint32_t & exec_time, uint32_t & mem_time);

#endif // CONFIG_TARGET_OPENCL

	/***********************************************************************
	 * RTLib Run-Time Configuration
	 **********************************************************************/

	static RTLIB_Conf_t rtlib_configuration;

	/**
	 * @brief Look-up configuration from environment variable BBQUE_RTLIB_OPTS
	 */
	static RTLIB_ExitCode_t ParseOptions();

	/**
	 * @brief Insert a raw performance counter into the events array
	 *
	 * @param perf_str The string containing label and event code of the
	 * performance counter
	 *
	 * @return The index of the performance counter in the events array
	 */
	static uint8_t InsertRAWPerfCounter(const char * perf_str);

	/***********************************************************************
	 * Channel Dependant interface
	 **********************************************************************/

	virtual RTLIB_ExitCode_t _Init(const char * name) = 0;

	virtual RTLIB_ExitCode_t _Register(pRegisteredEXC_t exc) = 0;

	virtual RTLIB_ExitCode_t _Unregister(pRegisteredEXC_t exc) = 0;

	virtual RTLIB_ExitCode_t _Enable(pRegisteredEXC_t exc) = 0;

	virtual RTLIB_ExitCode_t _Disable(pRegisteredEXC_t exc) = 0;

	virtual RTLIB_ExitCode_t _Set(
	        pRegisteredEXC_t exc,
	        RTLIB_Constraint_t * constraints,
	        uint8_t count) = 0;

	virtual RTLIB_ExitCode_t _Clear(pRegisteredEXC_t exc) = 0;

	virtual RTLIB_ExitCode_t _RTNotify(
	        pRegisteredEXC_t exc,
	        int percent,
	        int cusage,
	        int ctime_ms) = 0;

	virtual RTLIB_ExitCode_t _ScheduleRequest(pRegisteredEXC_t exc) = 0;

	virtual void _Exit() = 0;


	/**********************************************************************
	 * Runtime profiling
	 **********************************************************************/

	virtual RTLIB_ExitCode_t _GetRuntimeProfileResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        uint32_t exc_time,
	        uint32_t mem_time) = 0;


	/***********************************************************************
	 * Synchronization Protocol Messages
	 **********************************************************************/

//----- PreChange

	/**
	 * @brief Send response to a Pre-Change command
	 */
	virtual RTLIB_ExitCode_t _SyncpPreChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        uint32_t syncLatency) = 0;

	/**
	 * @brief A synchronization protocol Pre-Change for the EXC with the
	 * specified ID.
	 */
	RTLIB_ExitCode_t SyncP_PreChangeNotify(
	        rpc_msg_BBQ_SYNCP_PRECHANGE_t msg,
	        std::vector<rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM_t> & systems);

//----- SyncChange

	/**
	 * @brief Send response to a Sync-Change command
	 */
	virtual RTLIB_ExitCode_t _SyncpSyncChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc, RTLIB_ExitCode_t sync) = 0;

	/**
	 * @brief A synchronization protocol Sync-Change for the EXC with the
	 * specified ID.
	 */
	RTLIB_ExitCode_t SyncP_SyncChangeNotify(
	        rpc_msg_BBQ_SYNCP_SYNCCHANGE_t & msg);

//----- DoChange

	/**
	 * @brief A synchronization protocol Do-Change for the EXC with the
	 * specified ID.
	 */
	RTLIB_ExitCode_t SyncP_DoChangeNotify(
	        rpc_msg_BBQ_SYNCP_DOCHANGE_t & msg);

//----- PostChange

	/**
	 * @brief Send response to a Post-Change command
	 */
	virtual RTLIB_ExitCode_t _SyncpPostChangeResp(
	        rpc_msg_token_t token,
	        pRegisteredEXC_t exc,
	        RTLIB_ExitCode_t result) = 0;

	/**
	 * @brief A synchronization protocol Post-Change for the EXC with the
	 * specified ID.
	 */
	RTLIB_ExitCode_t SyncP_PostChangeNotify(
	        rpc_msg_BBQ_SYNCP_POSTCHANGE_t & msg);


protected:

	/**
	 * @brief The name of this application
	 */
	const char * application_name;

	/**
	 * @brief The PID of the Channel Thread
	 *
	 * The Channel Thread is the process/thread in charge to manage messages
	 * exchange with the Barbeque RTRM. Usually, this thread is spawned by the
	 * subclass of this based class which provides the low-level channel
	 * access methods.
	 */
	pid_t channel_thread_pid = 0;

	/**
	 * @brief The channel thread UID
	 *
	 * The Channel Thread and the corresponding application is uniquely
	 * identified by a UID string which is initialized by a call to
	 * @see setUid()
	 */
	char channel_thread_unique_id[20] = "00000:undef";

	/**
	 * @brief Set the channel thread id
	 *
	 * @param id Thread id
	 * @param name Thread name
	 */
	void SetChannelThreadID(pid_t id, const char * name)
	{
		channel_thread_pid = id;
		snprintf(channel_thread_unique_id, 20, "%05d:%-.13s",
		         channel_thread_pid, name);
	}

	/**
	 * @brief The PID of the application using the library
	 *
	 * This keep track of the application which initialize the library. This
	 * PID could be exploited by the Barbeque RTRM to directly control
	 * applications accessing its managed resources.
	 */
	pid_t application_pid = 0;

	/**
	 * @brief The PID of the application to restore
	 *
	 * This is set in case we are performing a restore of a managed
	 * application as a child of the current new one
	 */
	pid_t restore_pid = 0;

private:

	/**
	 * @brief True if the library has been properly initialized
	 */
	bool rtlib_is_initialized = false;

	/**
	 * @brief A map of registered Execution Context
	 *
	 * This maps Execution Context ID (exc_id) into pointers to
	 * RegisteredExecutionContext structures.
	 */
	typedef std::map<uint8_t, pRegisteredEXC_t> excMap_t;

	/**
	 * @brief The map of EXC (successfully) registered by this application
	 */
	excMap_t exc_map;

	/**
	 * @brief An entry of the map of registered EXCs
	 */
	typedef std::pair<uint8_t, pRegisteredEXC_t> excMapEntry_t;

	/**
	 * @brief The path of the application CGroup
	 */
	std::string pathCGroup;

	/**
	 * @brief Get the next available (and unique) Execution Context ID
	 */
	uint8_t NextExcID();


	/***********************************************************************
	 *   AWM Profiling
	 **********************************************************************/

	/**
	 * @brief Setup statistics for a new selecte AWM
	 */
	RTLIB_ExitCode_t SetupStatistics(pRegisteredEXC_t exc);

	/**
	 * @brief Update statistics for the currently selected AWM
	 */
	RTLIB_ExitCode_t UpdateStatistics(pRegisteredEXC_t exc);

	/**
	 * @brief Initialize the CPU usage profiling data
	 * @param exc the current execution context
	 */
	void InitCPUBandwidthStats(pRegisteredEXC_t exc);

	/**
	 * @brief Update the CPU usage profiling data
	 * @param exc the current execution context
	 * @return
	 */
	RTLIB_ExitCode_t UpdateCPUBandwidthStats(pRegisteredEXC_t exc);

	/**
	 * @brief Update statistics about onMonitor execution for the currently
	 * selected AWM
	 */
	RTLIB_ExitCode_t UpdateMonitorStatistics(pRegisteredEXC_t exc);

	/**
	 * @brief Compute the goal gap for the runtime profiling
	 */
	float ComputeGoalGap(pRegisteredEXC_t exc);

#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION

	/**
	 * @brief Update the CPU quota allocation according to
	 * the runtime profiling
	 */
	RTLIB_ExitCode_t UpdateDistributedAllocation(
		pRegisteredEXC_t exc,
		float goal_gap);
#endif

	/**
	 * @brief Log the header for statistics collection
	 */
	void DumpStatsHeader();

	/**
	 * @brief Log memory usage report
	 */
	void DumpMemoryReport(pRegisteredEXC_t exc);

	/**
	 * @brief Log execution statistics collected so far
	 */
	void DumpStats(pRegisteredEXC_t exc, bool verbose = false);

	/**
	 * @brief Log execution statistics collected so far (Console format)
	 */
	void DumpStatsConsole(pRegisteredEXC_t exc, bool verbose = false);

	/**
	 * @brief Update sync time [ms] estimation for the currently AWM.
	 *
	 * @note This method requires statistics being already initialized
	 */
	void _SyncTimeEstimation(pRegisteredEXC_t exc);

	/**
	 * @brief Update sync time [ms] estimation for the currently AWM
	 *
	 * This method ensure statistics update if they have been already
	 * initialized.
	 */
	void SyncTimeEstimation(pRegisteredEXC_t exc);

	/**
	 * @brief Get an extimation of the Synchronization Latency
	 */
	uint32_t GetSyncLatency(pRegisteredEXC_t exc);

	/***********************************************************************
	 *   Synchronization functions
	 **********************************************************************/

	/**
	 * @brief Get the assigned AWM (if valid)
	 *
	 * @return RTLIB_OK if a valid AWM has been returned, RTLIB_EXC_GWM_FAILED
	 * if the current AWM is not valid and thus a scheduling should be
	 * required to the RTRM
	 */
	RTLIB_ExitCode_t GetAssignedWorkingMode(
	        pRegisteredEXC_t exc,
	        RTLIB_WorkingModeParams_t * wm);

	/**
	 * @brief Suspend caller waiting for an AWM being assigned
	 *
	 * When the EXC has notified a scheduling request to the RTRM, this
	 * method put it to sleep waiting for an assignement.
	 *
	 * @return RTLIB_OK if a valid working mode has been assinged to the EXC,
	 * RTLIB_EXC_GWM_FAILED otherwise
	 */
	RTLIB_ExitCode_t WaitForWorkingMode(
	        pRegisteredEXC_t exc,
	        RTLIB_WorkingModeParams * wm);

	/**
	 * @brief Update the assigned resources information in the
	 * assigned working mode
	 */
	void UpdateWorkingModeAssignments(
	        pRegisteredEXC_t exc,
	        RTLIB_WorkingModeParams * wm);

	/**
	 * @brief Suspend caller waiting for a reconfiguration to complete
	 *
	 * When the EXC has notified to switch into a different AWM by the RTRM,
	 * this method put the RTLIB PostChange to sleep waiting for the
	 * completion of such reconfiguration.
	 *
	 * @param exc the regidstered EXC to wait reconfiguration for
	 *
	 * @return RTLIB_OK if the reconfigutation complete successfully,
	 * RTLIB_EXC_SYNCP_FAILED otherwise
	 */
	RTLIB_ExitCode_t WaitForSyncDone(pRegisteredEXC_t exc);


	/**********************************************************************
	 * Synchronization Protocol Messages
	 **********************************************************************/

	/**
	 * @brief A synchronization protocol Pre-Change for the specified EXC.
	 */
	RTLIB_ExitCode_t SyncP_PreChangeNotify(pRegisteredEXC_t exc);

	/**
	 * @brief A synchronization protocol Sync-Change for the specified EXC.
	 */
	RTLIB_ExitCode_t SyncP_SyncChangeNotify(pRegisteredEXC_t exc);

	/**
	 * @brief A synchronization protocol Do-Change for the specified EXC.
	 */
	RTLIB_ExitCode_t SyncP_DoChangeNotify(pRegisteredEXC_t exc);

	/**
	 * @brief A synchronization protocol Post-Change for the specified EXC.
	 */
	RTLIB_ExitCode_t SyncP_PostChangeNotify(pRegisteredEXC_t exc);


	/**********************************************************************
	 * Application Callbacks Proxies
	 **********************************************************************/

	RTLIB_ExitCode_t StopExecution(
	        RTLIB_EXCHandler_t exc_handler,
	        struct timespec timeout);

	/**********************************************************************
	 * Utility functions
	 **********************************************************************/

	pRegisteredEXC_t getRegistered(
	        const RTLIB_EXCHandler_t exc_handler);

	/**
	 * @brief Get an EXC handler for the give EXC ID
	 */
	pRegisteredEXC_t getRegistered(uint8_t exc_id);


	/**
	 * Check if the specified duration has expired.
	 *
	 * A run-time duration can be specified both in [s] or number of
	 * processing cycles. In case a duration has been specified via
	 * BBQUE_RTLIB_OPTS, once this duration has been passed, this method
	 * return true and the application is "forcely" terminated by the
	 * RTLIB.
	 */
	bool CheckDurationTimeout(pRegisteredEXC_t exc);

	/*********************************************************************
	 * Performance Counters
	 *********************************************************************/
#ifdef CONFIG_BBQUE_RTLIB_PERF_SUPPORT

#define BBQUE_RTLIB_PERF_ENABLE true

	/** Default performance attributes to collect for each task */
	static PerfEventAttr_t * raw_events;

	/** Default performance attributes to collect for each task */
	static PerfEventAttr_t default_events[];

	/** Detailed stats (-d), covering the L1 and last level data caches */
	static PerfEventAttr_t detailed_events[];

	/** Very detailed stats (-d -d), covering the instruction cache and the
	 * TLB caches: */
	static PerfEventAttr_t very_detailed_events[];

	/** Very, very detailed stats (-d -d -d), adding prefetch events */
	static PerfEventAttr_t very_very_detailed_events[];

	uint8_t PerfRegisteredEvents(pRegisteredEXC_t exc)
	{
		return exc->events_map.size();
	}

	bool PerfEventMatch(pPerfEventAttr_t ppea,
	                    perf_type_id type, uint64_t config)
	{
		return (ppea->type == type && ppea->config == config);
	}

	void PerfDisable(pRegisteredEXC_t exc)
	{
		exc->perf.Disable();
	}

	void PerfEnable(pRegisteredEXC_t exc)
	{
		exc->perf.Enable();
	}

	void PerfSetupEvents(pRegisteredEXC_t exc);

	void PerfSetupStats(pRegisteredEXC_t exc, pAwmStats_t awm_stats);

	void PerfCollectStats(pRegisteredEXC_t exc);

	void PerfPrintStats(pRegisteredEXC_t exc, pAwmStats_t awm_stats);

	bool IsNsecCounter(pRegisteredEXC_t exc, int fd);

	void PerfPrintNsec(pAwmStats_t awm_stats, pPerfEventStats_t perf_event_stats);

	void PerfPrintAbs(pAwmStats_t awm_stats, pPerfEventStats_t perf_event_stats);

	pPerfEventStats_t PerfGetEventStats(pAwmStats_t awm_stats, perf_type_id type,
	                                    uint64_t config);

	void PerfPrintMissesRatio(double avg_missed, double tot_branches,
	                          const char * text);

	void PrintNoisePct(double total, double avg);
#else

# define BBQUE_RTLIB_PERF_ENABLE false
# define PerfRegisteredEvents(exc) 0
# define PerfSetupStats(exc, awm_stats) {}
# define PerfSetupEvents(exc) {}
# define PerfEnable(exc) {}
# define PerfDisable(exc) {}
# define PerfCollectStats(exc) {}
# define PerfPrintStats(exc, awm_stats) {}

#endif // CONFIG_BBQUE_RTLIB_PERF_SUPPORT


	/*******************************************************************************
	 *    Cycles Per Second (CPS) Control Support
	 ******************************************************************************/

	void ForceCPS(pRegisteredEXC_t exc);


	/*******************************************************************************
	 *    RTLib-level cgroup management
	 ******************************************************************************/

	/**
	 * @brief Initialize CGroup support
	 */
	RTLIB_ExitCode_t CGroupCheckInitialization();

	/**
	 * @brief Create a CGroup for the specifed EXC
	 */
	RTLIB_ExitCode_t CGroupPathSetup(pRegisteredEXC_t exc);

	/**
	 * @brief Delete the CGroup of the specified EXC
	 */
	RTLIB_ExitCode_t CGroupDelete(pRegisteredEXC_t exc);

	/**
	 * @brief Create a CGroup of the specified EXC
	 */
	RTLIB_ExitCode_t CGroupCreate(pRegisteredEXC_t exc, int pid);

	/**
	 * @brief Updates the CGroup of the specified EXC
	 */
	RTLIB_ExitCode_t CGroupCommitAllocation(pRegisteredEXC_t exc);

};

} // namespace rtlib

} // namespace bbque

// Undefine locally defined module name
#undef MODULE_NAMESPACE

#endif /* end of include guard: BBQUE_RPC_H_ */
