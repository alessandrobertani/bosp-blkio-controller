/*
 * Copyright (C) 2019  Politecnico di Milano
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

#include "bbque/reliability_manager.h"
#include "bbque/resource_manager.h"

#undef MODULE_NAMESPACE
#define MODULE_NAMESPACE "bq.lm"


namespace bbque
{

ReliabilityManager & ReliabilityManager::GetInstance()
{
	static ReliabilityManager instance;
	return instance;
}

ReliabilityManager::ReliabilityManager():
	cm(CommandManager::GetInstance()),
	ra(ResourceAccounter::GetInstance()),
	am(ApplicationManager::GetInstance()),
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	prm(ProcessManager::GetInstance()),
#endif
	plm(PlatformManager::GetInstance()),
	logger(bu::Logger::GetLogger(MODULE_NAMESPACE))
{

	// Notify the degradation of a resource
#define CMD_NOTIFY_DEGRADATION "notify_degradation"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_NOTIFY_DEGRADATION,
	                   static_cast<CommandHandler*>(this),
	                   "Performance degradation affecting the resource "
	                   "[percentage]");

#define CMD_SIMULATE_FAULT "simulate_fault"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_SIMULATE_FAULT,
	                   static_cast<CommandHandler*>(this),
	                   "Simulate the occurrence of a resource fault");

#define CMD_FREEZE "freeze"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_FREEZE,
	                   static_cast<CommandHandler*>(this),
	                   "Freeze a managed application or process");

#define CMD_THAW "thaw"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_THAW,
	                   static_cast<CommandHandler*>(this),
	                   "Thaw a managed application or process");

#define CMD_CHECKPOINT "checkpoint"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_CHECKPOINT,
	                   static_cast<CommandHandler*>(this),
	                   "Checkpoint of a managed application or process");

#define CMD_RESTORE "restore"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_RESTORE,
	                   static_cast<CommandHandler*>(this),
	                   "Restore a managed application or process");

}


ReliabilityManager::~ReliabilityManager()
{

}

void ReliabilityManager::Task()
{

}

void ReliabilityManager::NotifyFaultDetection(res::ResourcePtr_t rsrc)
{
	// Freeze involved applications or processes
	br::AppUsageQtyMap_t apps;
	rsrc->Applications(apps);
	logger->Debug("NotifyFaultDetection: <%s> used by <%d> applications",
	              rsrc->Path()->ToString().c_str(),
	              apps.size());

	ba::SchedPtr_t psched;
	for (auto app_entry : apps) {
		psched = am.GetApplication(app_entry.first);

#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
		if (!psched) {
			psched = prm.GetProcess(app_entry.first);
			logger->Debug("NotifyFaultDetection: <%s> is a process",
			              psched->StrId());
		}
#endif
		if (!psched) {
			logger->Warn("NotifyFaultDetection: UID=<%d>"
			             ": no application or process",
			             app_entry.first);
			continue;
		}

		logger->Debug("NotifyFaultDetection: <%s> => freeze <%s>",
		              rsrc->Path()->ToString().c_str(),
		              psched->StrId());

		auto ret = plm.Freeze(psched);
		if (ret != ReliabilityActionsIF::ExitCode_t::OK) {
			logger->Error("NotifyFaultDetection: <%s> => <%s> "
			              "platform failure while freezing",
			              rsrc->Path()->ToString().c_str(),
			              psched->StrId());
			continue;
		}
		logger->Info("NotifyFaultDetection: <%s> => <%s> successfully frozen");
	}

	// Set offline faulty resources
	logger->Debug("NotifyFaultDetection: <%s> to switch off",
	              rsrc->Path()->ToString().c_str());
	ra.SetOffline(rsrc->Path());

	// Trigger policy execution by notifying a "platform" event
	ResourceManager & rm = ResourceManager::GetInstance();
	rm.NotifyEvent(ResourceManager::BBQ_PLAT);
}


/************************************************************************
 *                   COMMANDS HANDLING                                  *
 ************************************************************************/

int ReliabilityManager::CommandsCb(int argc, char * argv[])
{
	uint8_t cmd_offset = ::strlen(MODULE_NAMESPACE) + 1;
	char * cmd_id  = argv[0] + cmd_offset;
	logger->Info("CommandsCb: processing command [%s]", cmd_id);

	// Set the degradation value of the given resources
	if (!strncmp(CMD_NOTIFY_DEGRADATION, cmd_id, strlen(CMD_NOTIFY_DEGRADATION))) {
		if (!(argc % 2)) {
			logger->Error("'%s.%s' expecting {resource path, value} pairs.",
			              MODULE_NAMESPACE, CMD_NOTIFY_DEGRADATION);
			logger->Error("Example: '%s.%s <resource_path> "
			              "(e.g., sys0.cpu0.pe0)"
			              " <degradation_percentage> (e.g. 10) ...'",
			              MODULE_NAMESPACE, CMD_NOTIFY_DEGRADATION);
			return 1;
		}
		return ResourceDegradationHandler(argc, argv);
	}

	// Set the degradation value of the given resources
	if (!strncmp(CMD_SIMULATE_FAULT, cmd_id, strlen(CMD_SIMULATE_FAULT))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting {resource path} .",
			              MODULE_NAMESPACE, CMD_SIMULATE_FAULT);
			logger->Error("Example: '%s.%s <r1>"
			              "(e.g., sys0.cpu0.pe0 ...)",
			              MODULE_NAMESPACE, CMD_SIMULATE_FAULT);
			return 2;
		}
		SimulateFault(argv[1]);
		return 0;
	}

	if (!strncmp(CMD_FREEZE, cmd_id, strlen(CMD_FREEZE))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting process id.",
			              MODULE_NAMESPACE, CMD_FREEZE);
			logger->Error("Example: '%s.%s 12319",
			              MODULE_NAMESPACE, CMD_FREEZE);
			return 3;
		}
		Freeze(std::stoi(argv[1]));
		return 0;
	}


	if (!strncmp(CMD_THAW, cmd_id, strlen(CMD_THAW))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting process id.",
			              MODULE_NAMESPACE, CMD_THAW);
			logger->Error("Example: '%s.%s 12319",
			              MODULE_NAMESPACE, CMD_THAW);
			return 3;
		}
		Thaw(std::stoi(argv[1]));
		return 0;
	}

	if (!strncmp(CMD_CHECKPOINT, cmd_id, strlen(CMD_CHECKPOINT))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting process id.",
			              MODULE_NAMESPACE, CMD_CHECKPOINT);
			logger->Error("Example: '%s.%s 8823",
			              MODULE_NAMESPACE, CMD_CHECKPOINT);
			return 5;
		}
		Dump(std::stoi(argv[1]));
		return 0;
	}

	if (!strncmp(CMD_RESTORE, cmd_id, strlen(CMD_RESTORE))) {
		if (argc < 3) {
			logger->Error("'%s.%s' expecting process id and executable name",
			              MODULE_NAMESPACE, CMD_RESTORE);
			logger->Error("Example: '%s.%s 8823 myprogram",
			              MODULE_NAMESPACE, CMD_RESTORE);
			return 5;
		}
		Restore(std::stoi(argv[1]), argv[2]);
		return 0;
	}

	logger->Error("CommandsCb: unexpected value [%s]", cmd_id);

	return 0;
}


void ReliabilityManager::SimulateFault(std::string const & resource_path)
{
	auto resource_list = ra.GetResources(resource_path);
	if (resource_list.empty()) {
		logger->Error("SimulateFault: <%s> not a valid resource",
		              resource_path.c_str());
	}

	for (auto & rsrc : resource_list)  {
		logger->Notice("SimulateFault: fault on <%s>",
		               rsrc->Path()->ToString().c_str());
		NotifyFaultDetection(rsrc);
	}
}


void ReliabilityManager::Freeze(app::AppPid_t pid)
{
	AppUid_t uid = app::Application::Uid(pid, 0);
	app::SchedPtr_t psched = am.GetApplication(uid);
	if (psched) {
		logger->Debug("Freeze: moving application <%s> into freezer...",
		              psched->StrId());
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else  {
		psched = prm.GetProcess(pid);
		if (psched)
			logger->Debug("Freeze: moving process <%s> into freezer",
			              psched->StrId());
	}
#endif

	if (!psched) {
		logger->Warn("Freeze: pid=<%d> no application or process", pid);
		return;
	}
	plm.Freeze(psched);
	logger->Debug("Freeze: is <%s> frozen for you?", psched->StrId());
}


void ReliabilityManager::Thaw(app::AppPid_t pid)
{
	bool exec_found = false;
	AppUid_t uid = app::Application::Uid(pid, 0);
	auto ret = am.SetToThaw(uid);
	if (ret == ApplicationManager::ExitCode_t::AM_SUCCESS) {
		logger->Debug("Thaw: moving application uid=<%d> into freezer...", uid);
		exec_found = true;
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else {
		auto ret = prm.SetToThaw(pid);
		if (ret == ProcessManager::ExitCode_t::SUCCESS) {
			logger->Debug("Thaw: moving process pid=<%d> into freezer", pid);
			exec_found = true;
		}
	}
#endif

	if (!exec_found) {
		logger->Warn("Thaw: pid=<%d> no application or process", pid);
		return;
	}

	logger->Debug("Thaw: triggering re-scheduling");
	ResourceManager & rm = ResourceManager::GetInstance();
	rm.NotifyEvent(ResourceManager::BBQ_PLAT);
}


void ReliabilityManager::Dump(app::AppPid_t pid)
{
	app::SchedPtr_t psched;
	AppUid_t uid = app::Application::Uid(pid, 0);
	psched = am.GetApplication(uid);
	if (psched) {
		logger->Debug("Dump: <%s> application checkpoint...",
		              psched->StrId());
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else {
		psched = prm.GetProcess(pid);
		if (psched)
			logger->Debug("Dump: <%s> process checkpoint...",
			              psched->StrId());
	}
#endif

	if (!psched) {
		logger->Warn("Dump: pid=<%d> no application or process "
		             "to checkpoint", pid);
		return;
	}

	auto ret = plm.Dump(psched);
	if (ret != ReliabilityActionsIF::ExitCode_t::OK) {
		logger->Error("Dump: <%s> checkpoint failed", psched->StrId());
		return;
	}
	logger->Debug("Dump: <%s> checkpointed", psched->StrId());
}


void ReliabilityManager::Restore(app::AppPid_t pid, std::string exe_name)
{
	AppUid_t uid = app::Application::Uid(pid, 0);
	app::SchedPtr_t psched = am.GetApplication(uid);
	if (psched) {
		logger->Debug("Restore: trying to restore a "
		              "running application: <%s>",
		              psched->StrId());
		return;
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else  {
		psched = prm.GetProcess(pid);
		if (psched) {
			logger->Warn("Restore: trying to restore a "
			             "running process: <%s>",
			             psched->StrId());
			return;
		}
	}
#endif

	auto ret = plm.Restore(pid, exe_name);
	if (ret != ReliabilityActionsIF::ExitCode_t::OK) {
		logger->Error("Restore: [pid=%d name=%s] restore failed",
		              pid, exe_name.c_str());
		return;
	}

	logger->Debug("Restore: [pid=%d name=%s] resumed",
	              pid, exe_name.c_str());
}


int ReliabilityManager::ResourceDegradationHandler(int argc, char * argv[])
{
	int j = 1;
	int nr_args = argc + 1;

	// Parsing the "<resource> <degradation_value>" pairs
	while ((nr_args -= 2) && (j += 2)) {
		auto rsrc(ra.GetResource(argv[j]));
		if (rsrc == nullptr) {
			logger->Error("Resource degradation: "
			              " <%s> not a valid resource",
			              argv[j]);
			continue;
		}

		if (IsNumber(argv[j + 1])) {
			rsrc->UpdateDegradationPerc(atoi(argv[j + 1]));
			logger->Warn("Resource degradation: "
			             "<%s> = %2d%% [mean=%.2f]",
			             argv[j],
			             rsrc->CurrentDegradationPerc(),
			             rsrc->MeanDegradationPerc());
		} else {
			logger->Error("Resource degradation: "
			              "<%s> not a valid value",
			              argv[j + 1]);
		}
	}

	return 0;
}

} // namespace bbque
