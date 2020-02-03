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

#include "bbque/rtlib.h"

#include "bbque/version.h"
#include "bbque/utils/timer.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/logging/console_logger.h"

#include "bbque/rtlib/bbque_rpc.h"
#ifdef CONFIG_TARGET_OPENCL
#include "bbque/rtlib/bbque_ocl.h"
#endif

#ifdef CONFIG_TARGET_ANDROID
#include <stdint.h>
#else
#include <cstdint>
#endif

namespace bb = bbque;
namespace bu = bbque::utils;
namespace bl = bbque::rtlib;

// Setup logging
#undef  BBQUE_LOG_MODULE
#define BBQUE_LOG_MODULE "rtl"

static void RTLIB_Exit(void);

/**
 * The global timer, this can be used to get the time since the RTLib has been
 * initialized */
bu::Timer bbque_tmr(true);

/**
 * A pointer to the Barbeque RPC communication channel
 */
static bl::BbqueRPC * rpc = NULL;

/**
 * The collection of RTLib services accessible from applications.
 */
RTLIB_Services_t rtlib_services;

#ifdef CONFIG_TARGET_OPENCL
/**
 * The collection of RTLib wrapped OpenCL functions.
 */
RTLIB_OpenCL_t rtlib_ocl;

/**
 * The map contains OpenCL command queues and shared pointers to profiling
 * events data structures
 */
std::map<cl_command_queue, QueueProfPtr_t> ocl_queues_prof;

/**
 * The map contains OpenCL command types and their respective string values
 */
std::map<cl_command_type, std::string> ocl_cmd_str;
std::map<void *, cl_command_type> ocl_addr_cmd;

#endif // CONFIG_TARGET_OPENCL

static RTLIB_EXCHandler_t rtlib_register(const char * name,
                const RTLIB_EXCParameters_t * params)
{
	return rpc->Register(name, params);
}

static RTLIB_ExitCode_t rtlib_cgsetup(const RTLIB_EXCHandler_t exc_handler)
{
	return rpc->SetupCGroup(exc_handler);
}

static void rtlib_unregister(
        const RTLIB_EXCHandler_t exc_handler)
{
	return rpc->Unregister(exc_handler);
}

static RTLIB_ExitCode_t rtlib_enable(
        const RTLIB_EXCHandler_t exc_handler)
{
	return rpc->Enable(exc_handler);
}

static RTLIB_ExitCode_t rtlib_disable(
        const RTLIB_EXCHandler_t exc_handler)
{
	return rpc->Disable(exc_handler);
}

static RTLIB_ExitCode_t rtlib_getwm(
        const RTLIB_EXCHandler_t exc_handler,
        RTLIB_WorkingModeParams_t * wm,
        RTLIB_SyncType_t st)
{
	return rpc->GetWorkingMode(exc_handler, wm, st);
}

static RTLIB_ExitCode_t rtlib_set(
        RTLIB_EXCHandler_t exc_handler,
        RTLIB_Constraint_t * constraints, uint8_t count)
{
	return rpc->SetAWMConstraints(exc_handler, constraints, count);
}

static RTLIB_ExitCode_t rtlib_clear(
        RTLIB_EXCHandler_t exc_handler)
{
	return rpc->ClearAWMConstraints(exc_handler);
}

static RTLIB_ExitCode_t rtlib_ggap(
        RTLIB_EXCHandler_t exc_handler,
        int gap)
{
	return rpc->SetExplicitGoalGap(exc_handler, gap);
}

/*******************************************************************************
 *    Utility Functions
 ******************************************************************************/

static const char * rtlib_utils_getchuid()
{
	return rpc->GetCharUniqueID();
}

static AppUid_t rtlib_utils_getuid(RTLIB_EXCHandler_t exc_handler)
{
	return rpc->GetUniqueID(exc_handler);
}

static RTLIB_ExitCode_t rtlib_utils_get_resources(
        RTLIB_EXCHandler_t exc_handler,
        const RTLIB_WorkingModeParams_t * wm,
        RTLIB_ResourceType_t r_type,
        int32_t & r_amount)
{
	return rpc->GetAssignedResources(exc_handler, wm, r_type, r_amount);
}

static RTLIB_ExitCode_t rtlib_utils_get_affinity_mask(
        RTLIB_EXCHandler_t exc_handler,
        const RTLIB_WorkingModeParams_t * wm,
        int32_t * ids_vector,
        int vector_size)
{
	return rpc->GetAffinityMask(exc_handler, wm, ids_vector, vector_size);
}

static RTLIB_ExitCode_t rtlib_utils_get_resources_array(
        RTLIB_EXCHandler_t exc_handler,
        const RTLIB_WorkingModeParams_t * wm,
        RTLIB_ResourceType_t r_type,
        int32_t * sys_array,
        uint16_t array_size)
{
	return rpc->GetAssignedResources(exc_handler, wm, r_type, sys_array,
	                                 array_size);
}

static void rtlib_utils_start_pcounters_monitoring(RTLIB_EXCHandler_t exc_handler)
{
	rpc->StartPCountersMonitoring(exc_handler);
}

static uint32_t rtlib_utils_get_ctime_ms(RTLIB_EXCHandler_t exc_handler)
{
	return rpc->GetExecutionTimeMs(exc_handler);
}

/*******************************************************************************
 *    Cycles Per Second (CPS) and Jobs Per Secon (JPS) Control Support
 ******************************************************************************/

static RTLIB_ExitCode_t rtlib_cps_set(RTLIB_EXCHandler_t exc_handler, float cps)
{
	return rpc->SetCPS(exc_handler, cps);
}

static float rtlib_cps_get(RTLIB_EXCHandler_t exc_handler)
{
	return rpc->GetCPS(exc_handler);
}

static float rtlib_jps_get(RTLIB_EXCHandler_t exc_handler)
{
	return rpc->GetJPS(exc_handler);
}

static RTLIB_ExitCode_t rtlib_cps_goal_set(
        RTLIB_EXCHandler_t exc_handler,
        float cps_min, float cps_max)
{
	rpc->ResetRuntimeProfileStats(exc_handler);
	return rpc->SetCPSGoal(exc_handler, cps_min, cps_max);
}

static RTLIB_ExitCode_t rtlib_jps_goal_set(
        RTLIB_EXCHandler_t exc_handler,
        float jps_min, float jps_max, int jpc)
{
	rpc->ResetRuntimeProfileStats(exc_handler);
	return rpc->SetJPSGoal(exc_handler, jps_min, jps_max, jpc);
}

static RTLIB_ExitCode_t rtlib_jps_goal_update(
        RTLIB_EXCHandler_t exc_handler, int jpc)
{
	return rpc->UpdateJPC(exc_handler, jpc);
}

static RTLIB_ExitCode_t rtlib_cps_set_ctime_us(
        RTLIB_EXCHandler_t exc_handler, uint32_t us)
{
	return rpc->SetMinimumCycleTimeUs(exc_handler, us);
}

/*******************************************************************************
 *    Performance Monitoring Support
 ******************************************************************************/

static void rtlib_notify_exit(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyExit(exc_handler);
}

static void rtlib_notify_pre_configure(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPreConfigure(exc_handler);
}

static void rtlib_notify_post_configure(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPostConfigure(exc_handler);
}

static void rtlib_notify_pre_run(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPreRun(exc_handler);
}

static void rtlib_notify_post_run(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPostRun(exc_handler);
}

static void rtlib_notify_pre_monitor(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPreMonitor(exc_handler);
}

static void rtlib_notify_post_monitor(RTLIB_EXCHandler_t exc_handler)
{
	rpc->NotifyPostMonitor(exc_handler);
}


const char * rtlib_app_name;
static uint8_t rtlib_initialized = 0;
static std::unique_ptr<bu::Logger> logger;

#include "bbque_errors.cc"

RTLIB_ExitCode_t RTLIB_Init(const char * name, RTLIB_Services_t ** rtlib)
{
	RTLIB_ExitCode_t result;
	(*rtlib) = NULL;

	// Checking error string array consistency
	static_assert(ARRAY_SIZE(RTLIB_errorStr) == RTLIB_EXIT_CODE_COUNT,
	              "RTLIB error strings not matching errors count");
	assert(rtlib_initialized == 0);

	// Get a Logger module
	logger = bu::Logger::GetLogger(BBQUE_LOG_MODULE);

	// Welcome screen
	logger->Debug("Barbeque RTLIB (ver. %s)\n", g_git_version);
	logger->Debug("Built: " __DATE__  " " __TIME__ "\n");

	// Data structure initialization
	rtlib_services.version.major = RTLIB_VERSION_MAJOR;
	rtlib_services.version.minor = RTLIB_VERSION_MINOR;
	rtlib_services.Register = rtlib_register;
	rtlib_services.SetupCGroups = rtlib_cgsetup;
	rtlib_services.EnableEXC = rtlib_enable;
	rtlib_services.GetWorkingMode = rtlib_getwm;
	rtlib_services.SetAWMConstraints = rtlib_set;
	rtlib_services.ClearAWMConstraints = rtlib_clear;
	rtlib_services.SetGoalGap = rtlib_ggap;
	rtlib_services.Disable = rtlib_disable;
	rtlib_services.Unregister = rtlib_unregister;

	// Utility functions interface
	rtlib_services.Utils.GetUniqueID_String = rtlib_utils_getchuid;
	rtlib_services.Utils.GetUniqueID = rtlib_utils_getuid;
	rtlib_services.Utils.GetResources = rtlib_utils_get_resources;
	rtlib_services.Utils.GetAffinityMask = rtlib_utils_get_affinity_mask;
	rtlib_services.Utils.GetResourcesArray = rtlib_utils_get_resources_array;
	rtlib_services.Utils.MonitorPerfCounters =
	        rtlib_utils_start_pcounters_monitoring;

	// Cycles Time Control interface
	rtlib_services.CPS.ExecTime_ms = rtlib_utils_get_ctime_ms;
	rtlib_services.CPS.Set = rtlib_cps_set;
	rtlib_services.CPS.Get = rtlib_cps_get;
	rtlib_services.CPS.SetGoal = rtlib_cps_goal_set;
	rtlib_services.CPS.SetMinCycleTime_us = rtlib_cps_set_ctime_us;
	rtlib_services.JPS.Get = rtlib_jps_get;
	rtlib_services.JPS.SetGoal = rtlib_jps_goal_set;
	rtlib_services.JPS.UpdateJPC = rtlib_jps_goal_update;

	// Performance monitoring notifiers
	rtlib_services.Notify.Exit = rtlib_notify_exit;
	rtlib_services.Notify.PreConfigure = rtlib_notify_pre_configure;
	rtlib_services.Notify.PostConfigure = rtlib_notify_post_configure;
	rtlib_services.Notify.PreRun = rtlib_notify_pre_run;
	rtlib_services.Notify.PostRun = rtlib_notify_post_run;
	rtlib_services.Notify.PreMonitor = rtlib_notify_pre_monitor;
	rtlib_services.Notify.PostMonitor = rtlib_notify_post_monitor;

#ifdef CONFIG_TARGET_OPENCL
	// OpenCL support initialization
	rtlib_ocl_init();
#endif
	// Building a communication channel
	rpc = bl::BbqueRPC::GetInstance();
	if (! rpc) {
		logger->Error("RTLIB_Init: RPC channel not available");
		return RTLIB_BBQUE_CHANNEL_SETUP_FAILED;
	}

	// Initializing the RPC communication channel
	result = rpc->InitializeApplication(name);
	if (result != RTLIB_OK) {
		logger->Error("RTLIB_Init: RPC channel initialization FAILED");
		return RTLIB_BBQUE_UNREACHABLE;
	}

	// Setup configuration descriptor
	rtlib_services.config = bl::BbqueRPC::Configuration();
	rtlib_initialized = 1; // Marking library as intialized
	rtlib_app_name = name;
	(*rtlib) = & rtlib_services;
	atexit(RTLIB_Exit);

	logger->Debug("RTLIB_Init: initialized [tid=%d]", gettid());
	return RTLIB_OK;
}

static void RTLIB_Exit(void)
{
	logger = bu::ConsoleLogger::GetInstance(BBQUE_LOG_MODULE);
	logger->Debug("RTLIB_Exit: thread tid=%d terminating...", gettid());

	if (! rtlib_initialized) {
		logger->Warn("RTLIB_Exit: library not initialized");
		return;
	}

	// Close the RPC FIFO channel thus releasing all BBQUE resource used by
	// this application
	assert(rpc);
	logger->Debug("RTLIB_Exit: unregistering all the contexts...");
	rpc->UnregisterAll(); // Ensure all the EXCs are unregistered
	delete rpc;

	if (0) RTLIB_Exit(); // Fix compilation warning
}
