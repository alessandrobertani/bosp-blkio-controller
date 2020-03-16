/*
 * Copyright (C) 2014  Politecnico di Milano
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

#include "rxml_rloader.h"

#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
#include <list>
#include <fstream>
#include <boost/filesystem/operations.hpp>

#include "bbque/platform_manager.h"
#include "bbque/app/application.h"
#include "bbque/app/working_mode.h"
#include "bbque/res/resource_constraints.h"
#include "bbque/res/resource_type.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/string_utils.h"

namespace ba = bbque::app;
namespace br = bbque::res;
namespace bu = bbque::utils;
namespace po = boost::program_options;

namespace bbque
{
namespace plugins
{


/** Set true it means the plugin has read its options in the config file*/
bool RXMLRecipeLoader::configured = false;

/** Recipes directory */
std::string RXMLRecipeLoader::recipe_dir = "";

/** Map of options (in the Barbeque config file) for the plugin */
po::variables_map xmlrloader_opts_value;


RXMLRecipeLoader::RXMLRecipeLoader()
{
	// Get a logger
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	assert(logger);

	logger->Debug("Built RXML RecipeLoader object @%p", (void*)this);
}

RXMLRecipeLoader::~RXMLRecipeLoader()
{
}

bool RXMLRecipeLoader::Configure(PF_ObjectParams * params)
{

	if (configured)
		return true;

	// Declare the supported options
	po::options_description xmlrloader_opts_desc("RXML Recipe Loader Options");
	xmlrloader_opts_desc.add_options()
	(MODULE_CONFIG".recipe_dir", po::value<std::string>
	 (&recipe_dir)->default_value(BBQUE_PATH_PREFIX "/" BBQUE_PATH_RECIPES),
	 "recipes folder")
	;

	// Get configuration params
	PF_Service_ConfDataIn data_in;
	data_in.opts_desc = &xmlrloader_opts_desc;
	PF_Service_ConfDataOut data_out;
	data_out.opts_value = &xmlrloader_opts_value;

	PF_ServiceData sd;
	sd.id = MODULE_NAMESPACE;
	sd.request = &data_in;
	sd.response = &data_out;

	int32_t response =
	    params->platform_services->InvokeService(PF_SERVICE_CONF_DATA, sd);

	if (response!=PF_SERVICE_DONE)
		return false;

	if (daemonized)
		syslog(LOG_INFO, "Using RXMLRecipeLoader recipe folder [%s]",
		       recipe_dir.c_str());
	else
		fprintf(stdout, FI("Using RXMLRecipeLoader recipe folder [%s]\n"),
		        recipe_dir.c_str());

	return true;
}


// =======================[ Static plugin interface ]=========================

void * RXMLRecipeLoader::Create(PF_ObjectParams *params)
{
	if (!Configure(params))
		return nullptr;

	return new RXMLRecipeLoader();
}

int32_t RXMLRecipeLoader::Destroy(void *plugin)
{
	if (!plugin)
		return -1;
	delete (RXMLRecipeLoader *)plugin;
	return 0;
}


// =======================[ MODULE INTERFACE ]================================

RecipeLoaderIF::ExitCode_t RXMLRecipeLoader::LoadRecipe(
    std::string const & _recipe_name,
    RecipePtr_t _recipe)
{
	RecipeLoaderIF::ExitCode_t result = RL_SUCCESS;
	rapidxml::xml_document<> doc;
	rapidxml::xml_node<> * root_node = nullptr;
	rapidxml::xml_node<> * app_node = nullptr;
	rapidxml::xml_node<> * pp_node = nullptr;
	rapidxml::xml_attribute<> * bbq_attribute = nullptr;
	rapidxml::xml_attribute<> * app_attribute = nullptr;
	rapidxml::xml_attribute<> * plat_attribute = nullptr;
	uint16_t priority = 0;
	int maj, min;
	std::string version_id;

	// Recipe object
	recipe_ptr = _recipe;
	logger->Info("Loading recipe with name=[%s]...", _recipe_name.c_str());

	try {
		// Plugin needs a logger
		if (!logger) {
			fprintf(stderr, FE("Error: Plugin 'XMLRecipeLoader' needs a logger\n"));
			result = RL_ABORTED;
			throw std::runtime_error("");	// No info if logger not loaded
		}

		try {
			// Load the recipe parsing an XML file
			std::string path(recipe_dir + "/" + _recipe_name + ".recipe");
			logger->Info("Loading recipe <%s>...", path.c_str());
			std::ifstream xml_file(path);
			std::stringstream buffer;
			buffer << xml_file.rdbuf();
			xml_file.close();
			std::string xml_content(buffer.str());

			doc.parse<0>(&xml_content[0]);

			// <BarbequeRTRM> - Recipe root tag
			root_node = doc.first_node();
			CheckMandatoryNode(root_node, "application", root_node);

			// Recipe version control
			bbq_attribute = root_node->first_attribute("recipe_version", 0, true);
			version_id = bbq_attribute->value();
			logger->Debug("Recipe version = %s", version_id.c_str());
			sscanf(version_id.c_str(), "%d.%d", &maj, &min);
			if (maj < RECIPE_MAJOR_VERSION ||
			    (maj >= RECIPE_MAJOR_VERSION && min < RECIPE_MINOR_VERSION)) {
				logger->Error("Recipe version mismatch (REQUIRED %d.%d). "
				              "Found %d.%d", RECIPE_MAJOR_VERSION, RECIPE_MINOR_VERSION,
				              maj, min);
				result = RL_VERSION_MISMATCH;
				throw std::runtime_error("Recipe version mismatch");
			}

			// <application>
			app_node = root_node->first_node("application", 0, true);
			CheckMandatoryNode(app_node, "application", root_node);
			app_attribute = app_node->first_attribute("priority", 0, true);

			//setting the priority of the app
			std::string priority_str(app_attribute->value());
			priority = (uint8_t) atoi(priority_str.c_str());
			recipe_ptr->SetPriority(priority);

			// Load the proper platform section
			pp_node = LoadPlatform(app_node);
			if (!pp_node) {
				result = RL_PLATFORM_MISMATCH;
				throw std::runtime_error("LoadPlatform failed.");
			}

			plat_attribute = pp_node->first_attribute("id", 0, true);
			logger->Debug("Platform: parsing id='%s'", plat_attribute->value());

			// Application Working Modes
			result = LoadWorkingModes(pp_node);
			if (result != RL_SUCCESS) {
				throw std::runtime_error("LoadWorkingModes failed.");
			}

			// Task requirements (for task-graph based programming models)
			LoadTaskGraphInfo(pp_node);

			// "Static" constraints and plugins specific data
			LoadConstraints(pp_node);
			LoadPluginsData<ba::RecipePtr_t>(recipe_ptr, pp_node);

		} catch(rapidxml::parse_error &ex) {
			logger->Error(ex.what());
			result = RL_ABORTED;
			throw std::runtime_error("XML parsing failed.");
		}
	} // external try
	catch(std::runtime_error &ex) {
		if (logger) {
			logger->Crit(ex.what());
		}
		doc.clear();
		recipe_ptr = ba::RecipePtr_t();
		return result;
	}

	// Regular exit
	return result;
}


rapidxml::xml_node<> * RXMLRecipeLoader::LoadPlatform(rapidxml::xml_node<> * _xml_elem)
{
	rapidxml::xml_node<> * pp_elem = nullptr;
	rapidxml::xml_node<> * pp_last = nullptr;
#ifndef CONFIG_TARGET_EMULATED_HOST
	rapidxml::xml_node<> * pp_gen_elem = nullptr;
	const char * sys_platform_id;
	std::string sys_platform_hw;
	std::string platform_id;
	std::string platform_hw;
	PlatformManager & plm = PlatformManager::GetInstance();
	bool id_matched  = false;
#endif

	try {
		// <platform>
		pp_elem = _xml_elem->first_node("platform", 0, true);
		pp_last = pp_elem;
		CheckMandatoryNode(pp_elem, "platform", _xml_elem);
#ifndef CONFIG_TARGET_EMULATED_HOST
		// System platform ID
		sys_platform_id = plm.GetPlatformID();
		if (!sys_platform_id) {
			logger->Error("Platform: unable to get the system ID");
			assert(sys_platform_id != nullptr);
			return nullptr;
		}
		// Plaform hardware (optional)
		sys_platform_hw.assign(plm.GetHardwareID());
		logger->Info("Platform: system ID=%s HW=%s",
		             sys_platform_id, sys_platform_hw.c_str());

		// Look for the platform section matching the system platform id
		while (pp_elem) {
			platform_id = loadAttribute("id", true, pp_elem);
			platform_hw = loadAttribute("hw", false, pp_elem);
			logger->Info("Platform: search ID=%s HW=%s",
			             platform_id.c_str(), platform_hw.c_str());

			// Keep track of the "generic" platform section (if any)
			if (!pp_gen_elem
			    && (platform_id.compare(PLATFORM_ID_GENERIC) ==	0)) {
				pp_gen_elem = pp_elem;
				logger->Debug("Platform: found a generic section");
				pp_elem = pp_elem->next_sibling("platform", 0, true);
				continue;
			}

			// Keep track of the section matching the system platform ID
			if (platform_id.compare(sys_platform_id) == 0) {
				pp_last = pp_elem;
				id_matched = true;
				logger->Debug("Platform: found a ID match: %s",
				              platform_id.c_str());
				// Hardware (SoC) check required?
				if ((platform_hw.size() > 1)
				    && (platform_hw.compare(sys_platform_hw) == 0)) {
					logger->Debug("Platform: found a HW match: %s",
					              platform_hw.c_str());
					break;
				}
			}
			pp_elem = pp_elem->next_sibling("platform", 0, true);
		}

		// If the platform ID does not match the system platform, check if it
		// has been found the 'generic' section
		if (!id_matched && pp_gen_elem) {
			logger->Warn("Platform: mismatch. Section '%s' will be parsed",
			             PLATFORM_ID_GENERIC);
			return pp_gen_elem;
		}

		logger->Info("Platform: best matching = [%s:%s]",
		             platform_id.c_str(), platform_hw.c_str());
#else
		logger->Warn("TPD enabled: no platform ID check performed");
#endif

	} catch(rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return nullptr;
	}

	return pp_last;
}

std::time_t RXMLRecipeLoader::LastModifiedTime(std::string const & _name)
{
	boost::filesystem::path p(recipe_dir + "/" + _name + ".recipe");
	return boost::filesystem::last_write_time(p);
}


//========================[ Working modes ]===================================

RecipeLoaderIF::ExitCode_t RXMLRecipeLoader::LoadWorkingModes(rapidxml::xml_node<>  *_xml_elem)
{
	uint8_t result = __RSRC_SUCCESS;
	unsigned int wm_id;
	unsigned int wm_value;
	int wm_config_time = -1;
	std::string wm_name;
	rapidxml::xml_node<> * awms_elem = nullptr;
	rapidxml::xml_node<> * resources_elem = nullptr;
	rapidxml::xml_node<> * awm_elem = nullptr;
	std::string read_attribute;

	try {
		// For each working mode we need resource assignments data and (optionally)
		// plugins specific data
		awms_elem = _xml_elem->first_node("awms", 0, true);
		CheckMandatoryNode(awms_elem, "awms", _xml_elem);
		awm_elem  = awms_elem->first_node("awm", 0, true);
		CheckMandatoryNode(awm_elem, "awm", awms_elem);
		while (awm_elem) {
			// Working mode attributes
			read_attribute = loadAttribute("id", true, awm_elem);
			wm_id   = (uint8_t) atoi(read_attribute.c_str());
			wm_name = loadAttribute("name", false, awm_elem);
			read_attribute = loadAttribute("value", true, awm_elem);
			wm_value = (uint8_t) atoi(read_attribute.c_str());
			read_attribute = loadAttribute("config-time", false, awm_elem);
			wm_config_time = (uint8_t) atoi(read_attribute.c_str());

			// The awm ID must be unique!
			if (recipe_ptr->GetWorkingMode(wm_id)) {
				logger->Error("AWM {%d:%s} error: Double ID found %d",
				              wm_id, wm_name.c_str(), wm_id);
				return RL_FORMAT_ERROR;
			}
			logger->Debug("LoadWorkingMode: adding ID=%d name=%s value=%d ...",
			              wm_id, wm_name.c_str(), wm_value);

			// Add a new working mode (IDs MUST be numbered from 0 to N)
			AwmPtr_t awm(recipe_ptr->AddWorkingMode(
			                 wm_id, wm_name,	static_cast<uint8_t> (wm_value)));
			if (!awm) {
				logger->Error("AWM {%d:%s} error: Wrong ID specified %d",
				              wm_id, wm_name.c_str(), wm_id);
				return RL_FORMAT_ERROR;
			}

			// Configuration time
			if (wm_config_time > 0) {
				logger->Info("AWM {%d:%s} setting configuration time: %d",
				             wm_id, wm_name.c_str(), wm_config_time);
				awm->SetRecipeConfigTime(wm_config_time);
			} else
				logger->Info("AWM {%d:%s} no configuration time provided",
				             wm_id, wm_name.c_str());

			// Load resource assignments of the working mode
			resources_elem = awm_elem->first_node("resources", 0, false);
			CheckMandatoryNode(resources_elem, "resources", awm_elem);
			result = LoadResources(resources_elem, awm, "");
			if (result == __RSRC_FORMAT_ERR)
				return RL_FORMAT_ERROR;
			else if (result & __RSRC_WEAK_LOAD) {
				logger->Warn("AWM {%d:%s} weak load detected: skipping",
				             wm_id, wm_name.c_str());
				awm_elem = awm_elem->next_sibling("awm", 0, false);
				continue;
			}

			// AWM plugin specific data
			LoadPluginsData<ba::AwmPtr_t>(awm, awm_elem);

			// Next working mode
			awm_elem = awm_elem->next_sibling("awm", 0, false);
		}

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return RL_ABORTED;
	}

	// TODO: RL_WEAK_LOAD case
	return RL_SUCCESS;
}

// =======================[ Task-graph ]=======================================


void RXMLRecipeLoader::LoadTaskGraphInfo(rapidxml::xml_node<> *_xml_elem)
{
	rapidxml::xml_node<> * tg_elem = nullptr;

	try {
		// <tg>
		tg_elem = _xml_elem->first_node("tg", 0, true);
		if (tg_elem == nullptr) {
			logger->Warn("LoadTaskGraphInfo: <tg> section missing");
			return;
		}
		// <reqs>
		LoadTasksRequirements(tg_elem);
		// <mappings>
		LoadTaskGraphMappings(tg_elem);

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return;
	}
}


void RXMLRecipeLoader::LoadTasksRequirements(rapidxml::xml_node<> *_xml_elem)
{
	rapidxml::xml_node<> * reqs_elem = nullptr;
	rapidxml::xml_node<> * task_elem  = nullptr;
	std::string read_attrib;

	try {
		reqs_elem = _xml_elem->first_node("reqs", 0, true);
		if (reqs_elem == nullptr) {
			logger->Warn("LoadTaskRequirements: Missing <reqs> section");
			return;
		}

		CheckMandatoryNode(reqs_elem, "reqs", _xml_elem);
		task_elem  = reqs_elem->first_node("task", 0, true);
		if (task_elem == nullptr) {
			logger->Warn("LoadTaskRequirements: No <task> sections included");
			return;
		}
		CheckMandatoryNode(task_elem, "task", reqs_elem);

		// <task name="stage2" id="1" ctime_ms="500" hw_prefs="cpu"/>
		while (task_elem) {
			logger->Debug("LoadTasksRequirements: loading task...");
			//
			read_attrib  = loadAttribute("id", true, task_elem);
			logger->Debug("LoadTasksRequirements: task id=%s", read_attrib.c_str());
			uint32_t id = atoi(read_attrib.c_str());

			read_attrib = loadAttribute("throughput_cps", false, task_elem);
			logger->Debug("LoadTasksRequirements: throughput=%s", read_attrib.c_str());
			float tp = atof(read_attrib.c_str());

			read_attrib = loadAttribute("ctime_ms", false, task_elem);
			logger->Debug("LoadTasksRequirements: ctime=%s", read_attrib.c_str());
			uint32_t ct = atoi(read_attrib.c_str());

			read_attrib = loadAttribute("inbw_kbps", false, task_elem);
			logger->Debug("LoadTasksRequirements: inbw_kbps=%s", read_attrib.c_str());
			uint32_t inb = atoi(read_attrib.c_str());

			read_attrib = loadAttribute("outbw_kbps", false, task_elem);
			logger->Debug("LoadTasksRequirements: outbw_kbps=%s", read_attrib.c_str());
			uint32_t outb = atoi(read_attrib.c_str());

			recipe_ptr->AddTaskRequirements(id, TaskRequirements(tp, ct, inb, outb));

			read_attrib = loadAttribute("hw_prefs", false, task_elem);
			logger->Debug("LoadTasksRequirements: hw_prefs=%s", read_attrib.c_str());

			std::list<std::string> archs;
			bu::SplitString(read_attrib, archs, ",");
			for(auto const & s: archs) {
				ArchType type = GetArchTypeFromString(s);
				if (type == ArchType::NONE) {
					logger->Warn("LoadTaskRequirements: HW <%s> unsupported", s.c_str());
					continue;
				}
				recipe_ptr->GetTaskRequirements(id).AddArchPreference(type);
			}

			logger->Info("LoadTasksRequirements: <T%2d>: tput=%.2f ctime=%dms"
			             " in_bw=%dKbps, out_bw=%dKbps #hw=<%d>", id,
			             recipe_ptr->GetTaskRequirements(id).Throughput(),
			             recipe_ptr->GetTaskRequirements(id).CompletionTime(),
			             recipe_ptr->GetTaskRequirements(id).InBandwidth(),
			             recipe_ptr->GetTaskRequirements(id).OutBandwidth(),
			             recipe_ptr->GetTaskRequirements(id).NumArchPreferences());

			task_elem = task_elem->next_sibling("task", 0, false);
		}

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return;
	}
}


void RXMLRecipeLoader::LoadTaskGraphMappings(rapidxml::xml_node<> *_xml_elem)
{
	rapidxml::xml_node<> * mappings_elem = nullptr;
	rapidxml::xml_node<> * map_elem = nullptr;
	std::string read_attrib;
	ba::Recipe::TaskGraphMapping mapping;

	try {
		// <mappings>
		mappings_elem = _xml_elem->first_node("mappings", 0, false);
		if (mappings_elem == nullptr) {
			logger->Warn("LoadTaskGraphMappings: missing <mappings> section "
			             "under <%s>", _xml_elem->name());
			return;
		}
		CheckMandatoryNode(mappings_elem, "mappings", _xml_elem);

		// <mapping...>
		map_elem  = mappings_elem->first_node("mapping", 0, true);
		if (map_elem == nullptr) {
			logger->Warn("LoadTaskGraphMappings: No <task> sections included");
			return;
		}
		CheckMandatoryNode(map_elem, "mapping", mappings_elem);
		while (map_elem) {
			logger->Debug("LoadTaskGraphMappings: loading mapping...");

			// <mapping id=...>
			read_attrib  = loadAttribute("id", true, map_elem);
			uint32_t id = atoi(read_attrib.c_str());

			// <mapping ... exec_time_ms=...>
			read_attrib  = loadAttribute("exec_time_ms", true, map_elem);
			mapping.exec_time_ms = atoi(read_attrib.c_str());

			// <mapping ... power_mw=...>
			read_attrib  = loadAttribute("power_mw", false, map_elem);
			mapping.power_mw = atoi(read_attrib.c_str());

			// <mapping ... mem_bw=...>
			read_attrib  = loadAttribute("mem_bw", false, map_elem);
			mapping.mem_bw = atoi(read_attrib.c_str());

			logger->Debug("LoadTaskGraphMappings: id=%d "
				"exec_power_bw=%d power_mw=%d mem_bw=%d",
				id,
				mapping.exec_time_ms,
				mapping.power_mw,
				mapping.mem_bw);

			// Load the set of design-time mapping choices
			LoadTaskGraphMapping(map_elem, mapping);
			recipe_ptr->AddTaskGraphMapping(id, mapping);

			map_elem = map_elem->next_sibling("mapping", 0, false);
		}

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return;
	}
}

void RXMLRecipeLoader::LoadTaskGraphMapping(
    rapidxml::xml_node<> *_xml_elem,
    ba::Recipe::TaskGraphMapping & mapping)
{
	rapidxml::xml_node<> * tasks_elem = nullptr;
	rapidxml::xml_node<> * task_elem = nullptr;
	rapidxml::xml_node<> * buffs_elem = nullptr;
	rapidxml::xml_node<> * buff_elem = nullptr;
	std::string read_attrib;

	try {
		// <tasks>
		tasks_elem = _xml_elem->first_node("tasks", 0, true);
		if (tasks_elem == nullptr) {
			logger->Warn("LoadTaskGraphMapping: Missing <tasks> section");
			return;
		}
		CheckMandatoryNode(tasks_elem, "tasks", _xml_elem);
		// <task...>
		task_elem  = tasks_elem->first_node("task", 0, true);
		if (task_elem == nullptr) {
			logger->Warn("LoadTaskGraphMapping: No <task> included");
			return;
		}
		CheckMandatoryNode(task_elem, "task", tasks_elem);

		// Loading the mapping data for all the tasks
		LoadMappingData(task_elem, "task", mapping.tasks);

		// <buffers> ..
		buffs_elem  = _xml_elem->first_node("buffers", 0, false);
		if (buffs_elem == nullptr) {
			logger->Warn("LoadTaskGraphMapping: Missing <buffers> sections");
			return;
		}
		// <buffer...>
		buff_elem  = buffs_elem->first_node("buffer", 0, true);
		if (buff_elem == nullptr) {
			logger->Warn("LoadTaskGraphMapping: No <buffer> included");
			return;
		}
		CheckMandatoryNode(buff_elem, "buffer", buffs_elem);

		// Loading the mapping data for all the buffers
		LoadMappingData(buff_elem, "buffer", mapping.buffers);

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return;
	}
}


void RXMLRecipeLoader::LoadMappingData(
    rapidxml::xml_node<> * obj_elem,
    std::string const & obj_name,
    std::map<uint32_t, ba::Recipe::MappingData> & mapping_map)
{
	rapidxml::xml_attribute<> * attribute = nullptr;

	while (obj_elem) {
		ba::Recipe::MappingData map_data;
		std::string map_log(obj_name + " ");
		uint32_t obj_id = 9000;

		for (attribute = obj_elem->first_attribute();
		     attribute != nullptr;
		     attribute = attribute->next_attribute()) {

			map_log += std::string(attribute->name()) + "="
				+ std::string(attribute->value()) + " ";

			// <task/buffer id=...>
			if (strncmp(attribute->name(), "id", 2) == 0) {
				obj_id = atoi(attribute->value());
				continue;
			}

			if (strncmp(attribute->name(), "freq_khz", 8) == 0) {
				map_data.freq_khz = atoi(attribute->value());
				continue;
			}

			auto r_type = bbque::res::GetResourceTypeFromString(attribute->name());
			if (r_type != br::ResourceType::UNDEFINED) {
				map_data.type = r_type;
				map_data.id   = atoi(attribute->value());
			} else {
				logger->Error("LoadMappingData: unknown attribute '%s'",
				              attribute->name());
			}
		}

		logger->Debug("LoadMappingData: %s", map_log.c_str());

		mapping_map[obj_id] = map_data;
		obj_elem = obj_elem->next_sibling(obj_name.c_str(), 0, false);
	}
}


// =======================[ Resources ]=======================================

uint8_t RXMLRecipeLoader::LoadResources(rapidxml::xml_node<> * _xml_elem,
                                        AwmPtr_t & _wm,
                                        std::string const & _curr_path = "")
{
	uint8_t result = __RSRC_SUCCESS;
	std::string res_path;
	rapidxml::xml_node<> * res_elem = nullptr;

	try {
		// Get the resource xml element
		res_elem = _xml_elem->first_node(0, 0, true);
		CheckMandatoryNode(res_elem, "", _xml_elem);
		while (res_elem) {
			// Parse the attributes from the resource element
			res_path = _curr_path;
			result |= GetResourceAttributes(res_elem, _wm, res_path);
			if (result >= __RSRC_WEAK_LOAD) {
				logger->Warn("Weak load case (R): %s", res_path.c_str());
				return result;
			}

			// The current resource is a container of other resources,
			// thus load the children resources recursively
			if (res_elem->first_node(0, 0, true)) {
				result |= LoadResources(res_elem, _wm, res_path);
				if (result >= __RSRC_WEAK_LOAD) {
					logger->Warn("Weak load case (N): %s", res_path.c_str());
					return result;
				}
			}

			// Next resource
			res_elem = res_elem->next_sibling(0, 0, true);
		}
	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
		return __RSRC_FORMAT_ERR;
	}

	return result;
}


uint8_t RXMLRecipeLoader::AppendToWorkingMode(AwmPtr_t & wm,
        std::string const & _res_path,
        uint64_t _res_usage)
{
	// Add the resource usage to the working mode,
	// return a "weak load" code if some resources are missing
	auto preq = wm->AddResourceRequest(_res_path, _res_usage);
	if (preq == nullptr) {
		logger->Warn("'%s' recipe: resource '%s' not available",
		             recipe_ptr->Path().c_str(), _res_path.c_str());
		return __RSRC_WEAK_LOAD;
	}
	return __RSRC_SUCCESS;
}


uint8_t RXMLRecipeLoader::GetResourceAttributes(
    rapidxml::xml_node<> * _res_elem,
    AwmPtr_t & _wm,
    std::string & _res_path)
{
	uint64_t res_usage = 0;
	std::string res_units;
	std::string res_id;
	std::string read_usage;
	rapidxml::xml_attribute<> * attribute_usage;
	rapidxml::xml_attribute<> * attribute_id;

	// Resource ID
	attribute_id = _res_elem->first_attribute("id", 0, true);
	if (attribute_id != 0) {
		res_id = attribute_id->value();
	}

	// Build the resource path string
	if (!_res_path.empty())
		_res_path += ".";
	_res_path += _res_elem->name() + res_id;

	// Resource quantity request and units
	attribute_usage = _res_elem->first_attribute("qty", 0, true);
	if (attribute_usage != 0) {
		read_usage = attribute_usage->value();
		res_usage = (uint64_t) atoi(read_usage.c_str());
	}
	res_units = loadAttribute("units", false, _res_elem);

	// The usage requested must be > 0
	if (!(attribute_usage == 0) && res_usage <= 0) {
		logger->Error("Resource ""%s"": usage value not valid (%" PRIu64 ")",
		              _res_path.c_str(), res_usage);
		return __RSRC_FORMAT_ERR;
	}

	// If the quantity is 0 return without adding the resource request to the
	// current AWM
	if (res_usage == 0) {
		return __RSRC_SUCCESS;
	}

	// Convert the usage value accordingly to the units, and then append the
	// request to the working mode.
	res_usage = br::ConvertValue(res_usage, res_units);
	return AppendToWorkingMode(_wm, _res_path, res_usage);
}


// =======================[ Plugins specific data ]===========================

template<class T>
void RXMLRecipeLoader::LoadPluginsData(T _container,
                                       rapidxml::xml_node<> * _xml_node)
{
	rapidxml::xml_node<> * plugins_node = nullptr;
	rapidxml::xml_node<> * plug_node = nullptr;

	// <plugins> [Optional]
	// Section tag for plugin specific data. This can be included into the
	// <application> section and into the <awm> section.
	plugins_node = _xml_node->first_node("plugins", 0, true);
	if (plugins_node == 0) {
		return;
	}

	try {
		// Parse the <plugin> tags
		plug_node = plugins_node->first_node("plugin", 0, true);

		while (plug_node) {
			ParsePluginTag<T>(_container, plug_node);
			plug_node = plug_node->next_sibling("plugin", 0, true);
		}
	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
	}
}


template<class T>
void RXMLRecipeLoader::ParsePluginTag(T _container,
                                      rapidxml::xml_node<> * _plug_node)
{
	rapidxml::xml_node<> * plugdata_node = nullptr;
	std::string name;

	try {
		// Plugin attributes
		name = loadAttribute("name", true, _plug_node);

		// Plugin data in <plugin>
		plugdata_node = _plug_node->first_node(0, 0, false);
		while (plugdata_node) {
			GetPluginData<T>(_container, plugdata_node, name);
			plugdata_node = plugdata_node->next_sibling(0, 0, false);
		}

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
	}

}


template<class T>
void RXMLRecipeLoader::GetPluginData(T _container,
                                     rapidxml::xml_node<> * plugdata_node,
                                     std::string const & _plug_name)
{

	try {
		// Set the plugin data
		ba::AppPluginDataPtr_t pattr(new ba::AppPluginData_t(
		                                 _plug_name, plugdata_node->name()));
		pattr->str = plugdata_node->value();
		_container->SetPluginData(pattr);

	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
	}
}


// =======================[ Constraints ]=====================================

void RXMLRecipeLoader::LoadConstraints(rapidxml::xml_node<> * _xml_node)
{
	rapidxml::xml_node<> * constr_elem = nullptr;
	rapidxml::xml_node<> * con_elem    = nullptr;
	std::string constraint_type;
	std::string resource;
	std::string value_st;
	uint32_t value;

	// <constraints> [Optional]
	// An application can specify bounds for resource assignments
	// over which the execution can yield an unsatisfactory
	// behavior.
	// This method loads static constraint assertions.
	// Constraints may disable some working mode.
	constr_elem = _xml_node->first_node("constraints", 0, true);
	if (!constr_elem)
		return;

	try {
		// Constraint tags
		con_elem = constr_elem->first_node("constraint", 0, true);
		while (con_elem) {
			// Attributes
			constraint_type = loadAttribute("type", true, con_elem);
			resource = loadAttribute("resource", true, con_elem);
			value_st = loadAttribute("bound", true, con_elem);
			value = (uint32_t) atoi(value_st.c_str());

			// Add the constraint
			if (constraint_type.compare("L") == 0) {
				recipe_ptr->AddConstraint(resource, value, 0);
			} else if (constraint_type.compare("U") == 0) {
				recipe_ptr->AddConstraint(resource, 0, value);
			} else {
				logger->Warn("Constraint: unknown bound type");
				continue;
			}

			// Next constraint
			con_elem = con_elem->next_sibling("constraint",0 , true);
		}
	} catch (rapidxml::parse_error &ex) {
		logger->Error(ex.what());
	}

}


// =======================[ Utils ]=======================================

void RXMLRecipeLoader::CheckMandatoryNode (
    rapidxml::xml_node<> * _nodeToCheck,
    const char * _nodeToCheckName,
    rapidxml::xml_node<> * _nodeParent)
{

	if (_nodeParent == nullptr) {
		std::string exception_message("Null parent node");
		throw rapidxml::parse_error(exception_message.c_str(), _nodeParent);
	}

	std::string parent_name(_nodeParent->name());
	std::string child_name(_nodeToCheckName);


	//Throwing an exception if the mandatory node doesn't exist
	if (_nodeToCheck == 0) {
		std::string exception_message("The mandatory node doesn't exist in this"
		                              "recipe. The node name is: " + child_name +"."
		                              "The parent name is: " + parent_name);
		throw rapidxml::parse_error(exception_message.c_str(), _nodeParent);
	}
}


std::string RXMLRecipeLoader::loadAttribute(const char * _nameAttribute,
        bool mandatory,
        rapidxml::xml_node<> * _node)
{
	rapidxml::xml_attribute<> * attribute;

	attribute = _node->first_attribute(_nameAttribute, 0, true);
	if ((attribute == 0) && (mandatory)) {
		std::string node_name(_node->name());
		std::string attribute_name(_nameAttribute);
		std::string exception_message("The mandatory attribute doesn't"
		                              "exist in this node. The attribute name is: "
		                              + attribute_name +" . The node name is: " + node_name);
		throw rapidxml::parse_error(exception_message.c_str(), _node);
	} else if (attribute == 0) {
		return "0";
	}

	return attribute->value();
}

} // namespace plugins

} // namespace bque
