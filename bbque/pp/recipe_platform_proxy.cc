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

#include <cstring>

#include "bbque/pp/recipe_platform_proxy.h"

#define MODULE_NAMESPACE "bq.pp.recipe"

#ifndef CONFIG_MANGO_GN_EMULATION
#warning "This may work only with Emulate Acceleration enabled"
#endif

namespace br = bbque::res;
namespace po = boost::program_options;

namespace bbque {
namespace pp {

RecipePlatformProxy * RecipePlatformProxy::GetInstance()
{
	static RecipePlatformProxy * instance;
	if (instance == nullptr)
		instance = new RecipePlatformProxy();
	return instance;
}

RecipePlatformProxy::RecipePlatformProxy()
{
	//---------- Get a logger module
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);
	logger->Debug("RecipePlatformProxy");

	this->hardware_id = "recipe";
}

RecipePlatformProxy::~RecipePlatformProxy() { }

RecipePlatformProxy::ExitCode_t
RecipePlatformProxy::MapResources(SchedPtr_t psched,
				  ResourceAssignmentMapPtr_t pres,
				  bool excl) noexcept
{
	UNUSED(pres);
	UNUSED(excl);

	if (psched->GetType() == ba::Schedulable::Type::PROCESS) {
		logger->Debug("MapResources: [%s] is a PROCESS -> (TODO)", psched->StrId());
		return PLATFORM_OK;
	}

	auto papp = static_cast<ba::Application *>(psched.get());

	auto tg = papp->GetTaskGraph();
	if (tg == nullptr) {
		logger->Warn("MapResources: [%s] task-graph missing", papp->StrId());
		return PLATFORM_OK;
	}

	// Computing units
	for (auto & task_entry : tg->Tasks()) {
		auto & id(task_entry.first);
		auto & task(task_entry.second);
		ArchType arch = ArchType::GN;
		logger->Info("MapResources: [%s] task id=%d -> arch=%s",
			papp->StrId(), id, GetStringFromArchType(arch));
		task->SetAssignedArch(arch);
	}

	// Memory
	uint32_t base_addr = 0x0;
	for (auto & b : tg->Buffers()) {
		auto & id(b.first);
		auto & buffer(b.second);
		uint32_t mem_bank = 0;
		uint32_t phy_addr = base_addr;
		logger->Info("MapResources: [%s] buffer id=%d -> mem=%d [@%x]",
			papp->StrId(), id, mem_bank, phy_addr);
		buffer->SetMemoryBank(mem_bank);
		buffer->SetPhysicalAddress(phy_addr);
		base_addr = phy_addr + buffer->Size();
	}

	// Memory for events
	for (auto & e : tg->Events()) {
		auto & id(e.first);
		auto & event(e.second);
		uint32_t phy_addr = base_addr;
		logger->Info("MapResources: [%s] event id=%d -> [@%x]",
			papp->StrId(), id, phy_addr);
		event->SetPhysicalAddress(phy_addr);
		base_addr += 0x4;
	}

	papp->SetTaskGraph(tg);
	logger->Info("MapResources: [%s] task-graph mapping updated",
		papp->StrId());

	return PLATFORM_OK;
}

#ifdef CONFIG_BBQUE_CR_FPGA

bool RecipePlatformProxy::HasAssignedResources(SchedPtr_t psched)
{
	uint32_t acc_id = 0;
	auto curr_rsrc_map = psched->CurrentAWM()->GetResourceBinding();

	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	auto nr_acc_cores  = ra.GetAssignedAmount(
						curr_rsrc_map,
						psched,
						0,
						br::ResourceType::PROC_ELEMENT,
						br::ResourceType::ACCELERATOR,
						acc_id);

	// TODO: For C/R: check that the accelerator is actually the device
	// for which the libacre support is available

	return (nr_acc_cores > 0);
}

void RecipePlatformProxy::InitReliabilitySupport()
{
	boost::filesystem::perms prms(boost::filesystem::owner_all);
	prms |= boost::filesystem::others_read;
	prms |= boost::filesystem::group_read;
	prms |= boost::filesystem::group_write;

	// Checkpoint image path for the
	image_prefix_dir += "/recipe";
	logger->Info("Reliability: checkpoint images directory:  %s",
		image_prefix_dir.c_str());

	if (!boost::filesystem::exists(image_prefix_dir)) {
		if (boost::filesystem::create_directories(image_prefix_dir)) {
			logger->Debug("Reliability: "
				"checkpoint images directory created");
		}
		else
			logger->Error("Reliability: "
				"checkpoint images directory not created");
	}
	boost::filesystem::permissions(image_prefix_dir, prms);
}

ReliabilityActionsIF::ExitCode_t
RecipePlatformProxy::Dump(app::SchedPtr_t psched)
{
	logger->Debug("Dump: [%s] checkpoint [pid=%d]... (user=%d)",
		psched->StrId(), psched->Pid(), getuid());

	if (!HasAssignedResources(psched)) {
		logger->Warn("Dump: [%s] [pid=%d] not using RECIPE accelerators",
			psched->StrId(), psched->Pid());
		return ReliabilityActionsIF::ExitCode_t::WARN_RESOURCES_NOT_ASSIGNED;
	}

	std::string image_dir(ApplicationPath(
					image_prefix_dir,
					psched->Pid(),
					psched->Name()));
	if (!boost::filesystem::exists(image_dir)) {
		logger->Debug("Dump: [%s] creating directory [%s]",
			psched->StrId(), image_dir.c_str());
		boost::filesystem::create_directory(image_dir);
	}

	// CRIU image directory
	int fd = open(image_dir.c_str(), O_DIRECTORY);
	if (fd < 0) {
		logger->Warn("Dump: [%s] image directory [%s] not accessible",
			psched->StrId(), image_dir.c_str());
		perror("CRIU");
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}
	else {
		logger->Debug("Dump: [%s] image directory [%s] open",
			psched->StrId(), image_dir.c_str());
	}

	// Dump the FPGA checkpoint
	npu_handler.set_image_path(image_dir);
	npu_handler.freeze("", 0);
	npu_handler.checkpoint("", 0);
	npu_handler.thaw("", 0);

	logger->Info("Dump: [%s] checkpoint done [image_dir=%s]",
		psched->StrId(), image_dir.c_str());
	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
RecipePlatformProxy::Restore(uint32_t pid, std::string exe_name)
{
	// Retrieve checkpoint image directory
	std::string image_dir(image_prefix_dir
			+ "/" + std::to_string(pid)
			+ "_" + exe_name);

	logger->Debug("Restore: [pid=%d] recovering checkpoint from = [%s]",
		pid, image_dir.c_str());

	if (!boost::filesystem::exists(image_dir)) {
		logger->Debug("Restore: [pid=%d] missing directory [%s]",
			pid, image_dir.c_str());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}

	int fd = open(image_dir.c_str(), O_DIRECTORY);
	if (fd < 0) {
		logger->Warn("Restore: [pid=%d] image directory [%s] not accessible",
			pid, image_dir.c_str());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}
	else {
		logger->Debug("Restore: [pid=%d] image directory [%s] open",
			pid, image_dir.c_str());
	}

	// Do restore
	npu_handler.set_image_path(image_dir);
	npu_handler.restore("", 0);

	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
RecipePlatformProxy::Freeze(app::SchedPtr_t psched)
{
	if (!HasAssignedResources(psched)) {
		logger->Warn("Freeze: [%s] [pid=%d] not using RECIPE accelerators",
			psched->StrId(), psched->Pid());
		return ReliabilityActionsIF::ExitCode_t::WARN_RESOURCES_NOT_ASSIGNED;
	}

	// Freeze the FPGA
	npu_handler.freeze("", 0);

	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
RecipePlatformProxy::Thaw(app::SchedPtr_t psched)
{
	if (!HasAssignedResources(psched)) {
		logger->Warn("Thaw: [%s] [pid=%d] not using RECIPE accelerators",
			psched->StrId(), psched->Pid());
		return ReliabilityActionsIF::ExitCode_t::WARN_RESOURCES_NOT_ASSIGNED;
	}

	// Thaw the FPGA
	npu_handler.thaw("", 0);

	return ReliabilityActionsIF::ExitCode_t::OK;
}

#endif // CONFIG_BBQUE_CR_FPGA



} // namespace pp
} // namespace bbque
