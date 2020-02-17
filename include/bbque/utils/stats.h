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

#ifndef BBQUE_UTILS_STATS_H_
#define BBQUE_UTILS_STATS_H_

#include <memory>
#include <cmath>
#include <list>

#include <bbque/utils/utility.h>

namespace bbque
{
namespace utils
{


/**
 * @class EMA
 * @brief Exponential Moving Average accumulator
 *
 * This class provides a simple accumulator for on-line computation of an
 * Exponential Moving Average, with an exponential factor @see alpha.
 *
 * The alpha factor is expressed in terms of N samples, where alpha =
 * 2/(N+1). For example, N = 19 is equivalent to alpha = 0.1.
 * The half-life of the weights (the interval over which the
 * weights decrease by a factor of two) is approximately N/2.8854
 * (within 1% if N > 5).
 */
class EMA
{

private:
	double alpha;
	double value;
	double  last;
	uint8_t load;
public:
	EMA(int samples = 6, double firstValue = 0) :
		alpha(2.0 / (samples + 1)),
		value(firstValue),
		load(samples)
	{
	}
	double update(double newValue)
	{
		last  = newValue;
		value = ((alpha * last) + ((1 - alpha) * value));
		if (BBQUE_UNLIKELY(load > 0))
			--load;
		return value;
	}
	void reset(int samples, double firstValue)
	{
		alpha = 2.0 / (samples + 1);
		value = firstValue;
		load  = samples;
	}
	double get() const
	{
		if (BBQUE_UNLIKELY(load > 0))
			return 0;
		return value;
	}
	double last_value() const
	{
		return last;
	}
};

class StatsAnalysis
{

private:

	// Current number of stored values
	uint16_t samples_number = 0;

	// The last inserted value
	double last_value = 0.0f;

	// Note: Using Welford's algorithm to compute variance
	double mean = 0.0f;
	double mean2 = 0.0f;

	// Fase detection (optional)
	bool detect_phase_changes = false;
	bool out_of_phase = false;

public:

	StatsAnalysis() {}

	// Returns the average of the values, i.e. (V0 + .. + VN) / N
	inline double GetMean()
	{
		return mean;
	}

	// Return the variance, which equals to E(X^2) - E^2(X)
	inline double GetVariance()
	{
		return samples_number < 2 ? 0.0f : mean2 / (samples_number - 1);
	}

	// Standard deviation: square root of the variance
	inline double GetStandartDeviation()
	{
		return std::sqrt(GetVariance());
	}

	// Standard error: square root of (variance / N)
	inline double GetStandardError()
	{
		return samples_number == 0
		       ? 0.0f : GetStandartDeviation() / std::sqrt(samples_number);
	}

	// CI 90%: 1.96 * standard error
	inline double GetConfidenceInterval90()
	{
		return 1.645 * GetStandardError();
	}

	// CI 95%: 1.96 * standard error
	inline double GetConfidenceInterval95()
	{
		return 1.96 * GetStandardError();
	}

	// CI 99%: 2.58 * standard error
	inline double GetConfidenceInterval99()
	{
		return 2.58 * GetStandardError();
	}

	inline double GetSum()
	{
		return GetMean() * samples_number;
	}

	inline uint16_t GetWindowSize()
	{
		return samples_number;
	}

	inline double GetLastValue()
	{
		return last_value;
	}

	// Clear all values
	inline void Reset()
	{
		samples_number = 0;
		last_value = 0.0f;
		mean = 0.0f;
		mean2 = 0.0f;
		out_of_phase = false;
	}

	// Store a new value
	void InsertValue(double value)
	{

// Confidence Intervals have no meaning if the number of samples is too low
#define IC_MIN_SAMPLES 5

		if (detect_phase_changes && GetWindowSize() > IC_MIN_SAMPLES) {
			// If phase did not change, the new sample should stay in the
			// interval [MEAN-IC99 - MEAN+IC99]
			double min_allowed_value = GetMean() - GetConfidenceInterval99();
			double max_allowed_value = GetMean() + GetConfidenceInterval99();

			bool phase_change_detected =
			        value < min_allowed_value || value > max_allowed_value;

			if (phase_change_detected)
				Reset();
		}

		// Welford's algorithm, to compute variance without
		// Catastrophic cancellations
		samples_number ++;
		double delta = value - mean;
		mean  += delta / samples_number;
		mean2 += delta * (value - mean);

		last_value = value;
	}

	inline void EnablePhaseDetection()
	{
		detect_phase_changes = true;
	}

	inline void DisablePhaseDetection()
	{
		detect_phase_changes = false;
		out_of_phase = false;
	}

};

/**
 * @brief A pointer to an EMA-defined accounter
 */
typedef std::shared_ptr<EMA> pEma_t;

/**
 * @brief A pointer to an MovingStats-defined accounter
 */
typedef std::shared_ptr<StatsAnalysis> pMovingStats_t;


} // namespace utils

} // namespace bbque

#endif // BBQUE_UTILS_STATS_H_
