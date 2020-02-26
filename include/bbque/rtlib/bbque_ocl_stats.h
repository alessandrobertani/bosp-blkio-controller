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

#ifndef BBQUE_OCL_STATS_H_
#define BBQUE_OCL_STATS_H_

#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <CL/cl.h>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/sum.hpp>

#define CL_CMD_QUEUED_TIME 0
#define CL_CMD_SUBMIT_TIME 1
#define CL_CMD_EXEC_TIME   2
#define CL_TAG "opencl"

namespace bac = boost::accumulators;

typedef class RTLIB_OCL_QueueProf RTLIB_OCL_QueueProf_t;
using AccArray_t = std::array<bac::accumulator_set<double,
			bac::stats<bac::tag::sum, bac::tag::min, bac::tag::max,
			bac::tag::variance, bac::tag::mean>>, 3> ;
using CmdProf_t = std::map<cl_command_type, AccArray_t> ;
using QueueProfPtr_t = std::shared_ptr<RTLIB_OCL_QueueProf_t> ;
using CmdProfPtr_t   = std::shared_ptr<CmdProf_t> ;
using OclEventsStatsMap_t = std::map<cl_command_queue, QueueProfPtr_t> ;

extern std::map<cl_command_type, std::string> ocl_cmd_str;

/**
 * The set of commands related to memory transfer/copy operations, to include
 * in the runtime profile. This information will be make available to the
 * resource manager and thus to the optimization policy
 */
static const cl_command_type kernel_exec_cmds[] = {
	CL_COMMAND_NDRANGE_KERNEL ,
	CL_COMMAND_TASK           ,
	CL_COMMAND_NATIVE_KERNEL
};

/**
 * The set of commands related to performing real computations, to include in
 * the runtime profile. This information will be make available to the resource
 * manager and thus to the optimization policy
 */
static const cl_command_type memory_trans_cmds[] = {
	CL_COMMAND_READ_BUFFER           ,
	CL_COMMAND_WRITE_BUFFER          ,
	CL_COMMAND_COPY_BUFFER           ,
	CL_COMMAND_READ_IMAGE            ,
	CL_COMMAND_WRITE_IMAGE           ,
	CL_COMMAND_COPY_IMAGE            ,
	CL_COMMAND_COPY_IMAGE_TO_BUFFER  ,
	CL_COMMAND_COPY_BUFFER_TO_IMAGE  ,
	CL_COMMAND_MAP_BUFFER            ,
	CL_COMMAND_MAP_IMAGE             ,
	CL_COMMAND_UNMAP_MEM_OBJECT      ,
	CL_COMMAND_READ_BUFFER_RECT      ,
	CL_COMMAND_WRITE_BUFFER_RECT
#ifdef CL_API_SUFFIX__VERSION_1_2
	,
	CL_COMMAND_COPY_BUFFER_RECT      ,
	CL_COMMAND_MIGRATE_MEM_OBJECTS   ,
	CL_COMMAND_FILL_BUFFER           ,
	CL_COMMAND_FILL_IMAGE
#endif
};


/**
 * @class RTLIB_OCL_QueueProf
 *
 * @brief Collect events profiling info for an OpenCL command queue
 */
class RTLIB_OCL_QueueProf
{
public:
	~RTLIB_OCL_QueueProf()
	{
		std::map<void *, cl_event>::iterator it_ev;
		cl_uint ref_count;

		for (it_ev = events.begin(); it_ev != events.end(); it_ev++) {
			clGetEventInfo(it_ev->second, CL_EVENT_REFERENCE_COUNT, sizeof(cl_uint),
						   &ref_count, NULL);

			if (ref_count > 0)
				clReleaseEvent(it_ev->second);
		}

		cmd_prof.clear();
	};

	std::map<void *, cl_event> events;
	std::map<void *, AccArray_t> addr_prof;
	std::map<cl_command_type, AccArray_t> cmd_prof;
};

#endif // BBQUE_OCL_STATS_H_
