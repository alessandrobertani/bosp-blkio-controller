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

#define CMD_FREEZE_APPLICATION "freeze_application"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_FREEZE_APPLICATION,
	                   static_cast<CommandHandler*>(this),
	                   "Freeze an adaptive application");

#define CMD_FREEZE_PROCESS "freeze_process"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_FREEZE_PROCESS,
	                   static_cast<CommandHandler*>(this),
	                   "Freeze a generic process");

#define CMD_THAW_APPLICATION "thaw_application"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_THAW_APPLICATION,
	                   static_cast<CommandHandler*>(this),
	                   "Thaw an adaptive application");

#define CMD_THAW_PROCESS "thaw_process"
	cm.RegisterCommand(MODULE_NAMESPACE "." CMD_THAW_PROCESS,
	                   static_cast<CommandHandler*>(this),
	                   "Thaw a generic process");

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
		if (ret != CheckpointRestoreIF::ExitCode_t::OK) {
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

	if (!strncmp(CMD_FREEZE_APPLICATION, cmd_id, strlen(CMD_FREEZE_APPLICATION))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting application unique id.",
			              MODULE_NAMESPACE, CMD_FREEZE_APPLICATION);
			logger->Error("Example: '%s.%s 12319",
			              MODULE_NAMESPACE, CMD_FREEZE_APPLICATION);
			return 3;
		}
		Freeze(std::stoi(argv[1]), app::Schedulable::Type::ADAPTIVE);
		return 0;
	}


	if (!strncmp(CMD_THAW_APPLICATION, cmd_id, strlen(CMD_THAW_APPLICATION))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting application unique id.",
			              MODULE_NAMESPACE, CMD_THAW_APPLICATION);
			logger->Error("Example: '%s.%s 12319",
			              MODULE_NAMESPACE, CMD_THAW_APPLICATION);
			return 3;
		}
		Thaw(std::stoi(argv[1]), app::Schedulable::Type::ADAPTIVE);
		return 0;
	}


	if (!strncmp(CMD_FREEZE_PROCESS, cmd_id, strlen(CMD_FREEZE_PROCESS))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting process id.",
			              MODULE_NAMESPACE, CMD_FREEZE_PROCESS);
			logger->Error("Example: '%s.%s 8823",
			              MODULE_NAMESPACE, CMD_FREEZE_PROCESS);
			return 4;
		}
		Freeze(std::stoi(argv[1]), app::Schedulable::Type::PROCESS);
		return 0;
	}

	if (!strncmp(CMD_THAW_PROCESS, cmd_id, strlen(CMD_THAW_PROCESS))) {
		if (argc < 2) {
			logger->Error("'%s.%s' expecting process id.",
			              MODULE_NAMESPACE, CMD_THAW_PROCESS);
			logger->Error("Example: '%s.%s 8823",
			              MODULE_NAMESPACE, CMD_THAW_PROCESS);
			return 5;
		}
		Thaw(std::stoi(argv[1]), app::Schedulable::Type::PROCESS);
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


void ReliabilityManager::Freeze(
        app::AppUid_t uid, app::Schedulable::Type type)
{
	app::SchedPtr_t psched;
	if (type == app::Schedulable::Type::ADAPTIVE) {
		psched = am.GetApplication(uid);
		if (psched)
			logger->Debug("Freeze: moving application <%s> into freezer...",
			              psched->StrId());
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else if (type == app::Schedulable::Type::PROCESS) {
		psched = prm.GetProcess(uid);
		if (psched)
			logger->Debug("Freeze: moving process <%s> into freezer",
			              psched->StrId());
	}
#endif

	if (!psched) {
		logger->Warn("Freeze: uid=<%d> no application or process found",
		             uid);
		return;
	}
	plm.Freeze(psched);
	logger->Debug("Freeze: is <%s> frozen for you?", psched->StrId());
}

void ReliabilityManager::Thaw(
        app::AppUid_t uid, app::Schedulable::Type type)
{
	app::SchedPtr_t psched;
	if (type == app::Schedulable::Type::ADAPTIVE) {
		psched = am.GetApplication(uid);
		if (psched)
			logger->Debug("Thaw: moving application <%s> into freezer...",
			              psched->StrId());
	}
#ifdef CONFIG_BBQUE_LINUX_PROC_MANAGER
	else if (type == app::Schedulable::Type::PROCESS) {
		psched = prm.GetProcess(uid);
		if (psched)
			logger->Debug("Thaw: moving process <%s> into freezer",
			              psched->StrId());
	}
#endif

	if (!psched) {
		logger->Warn("Thaw: uid=<%d> no application or process found",
		             uid);
		return;
	}

	prm.SetToThaw(uid);
	logger->Debug("Thaw: trigger re-scheduling", psched->StrId());
	ResourceManager & rm = ResourceManager::GetInstance();
	rm.NotifyEvent(ResourceManager::BBQ_PLAT);
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
