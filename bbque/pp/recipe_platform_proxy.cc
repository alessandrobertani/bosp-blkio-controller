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

namespace bbque
{

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
}

RecipePlatformProxy::~RecipePlatformProxy()
{

}

RecipePlatformProxy::ExitCode_t
RecipePlatformProxy::MapResources(
    SchedPtr_t psched, ResourceAssignmentMapPtr_t pres, bool excl) noexcept
{
	UNUSED(pres);
	UNUSED(excl);
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
	for (auto & b: tg->Buffers()) {
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
	for (auto & e: tg->Events()) {
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


} // namespace bbque
