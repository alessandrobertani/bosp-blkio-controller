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

#include "bbque/app/recipe.h"

#include <cstring>

#include "bbque/app/working_mode.h"
#include "bbque/resource_accounter.h"
#include "bbque/res/resource_path.h"

namespace ba = bbque::app;
namespace br = bbque::res;

namespace bbque {
namespace app {

Recipe::Recipe(std::string const & name) :
    pathname(name)
{
	// Get a logger
	std::string logger_name(RECIPE_NAMESPACE"." + name);
	logger = bu::Logger::GetLogger(logger_name.c_str());
	assert(logger);

	// Init normalization info: VALUE
	memset(&values, 0, sizeof(Recipe::AwmNormalInfo));
	values.min  = UINT8_MAX;
	values.attr = VALUE;

	// Init normalization info: CONFIG_TIME
	memset(&config_times, 0, sizeof(Recipe::AwmNormalInfo));
	config_times.min  = UINT8_MAX;
	config_times.attr = CONFIG_TIME;

	working_modes.resize(MAX_NUM_AWM);
}

Recipe::~Recipe()
{
	working_modes.clear();
	constraints.clear();
}

AwmPtr_t const Recipe::AddWorkingMode(uint8_t _id,
				      std::string const & _name,
				      uint8_t _value)
{
	// Check if the AWMs are sequentially numbered
	if (_id != last_awm_id) {
		logger->Error("AddWorkingModes: Found ID = %d. Expected %d",
			_id, last_awm_id);
		return AwmPtr_t();
	}

	// Insert a new working mode descriptor into the vector
	auto new_awm = std::make_shared<app::WorkingMode>(_id, _name, _value);
	if (new_awm == nullptr) {
		logger->Error("AddWorkingMode: Error in new AWM	creation");
		return AwmPtr_t();
	}

	// Insert the AWM descriptor into the vector
	working_modes[_id] = new_awm;
	++last_awm_id;

	return working_modes[_id];
}

void Recipe::AddConstraint(std::string const & rsrc_path,
			   uint64_t lb,
			   uint64_t ub)
{
	// Check resource existence
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	ResourcePathPtr_t const r_path(ra.GetPath(rsrc_path));
	if (!r_path)
		return;

	// If there's a constraint yet, take the greater value between the bound
	// found and the one passed by argument
	ConstrMap_t::iterator cons_it(constraints.find(r_path));
	if (cons_it != constraints.end()) {
		(cons_it->second)->lower = std::max((cons_it->second)->lower, lb);
		(cons_it->second)->upper = std::max((cons_it->second)->upper, ub);
		logger->Debug("Constraint (edit): %s L=%d U=%d",
			r_path->ToString().c_str(),
			(cons_it->second)->lower,
			(cons_it->second)->upper);
		return;
	}

	// Insert a new constraint
	constraints.insert(std::pair<ResourcePathPtr_t, ConstrPtr_t>(
			r_path, ConstrPtr_t(new br::ResourceConstraint(lb, ub))));
	logger->Debug("Constraint (new): %s L=%" PRIu64 " U=%" PRIu64,
		r_path->ToString().c_str(),
		constraints[r_path]->lower,
		constraints[r_path]->upper);
}

void Recipe::Validate()
{
	working_modes.resize(last_awm_id);

	// Validate each AWM according to current resources total availability
	for (int i = 0; i < last_awm_id; ++i) {
		working_modes[i]->Validate();
		if (!working_modes[i]->Disabled()) {
			UpdateNormalInfo(values, working_modes[i]->RecipeValue());
			UpdateNormalInfo(config_times, working_modes[i]->RecipeConfigTime());
		}
	}
	// Normalize AWMs attributes
	Normalize();
}

void Recipe::UpdateNormalInfo(AwmNormalInfo & info, uint32_t last_value)
{
	// Update the max value
	if (last_value > info.max)
		info.max = last_value;

	// Update the min value
	if (last_value < info.min)
		info.min = last_value;

	// Delta
	info.delta = info.max - info.min;

	// Debug logging
	if (info.attr == VALUE)
		logger->Debug("AWMs values max: %d, min: %d, delta: %d",
			info.max, info.min, info.delta);
	else if (info.attr == CONFIG_TIME)
		logger->Debug("AWMs configuration times max: %d, min: %d, delta: %d",
			info.max, info.min, info.delta);
}

void Recipe::Normalize()
{
	// Set of AWMs: normalize attributes
	for (int i = 0; i < last_awm_id; ++i) {
		if (working_modes[i]->Disabled()) continue;
		NormalizeValue(i);
		NormalizeConfigTime(i);
	}
}

void Recipe::NormalizeValue(uint8_t awm_id)
{
	float normal_value = 0.0;

	// Normalize the value
	if (values.delta > 0)
		normal_value =
		static_cast<float>(working_modes[awm_id]->RecipeValue()) / values.max;
		// There is only one AWM in the recipe
	else if (working_modes.size() == 1)
		normal_value = 1.0;
		// Penalize set of working modes having always the same QoS value
	else
		normal_value = 0.0;

	// Set the normalized value into the AWM
	working_modes[awm_id]->SetNormalValue(normal_value);
	logger->Info("AWM %d normalized value = %.2f ",
		working_modes[awm_id]->Id(),
		working_modes[awm_id]->Value());
}

void Recipe::NormalizeConfigTime(uint8_t awm_id)
{
	float normal_time = 0.0;
	if (config_times.delta > 0)
		normal_time =
		static_cast<float>
		(working_modes[awm_id]->RecipeConfigTime() - config_times.min) /
		(config_times.delta);
	else
		normal_time = 0.0;

	working_modes[awm_id]->SetNormalConfigTime(normal_time);
	logger->Info("AWM %d normalized config time = %.2f ",
		working_modes[awm_id]->Id(),
		working_modes[awm_id]->ConfigTime());
}

} // namespace app

} // namespace bbque

