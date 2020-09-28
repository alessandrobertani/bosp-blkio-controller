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

#include "bbque/utils/extra_data_container.h"
#include "bbque/utils/utility.h"

#include <iostream>


namespace bbque { namespace utils {


ExtraDataContainer::ExtraDataContainer() {
}

ExtraDataContainer::~ExtraDataContainer() {
	plugin_data.clear();
	attributes.clear();
}

PluginDataKeyPtr_t ExtraDataContainer::GetPluginData(
		std::string const & _plugin_name,
		std::string const & _key) const {

	for (auto & data : plugin_data)
		if (data.first.c_str() == _plugin_name)
			if (data.second->key.c_str() == _key)
				return data.second;
	// Null return
	return PluginDataKeyPtr_t();
}

void ExtraDataContainer::ClearPluginData(
		std::string const & _plugin_name,
		std::string const & _key) {
	// Remove all the attributes under the namespace
	if (_key.empty()) {
		plugin_data.erase(_plugin_name);
		return;
	}

	// Find the plugin set of pairs
	std::pair<PluginDataMap_t::iterator, PluginDataMap_t::iterator> range =
		plugin_data.equal_range(_plugin_name);

	// Find the specific attribute
	PluginDataMap_t::iterator it = range.first;
	while (it != range.second && it->second->key.compare(_key) != 0) ++it;

	// Remove the single attribute
	if (it == plugin_data.end())
		return;
	plugin_data.erase(it);
}

} // namespace utils

} // namespace bbque

