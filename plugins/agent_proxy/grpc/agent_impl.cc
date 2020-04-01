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

#include "agent_impl.h"

#include "bbque/config.h"

#ifdef CONFIG_BBQUE_PM
#include "bbque/pm/power_manager.h"
#endif

namespace bbque {
namespace plugins {

grpc::Status
AgentImpl::GetResourceStatus(grpc::ServerContext * context,
			     const bbque::ResourceStatusRequest * request,
			     bbque::ResourceStatusReply * reply)
{
	logger->Debug("ResourceStatus: request from sys%d for sys%d",
		request->sender_id(), request->dest_id());
	if (request->path().empty()) {
		logger->Error("ResourceStatus: invalid resource path specified");
		return grpc::Status::CANCELLED;
	}

	// Call ResourceAccounter member functions...
	int64_t total = system.ResourceTotal(request->path());
	int64_t used  = system.ResourceUsed(request->path());
	reply->set_total(total);
	reply->set_used(used);

	// Power information...
	bbque::res::ResourcePtr_t resource(system.GetResource(request->path()));
	if (resource == nullptr) {
		logger->Error("ResourceStatus: invalid resource path specified");
		return grpc::Status::CANCELLED;
	}

	bbque::res::ResourcePathPtr_t resource_path(
						system.GetResourcePath(request->path()));
	if (resource_path == nullptr) {
		logger->Error("ResourceStatus: invalid resource path specified");
		return grpc::Status::CANCELLED;
	}

	uint32_t degr_perc = 100;
	uint32_t power_mw = 0, temp = 0, load = 0;
#ifdef CONFIG_BBQUE_PM
	bbque::PowerManager & pm(bbque::PowerManager::GetInstance());
	pm.GetPowerUsage(resource_path, power_mw);
	pm.GetTemperature(resource_path, temp);
	pm.GetLoad(resource_path, load);
#endif
	reply->set_degradation(degr_perc);
	reply->set_power_mw(power_mw);
	reply->set_temperature(temp);
	reply->set_load(load);

	return grpc::Status::OK;
}

grpc::Status
AgentImpl::GetWorkloadStatus(grpc::ServerContext * context,
			     const bbque::GenericRequest * request,
			     bbque::WorkloadStatusReply * reply)
{
	logger->Debug("WorkloadStatus: request from sys%d for sys%d",
		request->sender_id(), request->dest_id());
	reply->set_nr_running(system.ApplicationsCount(
		bbque::app::ApplicationStatusIF::RUNNING));
	reply->set_nr_ready(system.ApplicationsCount(
		bbque::app::ApplicationStatusIF::READY));

	return grpc::Status::OK;
}

grpc::Status
AgentImpl::GetChannelStatus(grpc::ServerContext * context,
			    const bbque::GenericRequest * request,
			    bbque::ChannelStatusReply * reply)
{
	logger->Debug("ChannelStatus: request from sys%d for sys%d",
		request->sender_id(), request->dest_id());
	reply->set_connected(true);

	return grpc::Status::OK;
}

grpc::Status
AgentImpl::SetNodeManagementAction(grpc::ServerContext * context,
				   const bbque::NodeManagementRequest * action,
				   bbque::GenericReply * error)
{
	logger->Debug(" === SetNodeManagementAction ===");
	logger->Info("Management action: %d ", action->value());
	error->set_value(bbque::GenericReply::OK);

	return grpc::Status::OK;
}

} // namespace plugins

} // namespace bbque
