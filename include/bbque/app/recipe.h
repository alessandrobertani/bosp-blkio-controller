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

#ifndef BBQUE_RECIPE_H_
#define BBQUE_RECIPE_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "bbque/utils/logging/logger.h"
#include "bbque/utils/extra_data_container.h"
#include "bbque/res/resource_constraints.h"
#include "bbque/res/resource_type.h"
#include "tg/requirements.h"

#define RECIPE_NAMESPACE "rcp"

// Maximum number of Application Working Modes manageable
#define MAX_NUM_AWM  255

namespace bu = bbque::utils;
namespace br = bbque::res;

namespace bbque {

namespace res {
class ResourcePath;
using ResourcePathPtr_t = std::shared_ptr<ResourcePath>;
}

using bbque::res::ResourcePathPtr_t;
using bbque::res::ResourceType;

namespace app {

class Application;
class WorkingMode;

using AwmPtr_t = std::shared_ptr<WorkingMode>;
using AwmPtrVect_t = std::vector<AwmPtr_t>;
using ConstrPtr_t = std::shared_ptr<br::ResourceConstraint>;
using ConstrMap_t = std::map<ResourcePathPtr_t, ConstrPtr_t>;

/**
 * @brief Attribute structure for plugin specific data
 */
struct AppPluginData : public bu::PluginDataKey
{

	AppPluginData(std::string const & _pname, std::string const & _key) :
	    bu::PluginDataKey(_pname, _key) { }

	/** Value: a string object */
	std::string value;

};

typedef std::shared_ptr<AppPluginData> AppPluginDataPtr_t;

/**
 * @brief An application recipe
 *
 * Applications need the set of working mode definitions, plus (optionally)
 * constraints) and other information in order to be managed by BarbequeRTRM.
 * Such stuff is stored in a Recipe object. Each application can use more than
 * one recipe, but a single instance must specify the one upon which base its
 * execution.
 */
class Recipe : public bu::ExtraDataContainer
{
	friend class Application;

public:

	/**
	 * @class MappingData
	 * @brief Resource mapping info associated to a task or a buffer
	 */
	class MappingData
	{
	public:

		MappingData() { }

		MappingData(ResourceType _t, uint32_t _id, uint32_t _f) :
		    type(_t), id(_id), freq_khz(_f) { }

		ResourceType type;
		uint32_t id;
		uint32_t freq_khz; // or whatever is the corresponding power state
	};

	/**
	 * @class TaskGraphMapping
	 * @brief Design-time profiled task-graph mapping
	 */
	class TaskGraphMapping
	{
	public:
		uint32_t exec_time_ms; /// Profiled execution time
		uint32_t power_mw; /// Power consumption
		uint32_t mem_bw; /// Memory bandwidth utilization
		std::map<uint32_t, MappingData> tasks;
		std::map<uint32_t, MappingData> buffers;
	};

	using TaskGraphMappingsMap_t = std::map<uint32_t, TaskGraphMapping>;

	/*
	 * @brief Constructor
	 * @param path The pathname for retrieving the recipe
	 */
	Recipe(std::string const & name);

	/**
	 * @brief Destructor
	 */
	virtual ~Recipe();

	/**
	 * @brief Get the path of the recipe
	 * @return The recipe path string (or an info for retrieving it)
	 */
	std::string const & Path()
	{
		return pathname;
	}

	/**
	 * @brief Get the priority parsed from the recipe
	 * @return Priority value
	 */
	uint8_t GetPriority() const
	{
		return priority;
	}

	/**
	 * @brief Set the priority value
	 */
	void SetPriority(uint8_t prio)
	{
		priority = prio;
	}

	/**
	 * @brief Insert an application working mode
	 *
	 * @param id Working mode ID
	 * @param name Working mode descriptive name
	 * @param value The user QoS value of the working mode
	 */
	AwmPtr_t const AddWorkingMode(
				uint8_t id,
				std::string const & name,
				uint8_t value);

	/**
	 * @brief Return an application working mode object by specifying
	 * its identifying name
	 *
	 * @param id Working mode ID
	 * @return A shared pointer to the application working mode searched
	 */
	AwmPtr_t GetWorkingMode(uint8_t id)
	{
		if (last_awm_id == 0)
			return AwmPtr_t();
		return working_modes[id];
	}

	/**
	 * @brief All the working modes defined into the recipe
	 * @return A vector containing all the working modes
	 */
	AwmPtrVect_t const & WorkingModesAll()
	{
		return working_modes;
	}

	/**
	 * @brief Add a set of performance requirements for a task
	 * @param task_id Task identification number
	 * @param tr Task requirements structure
	 */
	void AddTaskRequirements(uint32_t id, TaskRequirements tr)
	{
		tg_reqs[id] = tr;
	}

	/**
	 * @brief Get the set of performance requirements for a task
	 * @param task_id Task identification number
	 * @return Task requirements structure
	 */
	TaskRequirements & GetTaskRequirements(uint32_t id)
	{
		return tg_reqs[id];
	}

	/**
	 * @brief Add a the task-graph mapping option
	 * @param id Mapping choice number
	 * @param tg_map A TaskGraphMapping structure
	 */
	void AddTaskGraphMapping(uint32_t id, TaskGraphMapping tg_map)
	{
		tg_mappings[id] = tg_map;
	}

	/**
	 * @brief Get the task-graph mapping referenced by an id
	 * @param id Mapping choice number
	 * @return A TaskGraphMapping structure
	 */
	TaskGraphMapping GetTaskGraphMapping(uint32_t id) const
	{
		TaskGraphMapping mapping_opt;
		try {
			mapping_opt = tg_mappings.at(id);
		}
		catch (std::out_of_range & ex) {
		}
		return mapping_opt;
	}

	/**
	 * @brief Get the map of all the task-graph mappings
	 * @return A map of ids and TaskGraphMapping data structures
	 */
	TaskGraphMappingsMap_t const & GetTaskGraphMappingAll() const
	{
		return tg_mappings;
	}

	/**
	 * @brief Add a constraint to the static constraint map
	 *
	 * Lower bound assertion: AddConstraint(<resource_path>, value, 0);
	 * Upper bound assertion: AddConstraint(<resource_path>, 0, value);
	 *
	 * If a bound value has been specified previously, take the the greater
	 * between the previous and the current one.
	 *
	 * @param rsrc_path Resource path to constrain
	 * @param lb Lower bound value
	 * @param ub Upper bound value
	 */
	void AddConstraint(std::string const & rsrc_path, uint64_t lb, uint64_t ub);

	/**
	 * @brief Get all the static constraints
	 * @return The map of static constraints
	 */
	ConstrMap_t const & ConstraintsAll()
	{
		return constraints;
	}

	/**
	 * @brief Validate the recipe
	 *
	 * The validation consists of two operations:
	 *   - Validate each AWM, given the current resources availability
	 *   - Normalize the AWM values
	 */
	void Validate();

private:

	/** The logger used by the application */
	std::unique_ptr<bu::Logger> logger;

	/**
	 * Starting from a common recipes root directory, each recipe file
	 * should be named "<application>_<recipe-qualifier>.recipe".
	 * If the recipe is not stored in files but in a database system (i.e.,
	 * GSettings) the following attribute can be an information string useful
	 * for retrieving the recipe.
	 */
	std::string pathname;

	/** Priority */
	uint8_t priority = BBQUE_APP_PRIO_LEVELS - 1;

	/** Expected AWM ID */
	uint8_t last_awm_id = 0;

	/** The complete set of working modes descriptors defined in the recipe */
	AwmPtrVect_t working_modes;

	/** Static constraints included in the recipe */
	ConstrMap_t constraints;

	/** Per-task performance requirements */
	std::map<uint32_t, TaskRequirements> tg_reqs;

	/** Design-time task graph mappings */
	TaskGraphMappingsMap_t tg_mappings;

	/** AWM attribute type flag */
	enum AwmAttrType_t
	{
		VALUE,
		CONFIG_TIME
	};

	/**
	 * Store information to support the normalization of the AWM attributes
	 */
	struct AwmNormalInfo
	{
		/** The AWM attribute to normalize */
		AwmAttrType_t attr;
		/** Maximum value parsed */
		uint32_t max;
		/** Minimum value parsed */
		uint32_t min;
		/**
		 * delta = max - min.
		 * case VALUE: if delta == 0 the value will be set to 0.
		 * This is done in order to give a penalty to recipes wherein all the
		 * AWMs have been set to the same value
		 */
		uint32_t delta;
	};

	/** Normalization support for the AWM values */
	AwmNormalInfo values;

	/** Normalization support for the configuration times */
	AwmNormalInfo config_times;

	/**
	 * @brief Update the normalization info
	 *
	 * This is called when the entire set of working modes is get by an
	 * Application, during its initialization step.
	 *
	 * @param last_value The value of the last AWM inserted
	 */
	void UpdateNormalInfo(AwmNormalInfo & info, uint32_t last_value);

	/**
	 * @brief Normalize the AWMs attributes
	 *
	 * @note Actually "VALUE" and "CONFIGURATION TIME"
	 */
	void Normalize();

	/**
	 * @brief Perform the AWM values normalization
	 *
	 *                       recipe_value
	 * norm_value =  ------------------------------
	 *                         max_value
	 *
	 */
	void NormalizeValue(uint8_t awm_id);

	/**
	 * @brief Perform the AWM values normalization
	 *
	 *                  recipe_time - min(recipe_times)
	 * norm_time =  ---------------------------------------
	 *               max(recipe_times) - min(recipe_times)
	 *
	 */
	void NormalizeConfigTime(uint8_t awm_id);

};

} // namespace app

} // namespace bbque

#endif // BBQUE_RECIPE_H_
