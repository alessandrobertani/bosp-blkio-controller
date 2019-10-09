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

#include <cmath>

#include "bbque/application_manager.h"

#include "bbque/config.h"
#include "bbque/configuration_manager.h"
#include "bbque/modules_factory.h"
#include "bbque/plugin_manager.h"
#include "bbque/platform_manager.h"
#include "bbque/scheduler_manager.h"
#include "bbque/app/application.h"
#include "bbque/app/working_mode.h"
#include "bbque/app/recipe.h"
#include "bbque/plugins/recipe_loader.h"
#include "bbque/resource_accounter.h"
#include "bbque/resource_manager.h"
#include "bbque/cpp11/chrono.h"
#include "bbque/resource_partition_validator.h"
#include "bbque/utils/assert.h"
#include "bbque/utils/schedlog.h"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#define APPLICATION_MANAGER_NAMESPACE "bq.am"
#define MODULE_CONFIG                 "ApplicationManager"
#define MODULE_NAMESPACE APPLICATION_MANAGER_NAMESPACE

#define AM_TABLE_TITLE "|                    Applications status                                  |"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bp = bbque::plugins;
namespace po = boost::program_options;

using std::chrono::milliseconds;

namespace bbque
{

ApplicationManager & ApplicationManager::GetInstance()
{
	static ApplicationManager instance;
	return instance;
}


ApplicationManager::ApplicationManager() :
	cm(CommandManager::GetInstance()),
	plm(PlatformManager::GetInstance()),
	cleanup_dfr("am.cln", std::bind(&ApplicationManager::Cleanup, this))
{

	// Get a logger
	logger = bu::Logger::GetLogger(APPLICATION_MANAGER_NAMESPACE);
	assert(logger);

	//  Get the recipe loader instance
	std::string rloader_plugin_id(RECIPE_LOADER_NAMESPACE "." BBQUE_RLOADER_DEFAULT);
	rloader = ModulesFactory::GetModule<bp::RecipeLoaderIF>(rloader_plugin_id);
	if (!rloader) {
		logger->Fatal("Missing RecipeLoader plugin");
		assert(rloader);
	}

	// Debug logging
	logger->Debug("Priority levels: %d, (O = highest)", BBQUE_APP_PRIO_LEVELS);

	// Register commands
#define CMD_WIPE_RECP ".recipes_wipe"
	cm.RegisterCommand(
	        MODULE_NAMESPACE CMD_WIPE_RECP,
	        static_cast<CommandHandler*>(this),
	        "Wipe out all the recipes");

#define CMD_CONTAINER_ADD ".container_add"
	cm.RegisterCommand(
	        MODULE_NAMESPACE CMD_CONTAINER_ADD,
	        static_cast<CommandHandler*>(this),
	        "Add a new EXC Container");

#define CMD_CONTAINER_DEL ".container_del"
	cm.RegisterCommand(MODULE_NAMESPACE CMD_CONTAINER_DEL,
	                   static_cast<CommandHandler*>(this),
	                   "Remove an existing EXC Container");
}

int ApplicationManager::CommandsCb(int argc, char *argv[])
{
	ResourceManager &rm(ResourceManager::GetInstance());
	uint8_t cmd_offset = ::strlen(MODULE_NAMESPACE) + 1;
	uint32_t pid, prio;
	AppPtr_t papp;

	logger->Debug("Processing command [%s]", argv[0] + cmd_offset);

	cmd_offset = ::strlen(MODULE_NAMESPACE) + sizeof("recipes_");
	switch (argv[0][cmd_offset]) {
	case 'w': // Recipes wiping
		// recipes_wipe
		logger->Debug("CommandsCb: # recipes = %d", recipes.size());
		logger->Info("CommandsCb: wiping out all the recipes...");
		recipes.clear();
		logger->Debug("CommandsCb: # recipes = %d", recipes.size());
		return 0;
	}

	cmd_offset = ::strlen(MODULE_NAMESPACE) + sizeof("container_");
	switch (argv[0][cmd_offset]) {
	case 'a': // Container add
		// container_add <name> <pid> <recipe> <prio>
		if (strcmp(argv[0], MODULE_NAMESPACE CMD_CONTAINER_ADD))
			break;

		if (argc < 5) {
			logger->Error("Missing params for [container_add] command");
			break;
		}

		pid  = atoi(argv[2]);
		prio = atoi(argv[4]);

		logger->Notice("EXC [%s:%d] adding container, using recipe=<%s> prio=<%d>",
		               argv[1], pid, argv[3], prio);
		papp = CreateEXC(argv[1], pid, 0, argv[3], RTLIB_LANG_CPP, prio, false, true);
		if (!papp) {
			logger->Warn("EXC [%s:%d] container creation FAILED", argv[1], pid);
			break;
		}

		EnableEXC(papp);
		rm.NotifyEvent(ResourceManager::EXC_START);
		return 0;

	case 'd': // Container del
		// container_del <pid>
		if (strcmp(argv[0], MODULE_NAMESPACE CMD_CONTAINER_DEL))
			break;

		if (argc < 2) {
			logger->Error("Missing params for [container_del] command");
			break;
		}

		logger->Notice("Removing EXC container...");

		pid  = atoi(argv[1]);
		papp = GetApplication(pid, 0);
		if (!papp) {
			logger->Warn("Container EXC for PID [%d] not FOUND", pid);
			break;
		}

		DisableEXC(papp, true);
		rm.NotifyEvent(ResourceManager::EXC_STOP);
		return 0;
	}

	logger->Error("Command [%s] not supported by this module", argv[0]);
	return -1;
}


ApplicationManager::~ApplicationManager()
{

	// Clear the sync vector
	logger->Debug("Clearing SYNC vector...");
	for (uint8_t state = 0;
	     state < Application::SYNC_STATE_COUNT; ++state) {
		sync_vec[state].clear();
		sync_ret[state].clear();
	}

	// Clear the status vector
	logger->Debug("Clearing STATUS vector...");
	for (uint8_t state = 0;
	     state < Application::STATE_COUNT; ++state) {
		status_vec[state].clear();
		status_ret[state].clear();
	}

	// Clear the priority vector
	logger->Debug("Clearing PRIO vector...");
	for (uint8_t level = 0; level < BBQUE_APP_PRIO_LEVELS; ++level) {
		prio_vec[level].clear();
		prio_ret[level].clear();
	}

	// Clear the APPs map
	logger->Debug("Clearing APPs map...");
	apps.clear();

	// Clear the applications map
	logger->Debug("Clearing UIDs map...");
	uids.clear();
	uids_ret.clear();

	// Clear the recipes
	logger->Debug("Clearing RECIPES...");
	recipes.clear();

}

bp::RecipeLoaderIF::ExitCode_t ApplicationManager::LoadRecipe(
        std::string const & recipe_name,
        RecipePtr_t & recipe,
        bool weak_load)
{
	std::unique_lock<std::mutex> recipes_ul(recipes_mtx);
	bp::RecipeLoaderIF::ExitCode_t result;
	logger->Debug("LoadRecipe: loading <%s>...", recipe_name.c_str());

	assert(rloader);
	if (!rloader) {
		logger->Error("LoadRecipe: missing recipe loader module)");
		return bp::RecipeLoaderIF::RL_ABORTED;
	}

	//---  Checking for previously loaded recipe
	std::map<std::string, RecipePtr_t>::iterator it(
	        recipes.find(recipe_name));
	if (it != recipes.end()) {
		// Return a previously loaded recipe
		logger->Debug("LoadRecipe: recipe <%s> already loaded",
		              recipe_name.c_str());
		recipe = (*it).second;
		return bp::RecipeLoaderIF::RL_SUCCESS;
	}

	//---  Loading a new recipe
	logger->Info("LoadRecipe: loading new recipe <%s>...", recipe_name.c_str());
	recipe = std::make_shared<ba::Recipe>(recipe_name);
	result = rloader->LoadRecipe(recipe_name, recipe);

	// If a weak load has done, but the we didn't want it,
	// or a parsing error happened: then return an empty recipe
	if (result == bp::RecipeLoaderIF::RL_WEAK_LOAD && !weak_load) {
		logger->Error("LoadRecipe: loading <%s> FAILED: weak load not accepted)",
		              recipe_name.c_str());
		return result;

	}
	// On all other case just WEAK_LOAD and SUCCESS are acceptable
	if (result >= bp::RecipeLoaderIF::RL_FAILED ) {
		logger->Error("LoadRecipe: loading <%s> FAILED: error code %d",
		              recipe_name.c_str(), result);
		return result;
	}
	logger->Debug("LoadRecipe: <%s> load COMPLETED", recipe_name.c_str());

	// Validate the recipe
	recipe->Validate();

	// Place the new recipe object in the map, and return it
	recipes[recipe_name] = recipe;

	return bp::RecipeLoaderIF::RL_SUCCESS;

}


/*******************************************************************************
  *     Queued Access Functions
  *****************************************************************************/

void
ApplicationManager::UpdateIterators(AppsUidMapItRetainer_t & ret,
                                    AppPtr_t papp)
{
	AppsUidMapItRetainer_t::iterator it;
	AppsUidMapIt* pati;

	logger->Debug("Checking [%d] iterators...", ret.size());
	// Lookup for iterators on the specified map which pointes to the
	// specified apps
	for (it = ret.begin(); it != ret.end(); ++it) {
		pati = (*it);

		// Ignore iterators not pointing to the application of interest
		if (pati->it->first != papp->Uid())
			continue;

		// Update the iterator position one step backward
		logger->Debug("~ Updating iterator [@%p => %d]", pati->it, papp->Uid());

		// Move the iterator forward
		pati->Update();
	}

}

AppPtr_t ApplicationManager::GetFirst(AppsUidMapIt & ait)
{
	std::unique_lock<std::recursive_mutex> uids_ul(uids_mtx);
	AppPtr_t papp;

	ait.Init(uids, uids_ret);
	if (ait.End())
		return AppPtr_t();

	// Return the pointer application
	papp = ait.Get();

	// Add iterator to the retainers list
	ait.Retain();
	logger->Debug("GetFirst: > ADD retained UIDs iterator [@%p => %d]",
	              &(ait.it), papp->Uid());

	return papp;
}

AppPtr_t ApplicationManager::GetNext(AppsUidMapIt & ait)
{
	std::unique_lock<std::recursive_mutex> uids_ul(uids_mtx);
	AppPtr_t papp;

	ait++;
	if (ait.End()) {
		// Release the iterator retainer
		ait.Release();
		logger->Debug("GetNext: < DEL retained UIDs iterator [@%p => %d]",
		              &(ait.it));
		return AppPtr_t();
	}

	papp = ait.Get();
	return papp;
}

AppPtr_t ApplicationManager::GetFirst(AppPrio_t prio,
                                      AppsUidMapIt & ait)
{
	assert(prio < BBQUE_APP_PRIO_LEVELS);
	std::unique_lock<std::mutex> prio_ul(prio_mtx[prio]);
	AppPtr_t papp;

	ait.Init(prio_vec[prio], prio_ret[prio]);
	if (ait.End())
		return AppPtr_t();

	papp = ait.Get();

	// Add iterator to the retainers list
	ait.Retain();
	logger->Debug("GetFirst: > ADD retained PRIO[%d] iterator [@%p => %d]",
	              prio, &(ait.it), papp->Uid());

	return papp;
}

AppPtr_t ApplicationManager::GetNext(AppPrio_t prio,
                                     AppsUidMapIt & ait)
{
	assert(prio < BBQUE_APP_PRIO_LEVELS);
	std::unique_lock<std::mutex> prio_ul(prio_mtx[prio]);
	AppPtr_t papp;

	ait++;
	if (ait.End()) {
		// Release the iterator retainer
		ait.Release();
		logger->Debug("GetNext: < DEL retained PRIO[%d] iterator [@%p]",
		              prio, &(ait.it));
		return AppPtr_t();
	}

	papp = ait.Get();
	return papp;
}

AppPtr_t ApplicationManager::GetFirst(ApplicationStatusIF::State_t state,
                                      AppsUidMapIt & ait)
{
	assert(state < Application::STATE_COUNT);
	std::unique_lock<std::mutex> status_ul(status_mtx[state]);
	AppPtr_t papp;

	ait.Init(status_vec[state], status_ret[state]);
	if (ait.End())
		return AppPtr_t();

	papp = ait.Get();

	// Add iterator to the retainers list
	ait.Retain();
	logger->Debug("GetFirst: > ADD retained STATUS[%s] iterator [@%p => %d]",
	              Application::StateStr(state), &(ait.it), papp->Uid());

	return papp;
}

AppPtr_t ApplicationManager::GetNext(ApplicationStatusIF::State_t state,
                                     AppsUidMapIt & ait)
{
	assert(state < Application::STATE_COUNT);
	std::unique_lock<std::mutex> status_ul(status_mtx[state]);
	AppPtr_t papp;

	ait++;
	if (ait.End()) {
		// Release the iterator retainer
		ait.Release();
		logger->Debug("GetNext: < DEL retained STATUS[%s] iterator [@%p]",
		              Application::StateStr(state), &(ait.it));
		return AppPtr_t();
	}

	papp = ait.Get();
	return papp;
}

AppPtr_t ApplicationManager::GetFirst(ApplicationStatusIF::SyncState_t state,
                                      AppsUidMapIt & ait)
{
	assert(state < Application::SYNC_STATE_COUNT);
	std::unique_lock<std::mutex> sync_ul(sync_mtx[state]);
	AppPtr_t papp;

	ait.Init(sync_vec[state], sync_ret[state]);
	if (ait.End())
		return AppPtr_t();

	papp = ait.Get();

	// Add iterator to the retainers list
	ait.Retain();
	logger->Debug("GetFirst: > ADD retained SYNCS[%s] iterator [@%p => %d]",
	              Application::SyncStateStr(state), &(ait.it),
	              papp->Uid());

	return papp;
}

AppPtr_t ApplicationManager::GetNext(ApplicationStatusIF::SyncState_t state,
                                     AppsUidMapIt & ait)
{
	assert(state < Application::SYNC_STATE_COUNT);
	std::unique_lock<std::mutex> sync_ul(sync_mtx[state]);
	AppPtr_t papp;

	ait++;
	if (ait.End()) {
		// Release the iterator retainer
		ait.Release();
		logger->Debug("GetNext: < DEL retained SYNCS[%s] iterator [@%p]",
		              Application::SyncStateStr(state), &(ait.it));
		return AppPtr_t();
	}

	papp = ait.Get();
	return papp;
}

bool ApplicationManager::HasApplications (
        AppPrio_t prio)
{
	assert(prio < BBQUE_APP_PRIO_LEVELS);
	return !(prio_vec[prio].empty());
}

bool ApplicationManager::HasApplications (ApplicationStatusIF::State_t state)
{
	assert(state < Application::STATE_COUNT);
	return !(status_vec[state].empty());
}

bool ApplicationManager::HasApplications (ApplicationStatusIF::SyncState_t state)
{
	assert(state < Application::SYNC_STATE_COUNT);
	return !(sync_vec[state].empty());
}

bool ApplicationManager::HasApplications (RTLIB_ProgrammingLanguage_t lang)
{
	assert(lang < RTLIB_LANG_COUNT);
	return !(lang_vec[lang].empty());
}

uint16_t ApplicationManager::AppsCount() const
{
	uint16_t count = 0;
	for (uint16_t prio = 0; prio < BBQUE_APP_PRIO_LEVELS; ++prio)
		count +=  prio_vec[prio].size();
	return count;
}

uint16_t ApplicationManager::AppsCount (AppPrio_t prio) const
{
	assert(prio < BBQUE_APP_PRIO_LEVELS);
	return prio_vec[prio].size();
}

uint16_t ApplicationManager::AppsCount (ApplicationStatusIF::State_t state) const
{
	assert(state < Application::STATE_COUNT);
	return status_vec[state].size();
}

uint16_t ApplicationManager::AppsCount (ApplicationStatusIF::SyncState_t state) const
{
	assert(state < Application::SYNC_STATE_COUNT);
	return sync_vec[state].size();
}

uint16_t ApplicationManager::AppsCount (RTLIB_ProgrammingLanguage_t lang) const
{
	assert(lang < RTLIB_LANG_COUNT);
	return lang_vec[lang].size();
}

AppPtr_t ApplicationManager::HighestPrio(ApplicationStatusIF::State_t state)
{
	AppPtr_t papp, papp_hp;
	AppsUidMapIt apps_it;
	assert(state < Application::STATE_COUNT);
	logger->Debug("HighestPrio: looking for highest prio [%s] apps...",
	              ApplicationStatusIF::StateStr(state));

	if (!HasApplications(state)) {
		logger->Debug("HighestPrio: no applications in [%s]",
		              ApplicationStatusIF::StateStr(state));
		return AppPtr_t();
	}

	papp_hp = papp = GetFirst(state, apps_it);
	while (papp) {
		papp = GetNext(state, apps_it);
		if (papp &&
		    (papp->Priority() > papp_hp->Priority()))
			papp_hp = papp;
	}

	logger->Debug("HighestPrio: highest [%s] prio [%d] app [%s]",
	              ApplicationStatusIF::StateStr(state),
	              papp_hp->Priority(),
	              papp_hp->StrId());

	return papp_hp;

}

AppPtr_t ApplicationManager::HighestPrio(
        ApplicationStatusIF::SyncState_t syncState)
{
	AppPtr_t papp, papp_hp;
	AppsUidMapIt apps_it;
	assert(syncState < Application::SYNC_STATE_COUNT);
	logger->Debug("HighestPrio: looking for highest prio [%s] apps...",
	              ApplicationStatusIF::SyncStateStr(syncState));

	if (!HasApplications(syncState)) {
		logger->Debug("HighestPrio: no applications in [%s]",
		              ApplicationStatusIF::SyncStateStr(syncState));
		return AppPtr_t();
	}

	papp_hp = papp = GetFirst(syncState, apps_it);
	while (papp) {
		papp = GetNext(syncState, apps_it);
		if (papp &&
		    (papp->Priority() > papp_hp->Priority()))
			papp_hp = papp;
	}

	logger->Debug("HighestPrio: highest [%s] prio [%d] app [%s]",
	              ApplicationStatusIF::SyncStateStr(syncState),
	              papp_hp->Priority(),
	              papp_hp->StrId());

	return papp_hp;
}

/*******************************************************************************
 *  Get EXC handlers
 ******************************************************************************/

AppPtr_t const ApplicationManager::GetApplication(AppUid_t uid)
{
	std::unique_lock<std::recursive_mutex> uids_ul(uids_mtx);
	AppsUidMap_t::const_iterator it = uids.find(uid);

	logger->Debug("GetApplication: looking for UID [%07d]...", uid);

	//----- Find the required EXC
	if (it == uids.end()) {
		DB(logger->Debug("GetApplication: lookup for EXC [%05d:*:%02d] (UID: %07d) FAILED"
		                 " (Error: UID not registered)",
		                 Application::Uid2Pid(uid),
		                 Application::Uid2Eid(uid),
		                 uid));
		return AppPtr_t();
	}

	AppPtr_t papp = (*it).second;
	logger->Debug("GetApplication: found UID [%07d] => [%s]", uid, papp->StrId());

	return papp;
}

AppPtr_t const
ApplicationManager::GetApplication(AppPid_t pid, uint8_t exc_id)
{
	logger->Debug("Looking for EXC [%05d:*:%02d]...", pid, exc_id);
	return GetApplication(Application::Uid(pid, exc_id));
}

/*******************************************************************************
 *  EXC state handling
 ******************************************************************************/

#define DOUBLE_LOCK(cur, next)\
	if (cur > next) {\
		currState_ul.lock();\
		nextState_ul.lock();\
	} else {\
		nextState_ul.lock();\
		currState_ul.lock();\
	}\


void ApplicationManager::PrintStatusQ(bool verbose) const {

	// Report on current status queue
	char report[] = "StateQ: [NEW: 000, RDY: 000, SYC: 000, RUN: 000, FIN: 000]";

	if (verbose) {
		for (uint8_t i = 0; i < Application::STATE_COUNT; ++i) {
			::sprintf(report+14+i*10, "%03u",
				AppsCount(static_cast<ApplicationStatusIF::State_t>(i)));
			report[17+i*10] = ',';
		}
		report[17+10*(Application::STATE_COUNT-1)] = ']';
		logger->Info(report);
	} else {
		DB(
		for (uint8_t i = 0; i < Application::STATE_COUNT; ++i) {
			::sprintf(report+14+i*10, "%03u",
				AppsCount(static_cast<ApplicationStatusIF::State_t>(i)));
			report[17+i*10] = ',';
		}
		report[17+10*(Application::STATE_COUNT-1)] = ']';
		logger->Debug(report);
		);
	}

}

void ApplicationManager::PrintSyncQ(bool verbose) const {

	// Report on current status queue
	char report[] = "SyncQ:  [STA: 000, REC: 000, M/R: 000, MIG: 000, BLK: 000, DIS: 000]";

	if (verbose) {
		for (uint8_t i = 0; i < Application::SYNC_STATE_COUNT; ++i) {
			::sprintf(report+14+i*10, "%03u",
				AppsCount(static_cast<ApplicationStatusIF::SyncState_t>(i)));
			report[17+i*10] = ',';
		}
		report[17+10*(Application::SYNC_STATE_COUNT-1)] = ']';
		logger->Info(report);
	} else {
		DB(
		for (uint8_t i = 0; i < Application::SYNC_STATE_COUNT; ++i) {
			::sprintf(report+14+i*10, "%03u",
				AppsCount(static_cast<ApplicationStatusIF::SyncState_t>(i)));
			report[17+i*10] = ',';
		}
		report[17+10*(Application::SYNC_STATE_COUNT-1)] = ']';
		logger->Debug(report);
		);
	}

}

ApplicationManager::ExitCode_t
ApplicationManager::UpdateStatusMaps(AppPtr_t papp,
                                     Application::State_t prev, Application::State_t next)
{
	PrintStatusQ();
	assert(papp);
	if (prev == next) {
		logger->Debug("UpdateStatusMaps: %s => %s (nothing to do)",
		              papp->StateStr(prev), papp->StateStr(next));
		return AM_EXC_STATUS_CHANGE_NONE;
	}

	std::unique_lock<std::mutex> currState_ul(
	        status_mtx[prev], std::defer_lock);
	std::unique_lock<std::mutex> nextState_ul(
	        status_mtx[next], std::defer_lock);
	std::lock(currState_ul, nextState_ul);

	// Retrieve the runtime map from the status vector
	AppsUidMap_t *curr_state_map = &(status_vec[prev]);
	AppsUidMap_t *next_state_map = &(status_vec[next]);
	assert(curr_state_map != next_state_map);
	logger->Debug("UpdateStatusMap: [%s] moving %s => %s (sync=%s)",
	              papp->StrId(),
	              papp->StateStr(prev),
	              papp->StateStr(next),
	              papp->SyncStateStr(papp->SyncState()));

	// Move it from the current to the next status map
	// FIXME: maybe we could avoid to enqueue FINISHED EXCs
	next_state_map->insert(UidsMapEntry_t(papp->Uid(), papp));
	UpdateIterators(status_ret[prev], papp);
	curr_state_map->erase(papp->Uid());

	PrintStatusQ();
	return AM_SUCCESS;
}


void ApplicationManager::PrintStatus(bool verbose)
{
	AppsUidMapIt app_it;
	AppPtr_t papp;
	char line[80];
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV1);
	PRINT_NOTICE_IF_VERBOSE(verbose, AM_TABLE_TITLE);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV2);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_HEAD);
	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV2);

	papp = GetFirst(app_it);
	while (papp) {
		utils::SchedLog::BuildSchedStateLine(papp, line, 80);
		PRINT_NOTICE_IF_VERBOSE(verbose, line);
		papp = GetNext(app_it);
	}

	PRINT_NOTICE_IF_VERBOSE(verbose, HM_TABLE_DIV1);
}


ApplicationManager::ExitCode_t
ApplicationManager::ChangeEXCState(
        AppPtr_t papp,
        app::Schedulable::State_t next_state,
        app::Schedulable::SyncState_t next_sync)
{

	logger->Debug("ChangeEXCState: [%s] state transition [%d:%s => %d:%s]",
	              papp->StrId(),
	              papp->State(), Application::StateStr(papp->State()),
	              next_state, Application::StateStr(next_state));
	auto curr_state = papp->State();
	auto curr_sync  = papp->SyncState();

	// Is there an actual change?
	if ((curr_state == next_state) && (curr_sync == next_sync)) {
		logger->Debug("ChangeEXCState: nothing to do here");
		return AM_SUCCESS;
	}

	// Update application status
	auto ret = papp->SetState(next_state, next_sync);
	if (ret != Application::APP_SUCCESS) {
		logger->Error("ChangeEXCState: transition not allowed [%d:%s => %d:%s]",
		              papp->StrId(),
		              curr_state, Application::StateStr(curr_state),
		              next_state, Application::StateStr(next_state));
		return AM_EXC_STATUS_CHANGE_FAILED;
	}

	// Update the sync status maps (if it is the case)
	logger->Debug("ChangeEXCState: [%s] updating sync queue? [%d:%s => %d:%s]",
	              papp->StrId(),
	              curr_state, Application::StateStr(curr_state),
	              papp->State(), Application::StateStr(papp->State()));
	if (curr_state == Application::SYNC) {
		RemoveFromSyncMap(papp, curr_sync);
	}
	if (next_state == Application::SYNC) {
		AddToSyncMap(papp);
	}

	// Update the stable status maps
	if (curr_state != papp->State()) {
		logger->Debug("ChangeEXCState: [%s] updating state queue [%d:%s => %d:%s]",
		              papp->StrId(),
		              curr_state, Application::StateStr(curr_state),
		              papp->State(), Application::StateStr(papp->State()));
		return UpdateStatusMaps(papp, curr_state, next_state);
	}

	return AM_SUCCESS;
}


int ApplicationManager::UpdateRuntimeProfiles()
{
	ApplicationProxy & ap(ApplicationProxy::GetInstance());
	AppsUidMapIt app_it;
	ba::AppPtr_t papp;
	int count = 0;

	// For each running application, update the (OpenCL) runtime profile
	// information
	papp = GetFirst(ba::Application::RUNNING, app_it);
	for (; papp; papp = GetNext(ba::Application::RUNNING, app_it)) {
		logger->Debug("NotifyNewState: updating OpenCL profile for [%s]...",
		              papp->StrId());
		ap.Prof_GetRuntimeData(papp);
	}

	return count;
}

/*******************************************************************************
 *  EXC Creation
 ******************************************************************************/

AppPtr_t ApplicationManager::CreateEXC(
        std::string const & _name, AppPid_t _pid, uint8_t _exc_id,
        std::string const & _rcp_name,
        RTLIB_ProgrammingLanguage_t _lang,
        app::AppPrio_t _prio,
        bool _weak_load,
        bool container)
{
	std::unique_lock<std::mutex> lang_ul(lang_mtx[_lang], std::defer_lock);
	std::unique_lock<std::mutex> prio_ul(prio_mtx[_prio], std::defer_lock);
	std::unique_lock<std::recursive_mutex> uids_ul(uids_mtx, std::defer_lock);
	std::unique_lock<std::mutex> status_ul(status_mtx[Application::NEW], std::defer_lock);
	std::unique_lock<std::mutex> apps_ul(apps_mtx, std::defer_lock);

	// Create a new descriptor
	AppPtr_t papp = std::make_shared<ba::Application>(_name, _pid, _exc_id, _lang, container);
	if (papp == nullptr) {
		logger->Error("CreateEXC: [%s] FAILED during object construction");
		return papp;
	}
	papp->SetPriority(_prio);
	logger->Info("CreateEXC: [%s], prio[%d]", papp->StrId(), papp->Priority());

	// Load the required recipe
	RecipePtr_t rcp_ptr;
	auto rcp_result = LoadRecipe(_rcp_name, rcp_ptr, _weak_load);
	if (rcp_result != bp::RecipeLoaderIF::RL_SUCCESS) {
		logger->Error("CreateEXC: [%s] FAILED "
		              "(Error while loading recipe [%s])",
		              papp->StrId(), _rcp_name.c_str());
		return nullptr;
	}

	// Set the recipe into the Application/EXC
	auto app_result = papp->SetRecipe(rcp_ptr, papp);
	if (app_result != Application::APP_SUCCESS) {
		logger->Error("CreateEXC: [%s] FAILED "
		              "(Error: recipe rejected by application descriptor)",
		              papp->StrId());
		return nullptr;
	}

	// NOTE: a deadlock condition exists if other maps are locked before
	// this one. Up to now, this seem to be the only code path where we
	// need a double locking.
	// Should this lock be keep till the exit?!?
	apps_ul.lock();
	// Save application descriptors
	apps.emplace(papp->Pid(), papp);
	logger->Debug("CreateEXC: [%s] inserted in applications map", papp->StrId());

	uids_ul.lock();
	uids.emplace(papp->Uid(), papp);
	uids_ul.unlock();
	logger->Debug("CreateEXC: [%s] inserted in UIDs map", papp->StrId());

	// Priority vector
	prio_ul.lock();
	prio_vec[papp->Priority()].emplace(papp->Uid(), papp);
	prio_ul.unlock();
	logger->Debug("CreateEXC: [%s] inserted in priority map", papp->StrId());

	// Status vector (all new EXC are initially disabled)
	assert(papp->State() == Application::NEW);
	status_ul.lock();
	status_vec[papp->State()].emplace(papp->Uid(), papp);
	status_ul.unlock();
	logger->Debug("CreateEXC: [%s] inserted in status map", papp->StrId());

	// Language vector
	lang_ul.lock();
	lang_vec[papp->Language()].emplace(papp->Uid(), papp);
	lang_ul.unlock();
	logger->Info("CreateEXC: [%s] CREATED", papp->StrId());

	return papp;
}


/*******************************************************************************
 *  EXC Destruction
 ******************************************************************************/

ApplicationManager::ExitCode_t
ApplicationManager::PriorityRemove(AppPtr_t papp)
{
	logger->Debug("PriorityRemove: releasing [%s] from PRIORITY map...", papp->StrId());
	std::unique_lock<std::mutex> prio_ul(prio_mtx[papp->Priority()]);
	UpdateIterators(prio_ret[papp->Priority()], papp);
	prio_vec[papp->Priority()].erase(papp->Uid());
	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::StatusRemove(AppPtr_t papp)
{
	logger->Debug("StatusRemove: releasing [%s] from STATUS map...", papp->StrId());
	std::unique_lock<std::mutex> status_ul(status_mtx[papp->State()]);
	UpdateIterators(status_ret[papp->State()], papp);
	status_vec[papp->State()].erase(papp->Uid());
	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::LangRemove(AppPtr_t papp)
{
	logger->Debug("LangRemove: releasing [%s] from LANGUAGE map...", papp->StrId());
	std::unique_lock<std::mutex> lang_ul(lang_mtx[papp->Language()]);
	UpdateIterators(lang_ret[papp->Language()], papp);
	lang_vec[papp->Language()].erase(papp->Uid());
	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::AppsRemove(AppPtr_t papp)
{
	std::unique_lock<std::mutex> apps_ul(apps_mtx);
	std::pair<AppsMap_t::iterator, AppsMap_t::iterator> range;
	AppsMap_t::iterator it;

	logger->Debug("AppsRemove: [%s] removing from APPs map...", papp->StrId());
	range = apps.equal_range(papp->Pid());
	it = range.first;
	while (it != range.second &&
	       ((*it).second)->ExcId() != papp->ExcId()) {
		++it;
	}

	// Application/EXC could have been already removed...
	if (it == range.second) {
		logger->Debug("AppsRemove: [%s] not found: alredy removed?",
		              papp->StrId());
		return AM_SUCCESS;
	}
	apps.erase(it);

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::CleanupEXC(AppPtr_t papp)
{
	std::unique_lock<std::recursive_mutex> uids_ul(uids_mtx, std::defer_lock);
	PlatformManager::ExitCode_t pp_result;
	ExitCode_t am_result;

	// Remove application description from its status map
	am_result = StatusRemove(papp);
	if (am_result != AM_SUCCESS) {
		logger->Error("CleanupEXC: [%s] cleanup FAILED: status map error", papp->StrId());
		return am_result;
	}

	// Remove platform specific data
	if (papp->ScheduleCount() > 0) {
		logger->Debug("CleanupEXC: [%s] missing platform data", papp->StrId());
		pp_result = plm.Release(papp);
		if (pp_result != PlatformManager::PLATFORM_OK) {
			logger->Error("CleanupEXC: [%s] cleanup FAILED: platform data error", papp->StrId());
			return AM_PLAT_PROXY_ERROR;
		}
	}

	logger->Debug("CleanupEXC: [%s] cleaning up from UIDs map...", papp->StrId());

	// Remove application descriptor from UIDs map
	uids_ul.lock();
	UpdateIterators(uids_ret, papp);
	uids.erase(papp->Uid());
	uids_ul.unlock();

	PrintStatusQ();
	PrintSyncQ();
	logger->Info("CleanupEXC: [%s] cleaned up", papp->StrId());
	return AM_SUCCESS;
}

void ApplicationManager::Cleanup()
{
	AppsUidMapIt apps_it;
	logger->Debug("Cleanup EXCs...");
	// Loop on FINISHED apps to release all resources
	AppPtr_t papp(GetFirst(ApplicationStatusIF::FINISHED, apps_it));
	while (papp) {
		CleanupEXC(papp);
		papp = GetNext(ApplicationStatusIF::FINISHED, apps_it);
	}

}

ApplicationManager::ExitCode_t
ApplicationManager::DestroyEXC(AppPtr_t papp)
{
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	ExitCode_t result;
	logger->Debug("DestroyEXC: destroying descriptor for [%s]...", papp->StrId());

	// Change status to FINISHED
	if (papp->State() != app::Schedulable::FINISHED)
		ChangeEXCState(papp, app::Schedulable::FINISHED);

	// Remove execution context form priority and apps maps
	result = PriorityRemove(papp);
	if (result != AM_SUCCESS)
		return result;

	result = LangRemove(papp);
	if (result != AM_SUCCESS)
		return result;

	result = AppsRemove(papp);
	if (result != AM_SUCCESS)
		return result;

	// This is a simple cleanup triggering policy based on the
	// number of applications FINISHED
	uint32_t timeout = 0;
	timeout = 100 - (10 * (AppsCount(Schedulable::FINISHED) % 5));
	cleanup_dfr.Schedule(milliseconds(timeout));

#ifdef CONFIG_BBQUE_TG_PROG_MODEL
	// Destroy the task-graph object
	papp->ClearTaskGraph();
	logger->Debug("DestroyEXC: [%s] task-graph cleared", papp->StrId());
#endif // CONFIG_BBQUE_TG_PROG_MODEL

	logger->Info("DestroyEXC: [%s] FINISHED", papp->StrId());
	PrintStatus();
	ra.PrintStatusReport();

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::DestroyEXC(AppPid_t pid, uint8_t exc_id)
{
	// Find the required EXC
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	assert(papp);
	if (!papp) {
		logger->Warn("DestroyEXC: [%d:*:%d] stop FAILED: EXC not found", pid, exc_id);
		return AM_EXC_NOT_FOUND;
	}

	return DestroyEXC(papp);
}

ApplicationManager::ExitCode_t
ApplicationManager::DestroyEXC(AppPid_t pid)
{
	std::pair<AppsMap_t::iterator, AppsMap_t::iterator> range;
	AppsMap_t::iterator it;
	ExitCode_t result;
	AppPtr_t papp;

	range = apps.equal_range(pid);
	it = range.first;
	for ( ; it != range.second; ++it) {
		papp = (*it).second;
		result = DestroyEXC(papp);
		if (result != AM_SUCCESS)
			return result;
	}

	logger->Info("DestroyEXC: [%d:*:*] TERMINATED", pid);

	return AM_SUCCESS;
}


/*******************************************************************************
 *  EXC Constraints management
 ******************************************************************************/


ApplicationManager::ExitCode_t
ApplicationManager::SetConstraintsEXC(AppPtr_t papp,
                                      RTLIB_Constraint_t *constraints, uint8_t count)
{
	Application::ExitCode_t result;
	logger->Debug("SetConstraintsEXC: [%s] setting constraints...", papp->StrId());

	while (count) {
		result = papp->SetWorkingModeConstraint(constraints[0]);
		if (result != Application::APP_SUCCESS)
			return AM_ABORT;

		// Move to next constraint
		constraints++;
		count--;
	}

	// Check for the need of a new schedule request
	if (papp->CurrentAWMNotValid()) {
		logger->Warn("SetConstraintsEXC: [%s] re-schedule required", papp->StrId());
		return AM_RESCHED_REQUIRED;
	}

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::SetConstraintsEXC(AppPid_t pid, uint8_t exc_id,
                                      RTLIB_Constraint_t *constraints, uint8_t count)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("SetConstraintsEXC: [%d:*:%d] set constraints FAILED: EXC not found", pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}

	// Set constraints for this EXC
	return SetConstraintsEXC(papp, constraints, count);

}

ApplicationManager::ExitCode_t
ApplicationManager::ClearConstraintsEXC(AppPtr_t papp)
{
	logger->Debug("ClearConstraintsEXC: [%s] clearing constraints...", papp->StrId());
	papp->ClearWorkingModeConstraints();

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::ClearConstraintsEXC(AppPid_t pid, uint8_t exc_id)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("ClearConstraintsEXC: [%d:*:%d] clear FAILED: EXC not found", pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}
	return ClearConstraintsEXC(papp);
}


/*******************************************************************************
 *  Application Runtime Profiling
 ******************************************************************************/


ApplicationManager::ExitCode_t
ApplicationManager::CheckGoalGapEXC(
        AppPtr_t papp,
        struct app::RuntimeProfiling_t &rt_prof)
{
	// Define the contraints for this execution context
	logger->Debug("CheckGoalGapEXC: [%s] checking goal-gap (%d)...",
	              papp->StrId(), rt_prof.ggap_percent);

	// FIXME the reschedule should be activated based on some
	// configuration parameter or policy decision
	// Check for the need of a new schedule request
	if (rt_prof.ggap_percent != 0)
		return AM_RESCHED_REQUIRED;

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::IsReschedulingRequired(
        AppPtr_t papp,
        struct app::RuntimeProfiling_t &rt_prof)
{
	return CheckGoalGapEXC(papp, rt_prof);
}

ApplicationManager::ExitCode_t
ApplicationManager::IsReschedulingRequired(
        AppPid_t pid,
        uint8_t exc_id,
        struct app::RuntimeProfiling_t &rt_prof)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("IsReschedulingRequired: [%d:*:%d] "
		             "check for rescheduling FAILED: EXC not found",
		             pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}
	return IsReschedulingRequired(papp, rt_prof);
}


ApplicationManager::ExitCode_t
ApplicationManager::GetRuntimeProfile(
        AppPid_t pid, uint8_t exc_id, struct app::RuntimeProfiling_t &profile)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("GetRuntimeProfile: [%d:*:%d] profiling not available:"
		             " EXC not found", pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}
	return GetRuntimeProfile(papp, profile);
}

ApplicationManager::ExitCode_t
ApplicationManager::SetRuntimeProfile(
        AppPid_t pid, uint8_t exc_id, struct app::RuntimeProfiling_t profile)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("SetRuntimeProfile: [%d:*:%d] profiling setting FAILED: "
		             "EXC not found", pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}
	return SetRuntimeProfile(papp, profile);
}

ApplicationManager::ExitCode_t
ApplicationManager::SetRuntimeProfile(
        AppPid_t pid,
        uint8_t exc_id,
        int gap,
        int cusage,
        int ctime_ms)
{
	// Getting current runtime profile information
	ExitCode_t result;
	struct app::RuntimeProfiling_t rt_prof;
	result = GetRuntimeProfile(pid, exc_id, rt_prof);
	if (result != AM_SUCCESS)
		return AM_ABORT;

	// Updating runtime information with the received values
	rt_prof.ggap_percent_prev = rt_prof.ggap_percent;
	rt_prof.ggap_percent      = gap;
	rt_prof.cpu_usage_prev    = rt_prof.cpu_usage;
	rt_prof.cpu_usage    =
	        (cusage > 0) ? cusage : rt_prof.cpu_usage_prediction;
	// Removing fluctuation due to additional threads such as input collector,
	// which are not included in the CPU usage count
	rt_prof.cpu_usage = std::min(cusage, rt_prof.cpu_usage_prediction);
	rt_prof.ctime_ms = ctime_ms;
	rt_prof.is_valid = true;

	if (rt_prof.ggap_percent < 0) {
		// Update lower bound value and age
		rt_prof.gap_history.lower_cpu = rt_prof.cpu_usage;
		rt_prof.gap_history.lower_gap = rt_prof.ggap_percent;
		rt_prof.gap_history.lower_age = 0;

		// Invalidate the other bound if necessary
		if (rt_prof.gap_history.upper_cpu <= rt_prof.gap_history.lower_cpu)
			rt_prof.gap_history.upper_age = -1;
		// If valid, update the other bound age
		else if (rt_prof.gap_history.upper_age >= 0)
			rt_prof.gap_history.upper_age ++;

	} else {
		// Update upper bound value and age
		rt_prof.gap_history.upper_cpu = rt_prof.cpu_usage;
		rt_prof.gap_history.upper_gap = rt_prof.ggap_percent;
		rt_prof.gap_history.upper_age = 0;

		// Invalidate the other bound if necessary
		if (rt_prof.gap_history.lower_cpu > rt_prof.gap_history.upper_cpu)
			rt_prof.gap_history.lower_age = -1;
		// If valid, update the lower bound age
		else if (rt_prof.gap_history.lower_age >= 0)
			rt_prof.gap_history.lower_age ++;
	}

	// Checking if a new schedule is needed
	result = IsReschedulingRequired(pid, exc_id, rt_prof);
	if (result == AM_ABORT)
		return AM_ABORT;

	// Saving the new values for the application
	SetRuntimeProfile(pid, exc_id, rt_prof);
	return result;
}

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

void ApplicationManager::LoadTaskGraph(AppPid_t pid, uint8_t exc_id)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("LoadTaskGraph: [%d:*:%d] FAILED: EXC not found", pid, exc_id);
		assert(papp);
		return;
	}
	return LoadTaskGraph(papp);
}


void ApplicationManager::LoadTaskGraphAll()
{
	AppsUidMapIt app_it;

	AppPtr_t papp = GetFirst(ba::ApplicationStatusIF::READY, app_it);
	for (; papp; papp = GetNext(ba::ApplicationStatusIF::READY, app_it)) {
		papp->LoadTaskGraph();
	}

	papp = GetFirst(ba::ApplicationStatusIF::RUNNING, app_it);
	for (; papp; papp = GetNext(ba::ApplicationStatusIF::RUNNING, app_it)) {
		papp->LoadTaskGraph();
	}
}

#endif // CONFIG_BBQUE_TG_PROG_MODEL

/*******************************************************************************
 *  EXC Enabling
 ******************************************************************************/

ApplicationManager::ExitCode_t
ApplicationManager::EnableEXC(AppPtr_t papp)
{
	logger->Debug("EnableEXC: [%s] enabling...", papp->StrId());

	auto ret = ChangeEXCState(papp, app::Schedulable::READY);
	if (ret != AM_SUCCESS) {
		logger->Error("EnableEXC: [%s] enabling...", papp->StrId());
		return ret;
	}

	logger->Info("EnableEXC: [%s] ENABLED", papp->StrId());
	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::EnableEXC(AppPid_t pid, uint8_t exc_id)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("EnableEXC: [%d:*:%d] enabling FAILED: not found", pid, exc_id);
		assert(papp);
		return AM_EXC_NOT_FOUND;
	}

	return EnableEXC(papp);
}


/*******************************************************************************
 *  EXC Disabling
 ******************************************************************************/

ApplicationManager::ExitCode_t
ApplicationManager::DisableEXC(AppPtr_t papp, bool release)
{
	// A disabled EXC is moved (as soon as possible) into the DISABLED queue
	// NOTE: other code-path should check wheter an application is still
	// !DISABLED to _assume_ a normal operation
	logger->Debug("DisableEXC: [%s:%s/%s] disabling...",
	              papp->StrId(),
	              Application::stateStr[papp->State()],
	              Application::syncStateStr[papp->SyncState()]);

	// Scheduling in progress?
	logger->Debug("DisableEXC: waiting for scheduler manager...");
	SchedulerManager &sm(SchedulerManager::GetInstance());
	sm.WaitForReady();

	// Update the status to DISABLED
	auto ret = ChangeEXCState(papp, app::Schedulable::SYNC, app::Schedulable::DISABLED);
	if (ret == AM_EXC_STATUS_CHANGE_NONE) {
		logger->Warn("DisableEXC: [%s] already disabled", papp->StrId());
		return ret;
	}

	// Release should be performed if the application is actually dead
	if (!release) {
		logger->Info("DisableEXC: [%s] DISABLED without release", papp->StrId());
		return ret;
	}

	logger->Debug("DisableEXC: [%s] releasing assigned resources...", papp->StrId());
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ra.ReleaseResources(papp);
	PlatformManager & plm(PlatformManager::GetInstance());
	plm.ReclaimResources(papp);

	logger->Info("DisableEXC: [%s] DISABLED with release", papp->StrId());
	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t
ApplicationManager::DisableEXC(AppPid_t pid, uint8_t exc_id, bool release)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Warn("DisableEXC: [%d:*:%d] FAILED: EXC not found", pid, exc_id);
		assert(papp);
		return AM_ABORT;
	}

	return DisableEXC(papp, release);
}


/*******************************************************************************
 *  EXC Checking
 ******************************************************************************/

bool ApplicationManager::CheckEXC(AppPtr_t papp, bool release)
{
	logger->Debug("CheckEXC: [%s] checking life status...", papp->StrId());

	// Check if the required process is still alive
	bool dead = false;
	int ret = kill(papp->Pid(), 0);
	if (ret != 0) {
		dead = true;
		logger->Warn("CheckEXC: Dead process PID=%d", papp->Pid());
	}
	logger->Debug("CheckEXC: [%s] is %s",
	              papp->StrId(), dead ? "DEAD" : "still ALIVE");

	// If already finished, remove from the map of finished
	if (papp->Finished()) {
		logger->Warn("CheckEXC: [%s] destroying descriptor", papp->StrId());
		DestroyEXC(papp);
	}
	// If not alredy finished, change status for resources release
	else if (dead && release && !papp->Disabled()) {
		logger->Debug("CheckEXC: [%s] disabling...", papp->StrId());
		DisableEXC(papp, release);
	}

	return !dead;
}

bool ApplicationManager::CheckEXC(AppPid_t pid, uint8_t exc_id, bool release)
{
	AppPtr_t papp(GetApplication(Application::Uid(pid, exc_id)));
	if (!papp) {
		logger->Debug("CheckEXC: [%d:*:%d] FAILED: EXC not found", pid, exc_id);
		return AM_EXC_NOT_FOUND;
	}

	return CheckEXC(papp, release);
}


void ApplicationManager::CheckActiveEXCs()
{
	AppsUidMapIt app_it;
	AppPtr_t papp = GetFirst(ba::ApplicationStatusIF::READY, app_it);
	for (; papp; papp = GetNext(ba::ApplicationStatusIF::READY, app_it)) {
		CheckEXC(papp, true);
	}

	papp = GetFirst(ba::ApplicationStatusIF::RUNNING, app_it);
	for (; papp; papp = GetNext(ba::ApplicationStatusIF::RUNNING, app_it)) {
		CheckEXC(papp, true);
	}
}
/*******************************************************************************
 *  EXC Scheduling
 ******************************************************************************/

ApplicationManager::ExitCode_t ApplicationManager::ScheduleRequest(
        ba::AppCPtr_t papp,
        ba::AwmPtr_t awm,
        br::RViewToken_t status_view,
        size_t b_refn)
{

	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	ResourceAccounter::ExitCode_t ra_result;
	logger->Info("ScheduleRequest: [%s] schedule request for binding @[%d] view=%ld",
	             papp->StrId(), b_refn, status_view);

	// AWM safety check
	if (!awm) {
		logger->Crit("ScheduleRequest: [%s] AWM not existing)", papp->StrId());
		assert(awm);
		return AM_AWM_NULL;
	}
	logger->Debug("ScheduleRequest: [%s] request for scheduling in AWM [%02d:%s]",
	              papp->StrId(), awm->Id(), awm->Name().c_str());

	// App is SYNC/BLOCKED for a previously failed scheduling.
	// Reset state and syncState for this new attempt.
	if (papp->Blocking()) {
		logger->Warn("ScheduleRequest: [%s] request for blocking application",
		             papp->StrId());
		logger->Warn("ScheduleRequest: [%s] forcing a new state transition",
		             papp->StrId());
		ChangeEXCState(papp, papp->PreSyncState(), app::Schedulable::SYNC_NONE);
		return AM_APP_BLOCKING;
	}

	// Nothing to schedule if already disabled
	if (papp->Disabled()) {
		logger->Error("ScheduleRequest: [%s] already disabled", papp->StrId());
		return AM_APP_DISABLED;
	}

	// Checking for resources availability: unschedule if not
	ra_result = ra.BookResources(
	                    papp, awm->GetSchedResourceBinding(b_refn), status_view);
	if (ra_result != ResourceAccounter::RA_SUCCESS) {
		logger->Debug("ScheduleRequest: [%s] not enough resources...",
		              papp->StrId());
		Unschedule(papp);
		return AM_AWM_NOT_SCHEDULABLE;
	}

	// Bind the resource set to the working mode
	awm->SetResourceBinding(status_view, b_refn);

	// Reschedule accordingly to "awm"
	logger->Debug("ScheduleRequest: (re)scheduling [%s] into AWM [%d:%s]...",
	              papp->StrId(), awm->Id(), awm->Name().c_str());
	auto ret = Reschedule(papp, awm);
	if (ret != AM_SUCCESS) {
		ra.ReleaseResources(papp, status_view);
		awm->ClearResourceBinding();
		return ret;
	}

	// Set next awm
	papp->SetNextAWM(awm);

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t ApplicationManager::ScheduleRequestAsPrev(
        ba::AppCPtr_t papp,
        br::RViewToken_t status_view)
{
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	ResourceAccounter::ExitCode_t ra_result;
	if (papp == nullptr) {
		logger->Crit("ScheduleRequestAsPrev: null application pointer");
		return AM_ABORT;
	}
	logger->Debug("ScheduleRequestAsPrev: [%p == %p] ?", papp.get(), this);

	// Application must be already running
	if (!papp->Running()) {
		logger->Warn("ScheduleRequestAsPrev: [%s] not in RUNNING state [%s]",
		             papp->StrId(), papp->StateStr(papp->State()));
		return AM_EXC_INVALID_STATUS;
	}

	// Checking resources are still available
	ra_result = ra.BookResources(
	                    papp, papp->CurrentAWM()->GetResourceBinding(), status_view);
	if (ra_result != ResourceAccounter::RA_SUCCESS) {
		logger->Warn("ScheduleRequestAsPrev: [%s] unscheduling...", papp->StrId());
		return AM_AWM_NOT_SCHEDULABLE;
	}

	// Set next awm to the previous one
	papp->SetNextAWM(papp->CurrentAWM());
	logger->Debug("ScheduleRequestAsPrev: [%s] rescheduled as previously: AWM [%d -> %d]",
	              papp->StrId(), papp->CurrentAWM()->Id(), papp->NextAWM()->Id());

	return AM_SUCCESS;
}

ApplicationManager::ExitCode_t ApplicationManager::ScheduleRequestAbort(
        ba::AppCPtr_t papp,
        br::RViewToken_t status_view)
{

	logger->Info("ScheduleRequestAbort: [%s] abort schedule request [view=%ld]",
	             papp->StrId(), status_view);

	// AWM safety check
	if (!papp->NextAWM()) {
		logger->Crit("ScheduleRequestAbort: [%s] AWM not existing)", papp->StrId());
		return AM_AWM_NULL;
	}

	// Undo resource booking
	logger->Debug("ScheduleRequestAbort: [%s] undoing resource booking...", papp->StrId());
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	ra.ReleaseResources(papp, status_view);

	// Unschedule
	logger->Debug("ScheduleRequestAbort: [%s] unscheduling...", papp->StrId());
	auto ret = Unschedule(papp);
	if (ret == AM_SUCCESS) {
		papp->SetNextAWM(nullptr);
		return ret;
	}

	logger->Error("ScheduleRequestAbort: [%s] error=%d...", papp->StrId(), ret);
	return ret;
}


ApplicationManager::ExitCode_t ApplicationManager::Reschedule(
        ba::AppCPtr_t papp,
        ba::AwmPtr_t awm)
{
	// Ready application could be synchronized to start
	if (papp->State() == app::Schedulable::READY) {
		logger->Debug("(Re)schedule: [%s] for STARTING", papp->StrId());
		return SetForSynchronization(papp, app::Schedulable::STARTING);
	}

	// Otherwise, the application should be running...
	if (papp->State() != app::Schedulable::RUNNING) {
		logger->Crit("(Re)schedule: [%s] wrong status {%s/%s}",
		             papp->StrId(),
		             papp->StateStr(papp->State()),
		             papp->SyncStateStr(papp->SyncState()));
		return AM_ABORT;
	}

	// Checking if a synchronization is required
	auto next_sync = papp->NextSyncState(awm);
	logger->Debug("(Re)schedule: [%s] for %s",
	              papp->StrId(),
	              papp->SyncStateStr(next_sync));
	if (next_sync == app::Schedulable::SYNC_NONE)
		return AM_SUCCESS;

	// Request a synchronization for the identified reconfiguration
	return SetForSynchronization(papp, next_sync);
}


ApplicationManager::ExitCode_t ApplicationManager::Unschedule(
        ba::AppCPtr_t papp)
{
	// Do nothing if already ready or blocking
	if ((papp->State() == app::Schedulable::READY) || (papp->Blocking())) {
		logger->Debug("Unschedule: [%s] current status = {%s/%s})",
		              papp->StrId(),
		              papp->StateStr(papp->State()),
		              papp->SyncStateStr(papp->SyncState()));
		logger->Debug("Unschedule: [%s] no need further actions",
		              papp->StrId());
		return AM_SUCCESS;
	}
	// Request a synchronization to block the application
	return SetForSynchronization(papp, app::Schedulable::BLOCKED);
}

ApplicationManager::ExitCode_t ApplicationManager::NoSchedule(
        ba::AppCPtr_t papp)
{
	logger->Debug("NoSchedule: [%s] not scheduled", papp->StrId());
	return ChangeEXCState(papp,
	                      app::Schedulable::SYNC, app::Schedulable::BLOCKED);
}

ApplicationManager::ExitCode_t
ApplicationManager::SetForSynchronization(
        app::AppCPtr_t papp, Application::SyncState_t next_sync)
{
	// Check valid state has beed required
	if (next_sync >= Application::SYNC_STATE_COUNT) {
		logger->Crit("SetForSynchronization: [%s] FAILED : invalid sync state [%d]",
		             papp->StrId(), next_sync);
		assert(next_sync < app::Schedulable::SYNC_STATE_COUNT);
		return AM_ABORT;
	}
	logger->Debug("SetForSynchronization: [%s, %s] requesting synchronization...",
	              papp->StrId(), Application::SyncStateStr(next_sync));

	// Change synchronization state
	ChangeEXCState(papp, app::Schedulable::SYNC, next_sync);
	if (!papp->Synching()) {
		logger->Crit("SetForSynchronization: [%s] FAILED: invalid EXC state [%d]",
		             papp->StrId(), next_sync);
		assert(papp->Synching());
		return AM_ABORT;
	}

	// TODO notify the Resource Manager

	logger->Debug("SetForSynchronization: [%s, %s] completed", papp->StrId(),
	              Application::SyncStateStr(papp->SyncState()));

	return AM_SUCCESS;
}


/*******************************************************************************
 *  EXC Synchronization
 ******************************************************************************/

void ApplicationManager::RemoveFromSyncMap(AppPtr_t papp, Application::SyncState_t state)
{
	logger->Debug("RemoveFromSyncMap: [%s] removing sync [%s] request ...",
	              papp->StrId(), ba::Schedulable::SyncStateStr(state));
	std::unique_lock<std::mutex> sync_ul(sync_mtx[state]);
	assert(papp);
	UpdateIterators(sync_ret[state], papp);

	PrintSyncQ();
	logger->Debug("RemoveFromSyncMap: [%s] removing sync [%s] after request ...",
	              papp->StrId(), ba::Schedulable::SyncStateStr(state));

	// Get the applications map
	if (sync_vec[state].erase(papp->Uid())) {
		logger->Debug("RemoveFromSyncMap: [%s, %s] removed sync request",
		              papp->StrId(), papp->SyncStateStr(state));
		PrintSyncQ();
		return;
	}
	logger->Crit("RemoveFromSyncMap: [%s, %s] should not arrive here!",
	             papp->StrId(), papp->SyncStateStr(state));


	// We should never got there
	assert(false);
}

void ApplicationManager::RemoveFromSyncMap(AppPtr_t papp)
{
	assert(papp);
	logger->Debug("RemoveFromSyncMap: [%s] removing sync request ...", papp->StrId());

	// Disregard EXCs which are not in SYNC state
	if (!papp->Synching()) {
		logger->Debug("RemoveFromSyncMap: [%s] inconsistent state: %s/%s ...",
		              papp->StrId(),
		              papp->StateStr(papp->State()),
		              papp->SyncStateStr(papp->SyncState()));
		return;
	}

	RemoveFromSyncMap(papp, papp->SyncState());
}

void ApplicationManager::AddToSyncMap(AppPtr_t papp, Application::SyncState_t state)
{
	assert(papp);
	// Disregard EXCs which are not in SYNC state
	logger->Debug("AddToSyncMap: [%s] state: %s/%s removing from map...",
	              papp->StrId(),
	              papp->StateStr(papp->State()),
	              papp->SyncStateStr(papp->SyncState()));

	if (!papp->Synching()) {
		logger->Debug("AddToSyncMap: [%s] inconsistent state: %s/%s ...",
		              papp->StrId(),
		              papp->StateStr(papp->State()),
		              papp->SyncStateStr(papp->SyncState()));
		return;
	}
	std::unique_lock<std::mutex> sync_ul(sync_mtx[state]);
	sync_vec[state].insert(UidsMapEntry_t(papp->Uid(), papp));
}

void ApplicationManager::AddToSyncMap(AppPtr_t papp)
{
	assert(papp);
	AddToSyncMap(papp, papp->SyncState());
	logger->Debug("AddToSyncMap: [%s, %d:%s] added synchronization request",
	              papp->StrId(),
	              papp->SyncState(),
	              papp->SyncStateStr(papp->SyncState()));
	PrintSyncQ();
}



ApplicationManager::ExitCode_t
ApplicationManager::SyncCommit(AppPtr_t papp)
{
	logger->Debug("SyncCommit: [%s, %s] synchronization in progress...",
	              papp->StrId(), papp->SyncStateStr(papp->SyncState()));
	auto curr_state = papp->State();
	auto curr_sync  = papp->SyncState();

	// Notify application
	papp->SyncCommit();
	logger->Debug("SyncCommit: [%s] prev state [%s]...",
	              papp->StrId(), papp->StateStr(papp->PreSyncState()));

	// Update status maps
	UpdateStatusMaps(papp, curr_state, papp->State());

	// Remove from the sync map
	RemoveFromSyncMap(papp, curr_sync);

	// If FINISHED we can destroy the EXC descriptor
	if (papp->Finished()) {
		logger->Debug("SyncCommit: [%s] [%s/%s] destroying EXC...",
		              papp->StrId(),
		              papp->StateStr(papp->State()),
		              papp->SyncStateStr(papp->SyncState()));
		DestroyEXC(papp);
	}
	logger->Debug("SyncCommit: [%s] [%s/%s] synchronization COMPLETED",
	              papp->StrId(),
	              papp->StateStr(papp->State()),
	              papp->SyncStateStr(papp->SyncState()));

	return AM_SUCCESS;
}

void ApplicationManager::SyncAbort(AppPtr_t papp)
{
	Application::SyncState_t syncState = papp->SyncState();
	logger->Warn("SyncAbort: [%s, sync_state=%s] synchronization aborted...",
	             papp->StrId(), papp->SyncStateStr(syncState));

	// The abort must be performed only for SYNC applications
	Application::State_t state = papp->State();
	if (!papp->Synching()) {
		logger->Error("SyncAbort: [%s, state=%s] (expected SYNC)",
		              papp->StrId(), papp->StateStr(state));
	}

	// Move to READY map if still alive
	bool is_alive = CheckEXC(papp, false);
	if (is_alive)
		ChangeEXCState(papp, app::Schedulable::READY);
	else
		ChangeEXCState(papp, app::Schedulable::FINISHED);
	logger->Debug("SyncAbort: completed ");
}


ApplicationManager::ExitCode_t
ApplicationManager::SyncContinue(AppPtr_t papp)
{
	Application::State_t state = papp->State();
	Application::SyncState_t syncState = papp->SyncState();
	assert(papp->CurrentAWM()); // This must be called only for RUNNING App/ExC
	if (papp->State() != app::Schedulable::RUNNING) {
		logger->Error("SyncContinue: [%s] is not running. State {%s/%s}",
		              papp->StrId(),
		              papp->StateStr(state),
		              papp->SyncStateStr(syncState));
		assert(papp->State() == app::Schedulable::RUNNING);
		assert(papp->SyncState() == app::Schedulable::SYNC_NONE);
		return AM_ABORT;
	}

	// Return if Next AWN is already blank
	if (papp->NextAWM() == nullptr)
		return AM_SUCCESS;

	// AWM current and next must match
	if (papp->CurrentAWM()->Id() != papp->NextAWM()->Id()) {
		logger->Error("SyncContinue: [%s] AWMs differs. "
		              "{curr=%d / next=%d}",
		              papp->StrId(),
		              papp->CurrentAWM()->Id(),
		              papp->NextAWM()->Id());
		return AM_ABORT;
	}

	// Notify the application
	auto app_result = papp->SyncContinue();
	if (app_result != Application::APP_SUCCESS)
		return AM_ABORT;

	logger->Debug("SyncContinue: completed ");
	return AM_SUCCESS;
}

}   // namespace bbque

