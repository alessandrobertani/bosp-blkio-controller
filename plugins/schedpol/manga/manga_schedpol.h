/*
 * Copyright (C) 2016  Politecnico di Milano
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

#ifndef BBQUE_MANGA_SCHEDPOL_H_
#define BBQUE_MANGA_SCHEDPOL_H_

#include <cstdint>
#include <future>
#include <list>
#include <memory>
#include <vector>

#include "bbque/configuration_manager.h"
#include "bbque/plugins/plugin.h"
#include "bbque/plugins/scheduler_policy.h"
#include "bbque/scheduler_manager.h"
#include "bbque/resource_partition_validator.h"

#define SCHEDULER_POLICY_NAME "manga"

#define MODULE_NAMESPACE SCHEDULER_POLICY_NAMESPACE "." SCHEDULER_POLICY_NAME

using bbque::res::RViewToken_t;
using bbque::utils::MetricsCollector;
using bbque::utils::Timer;

// These are the parameters received by the PluginManager on create calls
struct PF_ObjectParams;

namespace bbque
{
namespace plugins
{

class LoggerIF;

/**
 * @class MangaSchedPol
 *
 * Manga scheduler policy registered as a dynamic C++ plugin.
 */
class MangASchedPol: public SchedulerPolicyIF
{

public:

	// :::::::::::::::::::::: Static plugin interface :::::::::::::::::::::::::

	/**
	 * @brief Create the manga plugin
	 */
	static void * Create(PF_ObjectParams *);

	/**
	 * @brief Destroy the manga plugin
	 */
	static int32_t Destroy(void *);


	// :::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

	/**
	 * @brief Destructor
	 */
	virtual ~MangASchedPol();

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

	/** Resource mapping validator for Partition management */
	ResourcePartitionValidator & rmv;

	/** System logger instance */
	std::unique_ptr<bu::Logger> logger;

	std::future<void> fut_tg;

	/** Number of cores per accelerator (and group/cluster) */
	std::map<uint32_t, std::vector<uint32_t>> pe_per_acc;

	/**
	 * @brief Constructor
	 *
	 * Plugins objects could be build only by using the "create" method.
	 * Usually the PluginManager acts as object
	 */
	MangASchedPol();

	/**
	 * @brief Optional initialization member function
	 */
	ExitCode_t _Init();

	ExitCode_t ServeApplicationsWithPriority(int priority) noexcept;

	ExitCode_t ServeApp(ba::AppCPtr_t papp) noexcept;

	ExitCode_t InitTaskGraphMappingOptions(ba::AppCPtr_t papp) noexcept;

	ExitCode_t NextTaskGraphMappingOption(
	        ba::AppCPtr_t papp,
	        std::vector<int> & arch_index,
	        uint32_t task_id = 0);

	ExitCode_t DealWithNoPartitionFound(ba::AppCPtr_t papp) noexcept;

	ExitCode_t ScheduleApplication(ba::AppCPtr_t papp, const std::list<Partition> &partitions) noexcept;

	ExitCode_t SortPartitions(ba::AppCPtr_t papp, const std::list<Partition> &partitions) noexcept;

	ExitCode_t SelectWorkingMode(
	        ba::AppCPtr_t papp, const Partition & selected_partition) noexcept;

	ExitCode_t ReassignWorkingMode(ba::AppCPtr_t papp) noexcept;

};


} // namespace plugins

} // namespace bbque

#endif // BBQUE_MANGA_SCHEDPOL_H_
