/*
 * Copyright (C) 2017  Politecnico di Milano
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

#ifndef BBQUE_REMOTE_PLATFORM_PROXY_H
#define BBQUE_REMOTE_PLATFORM_PROXY_H


#include "bbque/platform_proxy.h"
#include "bbque/plugins/agent_proxy_if.h"

#define REMOTE_PLATFORM_PROXY_NAMESPACE "bb.pp.rpp"

namespace bbque {
namespace pp {

class RemotePlatformProxy : public PlatformProxy, public bbque::plugins::AgentProxyIF
{
public:

	RemotePlatformProxy();

	virtual ~RemotePlatformProxy() { }

	/**
	 * @brief Platform specific resource setup interface.
	 */
	ExitCode_t Setup(SchedPtr_t papp);

	/**
	 * @brief Register/enumerate resources from remote nodes
	 */
	ExitCode_t LoadPlatformData();

	/**
	 * @brief Refresh resources availability from remote nodes
	 */
	ExitCode_t Refresh();

	/**
	 * @brief Release from remote nodes.
	 */
	ExitCode_t Release(SchedPtr_t papp);

	/**
	 * @brief Reclaim resources from remote nodes.
	 */
	ExitCode_t ReclaimResources(SchedPtr_t papp);

	/**
	 * @brief Map resources assignments on remote nodes.
	 */
	ExitCode_t MapResources(
				SchedPtr_t papp, ResourceAssignmentMapPtr_t pres, bool excl = true);

	/**
	 * @brief Actuate power management on remote nodes.
	 */
	ExitCode_t ActuatePowerManagement() override;

	/**
	 * @brief Actuate power management on a specific resource locate on a
	 * remote node.
	 */
	ExitCode_t ActuatePowerManagement(bbque::res::ResourcePtr_t resource) override;

	/**
	 * @brief Check if a remote resource is "high-performance" qualified.
	 */
	bool IsHighPerformance(
			bbque::res::ResourcePathPtr_t const & path) const override;

	/**
	 * @brief Remote platform termination.
	 */
	void Exit();


	//--- AgentProxy wrapper functions

	void StartServer();

	void StopServer();

	void WaitForServerToStop();


	bbque::agent::ExitCode_t GetResourceStatus(
						std::string const & resource_path, agent::ResourceStatus & status);


	bbque::agent::ExitCode_t GetWorkloadStatus(
						std::string const & system_path, agent::WorkloadStatus & status);

	bbque::agent::ExitCode_t GetWorkloadStatus(
						int system_id, agent::WorkloadStatus & status);


	bbque::agent::ExitCode_t GetChannelStatus(
						std::string const & system_path, agent::ChannelStatus & status);

	bbque::agent::ExitCode_t GetChannelStatus(
						int system_id, agent::ChannelStatus & status);


	bbque::agent::ExitCode_t SendJoinRequest(std::string const & system_path);

	bbque::agent::ExitCode_t SendJoinRequest(int system_id);


	bbque::agent::ExitCode_t SendDisjoinRequest(std::string const & system_path);

	bbque::agent::ExitCode_t SendDisjoinRequest(int system_id);


	bbque::agent::ExitCode_t SendScheduleRequest(
						std::string const & system_path,
						agent::ApplicationScheduleRequest const & request);

private:

	/**
	 * @brief The logger used by the worker thread
	 */
	std::unique_ptr<bu::Logger> logger;

	std::unique_ptr<bbque::plugins::AgentProxyIF> agent_proxy;

	ExitCode_t LoadAgentProxy();

};
} // namespace pp
} // namespace bbque

#endif // BBQUE_REMOTE_PLATFORM_PROXY_H
