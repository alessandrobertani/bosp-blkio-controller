/*
 * Copyright (C) 2017  Politecnico di Milano
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

#ifndef BBQUE_TRIGGER_H_
#define BBQUE_TRIGGER_H_

#include <functional>

namespace bbque {

namespace trig {

/**
 * @class Trigger
 * @brief A trigger is a component including boolean functions aimed at verify if, given
 * some input parameters, a certain condition is verified or not. A typical use case is
 * the monitoring of hardware resources status and the detection of condition for which
 * an execution of the optimization policy must be triggered.
 */
class Trigger
{
public:

	Trigger(uint32_t threshold_high,
		uint32_t threshold_low = 0,
		float margin = 0.1,
		std::function<void() > action_fn = nullptr,
		bool armed = true) :
	    threshold_high(threshold_high),
	    threshold_low(threshold_low),
	    margin(margin),
	    action_func(action_fn),
	    armed(armed) { }

	virtual ~Trigger() { }

	uint32_t GetThresholdHigh() const
	{
		return this->threshold_high;
	}

	uint32_t GetThresholdLow() const
	{
		return this->threshold_low;
	}

	float GetThresholdMargin() const
	{
		return this->margin;
	}

	virtual bool IsArmed() const
	{
		return this->armed;
	}

	virtual void NotifyUpdatedValue(uint32_t value)
	{
		if ((Check(value)) && this->action_func) {
			this->action_func();
		}
	}

protected:

	/// Threshold high value
	uint32_t threshold_high;

	/// Threshold low armed value
	uint32_t threshold_low;

	/// Margin [0..1)
	float margin;

	/// Callback function in case of trigger activation
	std::function<void() > action_func;

	/// Flag to verify if the trigger is armed
	bool armed;

	/**
	 * @brief Check if a condition is verified given a current value
	 * @return true in case of condition verified, false otherwise
	 */
	virtual bool Check(float curr_value) = 0;
};

} // namespace trig

} // namespace bbque


#endif // BBQUE_TRIGGER_H_
