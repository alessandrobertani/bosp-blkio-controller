/*
 * Copyright (C) 2018  Politecnico di Milano
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

#ifndef BBQUE_TRIGGER_OVERTHRESHOLD_H_
#define BBQUE_TRIGGER_OVERTHRESHOLD_H_

#include "bbque/app/application.h"
#include "bbque/res/resources.h"

namespace bbque {

namespace trig {

/**
 * @class OverThresholdTrigger
 * @brief Trigger an action function call when the update value is above a
 * threshold value
 */
class OverThresholdTrigger : public Trigger
{
public:

	OverThresholdTrigger(uint32_t threshold_high,
			uint32_t threshold_low,
			float margin,
			std::function<void() > action_fn = nullptr,
			bool armed = true) :
	    Trigger(threshold_high, threshold_low, margin, action_fn, armed) { }

	virtual ~OverThresholdTrigger() { }

protected:

	/**
	 * @brief Check if the condition holds (if the current value is above the provided value)
	 * for a given margin
	 * @brief true in case of condition verified, false otherwise
	 */
	bool Check(float curr_value) override
	{
		float t_high_with_margin = static_cast<float> (threshold_high) * (1.0 - margin);
		float t_low_with_margin = static_cast<float> (threshold_low) * (1.0 - margin);

		// Rearm
		if (curr_value < t_low_with_margin && !this->armed) {
			armed = true;
		}

		// Trigger!
		if (curr_value > t_high_with_margin && this->armed) {
			armed = false;
			return true;
		}
		return false;
	}

};

} // namespace trig

} // namespace bbque


#endif // BBQUE_TRIGGER_OVERTHRESHOLD_H_
