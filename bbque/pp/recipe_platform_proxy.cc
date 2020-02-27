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



} // namespace bbque
