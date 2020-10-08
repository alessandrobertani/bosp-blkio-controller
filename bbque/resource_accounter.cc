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

#include "bbque/resource_accounter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <string>
#include <sstream>

#include "bbque/application_manager.h"
#include "bbque/app/working_mode.h"
#include "bbque/config.h"
#include "bbque/pm/power_manager.h"
#include "bbque/process_manager.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/schedlog.h"

#undef  MODULE_CONFIG
#define MODULE_CONFIG "ResourceAccounter"

#define RA_DIV1 "==========================================================================="
#define RA_DIV2 "|-------------------------------+-----+-----------+-----------+-----------|"
#define RA_HEAD "|   RESOURCES                ON | MOD |   USED    | AVAILABLE |   TOTAL   |"
#define RA_DIV3 "|                               |     |           |           |           |"


namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque {

ResourceAccounter & ResourceAccounter::GetInstance()
{
	static ResourceAccounter instance;
	return instance;
}

ResourceAccounter::ResourceAccounter() :
    am(ApplicationManager::GetInstance()),
    cm(CommandManager::GetInstance()),
    fm(ConfigurationManager::GetInstance()),
    status(State::NOT_READY)
{
	// Get a logger
	logger = bu::Logger::GetLogger(RESOURCE_ACCOUNTER_NAMESPACE);
	assert(logger);

	// Init the system resources state view
	sys_assign_view = std::make_shared<AppAssignmentsMap_t>();
	sys_view_token  = 0;
	assign_per_views[sys_view_token] = sys_assign_view;
	rsrc_per_views[sys_view_token]   = std::make_shared<ResourceSet_t>();

	// Init sync session info
	sync_ssn.count = 0;

	// Init prefix path object
	r_prefix_path = std::make_shared<br::ResourcePath>(BBQUE_RESOURCE_PATH_PREFIX);

	// Register set quota command
#define CMD_SET_TOTAL "set_total"
	cm.RegisterCommand(RESOURCE_ACCOUNTER_NAMESPACE "." CMD_SET_TOTAL,
			static_cast<CommandHandler*>(this),
			"Set a new amount of resource that can be allocated");
}

ResourceAccounter::~ResourceAccounter()
{
	resources.clear();
	resource_set.clear();
	assign_per_views.clear();
	rsrc_per_views.clear();
	per_type_resource_ids.clear();
}

/************************************************************************
 *                   STATE SYNCHRONIZATION                              *
 ************************************************************************/

void ResourceAccounter::SetPlatformReady()
{
	std::unique_lock<std::mutex> status_ul(status_mtx);
	while (status == State::SYNC) {
		status_cv.wait(status_ul);
	}
	status = State::READY;
	status_cv.notify_all();
	PrintCountPerType();
}

void ResourceAccounter::SetPlatformNotReady()
{
	std::unique_lock<std::mutex> status_ul(status_mtx);
	while (status == State::SYNC) {
		status_cv.wait(status_ul);
	}
	status = State::NOT_READY;
	status_cv.notify_all();
}

void ResourceAccounter::WaitForPlatformReady()
{
	std::unique_lock<std::mutex> status_ul(status_mtx);
	while (status != State::READY) {
		status_cv.wait(status_ul);
	}
}

/************************************************************************
 *                   LOGGER REPORTS                                     *
 ************************************************************************/

void
ResourceAccounter::PrintStatus(br::RViewToken_t status_view, bool verbose) const
{
	// Print the head of the report table
	if (verbose) {
		logger->Notice("Report on state view: %ld", status_view);
		logger->Notice(RA_DIV1);
		logger->Notice(RA_HEAD);
		logger->Notice(RA_DIV2);
	}
	else {
		DB(
		logger->Debug("Report on state view: %ld", status_view);
		logger->Debug(RA_DIV1);
		logger->Debug(RA_HEAD);
		logger->Debug(RA_DIV2);
		);
	}

	// For each resource get the used amount
	for (auto & resource_ptr : resource_set) {

		// Attribute for online/offline resource status
		char online = 'Y';
		if (resource_ptr->IsOffline())
			online  = 'N';

		// Append '%' if resource is a processing element (core)
		bool show_percent = (resource_ptr->Type() == br::ResourceType::PROC_ELEMENT);

		// Resource info string
		std::stringstream resource_ss;
		resource_ss << "| " << std::left << std::setw(27)
			<< resource_ptr->Path()->ToString() << " "
			<< online << " | "
			<< std::setw(3)
			<< resource_ptr->Model().substr(0, 3).c_str();
		// Used
		resource_ss << " | "  << std::setw(9) << std::right
			<< bu::GetValueUnitStr(
					resource_ptr->Used(status_view), show_percent).c_str();
		// Unreserved
		resource_ss << " | " << std::setw(9) << std::right
			<< bu::GetValueUnitStr(
					resource_ptr->Unreserved(), show_percent).c_str();
		// Total
		resource_ss << " | " << std::setw(9) << std::right
			<< bu::GetValueUnitStr(
					resource_ptr->Total(), show_percent).c_str()
			<< " | ";

		PRINT_NOTICE_IF_VERBOSE(verbose, resource_ss.str().c_str());

		// Application resource allocation information
		if (resource_ptr->Used(status_view) > 0)
			PrintApplicationInfo(resource_ptr, show_percent, status_view, verbose);
	}

	PRINT_NOTICE_IF_VERBOSE(verbose, RA_DIV1);
}

void ResourceAccounter::PrintApplicationInfo(br::ResourcePtr_t resource_ptr,
					     bool percent,
					     br::RViewToken_t status_view,
					     bool verbose) const
{
	if (resource_ptr == nullptr) {
		logger->Warn("Null resource descriptor passed");
		return;
	}

	br::AppUsageQtyMap_t apps_map;
	resource_ptr->Applications(apps_map, status_view);
	for (auto & app : apps_map) {
		auto const & app_uid(app.first);
		auto const & app_usage(app.second);

		// Get the App/EXC descriptor
		ba::SchedPtr_t papp(am.GetApplication(app_uid));
		if (papp == nullptr) {
			logger->Debug("[uid=%d] no application found", app_uid);
		}

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
		// Get the process descriptor if we failed with AM
		if (papp == nullptr) {
			ProcessManager & prm(ProcessManager::GetInstance());
			papp = prm.GetProcess(app_uid);
			if (papp == nullptr) {
				logger->Debug("[pid=%d] no process found", app_uid);
			}
		}
#endif
		if (papp == nullptr)
			continue;

		// Skip finished applications
		if (papp->State() == Application::FINISHED) {
			logger->Debug("[pid=%d, uid=%d, state=%s] skipped",
				papp->Pid(), app_uid,
				Application::StateStr(papp->State()));
			continue;
		}

		// Get the working mode for resource assignments info.
		// If the status view is 0 we expect the application to have working mode
		// currently set, otherwise the function has been called to print the status
		// of an intermediate (scheduled) resources state view. In such
		// a case, we could have the application in a pre-synchronization
		// state, and working mode set in the 'next' attribute.
		auto pawm = papp->CurrentAWM();
		if (!pawm && (status_view != 0))
			pawm = papp->NextAWM();

		if (!pawm) {
			logger->Warn("[pid=%d, uid=%d, state=%s] no working mode",
				papp->Pid(), app_uid,
				Application::StateStr(papp->State()));
			continue;
		}

#define RA_PROGRESS_BAR_LEN 21
		char prog_bar[RA_PROGRESS_BAR_LEN];
		utils::SchedLog::BuildProgressBar(
						app_usage, resource_ptr->Total(), prog_bar,
						RA_PROGRESS_BAR_LEN, '|');

		// Build the row with the application info
		std::stringstream info_ss;
		info_ss << "| > "
			<< std::setw(15) << papp->StrId() << ","
			<< "pr:" << std::setw(2) << papp->Priority() << ","
			<< "wm:" << std::setw(2) << pawm->Id() << " | "
			<< std::setw(6) << std::setfill(' ') << " | "
			<< std::setw(9) << bu::GetValueUnitStr(app_usage, percent).c_str() << " | "
			<< std::setw(RA_PROGRESS_BAR_LEN) << prog_bar << " |";
		PRINT_NOTICE_IF_VERBOSE(verbose, info_ss.str().c_str());
	}
	// Print a separator line
	PRINT_NOTICE_IF_VERBOSE(verbose, RA_DIV2);
}

void ResourceAccounter::PrintCountPerType() const
{
	logger->Debug("======================");
	logger->Debug("| Resources per type |");
	logger->Debug("|--------------------|");
	for (auto const & entry : per_type_resource_ids) {
		auto & type(entry.first);
		auto & ids(entry.second);
		logger->Debug("| <%3s> : %9d  |",
			GetResourceTypeString(type), ids.size());
	}
	logger->Debug("======================");
}

/************************************************************************
 *             RESOURCE DESCRIPTORS ACCESS                              *
 ************************************************************************/

br::ResourcePtr_t ResourceAccounter::GetResource(std::string const & strpath)
{
	// Build a resource path object.
	// It can be a MIXED path(not inserted in resource_paths)
	ResourcePathPtr_t resource_path_ptr = GetPath(strpath);
	if (!resource_path_ptr)
		return nullptr;
	return GetResource(resource_path_ptr);
}

br::ResourcePtr_t
ResourceAccounter::GetResource(ResourcePathPtr_t resource_path_ptr) const
{
	auto matchings(resources.find_list(
					*resource_path_ptr,
					RT_MATCH_FIRST | RT_MATCH_MIXED));
	if (matchings.empty())
		return nullptr;
	return matchings.front();
}

br::ResourcePtrList_t ResourceAccounter::GetResources(std::string const & strpath)
{
	auto resource_path_ptr = GetPath(strpath);
	if (!resource_path_ptr)
		return br::ResourcePtrList_t();
	return GetResources(resource_path_ptr);
}

br::ResourcePtrList_t
ResourceAccounter::GetResources(ResourcePathPtr_t resource_path_ptr) const
{
	// If the path is a template find all the resources matching the
	// template. Otherwise perform a "mixed path" based search.
	if (resource_path_ptr->IsTemplate()) {
		logger->Debug("GetResources: path <%s> is a template",
			resource_path_ptr->ToString().c_str());
		return resources.find_list(*(resource_path_ptr.get()), RT_MATCH_TYPE);
	}
	return resources.find_list(*resource_path_ptr, RT_MATCH_MIXED);
}

bool ResourceAccounter::ExistResource(std::string const & strpath)
{
	auto resource_path_ptr = GetPath(strpath);
	return ExistResource(resource_path_ptr);
}

bool ResourceAccounter::ExistResource(ResourcePathPtr_t resource_path_ptr) const
{
	if (!resource_path_ptr)
		return false;
	auto matchings(resources.find_list(
					*resource_path_ptr,
					RT_MATCH_TYPE | RT_MATCH_FIRST));
	return !matchings.empty();
}

ResourcePathPtr_t const ResourceAccounter::GetPath(std::string const & strpath)
{
	auto rp_it = resource_paths.find(strpath);
	if (rp_it == resource_paths.end()) {
		// Create a new resource path object
		logger->Debug("GetPath: no resource path object for <%s>",
			strpath.c_str());
		auto new_path = std::make_shared<br::ResourcePath>(strpath);
		if (new_path == nullptr) {
			logger->Error("GetPath: <%s> is a not valid path",
				strpath.c_str());
			return nullptr;
		}

		// ...it must actually refer to a registered resource
		if (ExistResource(new_path)) {
			resource_paths.emplace(strpath, new_path);
			logger->Debug("GetPath: resource path object for <%s> added",
				strpath.c_str());
			return new_path;
		}
		else
			return nullptr;
	}
	return (*rp_it).second;
}

/************************************************************************
 *                   QUERY METHODS                                      *
 ************************************************************************/

uint64_t ResourceAccounter::Total(std::string const & path)
{
	br::ResourcePtrList_t matchings(GetResources(path));
	return QueryStatus(matchings, RA_TOTAL, 0);
}

uint64_t ResourceAccounter::Total(br::ResourcePtrList_t & resources_list) const
{
	if (resources_list.empty())
		return 0;
	return QueryStatus(resources_list, RA_TOTAL);
}

uint64_t ResourceAccounter::Total(ResourcePathPtr_t resource_path_ptr,
				  PathClass_t rpc) const
{
	br::ResourcePtrList_t matchings(GetList(resource_path_ptr, rpc));
	return QueryStatus(matchings, RA_TOTAL, 0);
}

uint64_t ResourceAccounter::Used(std::string const & path,
				 br::RViewToken_t status_view)
{
	br::ResourcePtrList_t matchings(GetResources(path));
	return QueryStatus(matchings, RA_USED, status_view);
}

uint64_t ResourceAccounter::Used(br::ResourcePtrList_t & resources_list,
				 br::RViewToken_t status_view) const
{
	if (resources_list.empty())
		return 0;
	return QueryStatus(resources_list, RA_USED, status_view);
}

uint64_t ResourceAccounter::Used(ResourcePathPtr_t resource_path_ptr,
				 PathClass_t rpc,
				 br::RViewToken_t status_view) const
{
	br::ResourcePtrList_t matchings(GetList(resource_path_ptr, rpc));
	return QueryStatus(matchings, RA_USED, status_view);
}

uint64_t ResourceAccounter::UsedBy(std::string const & path,
				   ba::SchedPtr_t papp,
				   br::RViewToken_t status_view)
{
	br::ResourcePtrList_t matchings(GetResources(path));
	return QueryStatus(matchings, RA_USED_BY, status_view, papp);
}

uint64_t ResourceAccounter::UsedBy(br::ResourcePtrList_t & resources_list,
				   ba::SchedPtr_t papp,
				   br::RViewToken_t status_view) const
{
	if (resources_list.empty())
		return 0;
	return QueryStatus(resources_list, RA_USED_BY, status_view, papp);
}

uint64_t ResourceAccounter::UsedBy(ResourcePathPtr_t resource_path_ptr,
				   ba::SchedPtr_t papp,
				   PathClass_t rpc,
				   br::RViewToken_t status_view) const
{
	br::ResourcePtrList_t matchings(GetList(resource_path_ptr, rpc));
	return QueryStatus(matchings, RA_USED_BY, status_view, papp);
}

uint64_t ResourceAccounter::Available(std::string const & path,
				      br::RViewToken_t status_view,
				      ba::SchedPtr_t papp)
{
	br::ResourcePtrList_t matchings(GetResources(path));
	return QueryStatus(matchings, RA_AVAIL, status_view, papp);
}

uint64_t ResourceAccounter::Available(br::ResourcePtrList_t & resources_list,
				      br::RViewToken_t status_view,
				      ba::SchedPtr_t papp) const
{
	if (resources_list.empty())
		return 0;
	return QueryStatus(resources_list, RA_AVAIL, status_view, papp);
}

uint64_t ResourceAccounter::Available(ResourcePathPtr_t resource_path_ptr,
				      PathClass_t rpc,
				      br::RViewToken_t status_view,
				      ba::SchedPtr_t papp) const
{
	br::ResourcePtrList_t matchings(GetList(resource_path_ptr, rpc));
	return QueryStatus(matchings, RA_AVAIL, status_view, papp);
}

uint64_t ResourceAccounter::Unreserved(std::string const & path)
{
	br::ResourcePtrList_t matchings(GetResources(path));
	return QueryStatus(matchings, RA_UNRESERVED, 0);
}

uint64_t
ResourceAccounter::Unreserved(br::ResourcePtrList_t & resources_list) const
{
	if (resources_list.empty())
		return 0;
	return QueryStatus(resources_list, RA_UNRESERVED);
}

uint64_t
ResourceAccounter::Unreserved(ResourcePathPtr_t resource_path_ptr) const
{
	br::ResourcePtrList_t matchings(GetList(resource_path_ptr, MIXED));
	return QueryStatus(matchings, RA_UNRESERVED, 0);
}

uint16_t ResourceAccounter::Count(ResourcePathPtr_t resource_path_ptr) const
{
	br::ResourcePtrList_t matchings(GetResources(resource_path_ptr));
	return matchings.size();
}

uint16_t ResourceAccounter::CountPerType(br::ResourceType type) const
{
	auto it = per_type_resource_ids.find(type);
	if (it == per_type_resource_ids.end())
		return 0;
	return it->second.size();
}

br::ResourcePtrList_t
ResourceAccounter::GetList(ResourcePathPtr_t resource_path_ptr,
			   PathClass_t rpc) const
{
	if (rpc == UNDEFINED)
		return GetResources(resource_path_ptr);
	return resources.find_list(*resource_path_ptr, RTFlags(rpc));
}

uint64_t
ResourceAccounter::QueryStatus(br::ResourcePtrList_t const & resources_list,
			       QueryOption_t _att,
			       br::RViewToken_t status_view,
			       ba::SchedPtr_t papp) const
{
	uint64_t value = 0;

	// For all the descriptors in the list add the quantity of resource in the
	// specified state (available, used, total)
	for (br::ResourcePtr_t const & rsrc : resources_list) {
		switch (_att) {
		case RA_AVAIL:
			value += rsrc->Available(papp, status_view);
			break;
		case RA_USED:
			value += rsrc->Used(status_view);
			break;
		case RA_UNRESERVED:
			value += rsrc->Unreserved();
			break;
		case RA_TOTAL:
			value += rsrc->Total();
			break;
		case RA_USED_BY:
			value += rsrc->UsedBy(papp, status_view);
			break;
		}
	}
	return value;
}

uint64_t
ResourceAccounter::GetAssignedAmount(br::ResourceAssignmentMapPtr_t const & assign_map,
				     ba::SchedPtr_t papp,
				     br::RViewToken_t status_view,
				     br::ResourceType r_type,
				     br::ResourceType r_scope_type,
				     BBQUE_RID_TYPE r_scope_id)
{
	if (assign_map == nullptr) {
		logger->Error("GetAssignedAmount: null pointer map");
		return 0;
	}
	logger->Debug("GetAssignedAmount: Getting usage amount from view [%d]", status_view);

	uint64_t amount = 0;
	for (auto const & r_entry : *assign_map) {
		br::ResourcePathPtr_t const & resource_path_ptr(r_entry.first);
		br::ResourceAssignmentPtr_t const & r_assign(r_entry.second);
		logger->Debug("GetAssignedAmount: type:<%-3s> scope:<%-3s>",
			br::GetResourceTypeString(r_type),
			br::GetResourceTypeString(r_scope_type));

		// Scope resource type
		if ((r_scope_type != br::ResourceType::UNDEFINED)
		&& (resource_path_ptr->GetIdentifier(r_scope_type) == nullptr))
			continue;

		// Iterate over the bound resources
		for (auto const & rsrc : r_assign->GetResourcesList()) {
			logger->Debug("GetAssignedAmount: path:<%s>",
				rsrc->Path()->ToString().c_str());
			// Scope resource ID
			if ((r_scope_id >= 0) &&
			(r_scope_id != rsrc->Path()->GetID(r_scope_type)))
				continue;
			// Resource type
			if (rsrc->Path()->Type() != r_type)
				continue;
			amount += rsrc->UsedBy(papp, status_view);
		}
	}
	logger->Debug("GetAssignedAmount: EXC:[%s] R:<%-3s> U:%" PRIu64 "",
		papp->StrId(), br::GetResourceTypeString(r_type), amount);
	return amount;
}

uint64_t
ResourceAccounter::GetAssignedAmount(br::ResourceAssignmentMap_t const & assign_map,
				     br::ResourceType r_type,
				     br::ResourceType r_scope_type,
				     BBQUE_RID_TYPE r_scope_id) const
{
	uint64_t amount = 0;
	for (auto & ru_entry : assign_map) {
		br::ResourcePathPtr_t const & resource_path_ptr(ru_entry.first);
		br::ResourceAssignmentPtr_t const & r_assign(ru_entry.second);
		logger->Debug("GetAssignedAmount: type:<%-3s> scope:<%-3s>",
			br::GetResourceTypeString(r_type),
			br::GetResourceTypeString(r_scope_type));
		// Scope resource type
		if ((r_scope_type != br::ResourceType::UNDEFINED)
		&& (resource_path_ptr->GetIdentifier(r_scope_type) == nullptr))
			continue;
		// Scope resource ID
		if ((r_scope_id >= 0)
		&& (r_scope_id != resource_path_ptr->GetID(r_scope_type)))
			continue;
		// Resource type
		if (resource_path_ptr->Type() != r_type)
			continue;
		amount += r_assign->GetAmount();
	}
	return amount;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::CheckAvailability(br::ResourceAssignmentMapPtr_t const & assign_map,
				     br::RViewToken_t status_view,
				     ba::SchedPtr_t papp) const
{
	uint64_t avail = 0;

	// Check availability for each Usage object
	for (auto const & ru_entry : * (assign_map.get())) {
		br::ResourcePathPtr_t const & rsrc_path(ru_entry.first);
		br::ResourceAssignmentPtr_t const & r_assign(ru_entry.second);

		logger->Debug("CheckAvailability: <%s> mapped to %d resources",
			rsrc_path->ToString().c_str(), r_assign->GetResourcesList().size());

		// Query the availability of the resources in the list
		avail = QueryStatus(r_assign->GetResourcesList(), RA_AVAIL, status_view, papp);
		if (avail < r_assign->GetAmount()) {
			logger->Debug("CheckAvailability: <%s> exceeding request"
				"[USG:%" PRIu64 " | AV:%" PRIu64 " | TOT:%" PRIu64 "] ",
				rsrc_path->ToString().c_str(),
				r_assign->GetAmount(), avail,
				QueryStatus(r_assign->GetResourcesList(), RA_TOTAL));
			return RA_ERR_USAGE_EXC;
		}
	}

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::GetAppAssignmentsByView(br::RViewToken_t status_view,
					   AppAssignmentsMapPtr_t & apps_assign)
{
	// Get the map of all the Apps/EXCs resource assignments
	// (default system resource state view)
	if (status_view == 0) {
		assert(sys_assign_view);
		apps_assign = sys_assign_view;
		return RA_SUCCESS;
	}

	// "Alternate" state view
	auto view_it = assign_per_views.find(status_view);
	if (view_it == assign_per_views.end()) {
		logger->Error("GetAppAssignmentsByView:"
			"Cannot find the resource state view referenced by %d",
			status_view);
		return RA_ERR_MISS_VIEW;
	}

	// Set the the map
	apps_assign = view_it->second;
	return RA_SUCCESS;
}

/************************************************************************
 *                   RESOURCE MANAGEMENT                                *
 ************************************************************************/

br::ResourcePath const & ResourceAccounter::GetPrefixPath() const
{
	return *(r_prefix_path.get());
}

br::ResourcePtr_t
ResourceAccounter::RegisterResource(std::string const & strpath,
				    std::string const & units,
				    uint64_t amount,
				    std::string const & model)
{
	// Build a resource path object (from the string)
	auto resource_path_ptr = std::make_shared<br::ResourcePath>(strpath);
	if (!resource_path_ptr) {
		logger->Fatal("RegisterResource: <%s> is an invalid path",
			strpath.c_str());
		return nullptr;
	}

	// Insert a new resource in the tree
	auto resource_ptr(resources.insert(*(resource_path_ptr.get())));
	if (!resource_ptr) {
		logger->Crit("RegisterResource: <%s> - "
			"unable to allocate a new resource descriptor",
			strpath.c_str());
		return nullptr;
	}
	resource_ptr->SetTotal(br::ConvertValue(amount, units));
	resource_ptr->SetPath(resource_path_ptr);
	resource_ptr->SetModel(model);
	logger->Debug("RegisterResource: <%s> [model=%s] total = %llu %s",
		strpath.c_str(),
		resource_ptr->Model().c_str(),
		resource_ptr->Total(),
		units.c_str());

	// Insert the path in the overall resource path set
	resource_set.emplace(resource_ptr);
	resource_paths.emplace(strpath, resource_path_ptr);
	path_max_len = std::max((int) path_max_len, (int) strpath.length());

	// Track the number of resources per type
	for (auto const & id : resource_path_ptr->GetIdentifiers()) {
		if (per_type_resource_ids.find(id->Type()) == per_type_resource_ids.end()) {
			std::set<BBQUE_RID_TYPE> ids = { id->ID() };
			per_type_resource_ids.emplace(id->Type(), ids);
		}
		else
			per_type_resource_ids[id->Type()].insert(id->ID());
	}

	return resource_ptr;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::UpdateResource(std::string const & _path,
				  std::string const & _units,
				  uint64_t _amount)
{
	uint64_t availability;
	uint64_t reserved;

	// Lookup for the resource to be updated
	auto resource_path_ptr(GetPath(_path));
	if (resource_path_ptr == nullptr) {
		logger->Fatal("Updating resource FAILED "
			"(Error: path [%s] does not reference a specific resource",
			_path.c_str());
		return RA_ERR_INVALID_PATH;
	}

	// Get the path of the resource to update
	auto resource_ptr(GetResource(resource_path_ptr));
	if (resource_ptr == nullptr) {
		logger->Fatal("Updating resource FAILED "
			"(Error: resource [%s] not found",
			resource_path_ptr->ToString().c_str());
		return RA_ERR_NOT_REGISTERED;
	}

	// Resource accounter is not ready now
	WaitForPlatformReady();
	SetState(State::NOT_READY);

	// If the required amount is <= 1, the resource is off-lined
	if (_amount == 0)
		resource_ptr->SetOffline();

	// Check if the required amount is compliant with the total defined at
	// registration time
	availability = br::ConvertValue(_amount, _units);
	if (resource_ptr->Total() < availability) {
		logger->Error("Updating resource FAILED "
			"(Error: availability [%d] exceeding registered amount [%d]",
			availability, resource_ptr->Total());
		SetState(State::READY);
		return RA_ERR_OVERFLOW;
	}

	// Setup reserved amount of resource, considering the units
	reserved = resource_ptr->Total() - availability;
	ReserveResources(resource_path_ptr, reserved);
	resource_ptr->SetOnline();

	// Back to READY
	SetState(State::READY);

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::ReserveResources(ResourcePathPtr_t resource_path_ptr,
				    uint64_t amount)
{
	br::Resource::ExitCode_t rresult;
	auto const & resources_list(resources.find_list(*resource_path_ptr, RT_MATCH_MIXED));
	logger->Info("Reserving [%" PRIu64 "] for [%s] resources...",
		amount, resource_path_ptr->ToString().c_str());

	if (resources_list.empty()) {
		logger->Error("Resource reservation FAILED "
			"(Error: resource [%s] not matching)",
			resource_path_ptr->ToString().c_str());
		return RA_FAILED;
	}

	for (auto & r : resources_list) {
		rresult = r->Reserve(amount);
		if (rresult != br::Resource::RS_SUCCESS) {
			logger->Warn("Reservation: Exceeding value [%" PRIu64 "] for [%s]",
				amount, resource_path_ptr->ToString().c_str());
			return RA_FAILED;
		}
	}

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::ReserveResources(std::string const & path, uint64_t amount)
{
	auto resource_path_ptr(GetPath(path));
	logger->Info("Reserve: built %d from %s",
		resource_path_ptr.get(), path.c_str());

	if (resource_path_ptr == nullptr) {
		logger->Fatal("Reserve resource FAILED "
			"(Error: path [%s] does not reference a specific resource.",
			path.c_str());
		return RA_ERR_INVALID_PATH;
	}

	return ReserveResources(resource_path_ptr, amount);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::SetOffline(std::string const & path)
{
	auto resource_path_ptr = GetPath(path);
	return SetOffline(resource_path_ptr);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::SetOffline(br::ResourcePathPtr_t path)
{
	logger->Info("SetOffline: <%s> -> (virtual) offline...",
		path->ToString().c_str());

	auto resources_list(GetResources(path));
	if (resources_list.empty()) {
		logger->Error("SetOffline: <%s> error: no matchings",
			path->ToString().c_str());
		return RA_FAILED;
	}

	for (auto & resource_ptr : resources_list) {
		resource_ptr->SetOffline();
		logger->Debug("SetOffline: <%s> -> (virtual) offline",
			resource_ptr->Path()->ToString().c_str());
#ifdef CONFIG_BBQUE_PM
		resources_to_power_manage[resource_ptr->Path()] = resource_ptr;
#endif
	}

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::SetOnline(std::string const & path)
{
	auto resource_path_ptr = GetPath(path);
	return SetOnline(resource_path_ptr);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::SetOnline(br::ResourcePathPtr_t path)
{
	logger->Info("SetOnline: <%s> -> online...", path->ToString().c_str());

	auto resources_list(GetResources(path));
	if (resources_list.empty()) {
		logger->Error("SetOnline: <%s> error: no matchings",
			path->ToString().c_str());
		return RA_FAILED;
	}

	for (auto & resource_ptr : resources_list) {
		resource_ptr->SetOnline();
		logger->Debug("SetOnline: <%s> -> online",
			resource_ptr->Path()->ToString().c_str());
#ifdef CONFIG_BBQUE_PM
		resources_to_power_on[resource_ptr->Path()] = resource_ptr;
#endif
	}

	return RA_SUCCESS;
}

bool ResourceAccounter::IsOffline(std::string const & path)
{
	auto resource_path_ptr = GetPath(path);
	return IsOffline(resource_path_ptr);
}

bool ResourceAccounter::IsOffline(ResourcePathPtr_t path) const
{
	logger->Debug("IsOffline: <%s> check virtual offline status...",
		path->ToString().c_str());
	auto resources_list(GetResources(path));
	if (resources_list.empty()) {
		logger->Error("IsOffline: <%s> error: no matchings",
			path->ToString().c_str());
		return true;
	}

	for (auto & resource_ptr : resources_list) {
		if (!resource_ptr->IsOffline()) {
			logger->Debug("IsOffline: <%s> is online",
				resource_ptr->Path()->ToString().c_str());
			return false;
		}
	}

	return true;
}


#ifdef CONFIG_BBQUE_PM

br::ResourcePtr_t ResourceAccounter::DequeueResourceToPowerManage()
{
	if (resources_to_power_manage.empty())
		return nullptr;
	auto it = resources_to_power_manage.begin();
	auto resource_ptr = it->second;
	resources_to_power_manage.erase(it);
	logger->Debug("DequeueResourceToPowerManage: <%> removed",
		resource_ptr->Path()->ToString().c_str());
	return resource_ptr;
}

void
ResourceAccounter::EnqueueResourceToPowerManage(br::ResourcePtr_t resource_ptr,
						br::Resource::PowerSettings config)
{
	resource_ptr->SetPowerSettings(config);
	resources_to_power_manage[resource_ptr->Path()] = resource_ptr;
	logger->Debug("EnqueueResourceToPowerManage: <%> added",
		resource_ptr->Path()->ToString().c_str());
}

bool ResourceAccounter::AnyResourceToPowerManage()
{
	return !resources_to_power_manage.empty();
}

void ResourceAccounter::RestoreResourcesToPowerOn()
{
	PowerManager & pm(PowerManager::GetInstance());

	for (auto & resource_pair : resources_to_power_on) {
		logger->Debug("RestoreResourcesToPowerOn: <%> -> ONLINE",
			resource_pair.first->ToString().c_str());
		pm.SetOn(resource_pair.first);
	}

	resources_to_power_on.clear();
}



#endif // CONFIG_BBQUE_PM

/************************************************************************
 *                   STATE VIEWS MANAGEMENT                             *
 ************************************************************************/

ResourceAccounter::ExitCode_t
ResourceAccounter::GetView(std::string const & req_path, br::RViewToken_t & token)
{
	WaitForPlatformReady();
	return _GetView(req_path, token);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::_GetView(std::string const & req_path, br::RViewToken_t & token)
{
	// Null-string check
	if (req_path.empty()) {
		logger->Error("GetView: Missing a valid string");
		return RA_ERR_MISS_PATH;
	}

	// Token
	token = std::hash<std::string>()(req_path);
	logger->Debug("GetView: new resource state view token = %ld", token);

	// Allocate a new view for the applications resource assignments
	assign_per_views.emplace(token, std::make_shared<AppAssignmentsMap_t>());
	//Allocate a new view for the set of resources allocated
	rsrc_per_views.emplace(token, std::make_shared<ResourceSet_t>());

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::PutView(br::RViewToken_t status_view)
{
	WaitForPlatformReady();
	return _PutView(status_view);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::_PutView(br::RViewToken_t status_view)
{
	// Do nothing if the token references the system state view
	if (status_view == sys_view_token) {
		logger->Warn("PutView: cannot release the system resources view");
		return RA_ERR_UNAUTH_VIEW;
	}

	// Get the resource set using the referenced view
	ResourceViewsMap_t::iterator rviews_it(rsrc_per_views.find(status_view));
	if (rviews_it == rsrc_per_views.end()) {
		logger->Warn("PutView: cannot find resource view token %ld", status_view);
		return RA_ERR_MISS_VIEW;
	}

	// For each resource delete the view
	for (auto & resource_set : * (rviews_it->second))
		resource_set->DeleteView(status_view);

	// Remove the map of Apps/EXCs resource assignments and the resource reference
	// set of this view
	assign_per_views.erase(status_view);
	rsrc_per_views.erase(status_view);

	logger->Debug("PutView: [%ld] cleared view", status_view);
	logger->Debug("PutView: [%ld] currently managed {resource sets = %ld, "
		" assign_map = %d}",
		status_view,
		rsrc_per_views.size(),
		assign_per_views.erase(status_view));

	return RA_SUCCESS;
}

br::RViewToken_t ResourceAccounter::SetView(br::RViewToken_t status_view)
{
	WaitForPlatformReady();
	return _SetView(status_view);
}

br::RViewToken_t ResourceAccounter::_SetView(br::RViewToken_t status_view)
{
	br::RViewToken_t old_sys_status_view;

	// Do nothing if the token references the system state view
	if (status_view == sys_view_token) {
		logger->Debug("SetView: [%ld] is the system state view yet!",
			status_view);
		return sys_view_token;
	}

	// Set the system state view pointer to the map of applications resource
	// usages of this view and point to
	AppAssignmentsViewsMap_t::iterator assign_view_it(assign_per_views.find(status_view));
	if (assign_view_it == assign_per_views.end()) {
		logger->Fatal("SetView: [%ld] unknown view", status_view);
		return sys_view_token;
	}

	// Save the old view token, update the system state view token and the map
	// of Apps/EXCs resource assignments
	old_sys_status_view = sys_view_token;
	sys_view_token      = status_view;
	sys_assign_view     = assign_view_it->second;

	// Put the old view
	_PutView(old_sys_status_view);

	logger->Info("SetView: [%ld] is the new system state view.", sys_view_token);
	logger->Debug("SetView: [%ld] currently managed {resource sets = %ld,"
		" assign_map = %d}",
		sys_view_token,
		rsrc_per_views.size(),
		assign_per_views.erase(status_view));
	return sys_view_token;
}

void ResourceAccounter::SetScheduledView(br::RViewToken_t svt)
{
	// Update the new scheduled view
	br::RViewToken_t old_svt = sch_view_token;
	sch_view_token = svt;

	// Release the old scheduled view if it is not the current system view
	if (old_svt != sys_view_token)
		_PutView(old_svt);
}

/************************************************************************
 *                   SYNCHRONIZATION SUPPORT                            *
 ************************************************************************/

ResourceAccounter::ExitCode_t ResourceAccounter::SyncStart()
{
	ResourceAccounter::ExitCode_t result;
	char tk_path[TOKEN_PATH_MAX_LEN];

	// Synchronization close to start...
	WaitForPlatformReady();
	SetState(State::SYNC);
	logger->Info("SyncMode: start...");

	// Build the path for getting the resource view token
	++sync_ssn.count;
	snprintf(tk_path, TOKEN_PATH_MAX_LEN, "%s%d", SYNC_RVIEW_PATH, sync_ssn.count);
	logger->Debug("SyncMode [%d]: requiring resource state view for %s",
		sync_ssn.count, tk_path);

	// Get a resource state view for the synchronization
	result = _GetView(tk_path, sync_ssn.view);
	if (result != RA_SUCCESS) {
		logger->Fatal("SyncMode [%d]: cannot get a resource state view",
			sync_ssn.count);
		SyncAbort();
		return RA_ERR_SYNC_VIEW;
	}
	logger->Debug("SyncMode [%d]: resource state view token = %ld",
		sync_ssn.count, sync_ssn.view);

	// Init the view with the resource accounting of running applications
	return SyncInit();
}

// NOTE this method should be called while holding the sync session mutex

ResourceAccounter::ExitCode_t ResourceAccounter::SyncInit()
{
	ResourceAccounter::ExitCode_t result;
	AppsUidMapIt apps_it;
	ba::SchedPtr_t papp;

	// Running Applications/ExC
	papp = am.GetFirst(Schedulable::RUNNING, apps_it);
	for ( ; papp; papp = am.GetNext(Schedulable::RUNNING, apps_it)) {
		logger->Info("SyncInit: [%s] current AWM: %d",
			papp->StrId(),
			papp->CurrentAWM()->Id());

		// Re-acquire the resources (these should not have a "Next AWM"!)
		result = _BookResources(
					papp, papp->CurrentAWM()->GetResourceBinding(),
					sync_ssn.view);
		if (result != RA_SUCCESS) {
			logger->Fatal("SyncInit [%d]: resource booking failed for %s."
				" Aborting sync session...",
				sync_ssn.count, papp->StrId());
			SyncAbort();
			return RA_ERR_SYNC_INIT;
		}
	}

	logger->Info("SyncInit [%d]: Initialization finished", sync_ssn.count);
	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::SyncAcquireResources(ba::SchedPtr_t const & papp)
{
	ResourceAccounter::ExitCode_t result = RA_SUCCESS;

	// Check that we are in a synchronized session
	if (!Synching()) {
		logger->Error("SyncMode [%d]: session not open", sync_ssn.count);
		return RA_ERR_SYNC_START;
	}

	// Check next AWM
	if (!papp->NextAWM()) {
		logger->Fatal("SyncMode [%d]: [%s] missing the next AWM",
			sync_ssn.count, papp->StrId());
		SyncAbort();
		return RA_ERR_MISS_AWM;
	}

	// Acquire resources
	br::ResourceAssignmentMapPtr_t const & assign_map(
							papp->NextAWM()->GetResourceBinding());
	result = _BookResources(papp, assign_map, sync_ssn.view);
	if (result != RA_SUCCESS) {
		logger->Fatal("SyncMode [%d]: [%s] resource booking failed",
			sync_ssn.count, papp->StrId());
		SyncAbort();
		return result;
	}

	// Update AWM binding info (resource bitsets)
	logger->Debug("SyncMode [%d]: [%s] updating binding information",
		sync_ssn.count, papp->StrId());
	papp->NextAWM()->UpdateBindingInfo(sync_ssn.view);
	return result;
}

void ResourceAccounter::SyncAbort()
{
	logger->Debug("SyncAbort [%d]: aborting synchronization...",
		sync_ssn.count);
	_PutView(sync_ssn.view);
	SyncFinalize();
	logger->Error("SyncMode [%d]: session aborted", sync_ssn.count);
}

ResourceAccounter::ExitCode_t ResourceAccounter::SyncCommit()
{
	ResourceAccounter::ExitCode_t result = RA_SUCCESS;
	br::RViewToken_t view;

	if (!Synching()) {
		logger->Error("SynCommit: synchronization not started");
		return RA_ERR_SYNC_START;
	}

	// Set the synchronization view as the new system one
	view = _SetView(sync_ssn.view);
	if (view != sync_ssn.view) {
		logger->Fatal("SyncCommit [%d]: "
			"unable to set the new system resource state view",
			sync_ssn.count);
		SyncAbort();
		return RA_ERR_SYNC_VIEW;
	}

	// Release the last scheduled view, by setting it to the system view
	SetScheduledView(sys_view_token);
	SyncFinalize();
	logger->Info("SyncCommit [%d]: session committed", sync_ssn.count);

	// Log the status report
	PrintStatus();
	return result;
}

ResourceAccounter::ExitCode_t ResourceAccounter::SyncFinalize()
{
	logger->Debug("SyncFinalize: ending synchronization...");
	if (!_Synching()) {
		logger->Error("SyncFinalize: synchronization not started");
		return RA_ERR_SYNC_START;
	}

	SetState(State::READY);
	logger->Info("SyncFinalize [%d]: session closed", sync_ssn.count);
	return RA_SUCCESS;
}

void ResourceAccounter::SyncWait()
{
	std::unique_lock<std::mutex> status_ul(status_mtx);
	while (status == State::SYNC)
		status_cv.wait(status_ul);
}

/************************************************************************
 *                   RESOURCE ACCOUNTING                                *
 ************************************************************************/

ResourceAccounter::ExitCode_t
ResourceAccounter::_BookResources(ba::SchedPtr_t papp,
				  br::ResourceAssignmentMapPtr_t const & assign_map,
				  br::RViewToken_t status_view)
{
	return IncBookingCounts(assign_map, papp, status_view);
}

ResourceAccounter::ExitCode_t
ResourceAccounter::BookResources(ba::SchedPtr_t papp,
				 br::ResourceAssignmentMapPtr_t const & assign_map,
				 br::RViewToken_t status_view)
{
	logger->Debug("Booking: assigning resources to [%s]", papp->StrId());

	// Check to avoid null pointer segmentation fault
	if (!papp) {
		logger->Fatal("Booking: application descriptor null pointer");
		return RA_ERR_MISS_APP;
	}

	// Check that the set of resource assignments is not null
	if ((!assign_map) || (assign_map->empty())) {
		logger->Fatal("Booking: empty resource assignments set");
		return RA_ERR_MISS_USAGES;
	}

	// Check resource availability (if this is not a sync session)
	// TODO refine this check to consider possible corner cases:
	// 1. scheduler running while in sync
	// 2. resource availability decrease while in sync
	if (!Synching()) {
		if (CheckAvailability(assign_map, status_view) == RA_ERR_USAGE_EXC) {
			logger->Debug("Booking: cannot allocate the assigned resource set");
			return RA_ERR_USAGE_EXC;
		}
	}

	// Increment the booking counts and save the reference to the resource set
	// used by the application
	return IncBookingCounts(assign_map, papp, status_view);
}

void ResourceAccounter::ReleaseResources(ba::SchedPtr_t papp,
					 br::RViewToken_t status_view)
{
	std::unique_lock<std::mutex> sync_ul(status_mtx);
	if (!papp) {
		logger->Fatal("Release: application descriptor null pointer");
		return;
	}

	if (rsrc_per_views.find(status_view) == rsrc_per_views.end()) {
		logger->Debug("Release: resource state view already cleared");
		return;
	}

	// Decrease resources in the sync view
	if (status_view == 0 && _Synching())
		_ReleaseResources(papp, sync_ssn.view);

	// Decrease resources in the required view
	if (status_view != sync_ssn.view)
		_ReleaseResources(papp, status_view);
}

void ResourceAccounter::_ReleaseResources(ba::SchedPtr_t papp,
					  br::RViewToken_t status_view)
{
	// Get the map of applications resource assignments related to the state view
	// referenced by 'status_view'
	AppAssignmentsMapPtr_t apps_assign;
	if (GetAppAssignmentsByView(status_view, apps_assign) == RA_ERR_MISS_VIEW) {
		logger->Fatal("Release: resource view unavailable");
		return;
	}

	// Get the map of resource assignments of the application
	auto usemap_it(apps_assign->find(papp->Uid()));
	if (usemap_it == apps_assign->end()) {
		logger->Debug("Release: resource set not assigned");
		return;
	}

	// Decrement resources counts and remove the assign_map map
	DecBookingCounts(usemap_it->second, papp, status_view);
	apps_assign->erase(papp->Uid());
	logger->Debug("Release: [%s] resource release terminated", papp->StrId());
}

ResourceAccounter::ExitCode_t
ResourceAccounter::IncBookingCounts(br::ResourceAssignmentMapPtr_t const & assign_map,
				    ba::SchedPtr_t const & papp,
				    br::RViewToken_t status_view)
{
	ResourceAccounter::ExitCode_t result;
	logger->Debug("IncBooking: getting the assigned amount from view [%ld]...",
		status_view);

	// Get the set of resources referenced in the view
	auto rsrc_view(rsrc_per_views.find(status_view));
	assert(rsrc_view != rsrc_per_views.end());
	if (rsrc_view == rsrc_per_views.end()) {
		logger->Fatal("IncBooking: invalid resource state view token [%ld]",
			status_view);
		return RA_ERR_MISS_VIEW;
	}
	auto & rsrc_set(rsrc_view->second);

	// Get the map of resources used by the application (from the state view
	// referenced by 'status_view').
	AppAssignmentsMapPtr_t apps_assign;
	if (GetAppAssignmentsByView(status_view, apps_assign) == RA_ERR_MISS_VIEW) {
		logger->Fatal("IncBooking: no applications using resource in state view "
			"[%ld]", status_view);
		return RA_ERR_MISS_APP;
	}

	// Each application can hold just one resource assignments set
	auto app_assign_map_it(apps_assign->find(papp->Uid()));
	if (app_assign_map_it != apps_assign->end()) {
		logger->Warn("IncBooking: [%s] currently using a resource set yet",
			papp->StrId());
		return RA_ERR_APP_USAGES;
	}

	// Book resources for the application
	for (auto & ru_entry : * (assign_map.get())) {
		br::ResourcePathPtr_t const & rsrc_path(ru_entry.first);
		br::ResourceAssignmentPtr_t & r_assign(ru_entry.second);
		logger->Debug("IncBooking: [%s] requires resource <%s>: % " PRIu64 " ",
			papp->StrId(),
			rsrc_path->ToString().c_str(), r_assign->GetAmount());

		// Do booking for the current resource request
		result = DoResourceBooking(papp, r_assign, status_view, rsrc_set);
		if (result != RA_SUCCESS)  {
			logger->Crit("IncBooking: [%s] unexpected fail! <%s> "
				"[USG:%" PRIu64 " | AV:%" PRIu64 " | TOT:%" PRIu64 "]",
				papp->StrId(),
				rsrc_path->ToString().c_str(),
				r_assign->GetAmount(),
				Available(rsrc_path, MIXED, status_view, papp),
				Total(rsrc_path, MIXED));
		}

		assert(result == RA_SUCCESS);
		logger->Debug("IncBooking: [%s] R<%s> SUCCESS "
			"[U:%" PRIu64 " | A:%" PRIu64 " | T:%" PRIu64 "]"
			" view=[%ld]",
			papp->StrId(),
			rsrc_path->ToString().c_str(), r_assign->GetAmount(),
			Available(rsrc_path, MIXED, status_view, papp),
			Total(rsrc_path, MIXED), status_view);
	}

	apps_assign->emplace(papp->Uid(), assign_map);
	logger->Debug("IncBooking: [%s] now holds %d resource(s) - view=[%ld]",
		papp->StrId(), assign_map->size(), status_view);

	return RA_SUCCESS;
}

ResourceAccounter::ExitCode_t
ResourceAccounter::DoResourceBooking(ba::SchedPtr_t const & papp,
				     br::ResourceAssignmentPtr_t & r_assign,
				     br::RViewToken_t status_view,
				     ResourceSetPtr_t & rsrc_set)
{
	// Amount of resource to book and list of resource descriptors
	auto requested = r_assign->GetAmount();
	size_t num_left_resources = r_assign->GetResourcesList().size();
	logger->Debug("DoResourceBooking: amount % " PRIu64 " to be spread over %d resources",
		requested, num_left_resources);
	auto alloc_amount_per_resource = 0;

	br::ResourceAssignment::Policy alloc_policy = r_assign->GetPolicy();

	// Get the list of the bound resources
	for (auto & resource : r_assign->GetResourcesList()) {
		// Break if the required resource has been completely allocated
		if (requested == 0)
			break;
		// Add the current resource binding to the set of resources used in
		// the view referenced by 'status_view'
		rsrc_set->insert(resource);

		// Synchronization: booking according to scheduling decisions
		if (Synching()) {
			SyncResourceBooking(papp, resource, requested);
			continue;
		}

		// In case of "balanced" filling policy, spread the requested amount
		// of resource over all the resources of the given binding
		if (alloc_policy == br::ResourceAssignment::Policy::BALANCED)
			alloc_amount_per_resource = requested / num_left_resources;

		// Scheduling: allocate required resource among its bindings
		SchedResourceBooking(
				papp, resource, status_view, requested,
				alloc_amount_per_resource);

		--num_left_resources;

		logger->Debug("DoResourceBooking: <%s> requested=%d num_left=%d",
			resource->Path()->ToString().c_str(),
			requested,
			num_left_resources);
	}

	// The availability of resources mismatches the one checked in the
	// scheduling phase. This should never happen!
	if (requested != 0 && alloc_policy == br::ResourceAssignment::Policy::BALANCED) {
		logger->Crit("DoResourceBooking: resource assignment mismatch in view=[%ld]."
			" Left=%d", status_view, requested);
		assert(requested != 0);
		return RA_ERR_USAGE_EXC;
	}

	return RA_SUCCESS;
}

void ResourceAccounter::SchedResourceBooking(ba::SchedPtr_t const & papp,
					     br::ResourcePtr_t & rsrc,
					     br::RViewToken_t status_view,
					     uint64_t & requested,
					     uint64_t alloc_amount_per_resource)
{
	// Check the available amount in the current resource binding
	uint64_t available = rsrc->Available(papp, status_view);
	logger->Debug("SchedResourceBooking: [%s] request for <%s> [view=%ld] ",
		papp->StrId(),
		rsrc->Path()->ToString().c_str(),
		status_view);

	logger->Debug("SchedResourceBooking: [%s] request for <%s> "
		"requested=%d alloca=%d available=%d",
		papp->StrId(),
		rsrc->Path()->ToString().c_str(),
		requested,
		alloc_amount_per_resource,
		available);

	if ((alloc_amount_per_resource > 0) &&
	(alloc_amount_per_resource <= available))
		requested -= rsrc->Acquire(papp, alloc_amount_per_resource, status_view);
	else if (requested < available)
		requested -= rsrc->Acquire(papp, requested, status_view);
	else
		requested -= rsrc->Acquire(papp, available, status_view);
}

void ResourceAccounter::SyncResourceBooking(ba::SchedPtr_t const & papp,
					    br::ResourcePtr_t & rsrc,
					    uint64_t & requested)
{
	// Skip the resource binding if the not assigned by the scheduler
	uint64_t sched_usage = rsrc->UsedBy(papp, sch_view_token);
	if (sched_usage == 0) {
		logger->Debug("SyncResourceBooking: [%s] no assignment of <%s>",
			papp->StrId(), rsrc->Name().c_str());
		return;
	}

	// Acquire the resource according to the amount assigned by the
	// scheduler
	requested -= rsrc->Acquire(papp, sched_usage, sync_ssn.view);
	logger->Debug("SyncResourceBooking: [%s] acquires %s (%d left) in view=[%ld]",
		papp->StrId(), rsrc->Name().c_str(), requested, sch_view_token);
}

void ResourceAccounter::DecBookingCounts(br::ResourceAssignmentMapPtr_t const & assign_map,
					 ba::SchedPtr_t const & papp,
					 br::RViewToken_t status_view)
{
	ExitCode_t ra_result;
	logger->Debug("DecCount: [%s] holds %d resources in view=[%ld]",
		papp->StrId(), assign_map->size(), status_view);

	// Get the set of resources referenced in the view
	ResourceViewsMap_t::iterator rsrc_view(rsrc_per_views.find(status_view));
	if (rsrc_view == rsrc_per_views.end()) {
		logger->Fatal("DecCount: invalid resource state view: [%ld]", status_view);
		return;
	}
	auto & rsrc_set(rsrc_view->second);

	// Release the all the resources hold by the Application/EXC
	for (auto & ru_entry : * (assign_map.get())) {
		br::ResourcePathPtr_t const & rsrc_path(ru_entry.first);
		br::ResourceAssignmentPtr_t & r_assign(ru_entry.second);
		// Release the resources bound to the current request
		ra_result = UndoResourceBooking(papp, r_assign, status_view, rsrc_set);
		if (ra_result == RA_ERR_MISS_VIEW)
			return;
		logger->Debug("DecCount: [%s] has freed {%s} of %" PRIu64 "",
			papp->StrId(), rsrc_path->ToString().c_str(),
			r_assign->GetAmount());
	}
}

ResourceAccounter::ExitCode_t
ResourceAccounter::UndoResourceBooking(ba::SchedPtr_t const & papp,
				       br::ResourceAssignmentPtr_t & r_assign,
				       br::RViewToken_t status_view,
				       ResourceSetPtr_t & rsrc_set)
{
	// Keep track of the amount of resource freed
	uint64_t usage_freed = 0;

	// For each resource binding release the amount allocated to the App/EXC
	assert(!r_assign->GetResourcesList().empty());
	for (br::ResourcePtr_t & rsrc : r_assign->GetResourcesList()) {
		if (usage_freed == r_assign->GetAmount())
			break;

		// Release the quantity hold by the Application/EXC
		usage_freed += rsrc->Release(papp, status_view);

		// If no more applications are using this resource, remove it from
		// the set of resources referenced in the resource state view
		if ((rsrc_set) && (rsrc->ApplicationsCount() == 0))
			rsrc_set->erase(rsrc);
	}
	assert(usage_freed == r_assign->GetAmount());
	return RA_SUCCESS;
}

/************************************************************************
 *                   COMMANDS HANDLING                                  *
 ************************************************************************/

int ResourceAccounter::CommandsCb(int argc, char *argv[])
{
	uint8_t cmd_offset = ::strlen(RESOURCE_ACCOUNTER_NAMESPACE) + 1;
	char * command_id  = argv[0] + cmd_offset;
	logger->Info("Processing command [%s]", command_id);

	// Set a new resource total quota
	if (!strncmp(CMD_SET_TOTAL, command_id, strlen(CMD_SET_TOTAL))) {
		if (argc != 3) {
			logger->Error("'%s' expecting 2 parameters.", CMD_SET_TOTAL);
			logger->Error("Ex: 'bq.ra.%s <resource_path> (e.g., sys0.cpu0.pe0)"
				" <new_total_value> (e.g. 90)'", CMD_SET_TOTAL);
			return 1;
		}
		return SetResourceTotalHandler(argv[1], argv[2]);
	}

	logger->Error("Unexpected command: %s", command_id);

	return 0;
}

int ResourceAccounter::SetResourceTotalHandler(char * r_path, char * value)
{
	uint64_t amount = atoi(value);

	ExitCode_t ra_result = UpdateResource(r_path, "", amount);
	if (ra_result != RA_SUCCESS) {
		logger->Error("SetResourceTotalHandler: "
			"cannot set quota %" PRIu64 " to [%s]", amount, r_path);
		return 2;
	}

	logger->Info("SetResourceTotalHandler: "
		"set quota %" PRIu64 " to [%s]", amount, r_path);
	PrintStatus(0, true);

	return 0;
}


}   // namespace bbque
