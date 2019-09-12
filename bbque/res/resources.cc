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

#include "bbque/res/resources.h"
#include "bbque/resource_accounter.h"

#define MODULE_NAMESPACE "bq.re"

namespace bu = bbque::utils;

namespace bbque { namespace res {

/*****************************************************************************
 * class Resource
 *****************************************************************************/

Resource::Resource(br::ResourceType type, BBQUE_RID_TYPE id, uint64_t tot):
	br::ResourceIdentifier(type, id),
	total(tot),
	reserved(0),
	offline(false) {
	name = std::string(GetResourceTypeString(type)) + std::to_string(id);

	// Initialize profiling data structures
	InitProfilingInfo();
}


void Resource::InitProfilingInfo() {
	av_profile.online_tmr.start();
	av_profile.lastOfflineTime = 0;
	av_profile.lastOnlineTime  = 0;
#ifdef CONFIG_BBQUE_PM
	pw_profile.values.resize(8);
#endif
	rb_profile.degradation_perc = std::make_shared<bu::EMA>(3);
}


Resource::ExitCode_t Resource::Reserve(uint64_t amount) {

	if (amount > total)
		return RS_FAILED;

	reserved = amount;
	DB(fprintf(stderr, FD("Resource {%s}: update reserved to [%" PRIu64 "] "
					"=> available [%" PRIu64 "]\n"),
				name.c_str(), reserved, total-reserved));
	return RS_SUCCESS;
}

void Resource::SetOffline() {
	if (offline)
		return;
	offline = true;

	// Keep track of last on-lining time
	av_profile.offline_tmr.start();
	av_profile.lastOnlineTime = av_profile.online_tmr.getElapsedTimeMs();

	fprintf(stderr, FI("Resource {%s} OFFLINED, last on-line %.3f[s]\n"),
			name.c_str(), (av_profile.lastOnlineTime / 1000.0));

}

void Resource::SetOnline() {
	if (!offline)
		return;
	offline = false;

	// Keep track of last on-lining time
	av_profile.online_tmr.start();
	av_profile.lastOfflineTime = av_profile.offline_tmr.getElapsedTimeMs();

	fprintf(stderr, FI("Resource {%s} ONLINED, last off-line %.3f[s]\n"),
			name.c_str(), (av_profile.lastOfflineTime / 1000.0));

}


uint64_t Resource::Used(RViewToken_t view_id) {
	// Retrieve the state view
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view)
		return 0;

	// Return the "used" value
	return view->used;
}

uint64_t Resource::Available(SchedPtr_t papp, RViewToken_t view_id) {
	uint64_t total_available = Unreserved();
	ResourceStatePtr_t view;

	// Offlined resources are considered not available
	if (IsOffline())
		return 0;

	// Retrieve the state view
	view = GetStateView(view_id);
	// If the view is not found, it means that nothing has been allocated.
	// Thus the availability value to return is the total amount of
	// resource
	if (!view)
		return total_available;

	// Remove resources already allocated in this vew
	total_available -= view->used;
	// Return the amount of available resource
	if (!papp)
		return total_available;

	// Add resources allocated by requesting applicatiion
	total_available += ApplicationUsage(papp, view->apps);
	return total_available;

}

uint64_t Resource::ApplicationUsage(SchedPtr_t const & papp, RViewToken_t view_id) {
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view) {
		DB(fprintf(stderr, FW("Resource {%s}: cannot find view %" PRIu64 "\n"),
					name.c_str(), view_id));
		return 0;
	}

	// Call the "low-level" ApplicationUsage()
	return ApplicationUsage(papp, view->apps);
}

Resource::ExitCode_t Resource::UsedBy(AppUid_t & app_uid,
		uint64_t & amount,
		uint8_t nth,
		RViewToken_t view_id) {
	// Get the map of Apps/EXCs using the resource
	AppUsageQtyMap_t apps_map;
	size_t mapsize = ApplicationsCount(apps_map, view_id);
	size_t count = 0;
	app_uid = 0;
	amount  = 0;

	// Index overflow check
	if (nth >= mapsize)
		return RS_NO_APPS;

	// Search the nth-th App/EXC using the resource
	for (auto const & apps_it: apps_map) {
		// Skip until the required index has not been reached
		if (count < nth) continue;
		// Return the amount of resource used and the App/EXC Uid
		amount  = apps_it.second;
		app_uid = apps_it.first;
		++count;

		return RS_SUCCESS;
	}

	return RS_NO_APPS;
}


uint64_t Resource::Acquire(SchedPtr_t const & papp, uint64_t amount,
		RViewToken_t view_id) {
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view) {
		view = std::make_shared<ResourceState>();
		state_views[view_id] = view;
	}

	// Try to set the new "used" value
	uint64_t fut_used = view->used + amount;
	if (fut_used > total)
		return 0;

	// Set new used value and application that requested the resource
	view->used = fut_used;
	view->apps[papp->Uid()] = amount;
	return amount;
}

uint64_t Resource::Release(SchedPtr_t const & papp, RViewToken_t view_id) {
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view) {
		DB(fprintf(stderr,
			FW("Resource {%s}: cannot find view %" PRIu64 "\n"),
				name.c_str(), view_id));
		return 0;
	}
	return Release(papp->Uid(), view);
}

uint64_t Resource::Release(AppUid_t app_uid, RViewToken_t view_id) {
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view) {
		DB(fprintf(stderr,
			FW("Resource {%s}: cannot find view %" PRIu64 "\n"),
				name.c_str(), view_id));
		return 0;
	}
	return Release(app_uid, view);
}

uint64_t Resource::Release(AppUid_t app_uid, ResourceStatePtr_t view) {
	// Lookup the application using the resource
	auto lkp = view->apps.find(app_uid);
	if (lkp == view->apps.end()) {
		DB(fprintf(stderr, FD("Resource {%s}: no resources allocated to uid=%d\n"),
					name.c_str(), app_uid));
		return 0;
	}

	// Decrease the used value and remove the application
	uint64_t used_by_app = lkp->second;
	view->used -= used_by_app;
	view->apps.erase(app_uid);

	// Return the amount of resource released
	return used_by_app;
}


void Resource::DeleteView(RViewToken_t view_id) {
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	// Avoid to delete the default view
	if (view_id == ra.GetSystemView())
		return;
	state_views.erase(view_id);
}

uint16_t Resource::ApplicationsCount(AppUsageQtyMap_t & apps_map, RViewToken_t view_id) {
	ResourceStatePtr_t view(GetStateView(view_id));
	if (!view)
		return 0;
	// Return the size and a reference to the map
	apps_map = view->apps;
	return apps_map.size();
}

uint64_t Resource::ApplicationUsage(SchedPtr_t const & papp, AppUsageQtyMap_t & apps_map) {
	if (!papp) {
		DB(fprintf(stderr, FW("Resource {%s}: App/EXC null pointer\n"),
					name.c_str()));
		return 0;
	}

	// Retrieve the application from the map
	auto app_using_it(apps_map.find(papp->Uid()));
	if (app_using_it == apps_map.end()) {
		DB(fprintf(stderr, FD("Resource {%s}: no usage value for [%s]\n"),
					name.c_str(), papp->StrId()));
		return 0;
	}

	// Return the amount of resource used
	return app_using_it->second;
}

ResourceStatePtr_t Resource::GetStateView(RViewToken_t view_id) {
	ResourceAccounter &ra(ResourceAccounter::GetInstance());
	// Default view if token = 0
	if (view_id == 0)
		view_id = ra.GetSystemView();

	// Retrieve the view from hash map otherwise
	auto it = state_views.find(view_id);
	if (it != state_views.end())
		return it->second;

	return nullptr;
}

#ifdef CONFIG_BBQUE_PM

void Resource::EnablePowerProfile(
		PowerManager::SamplesArray_t const & samples_window) {
	pw_profile.enabled_count = 0;

	// Check each power profiling information
	for (uint i = 0; i < samples_window.size(); ++i) {
		if (samples_window[i] <= 0)
			continue;
		++pw_profile.enabled_count;

		// Is the sample window size changed?
		if (pw_profile.values[i] &&
				pw_profile.samples_window[i] == samples_window[i])
			continue;

		// New sample window size
		pw_profile.values[i] = std::make_shared<bu::EMA>(samples_window[i]);
	}
	pw_profile.samples_window = samples_window;
}


void Resource::EnablePowerProfile() {
	EnablePowerProfile(default_samples_window);
}

double Resource::GetPowerInfo(PowerManager::InfoType i_type, ValueType v_type) {
	std::unique_lock<std::mutex> ul(pw_profile.mux);
	if (!pw_profile.values[int(i_type)])
		return 0.0;
	// Instant or mean value?
	switch (v_type) {
	case INSTANT:
		return pw_profile.values[int(i_type)]->last_value();
	case MEAN:
		return pw_profile.values[int(i_type)]->get();
	}
	return 0.0;
}

#endif // CONFIG_BBQUE_PM

}}
