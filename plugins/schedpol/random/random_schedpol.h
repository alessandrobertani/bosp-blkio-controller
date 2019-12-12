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

#ifndef BBQUE_RANDOM_SCHEDPOL_H_
#define BBQUE_RANDOM_SCHEDPOL_H_

#include "bbque/configuration_manager.h"
#include "bbque/binding_manager.h"
#include "bbque/app/application.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/plugins/plugin.h"
#include "bbque/utils/logging/logger.h"

#include <cstdint>
#include <random>

#define SCHEDULER_POLICY_NAME "random"
#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME
#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;

namespace bbque
{
namespace plugins
{

/**
 * @class RandomSchedPol
 * @brief A dynamic C++ plugin which implements the Random resource
 * scheduler heuristic.
 */
class RandomSchedPol : public SchedulerPolicyIF
{

public:

//----- static plugin interface

	/**
	 *
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 *
	 */
	static int32_t Destroy(void *);

	virtual ~RandomSchedPol();

//----- Scheduler Policy module interface

	char const * Name();

	ExitCode_t Schedule(bbque::System & sv, br::RViewToken_t & rav);

private:

	/**
	 * @brief System logger instance
	 */
	std::unique_ptr<bu::Logger> logger;

	/** Configuration manager instance */
	ConfigurationManager & cm;

	BindingManager & bdm;


	/** The base resource path for the binding step */
	std::string binding_domain;

	/** The type of resource for the binding step */
	br::ResourceType binding_type;

	/**
	 * @brief Random Number Generator engine used for AWM selection
	 */
	std::uniform_int_distribution<uint16_t> dist;

	/**
	 * @brief   The plugins constructor
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 * @param
	 * @return
	 */
	RandomSchedPol();

	/**
	 * @brief Scheduler run intialization
	 *
	 * @return SCHED_DONE on success, SCHED_ERROR on falilure
	 */
	SchedulerPolicyIF::ExitCode_t _Init();

	/**
	 * @brief Randonly select an AWM for the application
	 */
	void ScheduleApp(ba::AppCPtr_t papp);
};

} // namespace plugins

} // namespace bbque

#endif // BBQUE_RANDOM_SCHEDPOL_H_
