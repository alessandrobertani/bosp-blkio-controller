/* Copyright (C) 2018  Politecnico di Milano
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

#ifndef BBQUE_TRIGGER_MANAGER_H_
#define BBQUE_TRIGGER_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include "bbque/utils/logging/logger.h"
#include "bbque/trig/trigger.h"
#include "bbque/trig/trigger_overthreshold.h"
#include "bbque/trig/trigger_underthreshold.h"

#define TRIGGER_MODULE_NAME BBQUE_MODULE_NAME("trig")

#define OVER_THRESHOLD_TRIGGER  "over_threshold"
#define UNDER_THRESHOLD_TRIGGER "under_threshold"

namespace bbque {

namespace trig {

/**
 * @class TriggerFactory
 * @brief The component responsible of collecting the instances of Trigger
 */
class TriggerFactory
{
public:

	static TriggerFactory & GetInstance()
	{
		static TriggerFactory instance;
		return instance;
	}

	virtual ~TriggerFactory() { }

	/**
	 * @brief Get a trigger instance
	 * @param id The identification string to retrieve it
	 * @return The shared pointer to the trigger instance, if no id
	 * is provided it instantiates a default OverThresholdTrigger
	 */
	inline std::shared_ptr<Trigger> GetTrigger(
						std::string const & id,
						uint32_t t_high,
						uint32_t t_low,
						float t_margin,
						std::function<void() > action_fn = nullptr,
						bool armed = true)
	{
		if (id.compare(OVER_THRESHOLD_TRIGGER) == 0) {
			logger->Debug("GetTrigger: built a 'over_threshold' trigger");
			return std::make_shared<OverThresholdTrigger>(t_high, t_low, t_margin, action_fn, armed);
		}
		if (id.compare(UNDER_THRESHOLD_TRIGGER) == 0) {
			logger->Debug("GetTrigger: built a 'under_threshold' trigger");
			return std::make_shared<UnderThresholdTrigger>(t_high, t_low, t_margin, action_fn, armed);
		}
		logger->Error("GetTrigger: unknown trigger type specified [%s]", id.c_str());
		return nullptr;
	}

private:

	std::unique_ptr<bu::Logger> logger;

	TriggerFactory()
	{
		logger = bbque::utils::Logger::GetLogger(TRIGGER_MODULE_NAME);

	}

};

} // namespace trig

} // namespace bbque

#endif // BBQUE_TRIGGER_MANAGER_H_

