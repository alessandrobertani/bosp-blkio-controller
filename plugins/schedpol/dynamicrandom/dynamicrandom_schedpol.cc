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

namespace bbque { namespace plugins {

// :::::::::::::::::::::: Static plugin interface ::::::::::::::::::::::::::::

void * DynamicRandomSchedPol::Create(PF_ObjectParams *) {
	return new DynamicRandomSchedPol();
}

int32_t DynamicRandomSchedPol::Destroy(void * plugin) {
	if (!plugin)
		return -1;
	delete (DynamicRandomSchedPol *)plugin;
	return 0;
}

// ::::::::::::::::::::: Scheduler policy module interface :::::::::::::::::::

char const * DynamicRandomSchedPol::Name() {
	return SCHEDULER_POLICY_NAME;
}

DynamicRandomSchedPol::DynamicRandomSchedPol():
		cm(ConfigurationManager::GetInstance()),
		ra(ResourceAccounter::GetInstance()) {
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
		("DynamicRandomParameters.distribution", po::value<int> ((int*)&distribution)->default_value(1), "distribution")
		("DynamicRandomParameters.parameter1", po::value<float> (&parameter1)->default_value(-1), "parameter1")
		("DynamicRandomParameters.parameter2", po::value<float> (&parameter2)->default_value(-1), "parameter2")
	;

	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);
}


DynamicRandomSchedPol::~DynamicRandomSchedPol() {

}


SchedulerPolicyIF::ExitCode_t DynamicRandomSchedPol::Init() {
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


	// Available resources 
	nbr_av_res = sys->ResourceAvailable("sys.cpu.pe");
	if(nbr_av_res == 0)
	{
		logger->Fatal("Init: no available resources");
		return SCHED_ERROR;
	}

	nbr_app = sys->ApplicationsCount(bbque::app::ApplicationStatusIF::State_t::RUNNING) 
			+ sys->ApplicationsCount(bbque::app::ApplicationStatusIF::State_t::READY);



	return SCHED_OK;
}


SchedulerPolicyIF::ExitCode_t
DynamicRandomSchedPol::Schedule(
		System & system,
		RViewToken_t & status_view) {
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


#define LOW_BOUND_PERCENTAGE 10
#define UPPER_BOUND_PERCENTAGE 50

SchedulerPolicyIF::ExitCode_t
DynamicRandomSchedPol::AssignWorkingModeAndBind(bbque::app::AppCPtr_t papp) {

	// Define the upper bound for the application
	uint16_t upper_bound = nbr_av_res * (float(UPPER_BOUND_PERCENTAGE)/float(100));
	uint16_t lower_bound = nbr_av_res * (float(LOW_BOUND_PERCENTAGE)/float(100));

	uint16_t selected_nb_res = GenerateRandomValue(lower_bound, upper_bound);


	// Build a new working mode featuring assigned resources
	ba::AwmPtr_t pawm = papp->CurrentAWM();
	if (pawm == nullptr) {
		pawm = std::make_shared<ba::WorkingMode>(
				papp->WorkingModes().size(),"Run-time", 1, papp);
	}

	// Define the resource path
	std::string resource_path_str("sys0.cpu.pe");
	auto resource_path = ra.GetPath(resource_path_str);

	// Resource request addition
	pawm->AddResourceRequest(resource_path_str, selected_nb_res, br::ResourceAssignment::Policy::BALANCED);
	int32_t ref_num = -1;
	

	// The ResourceBitset object is used for the processing elements binding	
	br::ResourceBitset pes;
	uint16_t pe_count = 0;

	// We set processing elements ids to the ResourceBitset object	
	for (auto & pe_id: pe_ids) {
		pes.Set(pe_id);
		resource_path = ra.GetPath(resource_path_str);
		logger->Info("AssignWorkingModeAndBind: binding refn: %d", ref_num);
		++pe_count;
		if (pe_count * 100 >= selected_nb_res) break;
	}
	ref_num = pawm->BindResource(resource_path, pes, ref_num);


	auto ret = papp->ScheduleRequest(pawm, sched_status_view, ref_num);
	if (ret != ba::ApplicationStatusIF::APP_SUCCESS) {
		logger->Error("AssignWorkingModeAndBind: schedule request failed for [%d]", papp->StrId());
		return SCHED_ERROR;
	}
	else
	{
		nbr_av_res -= selected_nb_res;
	}

	return SCHED_OK;	
}



uint16_t DynamicRandomSchedPol::GenerateRandomValue(uint16_t lower_bound, uint16_t upper_bound)
{
	// Generate the random value
	std::random_device seed;
	std::default_random_engine generator(seed());
	uint16_t selected_nb_res;


	if(distribution==UNIFORM)
	{

		std::uniform_int_distribution<int> res_dist(lower_bound, upper_bound);
		selected_nb_res = res_dist(generator);

	}
	else if(distribution==NORMAL)
	{

		std::normal_distribution<double> res_dist(parameter1, parameter2);
		do
		{
			selected_nb_res = res_dist(generator);
		} while( selected_nb_res > upper_bound || selected_nb_res < lower_bound );

	}
	else if(distribution==POISSON)
	{
	
		std::poisson_distribution<int> res_dist( parameter1 );
		do
		{
			selected_nb_res = res_dist(generator);
		} while( selected_nb_res > upper_bound || selected_nb_res < lower_bound );

	}
	else if(distribution==BINOMIAL)
	{
		
		// p has to be less than 1
		if(parameter1>1) parameter1=0.5;

		std::binomial_distribution<int> res_dist( upper_bound, parameter1 );
		do
		{
			selected_nb_res = res_dist(generator);
		} while( selected_nb_res > upper_bound || selected_nb_res < lower_bound );

	}
	else if(distribution==EXPONENTIAL)
	{

		std::exponential_distribution<double> res_dist( parameter1 );
		do
		{
			selected_nb_res = res_dist(generator);
		} while( selected_nb_res > upper_bound || selected_nb_res < lower_bound );
	
	}
	else
	{
		selected_nb_res = lower_bound/2 + upper_bound/2;
	}

	logger->Info("GenerateRandomValue: random value in interval [%d - %d] generated : %d", lower_bound, upper_bound, selected_nb_res);

	return selected_nb_res;
	
}


} // namespace plugins

} // namespace bbque
