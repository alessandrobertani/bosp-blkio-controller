/*
 * Copyright (C) 2015  Politecnico di Milano
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

#include "bbque/config.h"
#include "bbque/pm/models/system_model.h"

namespace bbque  { namespace pm {


uint32_t SystemModel::GetResourcePowerFromSystem(
		uint32_t sys_power_budget_mw,
		std::string const & freq_governor) const {
	(void) freq_governor;
	return sys_power_budget_mw;
}


} // namespace pm

} // namespace bbque



