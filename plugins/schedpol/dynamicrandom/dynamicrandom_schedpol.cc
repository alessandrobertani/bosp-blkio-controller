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

#include "dynamicrandom_schedpol.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <unistd.h>

#include "bbque/modules_factory.h"
#include "bbque/utils/logging/logger.h"

#include "bbque/app/working_mode.h"
#include "bbque/app/application_status.h"
#include "bbque/res/binder.h"

#define MODULE_CONFIG SCHEDULER_POLICY_CONFIG "." SCHEDULER_POLICY_NAME

namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque {
namespace plugins {

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * DynamicRandomSchedPol::Create(PF_ObjectParams *)
{
	return new DynamicRandomSchedPol();
}

int32_t DynamicRandomSchedPol::Destroy(void * plugin)
{
	if (!plugin)
		return -1;
	delete (DynamicRandomSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * DynamicRandomSchedPol::Name()
{
	return SCHEDULER_POLICY_NAME;
}

DynamicRandomSchedPol::DynamicRandomSchedPol() :
    cm(ConfigurationManager::GetInstance()),
    ra(ResourceAccounter::GetInstance())
{
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	if (logger)
		logger->Info("dynamicrandom: Built a new dynamic object[%p]", this);
	else
		fprintf(stderr,
			FI("dynamicrandom: Built new dynamic object [%p]\n"), (void *)this);


	// Parse the distribution and parameters for computing random values
	po::options_description opts_desc("Scheduling policy parameters");
	opts_desc.add_options()
		("DynamicRandomSchedPol.distribution", po::value<int> ((int*)&distribution)->default_value(1), "distribution")
		("DynamicRandomSchedPol.perc_lb", po::value<int> (&lower_bound_perc)->default_value(10), "lowerBound")
		("DynamicRandomSchedPol.perc_ub", po::value<int> (&upper_bound_perc)->default_value(100), "upperBound")
		("DynamicRandomSchedPol.param1", po::value<float> (&param1)->default_value(-1), "parameter1")
		("DynamicRandomSchedPol.param2", po::value<float> (&param2)->default_value(-1), "parameter2")
		;

	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);
}

DynamicRandomSchedPol::~DynamicRandomSchedPol() { }

SchedulerPolicyIF::ExitCode_t DynamicRandomSchedPol::Init()
{
	// Build a string path for the resource state view
	std::string token_path(MODULE_NAMESPACE);
	++status_view_count;
	token_path.append(std::to_string(status_view_count));
	logger->Debug("Init: Require a new resource state view [%s]",
		token_path.c_str());

	// Get a fresh resource status view
	ResourceAccounterStatusIF::ExitCode_t ra_result =
		ra.GetView(token_path, sched_status_view);
	if (ra_result != ResourceAccounterStatusIF::RA_SUCCESS) {
		logger->Fatal("Init: cannot get a resource state view");
		return SCHED_ERROR_VIEW;
	}
	logger->Debug("Init: resources state view token: %ld", sched_status_view);

	// Processing elements IDs
	auto & resource_types = sys->ResourceTypes();
	auto const & r_pe_ids_entry = resource_types.find(br::ResourceType::PROC_ELEMENT);
	pe_ids = r_pe_ids_entry->second;
	logger->Debug("Init: %d processing elements available", pe_ids.size());

	// Available CPU resources
	nbr_av_res = sys->ResourceAvailable("sys.cpu.pe", sched_status_view);
	if (nbr_av_res == 0) {
		logger->Fatal("Init: no available resources");
		return SCHED_ERROR;
	}

	nbr_app = sys->ApplicationsCount(bbque::app::ApplicationStatusIF::State_t::RUNNING)
		+ sys->ApplicationsCount(bbque::app::ApplicationStatusIF::State_t::READY);

	return SCHED_OK;
}

SchedulerPolicyIF::ExitCode_t
DynamicRandomSchedPol::Schedule(System & system,
				RViewToken_t & status_view)
{
	SchedulerPolicyIF::ExitCode_t result = SCHED_DONE;

	// Class providing query functions for applications and resources
	sys = &system;
	Init();

	bbque::app::AppCPtr_t papp;
	AppsUidMapIt app_it;

	// Ready applications
	papp = sys->GetFirstReady(app_it);
	for (; papp; papp = sys->GetNextReady(app_it)) {
		AssignWorkingModeAndBind(papp);
	}

	// Running applications
	papp = sys->GetFirstRunning(app_it);
	for (; papp; papp = sys->GetNextRunning(app_it)) {
		papp->CurrentAWM()->ClearResourceRequests();
		AssignWorkingModeAndBind(papp);
	}

	// Return the new resource status view according to the new resource
	// allocation performed
	status_view = sched_status_view;
	return result;
}

SchedulerPolicyIF::ExitCode_t
DynamicRandomSchedPol::AssignWorkingModeAndBind(bbque::app::AppCPtr_t papp)
{
	// Define the upper bound for the application
	uint16_t upper_bound = nbr_av_res * (float(lower_bound_perc) / float(100));
	uint16_t lower_bound = nbr_av_res * (float(upper_bound_perc) / float(100));

	// We check if upper bound is greater than lower bound, otherwise we swap the values
	if (upper_bound < lower_bound) {
		uint16_t tmp = lower_bound;
		lower_bound = upper_bound;
		upper_bound = tmp;
	}

	// We check if the defined bounds are correct
	if (upper_bound > nbr_av_res) upper_bound = nbr_av_res;
	if (lower_bound < 1) lower_bound = 1;

	// Generate a random amount of CPU resource
	uint16_t next_cpu_quota = GenerateRandomValue(lower_bound, upper_bound);

	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
			papp->WorkingModes().size(), "Run-time", 1, papp);
	}

	// Define the resource path
	std::string resource_path_str("sys.cpu.pe");

	// Resource request addition
	pawm->AddResourceRequest(resource_path_str, next_cpu_quota, br::ResourceAssignment::Policy::BALANCED);

	// The ResourceBitset object is used for the processing elements binding
	br::ResourceBitset pes;
	uint16_t pe_count = 0;

	// We set processing elements ids to the ResourceBitset object
	for (auto & pe_id : pe_ids) {
		pes.Set(pe_id);
		logger->Debug("AssignWorkingModeAndBind: processing_element: %d", pe_id);
		++pe_count;
		if (pe_count * 100 >= next_cpu_quota) break;
	}

	logger->Debug("AssignWorkingModeAndBind: processing elements set: %s",
		pes.ToString().c_str());

	auto resource_path = ra.GetPath(resource_path_str);
	int32_t ref_num = -1;
	ref_num = pawm->BindResource(resource_path, pes, ref_num);
	logger->Debug("AssignWorkingModeAndBind: reference number: %d", ref_num);

	// Schedule request validation
	ApplicationManager & am(ApplicationManager::GetInstance());
	auto am_ret = am.ScheduleRequest(papp, pawm, sched_status_view, ref_num);
	if (am_ret != ApplicationManager::AM_SUCCESS) {
		logger->Error("AssignWorkingModeAndBind: schedule request failed for [%d]",
			papp->StrId());
		return SCHED_ERROR;
	}
	else {
		nbr_av_res -= next_cpu_quota;
	}

	return SCHED_OK;
}

uint16_t DynamicRandomSchedPol::GenerateRandomValue(uint16_t lower_bound, uint16_t upper_bound)
{
	// Generate the random value
	std::random_device seed;
	std::default_random_engine generator(seed());
	uint16_t next_cpu_quota;

	switch (distribution) {

	case Distribution::UNIFORM:
	{
		logger->Debug("GenerateRandomValue: UNIFORM distribution");
		std::uniform_int_distribution<int> uni_dist(lower_bound, upper_bound);
		next_cpu_quota = uni_dist(generator);
		break;
	}
	case Distribution::NORMAL:
	{
		logger->Debug("GenerateRandomValue: NORMAL distribution");
		// Mean rate
		if (param1 < 0)
			param1 = lower_bound / 2 + upper_bound / 2;
		// Standard deviation
		if (param2 < 0)
			param2 = 1;
		std::normal_distribution<double> norm_dist(param1, param2);
		do {
			next_cpu_quota = norm_dist(generator);
		}
		while (next_cpu_quota > upper_bound || next_cpu_quota < lower_bound);
		break;
	}
	case Distribution::POISSON:
	{
		logger->Debug("GenerateRandomValue: POISSON distribution");
		// Mean rate
		if (param1 < 0)
			param1 = lower_bound / 2 + upper_bound / 2;

		std::poisson_distribution<int> pois_dist( param1 );
		do {
			next_cpu_quota = pois_dist(generator);
		}
		while (next_cpu_quota > upper_bound || next_cpu_quota < lower_bound);
		break;
	}
	case Distribution::BINOMIAL:
	{
		logger->Debug("GenerateRandomValue: BINOMIAL distribution");
		// Probability of success (must: p < 1)
		if (param1 > 1 || param1 < 0)
			param1 = 0.5;

		std::binomial_distribution<int> bin_dist(upper_bound, param1);
		do {
			next_cpu_quota = bin_dist(generator);
		}
		while (next_cpu_quota > upper_bound || next_cpu_quota < lower_bound);
		break;
	}
	case Distribution::EXPONENTIAL:
	{
		logger->Debug("GenerateRandomValue: EXPONENTIAL distribution");
		// Lambda
		std::exponential_distribution<double> exp_dist(param1);
		do {
			next_cpu_quota = exp_dist(generator);
		}
		while (next_cpu_quota > upper_bound || next_cpu_quota < lower_bound);
		break;
	}
	default:
		logger->Warn("GenerateRandomValue: invalid distribution value (%d)",
			distribution);
		next_cpu_quota = lower_bound / 2 + upper_bound / 2;
	}

	logger->Debug("GenerateRandomValue: random value in interval [%d - %d] generated : %d",
		lower_bound, upper_bound, next_cpu_quota);

	return next_cpu_quota;
}


} // namespace plugins

} // namespace bbque
