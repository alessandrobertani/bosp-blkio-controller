/*
 * Copyright (C) 2020  Politecnico di Milano
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

#ifndef BBQUE_DYNAMICRANDOM_SCHEDPOL_H_
#define BBQUE_DYNAMICRANDOM_SCHEDPOL_H_

#include <cstdint>
#include <list>
#include <memory>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"

#include <cstdint>
#include <random>

#define SCHEDULER_POLICY_NAME "dynamicrandom"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME

using bbque::res::RViewToken_t;
using bbque::utils::MetricsCollector;
using bbque::utils::Timer;

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

enum Distribution { UNIFORM = 1, NORMAL = 2, POISSON = 3, BINOMIAL = 4, EXPONENTIAL = 5 };

namespace bbque { namespace plugins {

class LoggerIF;

/**
 * @class DynamicRandomSchedPol
 *
 * DynamicRandom scheduler policy registered as a dynamic C++ plugin.
 */
class DynamicRandomSchedPol: public SchedulerPolicyIF {

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the dynamicrandom plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the dynamicrandom plugin 
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~DynamicRandomSchedPol();

	/**
	 * @brief Return the name of the policy plugin
	 */
	char const * Name();


	/**
	 * @brief The member function called by the SchedulerManager to perform a
	 * new scheduling / resource allocation
	 */
	ExitCode_t Schedule(System & system, RViewToken_t & status_view);

private:

	/** Configuration manager instance */
	ConfigurationManager & cm;

	/** Resource accounter instance */
	ResourceAccounter & ra;

	/** System logger instance */
	std::unique_ptr<bu::Logger> logger;

	/** List of PE Ids */
	std::set<BBQUE_RID_TYPE> pe_ids;

	/** Number of available resources and applications */
	uint16_t nbr_av_res, nbr_app;

	/** The chosen distribution */
	Distribution distribution;

	/** The two parameters read from config file and used for distribution */ 
	float parameter1, parameter2;

	/** The two bounds used to generate number of resources to use */
	int lower_bound_perc, upper_bound_perc;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	DynamicRandomSchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	ExitCode_t Init();

	ExitCode_t AssignWorkingModeAndBind(bbque::app::AppCPtr_t papp);

	uint16_t GenerateRandomValue(uint16_t lower_bound, uint16_t upper_bound);
};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_DYNAMICRANDOM_SCHEDPOL_H_
