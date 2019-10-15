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

#include "bbque/app/application.h"

#ifdef CONFIG_TARGET_ANDROID
# include <stdint.h>
# include <math.h>
#else
# include <cstdint>
# include <cmath>
#endif
#include <limits>

#include "bbque/application_manager.h"
#include "bbque/app/working_mode.h"
#include "bbque/app/recipe.h"
#include "bbque/plugin_manager.h"
#include "bbque/resource_accounter.h"
#include "bbque/res/resource_path.h"
#include "bbque/res/resource_assignment.h"

#define BBQUE_APP_CHANNEL_ID_LENGTH    11

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bp = bbque::plugins;

namespace bbque
{
namespace app
{


// Compare two working mode values.
// This is used to sort the list of enabled working modes.
bool AwmValueLesser(const AwmPtr_t & wm1, const AwmPtr_t & wm2)
{
	return wm1->Value() < wm2->Value();
}

bool AwmIdLesser(const AwmPtr_t & wm1, const AwmPtr_t & wm2)
{
	return wm1->Id() < wm2->Id();
}

Application::Application(std::string const & _name,
                         AppPid_t _pid,
                         uint8_t _exc_id,
                         RTLIB_ProgrammingLanguage_t lang,
                         bool container):
	exc_id(_exc_id),
	language(lang),
	container(container)
{
	name = _name;
	pid  = _pid;
	type = Schedulable::Type::ADAPTIVE;

	// Init the working modes vector
	awms.recipe_vect.resize(MAX_NUM_AWM);

	// Get a logger
	logger = bu::Logger::GetLogger(APPLICATION_NAMESPACE);
	assert(logger);

	// Format the application string identifier for logging purpose
	snprintf(str_id, SCHEDULABLE_ID_MAX_LEN, "%05d:%5s:%02d",
	         Pid(), Name().substr(0, 5).c_str(), ExcId());

#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	// Task-graph file paths
	std::string app_str(std::string(str_id).substr(0, 6) + Name());
	tg_path.assign(BBQUE_TG_FILE_PREFIX + app_str);
	std::replace(app_str.begin(), app_str.end(), ':', '.');
	tg_sem_name.assign("/" + app_str);
	logger->Info("Task-graph serial file: <%s> sem: <%s>",
	             tg_path.c_str(), tg_sem_name.c_str());
#endif // CONFIG_BBQUE_TG_PROG_MODEL

	// Initialized scheduling state
	schedule.state        = NEW;
	schedule.preSyncState = NEW;
	schedule.syncState    = SYNC_NONE;
	logger->Info("Built new EXC [%s]", StrId());
}

Application::~Application()
{
	logger->Debug("Destroying EXC [%s]", StrId());
	awms.recipe_vect.clear();
	awms.enabled_list.clear();
	rsrc_constraints.clear();
#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	if (tg_sem != nullptr)
		sem_close(tg_sem);
#endif // CONFIG_BBQUE_TG_PROG_MODEL
}

void Application::SetPriority(AppPrio_t _prio)
{
	bbque::ApplicationManager &am(bbque::ApplicationManager::GetInstance());
	// If _prio value is greater then the lowest priority
	// (maximum integer value) it is trimmed to the last one.
	priority = std::min(_prio, am.LowestPriority());
}

void Application::InitWorkingModes(AppPtr_t & papp)
{
	// Get the working modes from recipe and init the vector size
	AwmPtrVect_t const & recipe_awms(recipe->WorkingModesAll());

	// Init AWM range attributes (for AWM constraints)
	awms.max_id   = recipe_awms.size() - 1;
	awms.low_id   = 0;
	awms.upp_id   = awms.max_id;
	awms.curr_inv = false;
	awms.enabled_bset.set();
	logger->Debug("InitWorkingModes: max ID = %d", awms.max_id);

	// Init AWM lists
	for (int i = 0; i <= awms.max_id; ++i) {
		// Copy the working mode and set the owner (current Application)
		AwmPtr_t app_awm = std::make_shared<WorkingMode>(*recipe_awms[i]);
		app_awm->SetOwner(papp);
		awms.recipe_vect[app_awm->Id()] = app_awm;

		// Do not insert the hidden AWMs into the enabled list
		if (app_awm->Hidden()) {
			logger->Debug("InitWorkingModes: skipping hidden AWM %d", app_awm->Id());
			continue;
		}

		// Valid (not hidden) AWM: Insert it into the list
		awms.enabled_list.push_back(app_awm);
	}

	// Sort the enabled list by "value"
	awms.enabled_list.sort(AwmValueLesser);
	logger->Info("InitWorkingModes: %d enabled AWMs", awms.enabled_list.size());
}

void Application::InitResourceConstraints()
{
	// For each static constraint on a resource make an assertion
	for (auto & constr_entry : recipe->ConstraintsAll()) {
		ResourcePathPtr_t const & rsrc_path(constr_entry.first);
		ConstrPtr_t const & rsrc_constr(constr_entry.second);

		// Lower bound
		if (rsrc_constr->lower > 0)
			SetResourceConstraint(
			        rsrc_path, br::ResourceConstraint::LOWER_BOUND, rsrc_constr->lower);
		// Upper bound
		if (rsrc_constr->upper > 0)
			SetResourceConstraint(
			        rsrc_path, br::ResourceConstraint::UPPER_BOUND, rsrc_constr->upper);
	}

	logger->Debug("InitResourceConstraints: %d resource constraints", rsrc_constraints.size());
}

Application::ExitCode_t
Application::SetRecipe(RecipePtr_t & _recipe, AppPtr_t & papp)
{
	// Safety check on recipe object
	if (!_recipe) {
		logger->Error("SetRecipe: null recipe object");
		return APP_RECP_NULL;
	}

	// Set the recipe and the static priority
	recipe   = _recipe;
	priority = recipe->GetPriority();

	// Set working modes
	InitWorkingModes(papp);
	logger->Info("SetRecipe: %d working modes", awms.enabled_list.size());

	// Set (optional) resource constraints
	InitResourceConstraints();
	logger->Info("SetRecipe: %d constraints", rsrc_constraints.size());

	// Specific attributes (i.e. plugin specific)
	plugin_data = PluginDataMap_t(recipe->plugin_data);
	logger->Info("SetRecipe: %d plugin-specific attributes", plugin_data.size());

	return APP_SUCCESS;
}

AwmPtr_t Application::GetWorkingMode(uint8_t wmId)
{
	for (auto & awm : awms.enabled_list) {
		if (awm->Id() == wmId)
			return awm;
	}

	return nullptr;
}


/*******************************************************************************
 *  EXC Optimization
 ******************************************************************************/

void Application::SetNextAWM(AwmPtr_t awm)
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	schedule.next_awm = awm;
	awms.curr_inv = false;
	logger->Debug("SetNewAWM: [%s] next_awm = %d",
	              StrId(), schedule.next_awm->Id());
}

/*******************************************************************************
 *  EXC Synchronization
 ******************************************************************************/

Application::ExitCode_t Application::SyncCommit()
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	Application::ExitCode_t ret;

	assert(_State() == SYNC);
	if (_State() != SYNC) {
		logger->Error("SyncCommit: [%s] wrong state <%s>", StrId(), StateStr(_State()));
		return APP_STATUS_NOT_EXP;
	}

	// Synchronization state
	switch(_SyncState()) {
	case STARTING:
	case RECONF:
	case MIGREC:
	case MIGRATE:
		// Reset GoalGap whether the Application has been scheduled into a AWM
		// having a value higher than the previous one
		if (schedule.awm &&
		    (schedule.awm->Value() < schedule.next_awm->Value())) {
			logger->Debug("Resetting GoalGap (%d%c) on [%s]",
			              rt_prof.ggap_percent, '%', StrId());
			rt_prof.ggap_percent = 0;
		}

		ret = SetState(RUNNING);
		if (ret != APP_SUCCESS) {
			logger->Error("SyncCommit: status transition failed");
			return ret;
		}
		logger->Debug("Scheduling count: %" PRIu64 "", schedule.count);
		break;

	case BLOCKED:
		SetState(READY);
		break;

	case DISABLED:
		SetState(FINISHED);
		schedule.awm.reset();
		schedule.next_awm.reset();
		break;

	default:
		logger->Crit("SyncCommit: synchronization failed for EXC [%s]"
		             "(Error: invalid synchronization state)");
		assert(_SyncState() < Application::SYNC_NONE);
		return APP_ABORT;
	}

	logger->Info("SyncCommit: synchronization completed [%s, %d:%s]",
	             StrId(), _State(), StateStr(_State()));

	return APP_SUCCESS;
}



Application::ExitCode_t Application::SyncContinue()
{
	std::unique_lock<std::recursive_mutex> state_ul(schedule.mtx);
	// Reset next AWM (only current must be set)
	schedule.next_awm.reset();
	schedule.awm->IncSchedulingCount();
	return APP_SUCCESS;
}

/*******************************************************************************
 *  EXC Constraints Management
 ******************************************************************************/

Application::ExitCode_t Application::SetWorkingModeConstraint(
        RTLIB_Constraint & constraint)
{
	// Get a lock. The assertion may invalidate the current AWM.
	std::unique_lock<std::recursive_mutex> schedule_ul(schedule.mtx);
	ExitCode_t result = APP_ABORT;

	logger->Debug("SetConstraint, AWM_ID: %d, OP: %s, TYPE: %d",
	              constraint.awm,
	              constraint.operation ? "ADD" : "REMOVE",
	              constraint.type);

	// Check the working mode ID validity
	if (constraint.awm > awms.max_id)
		return APP_WM_NOT_FOUND;

	// Dispatch the constraint assertion
	switch (constraint.operation) {
	case CONSTRAINT_REMOVE:
		result = RemoveWorkingModeConstraint(constraint);
		break;
	case CONSTRAINT_ADD:
		result = AddWorkingModeConstraint(constraint);
		break;
	default:
		logger->Error("SetConstraint (AWMs): operation not supported");
		return result;
	}

	// If there are no changes in the enabled list return
	if (result == APP_WM_ENAB_UNCHANGED) {
		logger->Debug("SetConstraint (AWMs): nothing to change");
		return APP_SUCCESS;
	}

	// Rebuild the list of enabled working modes
	RebuildEnabledWorkingModes();

	logger->Debug("SetConstraint (AWMs): %d total working modes",
	              awms.recipe_vect.size());
	logger->Debug("SetConstraint (AWMs): %d enabled working modes",
	              awms.enabled_list.size());

	DB(DumpValidAWMs());

	return APP_SUCCESS;
}

void Application::DumpValidAWMs() const
{
	uint8_t len = 0;
	char buff[256];

	for (int j = 0; j <= awms.max_id; ++j) {
		if (awms.enabled_bset.test(j))
			len += snprintf(buff + len, 265 - len, "%d,", j);
	}
	// Remove leading comma
	buff[len - 1] = 0;
	logger->Info("SetConstraint (AWMs): enabled map/list = {%s}", buff);
}

Application::ExitCode_t Application::AddWorkingModeConstraint(
        RTLIB_Constraint & constraint)
{
	// Check the type of constraint to set
	switch (constraint.type) {
	case RTLIB_ConstraintType::LOWER_BOUND:
		// Return immediately if there is nothing to change
		if (constraint.awm == awms.low_id)
			return APP_WM_ENAB_UNCHANGED;

		// If the lower > upper: upper = max
		if (constraint.awm > awms.upp_id)
			awms.upp_id = awms.max_id;

		// Update the bitmap of the enabled working modes
		SetWorkingModesLowerBound(constraint);
		return APP_WM_ENAB_CHANGED;

	case RTLIB_ConstraintType::UPPER_BOUND:
		// Return immediately if there is nothing to change
		if (constraint.awm == awms.upp_id)
			return APP_WM_ENAB_UNCHANGED;

		// If the upper < lower: lower = begin
		if (constraint.awm < awms.low_id)
			awms.low_id = 0;

		// Update the bitmap of the enabled working modes
		SetWorkingModesUpperBound(constraint);
		return APP_WM_ENAB_CHANGED;

	case RTLIB_ConstraintType::EXACT_VALUE:
		// If the AWM is set yet, return
		if (awms.enabled_bset.test(constraint.awm))
			return APP_WM_ENAB_UNCHANGED;

		// Mark the corresponding bit in the enabled map
		awms.enabled_bset.set(constraint.awm);
		logger->Debug("SetConstraint (AWMs): set exact value AWM {%d}",
		              constraint.awm);
		return APP_WM_ENAB_CHANGED;
	}

	return APP_WM_ENAB_UNCHANGED;
}

void Application::SetWorkingModesLowerBound(RTLIB_Constraint & constraint)
{
	uint8_t hb = std::max(constraint.awm, awms.low_id);

	// Disable all the AWMs lower than the new lower bound and if the
	// previous lower bound was greater than the new one, enable the AWMs
	// lower than it
	for (int i = hb; i >= 0; --i) {
		if (i < constraint.awm)
			awms.enabled_bset.reset(i);
		else
			awms.enabled_bset.set(i);
	}

	// Save the new lower bound
	awms.low_id = constraint.awm;
	logger->Debug("SetConstraint (AWMs): set lower bound AWM {%d}",
	              awms.low_id);
}

void Application::SetWorkingModesUpperBound(RTLIB_Constraint & constraint)
{
	uint8_t lb = std::min(constraint.awm, awms.upp_id);

	// Disable all the AWMs greater than the new upper bound and if the
	// previous upper bound was lower than the new one, enable the AWMs
	// greater than it
	for (int i = lb; i <= awms.max_id; ++i) {
		if (i > constraint.awm)
			awms.enabled_bset.reset(i);
		else
			awms.enabled_bset.set(i);
	}

	// Save the new upper bound
	awms.upp_id = constraint.awm;
	logger->Debug("SetConstraint (AWMs): set upper bound AWM {%d}",
	              awms.upp_id);
}

Application::ExitCode_t Application::RemoveWorkingModeConstraint(
        RTLIB_Constraint & constraint)
{
	// Check the type of constraint to remove
	switch (constraint.type) {
	case RTLIB_ConstraintType::LOWER_BOUND:
		// Set the bit related to the AWM below the lower bound
		ClearWorkingModesLowerBound();
		return APP_WM_ENAB_CHANGED;

	case RTLIB_ConstraintType::UPPER_BOUND:
		// Set the bit related to the AWM above the upper bound
		ClearWorkingModesUpperBound();
		return APP_WM_ENAB_CHANGED;

	case RTLIB_ConstraintType::EXACT_VALUE:
		// If the AWM is not set yet, return
		if (!awms.enabled_bset.test(constraint.awm))
			return APP_WM_ENAB_UNCHANGED;

		// Reset the bit related to the AWM
		awms.enabled_bset.reset(constraint.awm);
		return APP_WM_ENAB_CHANGED;
	}

	return APP_WM_ENAB_UNCHANGED;
}

void Application::ClearWorkingModesLowerBound()
{
	// Set all the bit previously unset
	for (int i = awms.low_id - 1; i >= 0; --i)
		awms.enabled_bset.set(i);

	// Reset the lower bound
	logger->Debug("SetConstraint (AWMs): cleared lower bound AWM {%d}", awms.low_id);
	awms.low_id = 0;
}

void Application::ClearWorkingModesUpperBound()
{
	// Set all the bit previously unset
	for (int i = awms.upp_id + 1; i <= awms.max_id; ++i)
		awms.enabled_bset.set(i);
	// Reset the upperbound
	logger->Debug("SetConstraint (AWMs): cleared upper bound AWM {%d}", awms.upp_id);
	awms.upp_id = awms.max_id;
}

void Application::ClearWorkingModeConstraints()
{
	// Reset range bounds
	awms.low_id = 0;
	awms.upp_id = awms.max_id;

	// Rebuild the list of enabled working modes
	RebuildEnabledWorkingModes();

	logger->Debug("ClearConstraint (AWMs): %d total working modes", awms.recipe_vect.size());
	logger->Debug("ClearConstraint (AWMs): %d enabled working modes", awms.enabled_list.size());
}

void Application::RebuildEnabledWorkingModes()
{
	// Clear the list
	awms.enabled_list.clear();

	// Rebuild the enabled working modes list
	for (int j = 0; j <= awms.max_id; ++j) {
		// Skip if the related bit of the map is not set, or one of the
		// resource usage required violates a resource constraint, or
		// the AWM is hidden according to the current status of the hardware
		// resources
		if ((!awms.enabled_bset.test(j))
		    || (UsageOutOfBounds(awms.recipe_vect[j]))
		    || awms.recipe_vect[j]->Hidden())
			continue;

		// Insert the working mode
		awms.enabled_list.push_back(awms.recipe_vect[j]);
	}

	// Check current AWM and re-order the list
	FinalizeEnabledWorkingModes();
}

void Application::FinalizeEnabledWorkingModes()
{
	// Check if the current AWM has been invalidated
	if (schedule.awm &&
	    !awms.enabled_bset.test(schedule.awm->Id())) {
		logger->Warn("WorkingMode constraints: current AWM (""%s"" ID:%d)"
		             " invalidated.", schedule.awm->Name().c_str(),
		             schedule.awm->Id());
		awms.curr_inv = true;
	}

	// Sort by working mode "value
	awms.enabled_list.sort(AwmValueLesser);
}

/************************** Resource Constraints ****************************/

bool Application::UsageOutOfBounds(AwmPtr_t & awm)
{
	// Check if there are constraints on the resource assignments
	for (auto const & rsrc_req_entry : awm->ResourceRequests()) {
		auto & rsrc_path(rsrc_req_entry.first);
		auto & rsrc_amount(rsrc_req_entry.second);

		auto rsrc_constr_it = rsrc_constraints.find(rsrc_path);
		if (rsrc_constr_it == rsrc_constraints.end())
			continue;

		// Check if the usage value is out of the constraint bounds
		br::ResourceAssignmentPtr_t const & r_assign(rsrc_amount);
		if ((r_assign->GetAmount() < rsrc_constr_it->second->lower) ||
		    (r_assign->GetAmount() > rsrc_constr_it->second->upper))
			return true;
	}

	return false;
}

void Application::UpdateEnabledWorkingModes()
{
	// Remove AWMs violating resources constraints
	awms.enabled_list.remove_if([this](AwmPtr_t & awm) {
		return UsageOutOfBounds(awm);
	});
	// Check current AWM and re-order the list
	FinalizeEnabledWorkingModes();
	logger->Debug("UpdateEnabledWorkingModes:", awms.enabled_list.size());
}

Application::ExitCode_t Application::SetResourceConstraint(
        ResourcePathPtr_t r_path,
        br::ResourceConstraint::BoundType_t b_type,
        uint64_t _value)
{

	// Check the existance of the resource
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	if (!ra.ExistResource(r_path)) {
		logger->Warn("SetResourceConstraint: %s not found", r_path->ToString().c_str());
		return APP_RSRC_NOT_FOUND;
	}

	// Init a new constraint (if do not exist yet)
	auto const it_con(rsrc_constraints.find(r_path));
	if (it_con == rsrc_constraints.end()) {
		rsrc_constraints.emplace(r_path, std::make_shared<br::ResourceConstraint>());
	}

	// Set the constraint bound value (if value exists overwrite it)
	switch(b_type) {
	case br::ResourceConstraint::LOWER_BOUND:
		rsrc_constraints[r_path]->lower = _value;
		if (rsrc_constraints[r_path]->upper < _value)
			rsrc_constraints[r_path]->upper =
			        std::numeric_limits<uint64_t>::max();

		logger->Debug("SetConstraint (Resources): Set on {%s} LB = %" PRIu64,
		              r_path->ToString().c_str(), _value);
		break;

	case br::ResourceConstraint::UPPER_BOUND:
		rsrc_constraints[r_path]->upper = _value;
		if (rsrc_constraints[r_path]->lower > _value)
			rsrc_constraints[r_path]->lower = 0;

		logger->Debug("SetConstraint (Resources): Set on {%s} UB = %" PRIu64,
		              r_path->ToString().c_str(), _value);
		break;
	}

	// Check if there are some AWMs to disable
	UpdateEnabledWorkingModes();

	return APP_SUCCESS;
}

Application::ExitCode_t Application::ClearResourceConstraint(
        ResourcePathPtr_t r_path,
        br::ResourceConstraint::BoundType_t b_type)
{
	// Lookup the constraint by resource pathname
	auto const it_con(rsrc_constraints.find(r_path));
	if (it_con == rsrc_constraints.end()) {
		logger->Warn("ClearConstraint (Resources): failed due to unknown resource path");
		return APP_CONS_NOT_FOUND;
	}

	// Reset the constraint
	switch (b_type) {
	case br::ResourceConstraint::LOWER_BOUND :
		it_con->second->lower = 0;
		if (it_con->second->upper == std::numeric_limits<uint64_t>::max())
			rsrc_constraints.erase(it_con);
		break;

	case br::ResourceConstraint::UPPER_BOUND :
		it_con->second->upper = std::numeric_limits<uint64_t>::max();
		if (it_con->second->lower == 0)
			rsrc_constraints.erase(it_con);
		break;
	}

	// Check if there are some awms to enable
	UpdateEnabledWorkingModes();

	return APP_SUCCESS;
}


uint64_t Application::GetResourceRequestStat(
        std::string const & rsrc_path,
        ResourceUsageStatType_t stats_type)
{
	uint64_t min_val  = UINT64_MAX;
	uint64_t max_val  = 0;
	uint64_t total = 0;

	for (auto const & awm : awms.enabled_list) {                // AWMs (enabled)
		for (auto const & r_entry : awm->ResourceRequests()) {     // Resources
			ResourcePathPtr_t const & curr_path(r_entry.first);
			uint64_t curr_amount = (r_entry.second)->GetAmount();

			// Is current resource the one required?
			br::ResourcePath key_path(rsrc_path);
			if (key_path.Compare(*(curr_path.get())) == br::ResourcePath::NOT_EQUAL)
				continue;

			// Cumulate the resource usage and update min or max
			total += curr_amount;
			curr_amount < min_val ? min_val = curr_amount : min_val;
			curr_amount > max_val ? max_val = curr_amount : max_val;
		}
	}

	// Return the resource usage statistics required
	switch (stats_type) {
	case RU_STAT_MIN:
		return min_val;
	case RU_STAT_AVG:
		return total / awms.enabled_list.size();
	case RU_STAT_MAX:
		return max_val;
	};

	return 0;
}


/*******************************************************************************
 *  Task-graph Management
 ******************************************************************************/

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

Application::ExitCode_t Application::LoadTaskGraph()
{
	logger->Debug("LoadTaskGraph: [%s] getting task-graph information...", StrId());

	if (tg_sem == nullptr) {
		logger->Info("LoadTaskGraph: [%s] loading [path:%s sem=%s]...",
		             StrId(), tg_path.c_str(), tg_sem_name.c_str());
		tg_sem = sem_open(tg_sem_name.c_str(), O_RDWR);
		if (errno != 0) {
			logger->Crit("LoadTaskGraph: [%s] error while opening semaphore [errno=%d]: %s",
			             StrId(), errno, strerror(errno));
			return APP_TG_SEM_ERROR;
		}
	}

	if (tg_sem == SEM_FAILED) {
		logger->Warn("LoadTaskGraph: [%s] task-graph not available on the application side",
		             StrId());
		return APP_TG_SEM_ERROR;
	}

	logger->Debug("LoadTaskGraph: [%s] waiting on task-graph semaphore...", StrId());
	if (sem_wait(tg_sem) != 0) {
		logger->Error("LoadTaskGraph: [%s] wait on semaphore failed [errno=%d]: %s",
		              StrId(), errno, strerror(errno));
		return APP_TG_SEM_ERROR;
	}

	std::ifstream ifs(tg_path);
	if (!ifs.good()) {
		logger->Warn("LoadTaskGraph: [%s] task-graph not provided", StrId());
		sem_post(tg_sem);
		return APP_TG_FILE_ERROR;
	}

	logger->Debug("LoadTaskGraph: [%s] building the task-graph...", StrId());
	if (task_graph == nullptr) {
		task_graph = std::make_shared<TaskGraph>();
		logger->Info("LoadTaskGraph: [%s] loading from scratch...", StrId());
	}

	logger->Debug("LoadTaskGraph: [%s] task-graph de-serialization...", StrId());
	try {
		boost::archive::text_iarchive ia(ifs);
		ia >> *task_graph;
		sem_post(tg_sem);
		logger->Info("LoadTaskGraph: [%s] task-graph loaded", StrId());
		return APP_SUCCESS;
	} catch(std::exception & ex) {
		logger->Error("LoadTaskGraph: [%s] exception [%s]",
		              StrId(), ex.what());
		sem_post(tg_sem);
		return APP_TG_FILE_ERROR;
	}
}


void Application::UpdateTaskGraph()
{
	if (tg_sem == nullptr) {
		logger->Warn("UpdateTaskGraph: [%s] task-graph is null", StrId());
		return;
	}

	sem_wait(tg_sem);
	try {
		std::ofstream ofs(tg_path);
		boost::archive::text_oarchive oa(ofs);
		oa << *task_graph;
	} catch(std::exception & ex) {
		logger->Error("UpdateTaskGraph: [%s] exception [%s]",
		              StrId(), ex.what());
	}
	sem_post(tg_sem);

	logger->Debug("UpdateTaskGraph: [%s] task-graph sent back to "
	              "programming library", StrId());
}

#endif //CONFIG_BBQUE_TG_PROG_MODEL

} // namespace app

} // namespace bbque
