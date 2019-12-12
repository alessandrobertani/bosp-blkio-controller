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

#include "random_schedpol.h"

#include "bbque/system.h"
#include "bbque/binding_manager.h"
#include "bbque/app/application.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/resource_path.h"
#include "bbque/resource_accounter.h"
#include "bbque/utils/logging/logger.h"

#include <iostream>
#include <random>

#define NR_ATTEMPTS_MAX  5

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{

RandomSchedPol::RandomSchedPol() :
	cm(ConfigurationManager::GetInstance()),
	bdm(BindingManager::GetInstance()),
	dist(0, 100)
{

	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	logger->Debug("Built RANDOM SchedPol object @%p", (void*)this);
}

RandomSchedPol::~RandomSchedPol()
{
}

//----- Scheduler policy module interface

char const * RandomSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

void RandomSchedPol::ScheduleApp(ba::AppCPtr_t papp)
{
	ba::AwmPtr_t selected_awm;
	int8_t selected_awm_id;
	uint32_t selected_cpu_id;
	uint8_t bd_count;
	int32_t b_refn;
	std::default_random_engine generator;

	assert(papp);

	// Check for a valid binding domain count
	BindingMap_t & bindings(bdm.GetBindingDomains());
	auto cpu_it = bindings.find(br::ResourceType::CPU);
	if (cpu_it == bindings.end()) {
		logger->Warn("ScheduleApp: no CPU bindings (?)");
		return;
	}

	auto & cpu_binding = cpu_it->second;
	bd_count = cpu_binding->resources.size();
	logger->Debug("CPU binding domains : %d", bd_count);
	if (bd_count == 0) {
		logger->Warn("ScheduleApp: CPU bindings not available (?)");
		assert(bd_count != 0);
		return;
	}

	ba::AwmPtrList_t const & awms(papp->WorkingModes());
	logger->Debug("ScheduleApp: application working modes: %d", awms.size());
	std::uniform_int_distribution<int> awm_dist(0, awms.size() - 1);
	std::uniform_int_distribution<int> bd_dist(0, bd_count);
	bool binding_done = false;
	int  nr_attempts  = 0;

	while (!binding_done) {
		// Select a random AWM for this EXC
		selected_awm_id = awm_dist(generator);
		logger->Debug("ScheduleApp: EXC [%s] on AWM [%d of %d]",
		              papp->StrId(), selected_awm_id, awms.size());
		for (auto & pawm : awms) {
			if (pawm->Id() == selected_awm_id)
				selected_awm = pawm;
		}
		assert(selected_awm != nullptr);

		// Bind to a random virtual binding domain
		selected_cpu_id = bd_dist(generator);
		logger->Debug("ScheduleApp: EXC [%s] on binding domain (CPU) <%d of %d>",
		              papp->StrId(), selected_cpu_id, bd_count);

		// Binding to (CPU) id
		b_refn = selected_awm->BindResource(
		                 br::ResourceType::CPU, R_ID_ANY, selected_cpu_id);
		if (b_refn >= 0) {
			// Schedule request
			ApplicationManager & am(ApplicationManager::GetInstance());
			auto ret = am.ScheduleRequest(papp, selected_awm, sched_status_view, b_refn);
			if (ret == ApplicationManager::AM_SUCCESS) {
				logger->Info("ScheduleApp: EXC [%s] on binding domain <%d> done.",
				             papp->StrId(), selected_cpu_id);
				return;
			} else {
				logger->Error("ScheduleApp: EXC [%s] AWM=<%d> CPU=<%d> not schedulable",
				              papp->StrId(), selected_awm_id, selected_cpu_id);
			}
		} else {
			logger->Warn("ScheduleApp: EXC [%s] resource binding to CPU<%d> FAILED ",
			             papp->StrId(), selected_cpu_id);
		}

		// Check if we still have attempts
		logger->Info("ScheduleApp: EXC [%s] scheduling attempt %d out of %d",
		             papp->StrId(), nr_attempts, NR_ATTEMPTS_MAX);

		if (++nr_attempts >= NR_ATTEMPTS_MAX)
			binding_done = true;
	}
}

SchedulerPolicyIF::ExitCode_t
RandomSchedPol::_Init()
{

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
RandomSchedPol::Schedule(bbque::System & sv, br::RViewToken_t &rav)
{
	SchedulerPolicyIF::ExitCode_t result;
	AppsUidMapIt app_it;
	ba::AppCPtr_t papp;

	if (!logger) {
		assert(logger);
		return SCHED_ERROR;
	}

	// Initialize a new resources state view
	result = Init();
	if (result != SCHED_OK)
		return result;

	logger->Info("Random scheduling RUNNING applications...");
	papp = sv.GetFirstRunning(app_it);
	while (papp) {
		ScheduleApp(papp);
		papp = sv.GetNextRunning(app_it);
	}

	logger->Info("Random scheduling READY applications...");
	papp = sv.GetFirstReady(app_it);
	while (papp) {
		ScheduleApp(papp);
		papp = sv.GetNextReady(app_it);
	}

	// Pass back to the SchedulerManager a reference to the scheduled view
	rav = sched_status_view;
	return SCHED_DONE;
}

//----- static plugin interface

void * RandomSchedPol::Create(PF_ObjectParams *)
{
	return new RandomSchedPol();
}

int32_t RandomSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (RandomSchedPol *)plugin;
	return 0;
}

} // namesapce plugins

} // namespace bque
