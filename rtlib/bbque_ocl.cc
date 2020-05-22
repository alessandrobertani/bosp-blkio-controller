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
#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "bbque/config.h"
#include "bbque/rtlib.h"
#include "bbque/rtlib/bbque_ocl.h"
#include "bbque/utils/utility.h"
#include "bbque/utils/logging/logger.h"
#include "bbque/pp/opencl_platform_proxy.h"

#undef  BBQUE_LOG_MODULE
#define BBQUE_LOG_MODULE "rtl.ocl"

namespace bu = bbque::utils;

#ifdef __cplusplus
extern "C" {
#endif

static std::unique_ptr<bu::Logger> logger;

#define DB2(x)

extern const char * rtlib_app_name;
extern RTLIB_OpenCL_t rtlib_ocl;
extern RTLIB_Services_t rtlib_services;
extern std::map<cl_command_queue, QueueProfPtr_t> ocl_queues_prof;
extern std::map<void *, cl_command_type> ocl_addr_cmd;

/* Platform API */
CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformIDs(cl_uint num_entries,
		 cl_platform_id * platforms,
		 cl_uint * num_platforms)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int result = rtlib_ocl.getPlatformIDs(num_entries, platforms, num_platforms);
	logger->Debug("Calling clGetPlatformIDs(num_entries: %u, *platforms: %p, *num_platforms: %p)...",
		num_entries, platforms, num_platforms);
	DB2(
	if (num_platforms != nullptr)
	logger->Debug("Result clGetPlatformIDs(platforms: %p, num_platforms: %d)...",
		platforms, *num_platforms);
	)
		return result;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformInfo(cl_platform_id platform,
		  cl_platform_info param_name,
		  size_t param_value_size,
		  void * param_value,
		  size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	logger->Debug("Calling clGetPlatformInfo("
		"platform: %p, param_name: %u, param_value_size: %d"
		",*param_value: %p, *param_value_size_ret: %p)...",
		platform, param_name, param_value_size, param_value, param_value_size_ret);

	cl_int result = rtlib_ocl.getPlatformInfo(
						platform, param_name, param_value_size, param_value, param_value_size_ret);
	DB2(

	if (param_value != nullptr && param_value_size_ret != nullptr) {
	logger->Debug("Result clGetPlatformInfo(param_value: %p, param_value_size_ret: %d)...",
		param_value, *param_value_size_ret);
	})
	return result;
}

/* Device APIs */
CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceIDs(cl_platform_id platform,
	       cl_device_type device_type,
	       cl_uint num_entries,
	       cl_device_id * devices,
	       cl_uint * num_devices)
CL_API_SUFFIX__VERSION_1_0
{
	(void) device_type;
	(void) num_entries;

	logger->Debug("Calling clGetDeviceIDs("
		"platform: %p, device_type: %u, num_entries: %u, devices: %p, num_devices: %p)...",
		platform, device_type, num_entries, devices, num_devices);

	// If BarbequeRTRM has not assigned any platform, take the one specified
	// by the application as argument
	if (rtlib_ocl.platform_id < 0) {
		logger->Warn("clGetDeviceIDs: platform not assigned, setting 0 by default");
	}
	else if (platform != rtlib_ocl.platforms[rtlib_ocl.platform_id]) {
		logger->Error("clGetDeviceIDs: Invalid platform specified");
		return CL_INVALID_PLATFORM;
	}
	else {
		// Force the assignment of a single OpenCL Platform
		logger->Notice("clGetDeviceIds: BarbequeRTRM assigned platform: %d",
			rtlib_ocl.platform_id);
		platform = rtlib_ocl.platforms[rtlib_ocl.platform_id];
	}

	if (rtlib_ocl.device_id < 0) {
		logger->Warn("clGetDeviceIDs: no BarbequeRTRM assignment, using the "
			"application choices...");
		return rtlib_ocl.getDeviceIDs(platform,
					device_type,
					num_entries,
					devices,
					num_devices);
	}
	else {
		// TODO: Current implementation: return at most ONCE device,
		// please extend with multiple devices
		(*num_devices) = 1;
		logger->Warn("clGetDeviceIDs: BarbequeRTRM assigning a single device");
	}

	if (devices == NULL)
		return CL_SUCCESS;

	char dev_name[1024];
	cl_device_type dev_type;
	cl_device_id * curr_devices = rtlib_ocl.devices[rtlib_ocl.platform_id];

	for (uint8_t i = 0; i < rtlib_ocl.num_devices; ++ i) {
		clGetDeviceInfo(curr_devices[i],
				CL_DEVICE_TYPE,
				sizeof (dev_type),
				&dev_type, NULL);
		clGetDeviceInfo(curr_devices[i], CL_DEVICE_NAME, 1024, dev_name, NULL);
		logger->Debug("clGetDeviceIDs: device type=%d, name=%s\n",
			(int) dev_type, dev_name);
	};

	// Assigned device
	(*devices) = curr_devices[rtlib_ocl.device_id];
	logger->Notice("clGetDeviceIDs [BarbequeRTRM assigned device: %d @{%p}]",
		rtlib_ocl.device_id, (*devices));

	return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clGetDeviceInfo(cl_device_id device,
		cl_device_info param_name,
		size_t param_value_size,
		void * param_value,
		size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(
	logger->Debug("Calling clGetDeviceInfo(device: %p, param_name: %u, param_value_size: %d, param_value: %p, param_value_size_ret: %p)...",
		device, param_name, param_value_size, param_value, param_value_size_ret));
	cl_int result = rtlib_ocl.getDeviceInfo(device,
						param_name,
						param_value_size,
						param_value,
						param_value_size_ret);
	DB2(
	if (param_value != nullptr && param_value_size_ret != nullptr) {
	logger->Debug("Result clGetDeviceInfo(param_value: %p, param_value_size: %p, param_value_size_ret: %d)...",
		param_value, param_value_size, *param_value_size_ret);
	})
	return result;
}

#ifdef CL_API_SUFFIX__VERSION_1_2

CL_API_ENTRY cl_int CL_API_CALL
clCreateSubDevices(cl_device_id in_device,
		   const cl_device_partition_property * properties,
		   cl_uint num_devices,
		   cl_device_id * out_devices,
		   cl_uint * num_devices_ret)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clCreateSubDevices()..."));
	return rtlib_ocl.createSubDevices(in_device, properties, num_devices, out_devices, num_devices_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainDevice(cl_device_id device) CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clRetainDevice()..."));
	return rtlib_ocl.retainDevice(device);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseDevice(cl_device_id device) CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clReleaseDevice()..."));
	return rtlib_ocl.releaseDevice(device);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelArgInfo(cl_kernel kernel,
		   cl_uint arg_indx,
		   cl_kernel_arg_info param_name,
		   size_t param_value_size,
		   void * param_value,
		   size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clGetKernelArgInfo()..."));
	return rtlib_ocl.getKernelArgInfo(kernel, arg_indx, param_name,
					param_value_size, param_value, param_value_size_ret);
}

CL_API_ENTRY cl_mem CL_API_CALL
clCreateImage(cl_context context,
	      cl_mem_flags flags,
	      const cl_image_format * image_format,
	      const cl_image_desc * image_desc,
	      void * host_ptr,
	      cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clCreateImage()..."));
	return rtlib_ocl.createImage(context, flags, image_format, image_desc,
				host_ptr, errcode_ret);
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBuiltInKernels(cl_context context,
				  cl_uint num_devices,
				  const cl_device_id * device_list,
				  const char * kernel_names,
				  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clCreateProgramWithBuiltInKernels()..."));
	return rtlib_ocl.createProgramWithBuiltInKernels(context, num_devices,
							device_list, kernel_names, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clCompileProgram(cl_program program,
		 cl_uint num_devices,
		 const cl_device_id * device_list,
		 const char * options,
		 cl_uint num_input_headers,
		 const cl_program * input_headers,
		 const char ** header_include_names,
		 void (CL_CALLBACK * pfn_notify)(cl_program program, void * user_data),
		 void * user_data)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clCompileProgram()..."));
	return rtlib_ocl.compileProgram(program, num_devices, device_list, options,
					num_input_headers, input_headers, header_include_names, (*pfn_notify), user_data);
}

CL_API_ENTRY cl_program CL_API_CALL
clLinkProgram(cl_context context,
	      cl_uint num_devices,
	      const cl_device_id * device_list,
	      const char * options,
	      cl_uint num_input_programs,
	      const cl_program * input_programs,
	      void (CL_CALLBACK * pfn_notify)(cl_program program, void * user_data),
	      void * user_data,
	      cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clLinkProgram()..."));
	return rtlib_ocl.linkProgram(context, num_devices, device_list, options,
				num_input_programs, input_programs, (*pfn_notify), user_data, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clUnloadPlatformCompiler(cl_platform_id platform) CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clUnloadPlatformCompiler()..."));
	return rtlib_ocl.unloadPlatformCompiler(platform);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueFillBuffer(cl_command_queue command_queue,
		    cl_mem buffer,
		    const void * pattern,
		    size_t pattern_size,
		    size_t offset,
		    size_t size,
		    cl_uint num_events_in_wait_list,
		    const cl_event * event_wait_list,
		    cl_event * event)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clEnqueueFillBuffer()..."));
	return rtlib_ocl.enqueueFillBuffer(command_queue, buffer, pattern, pattern_size,
					offset, size, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueFillImage(cl_command_queue command_queue,
		   cl_mem image,
		   const void * fill_color,
		   const size_t * origin,
		   const size_t * region,
		   cl_uint num_events_in_wait_list,
		   const cl_event * event_wait_list,
		   cl_event * event)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clEnqueueFillImage()..."));
	return rtlib_ocl.enqueueFillImage(command_queue, image, fill_color, origin,
					region, num_events_in_wait_list, event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueMigrateMemObjects(cl_command_queue command_queue,
			   cl_uint num_mem_objects,
			   const cl_mem * mem_objects,
			   cl_mem_migration_flags flags,
			   cl_uint num_events_in_wait_list,
			   const cl_event * event_wait_list,
			   cl_event * event)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clEnqueueMigrateMemObjects()..."));
	return rtlib_ocl.enqueueMigrateMemObjects(command_queue, num_mem_objects,
						mem_objects,
						flags,
						num_events_in_wait_list,
						event_wait_list,
						event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueMarkerWithWaitList(cl_command_queue command_queue,
			    cl_uint num_events_in_wait_list,
			    const cl_event * event_wait_list,
			    cl_event * event)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clEnqueueMarkerWithWaitList()..."));
	return rtlib_ocl.enqueueMarkerWithWaitList(command_queue, num_events_in_wait_list,
						event_wait_list, event);
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueBarrierWithWaitList(cl_command_queue command_queue,
			     cl_uint num_events_in_wait_list,
			     const cl_event * event_wait_list,
			     cl_event * event)
CL_API_SUFFIX__VERSION_1_2
{
	DB2(logger->Debug("Calling clEnqueueBarrierWithWaitList()..."));
	return rtlib_ocl.enqueueBarrierWithWaitList(command_queue,
						num_events_in_wait_list, event_wait_list, event);
}

#endif

/* Context APIs  */
CL_API_ENTRY cl_context CL_API_CALL
clCreateContext(
		const cl_context_properties * properties,
		cl_uint num_devices,
		const cl_device_id * devices,
		void (CL_CALLBACK * pfn_notify)(const char * errinfo, const void * private_info,
		size_t cb, void * user_data),
		void * user_data,
		cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateContext()..."));
	return rtlib_ocl.createContext(properties, num_devices, devices, (*pfn_notify),
				user_data, errcode_ret);
}

CL_API_ENTRY cl_context CL_API_CALL
clCreateContextFromType(
			const cl_context_properties * properties,
			cl_device_type device_type,
			void (CL_CALLBACK * pfn_notify)(const char * errinfo, const void * private_info,
			size_t cb, void * user_data),
			void * user_data,
			cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateContextFromType(properties: %p, device_type: 0x%08X)...",
			properties, device_type));
	cl_context context = rtlib_ocl.createContextFromType(properties,
							device_type,
							(*pfn_notify),
							user_data,
							errcode_ret);
	DB2(logger->Debug("Result clCreateContextFromType [%p]...", context));
	return context;
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainContext(cl_context context) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainContext()..."));
	return rtlib_ocl.retainContext(context);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseContext(cl_context context) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseContext()..."));
	return rtlib_ocl.releaseContext(context);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetContextInfo(
		 cl_context context,
		 cl_context_info param_name,
		 size_t param_value_size,
		 void * param_value,
		 size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetContextInfo()..."));
	cl_int result = CL_SUCCESS;

	if ((rtlib_ocl.device_id < 0) || param_name != CL_CONTEXT_DEVICES) {
		DB2(logger->Debug("clGetContextInfo: call forwarding..."));
		return rtlib_ocl.getContextInfo(
						context, param_name, param_value_size,
						param_value, param_value_size_ret);
	}

	// First call - The first call is used just to collect the size
	// required to properly setup the value container
	if (param_value_size_ret != NULL)
		(*param_value_size_ret) = sizeof (cl_device_id);

	if (param_value == NULL)
		return result;

	// Second call - The second call is used to collect the values which count
	// has been identified by the first call.
	cl_device_id * dev = & rtlib_ocl.devices[rtlib_ocl.platform_id][rtlib_ocl.device_id];
	result = rtlib_ocl.getContextInfo(
					context, param_name, sizeof (cl_device_id),
					(*dev), NULL);
	memcpy(param_value, dev, sizeof (cl_device_id));

	// FIXME: This forces a successful exit. It is unclear why the native
	// call returns a CL_INVALID_VALUE error although the wrong conditions
	// explained in the API documentation do not hold.
	return CL_SUCCESS;
}

/* Command Queue APIs */
CL_API_ENTRY cl_command_queue CL_API_CALL
clCreateCommandQueue(
		     cl_context context,
		     cl_device_id device,
		     cl_command_queue_properties properties,
		     cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	properties |= CL_QUEUE_PROFILING_ENABLE;
	DB2(logger->Debug("Calling clCreateCommandQueue(context: %p, device: %p, properties: 0x%08X, <...>)...",
			context, device, properties));
	return rtlib_ocl.createCommandQueue(context, device, properties, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainCommandQueue(cl_command_queue command_queue)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainCommandQueue()..."));
	return rtlib_ocl.retainCommandQueue(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueue(cl_command_queue command_queue)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseCommandQueue()..."));
	return rtlib_ocl.releaseCommandQueue(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetCommandQueueInfo(
		      cl_command_queue command_queue,
		      cl_command_queue_info param_name,
		      size_t param_value_size,
		      void * param_value,
		      size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetCommandQueueInfo()..."));
	return rtlib_ocl.getCommandQueueInfo(command_queue, param_name,
					param_value_size, param_value, param_value_size_ret);
}

/* Memory Object APIs */
CL_API_ENTRY cl_mem CL_API_CALL
clCreateBuffer(
	       cl_context context,
	       cl_mem_flags flags,
	       size_t size,
	       void * host_ptr,
	       cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateBuffer()..."));
	return rtlib_ocl.createBuffer(context, flags, size, host_ptr, errcode_ret);
}

CL_API_ENTRY cl_mem CL_API_CALL
clCreateSubBuffer(
		  cl_mem buffer,
		  cl_mem_flags flags,
		  cl_buffer_create_type buffer_create_type,
		  const void * buffer_create_info,
		  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_1
{
	DB2(logger->Debug("Calling clCreateSubBuffer()..."));
	return rtlib_ocl.createSubBuffer(buffer, flags, buffer_create_type,
					buffer_create_info, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainMemObject()..."));
	return rtlib_ocl.retainMemObject(memobj);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseMemObject(cl_mem memobj) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseMemObject()..."));
	return rtlib_ocl.releaseMemObject(memobj);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetSupportedImageFormats(
			   cl_context context,
			   cl_mem_flags flags,
			   cl_mem_object_type image_type,
			   cl_uint num_entries,
			   cl_image_format * image_formats,
			   cl_uint * num_image_formats)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetSupportedImageFormats()..."));
	return rtlib_ocl.getSupportedImageFormats(context, flags, image_type,
						num_entries, image_formats, num_image_formats);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetMemObjectInfo(
		   cl_mem memobj,
		   cl_mem_info param_name,
		   size_t param_value_size,
		   void * param_value,
		   size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetMemObjectInfo()..."));
	return rtlib_ocl.getMemObjectInfo(memobj, param_name, param_value_size,
					param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetImageInfo(
	       cl_mem  image,
	       cl_image_info param_name,
	       size_t param_value_size,
	       void * param_value,
	       size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetImageInfo()..."));
	return rtlib_ocl.getImageInfo(image, param_name, param_value_size,
				param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clSetMemObjectDestructorCallback(cl_mem memobj,
				 void (CL_CALLBACK * pfn_notify)(cl_mem memobj, void * user_data),
				 void * user_data)
CL_API_SUFFIX__VERSION_1_1
{
	DB2(logger->Debug("Calling clSetMemObjectDestructorCallback()..."));
	return rtlib_ocl.setMemObjectDestructorCallback(memobj, (*pfn_notify), user_data);
}

/* Sampler APIs */
CL_API_ENTRY cl_sampler CL_API_CALL
clCreateSampler(
		cl_context context,
		cl_bool normalized_coords,
		cl_addressing_mode addressing_mode,
		cl_filter_mode filter_mode,
		cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateSampler()..."));
	return rtlib_ocl.createSampler(context,
				normalized_coords,
				addressing_mode,
				filter_mode,
				errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainSampler(cl_sampler sampler) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainSampler()..."));
	return rtlib_ocl.retainSampler(sampler);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseSampler(cl_sampler sampler) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseSampler()..."));
	return rtlib_ocl.releaseSampler(sampler);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetSamplerInfo(
		 cl_sampler sampler,
		 cl_sampler_info param_name,
		 size_t param_value_size,
		 void * param_value,
		 size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetSamplerInfo()..."));
	return rtlib_ocl.getSamplerInfo(sampler,
					param_name,
					param_value_size,
					param_value,
					param_value_size_ret);
}

/* Program Object APIs  */
CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithSource(
			  cl_context context,
			  cl_uint count,
			  const char ** strings,
			  const size_t * lengths,
			  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateProgramWithSource()..."));
	return rtlib_ocl.createProgramWithSource(context,
						count,
						strings,
						lengths,
						errcode_ret);
}

CL_API_ENTRY cl_program CL_API_CALL
clCreateProgramWithBinary(
			  cl_context context,
			  cl_uint num_devices,
			  const cl_device_id * device_list,
			  const size_t * lengths,
			  const unsigned char ** binaries,
			  cl_int * binary_status,
			  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateProgramWithBinary()..."));
	return rtlib_ocl.createProgramWithBinary(context,
						num_devices,
						device_list,
						lengths,
						binaries,
						binary_status,
						errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainProgram()..."));
	return rtlib_ocl.retainProgram(program);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseProgram(cl_program program) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseProgram()..."));
	return rtlib_ocl.releaseProgram(program);
}

CL_API_ENTRY cl_int CL_API_CALL
clBuildProgram(
	       cl_program program,
	       cl_uint num_devices,
	       const cl_device_id * device_list,
	       const char * options,
	       void (CL_CALLBACK * pfn_notify)(cl_program program, void * user_data),
	       void * user_data)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clBuildProgram()..."));
	return rtlib_ocl.buildProgram(program, num_devices, device_list, options,
				(*pfn_notify), user_data);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramInfo(
		 cl_program program,
		 cl_program_info param_name,
		 size_t param_value_size,
		 void * param_value,
		 size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetProgramInfo()..."));
	return rtlib_ocl.getProgramInfo(program, param_name, param_value_size,
					param_value, param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetProgramBuildInfo(
		      cl_program program,
		      cl_device_id device,
		      cl_program_build_info param_name,
		      size_t  param_value_size,
		      void * param_value,
		      size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetProgramBuildInfo()..."));
	return rtlib_ocl.getProgramBuildInfo(program, device, param_name,
					param_value_size, param_value,
					param_value_size_ret);
}

/* Kernel Object APIs */
CL_API_ENTRY cl_kernel CL_API_CALL
clCreateKernel(
	       cl_program program,
	       const char * kernel_name,
	       cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateKernel()..."));
	return rtlib_ocl.createKernel(program, kernel_name, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clCreateKernelsInProgram(
			 cl_program program,
			 cl_uint num_kernels,
			 cl_kernel * kernels,
			 cl_uint * num_kernels_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clCreateKernelsInProgram()..."));
	return rtlib_ocl.createKernelsInProgram(program,
						num_kernels,
						kernels,
						num_kernels_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainKernel(cl_kernel kernel) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainKernel()..."));
	return rtlib_ocl.retainKernel(kernel);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseKernel(cl_kernel kernel) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseKernel()..."));
	return rtlib_ocl.releaseKernel(kernel);
}

CL_API_ENTRY cl_int CL_API_CALL
clSetKernelArg(
	       cl_kernel kernel,
	       cl_uint arg_index,
	       size_t arg_size,
	       const void * arg_value)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clSetKernelArg()..."));
	return rtlib_ocl.setKernelArg(kernel, arg_index, arg_size, arg_value);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelInfo(
		cl_kernel kernel,
		cl_kernel_info param_name,
		size_t param_value_size,
		void * param_value,
		size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetKernelInfo()..."));
	return rtlib_ocl.getKernelInfo(kernel,
				param_name,
				param_value_size,
				param_value,
				param_value_size_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetKernelWorkGroupInfo(
			 cl_kernel kernel,
			 cl_device_id device,
			 cl_kernel_work_group_info param_name,
			 size_t param_value_size,
			 void * param_value,
			 size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetKernelWorkGroupInfo()..."));
	return rtlib_ocl.getKernelWorkGroupInfo(kernel,
						device,
						param_name,
						param_value_size,
						param_value,
						param_value_size_ret);
}

/* Event Object APIs */
CL_API_ENTRY cl_int CL_API_CALL
clWaitForEvents(
		cl_uint num_events,
		const cl_event * event_list)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clWaitForEvents()..."));
	return rtlib_ocl.waitForEvents(num_events, event_list);
}

CL_API_ENTRY cl_int CL_API_CALL
clGetEventInfo(
	       cl_event event,
	       cl_event_info param_name,
	       size_t param_value_size,
	       void * param_value,
	       size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetEventInfo()..."));
	return rtlib_ocl.getEventInfo(event, param_name, param_value_size,
				param_value, param_value_size_ret);
}

CL_API_ENTRY cl_event CL_API_CALL
clCreateUserEvent(
		  cl_context context,
		  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_1
{
	DB2(logger->Debug("Calling clCreateUserEvent()..."));
	return rtlib_ocl.createUserEvent(context, errcode_ret);
}

CL_API_ENTRY cl_int CL_API_CALL
clRetainEvent(cl_event event) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clRetainEvent()..."));
	return rtlib_ocl.retainEvent(event);
}

CL_API_ENTRY cl_int CL_API_CALL
clReleaseEvent(cl_event event) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clReleaseEvent()..."));
	return rtlib_ocl.releaseEvent(event);
}

CL_API_ENTRY cl_int CL_API_CALL
clSetUserEventStatus(
		     cl_event event,
		     cl_int execution_status)
CL_API_SUFFIX__VERSION_1_1
{
	DB2(logger->Debug("Calling clSetUserEventStatus()..."));
	return rtlib_ocl.setUserEventStatus(event, execution_status);
}

CL_API_ENTRY cl_int CL_API_CALL
clSetEventCallback(
		   cl_event event,
		   cl_int command_exec_callback_type,
		   void (CL_CALLBACK * pfn_event_notify)(cl_event event,
		   cl_int event_command_exec_status, void * user_data),
		   void * user_data)
CL_API_SUFFIX__VERSION_1_1
{
	DB2(logger->Debug("Calling clSetEventCallback()..."));
	return rtlib_ocl.setEventCallback(event, command_exec_callback_type,
					(*pfn_event_notify), user_data);
}

/* Profiling APIs */
CL_API_ENTRY cl_int CL_API_CALL
clGetEventProfilingInfo(
			cl_event event,
			cl_profiling_info param_name,
			size_t param_value_size,
			void * param_value,
			size_t * param_value_size_ret)
CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clGetEventProfilingInfo()..."));
	return rtlib_ocl.getEventProfilingInfo(event,
					param_name,
					param_value_size,
					param_value,
					param_value_size_ret);
}

/* Flush and Finish APIs */
CL_API_ENTRY cl_int CL_API_CALL
clFlush(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clFlush()..."));
	return rtlib_ocl.flush(command_queue);
}

CL_API_ENTRY cl_int CL_API_CALL
clFinish(cl_command_queue command_queue) CL_API_SUFFIX__VERSION_1_0
{
	DB2(logger->Debug("Calling clFinish()..."));
	return rtlib_ocl.finish(command_queue);
}

/* Enqueued Commands APIs */
CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadBuffer(
		    cl_command_queue command_queue,
		    cl_mem buffer,
		    cl_bool blocking_read,
		    size_t offset,
		    size_t size,
		    void * ptr,
		    cl_uint num_events_in_wait_list,
		    const cl_event * event_wait_list,
		    cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueReadBuffer()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueReadBuffer(command_queue,
					buffer,
					blocking_read,
					offset,
					size,
					ptr,
					num_events_in_wait_list,
					event_wait_list,
					event);
	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueReadBuffer()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadBufferRect(
			cl_command_queue command_queue,
			cl_mem buffer,
			cl_bool blocking_read,
			const size_t * buffer_origin,
			const size_t * host_origin,
			const size_t * region,
			size_t buffer_row_pitch,
			size_t buffer_slice_pitch,
			size_t host_row_pitch,
			size_t host_slice_pitch,
			void * ptr,
			cl_uint num_events_in_wait_list,
			const cl_event * event_wait_list,
			cl_event * event)
CL_API_SUFFIX__VERSION_1_1
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueReadBufferRect()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueReadBufferRect(command_queue, buffer,
						blocking_read,
						buffer_origin,
						host_origin,
						region,
						buffer_row_pitch,
						buffer_slice_pitch,
						host_row_pitch,
						host_slice_pitch,
						ptr,
						num_events_in_wait_list,
						event_wait_list,
						event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueReadBufferRect()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteBuffer(
		     cl_command_queue command_queue,
		     cl_mem buffer,
		     cl_bool blocking_write,
		     size_t offset,
		     size_t size,
		     const void * ptr,
		     cl_uint num_events_in_wait_list,
		     const cl_event * event_wait_list,
		     cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueWriteBuffer()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueWriteBuffer(command_queue,
					buffer,
					blocking_write,
					offset,
					size,
					ptr,
					num_events_in_wait_list,
					event_wait_list,
					event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueWriteBuffer()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteBufferRect(
			 cl_command_queue command_queue,
			 cl_mem buffer,
			 cl_bool blocking_write,
			 const size_t * buffer_origin,
			 const size_t * host_origin,
			 const size_t * region,
			 size_t buffer_row_pitch,
			 size_t buffer_slice_pitch,
			 size_t host_row_pitch,
			 size_t host_slice_pitch,
			 const void * ptr,
			 cl_uint num_events_in_wait_list,
			 const cl_event * event_wait_list,
			 cl_event * event)
CL_API_SUFFIX__VERSION_1_1
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueWriteBufferRect()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueWriteBufferRect(command_queue,
						buffer,
						blocking_write,
						buffer_origin,
						host_origin,
						region,
						buffer_row_pitch,
						buffer_slice_pitch,
						host_row_pitch,
						host_slice_pitch,
						ptr,
						num_events_in_wait_list,
						event_wait_list, event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueWriteBufferRect()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyBuffer(
		    cl_command_queue command_queue,
		    cl_mem src_buffer,
		    cl_mem dst_buffer,
		    size_t src_offset,
		    size_t dst_offset,
		    size_t size,
		    cl_uint num_events_in_wait_list,
		    const cl_event * event_wait_list,
		    cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueCopyBuffer()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueCopyBuffer(command_queue,
					src_buffer,
					dst_buffer,
					src_offset,
					dst_offset,
					size,
					num_events_in_wait_list,
					event_wait_list,
					event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueCopyBuffer()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyBufferRect(
			cl_command_queue command_queue,
			cl_mem src_buffer,
			cl_mem dst_buffer,
			const size_t * src_origin,
			const size_t * dst_origin,
			const size_t * region,
			size_t src_row_pitch,
			size_t src_slice_pitch,
			size_t dst_row_pitch,
			size_t dst_slice_pitch,
			cl_uint num_events_in_wait_list,
			const cl_event * event_wait_list,
			cl_event * event)
CL_API_SUFFIX__VERSION_1_1
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueCopyBufferRect()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueCopyBufferRect(command_queue,
						src_buffer,
						dst_buffer,
						src_origin,
						dst_origin,
						region,
						src_row_pitch,
						src_slice_pitch,
						dst_row_pitch,
						dst_slice_pitch,
						num_events_in_wait_list,
						event_wait_list,
						event);
	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueCopyBufferRect()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueReadImage(
		   cl_command_queue command_queue,
		   cl_mem image,
		   cl_bool blocking_read,
		   const size_t * origin,
		   const size_t * region,
		   size_t row_pitch,
		   size_t slice_pitch,
		   void * ptr,
		   cl_uint num_events_in_wait_list,
		   const cl_event * event_wait_list,
		   cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueReadImage()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueReadImage(command_queue,
					image,
					blocking_read,
					origin,
					region,
					row_pitch,
					slice_pitch,
					ptr,
					num_events_in_wait_list,
					event_wait_list,
					event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueReadImage()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueWriteImage(
		    cl_command_queue command_queue,
		    cl_mem image,
		    cl_bool blocking_write,
		    const size_t * origin,
		    const size_t * region,
		    size_t input_row_pitch,
		    size_t input_slice_pitch,
		    const void * ptr,
		    cl_uint num_events_in_wait_list,
		    const cl_event * event_wait_list,
		    cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueWriteImage()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueWriteImage(command_queue,
					image,
					blocking_write,
					origin,
					region,
					input_row_pitch,
					input_slice_pitch,
					ptr,
					num_events_in_wait_list,
					event_wait_list,
					event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueWriteImage()",
			status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyImage(
		   cl_command_queue command_queue,
		   cl_mem src_image,
		   cl_mem dst_image,
		   const size_t * src_origin,
		   const size_t * dst_origin,
		   const size_t * region,
		   cl_uint num_events_in_wait_list,
		   const cl_event * event_wait_list,
		   cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueCopyImage()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueCopyImage(command_queue,
					src_image,
					dst_image,
					src_origin,
					dst_origin,
					region,
					num_events_in_wait_list,
					event_wait_list,
					event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueCopyImage()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyImageToBuffer(
			   cl_command_queue command_queue,
			   cl_mem src_image,
			   cl_mem dst_buffer,
			   const size_t * src_origin,
			   const size_t * region,
			   size_t dst_offset,
			   cl_uint num_events_in_wait_list,
			   const cl_event * event_wait_list,
			   cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueCopyImageToBuffer()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueCopyImageToBuffer(command_queue, src_image,
						dst_buffer,
						src_origin,
						region,
						dst_offset,
						num_events_in_wait_list,
						event_wait_list,
						event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueCopyImageToBuffer()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueCopyBufferToImage(
			   cl_command_queue command_queue,
			   cl_mem src_buffer,
			   cl_mem dst_image,
			   size_t src_offset,
			   const size_t * dst_origin,
			   const size_t * region,
			   cl_uint num_events_in_wait_list,
			   const cl_event * event_wait_list,
			   cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueCopyBufferToImage()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueCopyBufferToImage(command_queue,
						src_buffer,
						dst_image,
						src_offset,
						dst_origin,
						region,
						num_events_in_wait_list,
						event_wait_list, event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueCopyBufferToImage()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY void * CL_API_CALL
clEnqueueMapBuffer(
		   cl_command_queue command_queue,
		   cl_mem buffer,
		   cl_bool blocking_map,
		   cl_map_flags map_flags,
		   size_t offset,
		   size_t size,
		   cl_uint num_events_in_wait_list,
		   const cl_event * event_wait_list,
		   cl_event * event,
		   cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	void * buff_ptr;
	DB2(logger->Debug("Calling clEnqueueMapBuffer()..."));
	EVENT_RC_CONTROL(event);
	buff_ptr = rtlib_ocl.enqueueMapBuffer(command_queue,
					buffer,
					blocking_map,
					map_flags,
					offset,
					size,
					num_events_in_wait_list,
					event_wait_list,
					event,
					errcode_ret);

	if ((*errcode_ret) != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueMapBuffer()", *errcode_ret);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return buff_ptr;
}

CL_API_ENTRY void * CL_API_CALL
clEnqueueMapImage(
		  cl_command_queue command_queue,
		  cl_mem image,
		  cl_bool blocking_map,
		  cl_map_flags map_flags,
		  const size_t * origin,
		  const size_t * region,
		  size_t * image_row_pitch,
		  size_t * image_slice_pitch,
		  cl_uint num_events_in_wait_list,
		  const cl_event * event_wait_list,
		  cl_event * event,
		  cl_int * errcode_ret)
CL_API_SUFFIX__VERSION_1_0
{
	void * buff_ptr;
	DB2(logger->Debug("Calling clEnqueueMapImage()..."));
	EVENT_RC_CONTROL(event);
	buff_ptr = rtlib_ocl.enqueueMapImage(command_queue,
					image,
					blocking_map,
					map_flags,
					origin,
					region,
					image_row_pitch,
					image_slice_pitch,
					num_events_in_wait_list,
					event_wait_list,
					event,
					errcode_ret);

	if (errcode_ret != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueMapImage()", *errcode_ret);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return buff_ptr;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueUnmapMemObject(
			cl_command_queue command_queue,
			cl_mem memobj,
			void * mapped_ptr,
			cl_uint num_events_in_wait_list,
			const cl_event * event_wait_list,
			cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueUnmapMemObject()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueUnmapMemObject(command_queue,
						memobj,
						mapped_ptr,
						num_events_in_wait_list,
						event_wait_list,
						event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueUnmapMemObject()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueNDRangeKernel(
		       cl_command_queue command_queue,
		       cl_kernel kernel,
		       cl_uint work_dim,
		       const size_t * global_work_offset,
		       const size_t * global_work_size,
		       const size_t * local_work_size,
		       cl_uint num_events_in_wait_list,
		       const cl_event * event_wait_list,
		       cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueNDRangeKernel()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueNDRangeKernel(command_queue,
						kernel,
						work_dim,
						global_work_offset,
						global_work_size,
						local_work_size,
						num_events_in_wait_list,
						event_wait_list,
						event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueNDRangeKernel()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueTask(
	      cl_command_queue command_queue,
	      cl_kernel kernel,
	      cl_uint num_events_in_wait_list,
	      const cl_event * event_wait_list,
	      cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueTask()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueTask(command_queue, kernel, num_events_in_wait_list,
				event_wait_list, event);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueTask()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}

CL_API_ENTRY cl_int CL_API_CALL
clEnqueueNativeKernel(
		      cl_command_queue command_queue,
		      void (CL_CALLBACK * user_func)(void *),
		      void * args,
		      size_t cb_args,
		      cl_uint num_mem_objects,
		      const cl_mem * mem_list,
		      const void ** args_mem_loc,
		      cl_uint num_events_in_wait_list,
		      const cl_event * event_wait_list,
		      cl_event * event)
CL_API_SUFFIX__VERSION_1_0
{
	cl_int status;
	DB2(logger->Debug("Calling clEnqueueNativeKernel()..."));
	EVENT_RC_CONTROL(event);
	status = rtlib_ocl.enqueueNativeKernel(command_queue,
					(*user_func),
					args,
					cb_args,
					num_mem_objects,
					mem_list,
					args_mem_loc,
					num_events_in_wait_list,
					event_wait_list,
					event);
	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in clEnqueueNativeKernel()", status);
	}

	rtlib_ocl_coll_event(command_queue, event, __builtin_return_address(0));
	return status;
}


// Initialize OpenCL wrappers

void rtlib_ocl_init()
{
	// Get a Logger module
	logger = bu::Logger::GetLogger(BBQUE_LOG_MODULE);
	assert(logger);

	// Native OpenCL calls
	logger->Info("OpenCL runtime: %s", TARGET_OPENCL_PATH_LIB);
	void * handle = dlopen(TARGET_OPENCL_PATH_LIB, RTLD_LOCAL | RTLD_LAZY);
	*(void **) (&rtlib_ocl.getPlatformIDs)			= dlsym(handle, "clGetPlatformIDs");
	*(void **) (&rtlib_ocl.getPlatformInfo) 		= dlsym(handle, "clGetPlatformInfo");
	*(void **) (&rtlib_ocl.getDeviceIDs) 			= dlsym(handle, "clGetDeviceIDs");
	*(void **) (&rtlib_ocl.getDeviceInfo) 			= dlsym(handle, "clGetDeviceInfo");
	*(void **) (&rtlib_ocl.createContext) 			= dlsym(handle, "clCreateContext");
	*(void **) (&rtlib_ocl.createContextFromType) 		= dlsym(handle, "clCreateContextFromType");
	*(void **) (&rtlib_ocl.retainContext) 			= dlsym(handle, "clRetainContext");
	*(void **) (&rtlib_ocl.releaseContext) 			= dlsym(handle, "clReleaseContext");
	*(void **) (&rtlib_ocl.getContextInfo) 			= dlsym(handle, "clGetContextInfo");
	*(void **) (&rtlib_ocl.createCommandQueue) 		= dlsym(handle, "clCreateCommandQueue");
	*(void **) (&rtlib_ocl.retainCommandQueue) 		= dlsym(handle, "clRetainCommandQueue");
	*(void **) (&rtlib_ocl.releaseCommandQueue) 		= dlsym(handle, "clReleaseCommandQueue");
	*(void **) (&rtlib_ocl.getCommandQueueInfo) 		= dlsym(handle, "clGetCommandQueueInfo");
	*(void **) (&rtlib_ocl.createBuffer) 			= dlsym(handle, "clCreateBuffer");
	*(void **) (&rtlib_ocl.createSubBuffer) 		= dlsym(handle, "clCreateSubBuffer");
	*(void **) (&rtlib_ocl.retainMemObject) 		= dlsym(handle, "clRetainMemObject");
	*(void **) (&rtlib_ocl.releaseMemObject) 		= dlsym(handle, "clReleaseMemObject");
	*(void **) (&rtlib_ocl.getSupportedImageFormats) 	= dlsym(handle, "clGetSupportedImageFormats");
	*(void **) (&rtlib_ocl.getMemObjectInfo) 		= dlsym(handle, "clGetMemObjectInfo");
	*(void **) (&rtlib_ocl.getImageInfo) 			= dlsym(handle, "clGetImageInfo");
	*(void **) (&rtlib_ocl.setMemObjectDestructorCallback) 	= dlsym(handle, "clSetMemObjectDestructorCallback");
	*(void **) (&rtlib_ocl.createSampler) 			= dlsym(handle, "clCreateSampler");
	*(void **) (&rtlib_ocl.retainSampler) 			= dlsym(handle, "clRetainSampler");
	*(void **) (&rtlib_ocl.releaseSampler) 			= dlsym(handle, "clReleaseSampler");
	*(void **) (&rtlib_ocl.getSamplerInfo) 			= dlsym(handle, "clGetSamplerInfo");
	*(void **) (&rtlib_ocl.createProgramWithSource) 	= dlsym(handle, "clCreateProgramWithSource");
	*(void **) (&rtlib_ocl.createProgramWithBinary) 	= dlsym(handle, "clCreateProgramWithBinary");
	*(void **) (&rtlib_ocl.retainProgram) 			= dlsym(handle, "clRetainProgram");
	*(void **) (&rtlib_ocl.releaseProgram) 			= dlsym(handle, "clReleaseProgram");
	*(void **) (&rtlib_ocl.buildProgram) 			= dlsym(handle, "clBuildProgram");
	*(void **) (&rtlib_ocl.getProgramInfo) 			= dlsym(handle, "clGetProgramInfo");
	*(void **) (&rtlib_ocl.getProgramBuildInfo) 		= dlsym(handle, "clGetProgramBuildInfo");
	*(void **) (&rtlib_ocl.createKernel) 			= dlsym(handle, "clCreateKernel");
	*(void **) (&rtlib_ocl.createKernelsInProgram) 		= dlsym(handle, "clCreateKernelsInProgram");
	*(void **) (&rtlib_ocl.retainKernel) 			= dlsym(handle, "clRetainKernel");
	*(void **) (&rtlib_ocl.releaseKernel) 			= dlsym(handle, "clReleaseKernel");
	*(void **) (&rtlib_ocl.setKernelArg) 			= dlsym(handle, "clSetKernelArg");
	*(void **) (&rtlib_ocl.getKernelInfo) 			= dlsym(handle, "clGetKernelInfo");
	*(void **) (&rtlib_ocl.getKernelWorkGroupInfo) 		= dlsym(handle, "clGetKernelWorkGroupInfo");
	*(void **) (&rtlib_ocl.waitForEvents) 			= dlsym(handle, "clWaitForEvents");
	*(void **) (&rtlib_ocl.getEventInfo) 			= dlsym(handle, "clGetEventInfo");
	*(void **) (&rtlib_ocl.createUserEvent) 		= dlsym(handle, "clCreateUserEvent");
	*(void **) (&rtlib_ocl.retainEvent) 			= dlsym(handle, "clRetainEvent");
	*(void **) (&rtlib_ocl.releaseEvent) 			= dlsym(handle, "clReleaseEvent");
	*(void **) (&rtlib_ocl.setUserEventStatus) 		= dlsym(handle, "clSetUserEventStatus");
	*(void **) (&rtlib_ocl.setEventCallback) 		= dlsym(handle, "clSetEventCallback");
	*(void **) (&rtlib_ocl.getEventProfilingInfo) 		= dlsym(handle, "clGetEventProfilingInfo");
	*(void **) (&rtlib_ocl.enqueueReadBuffer) 		= dlsym(handle, "clEnqueueReadBuffer");
	*(void **) (&rtlib_ocl.enqueueReadBufferRect) 		= dlsym(handle, "clEnqueueReadBufferRect");
	*(void **) (&rtlib_ocl.enqueueWriteBuffer) 		= dlsym(handle, "clEnqueueWriteBuffer");
	*(void **) (&rtlib_ocl.enqueueWriteBufferRect) 		= dlsym(handle, "clEnqueueWriteBufferRect");
	*(void **) (&rtlib_ocl.enqueueCopyBuffer) 		= dlsym(handle, "clEnqueueCopyBuffer");
	*(void **) (&rtlib_ocl.enqueueCopyBufferRect) 		= dlsym(handle, "clEnqueueCopyBufferRect");
	*(void **) (&rtlib_ocl.enqueueReadImage) 		= dlsym(handle, "clEnqueueReadImage");
	*(void **) (&rtlib_ocl.enqueueWriteImage) 		= dlsym(handle, "clEnqueueWriteImage");
	*(void **) (&rtlib_ocl.enqueueCopyImage) 		= dlsym(handle, "clEnqueueCopyImage");
	*(void **) (&rtlib_ocl.enqueueCopyImageToBuffer) 	= dlsym(handle, "clEnqueueCopyImageToBuffer");
	*(void **) (&rtlib_ocl.enqueueCopyBufferToImage) 	= dlsym(handle, "clEnqueueCopyBufferToImage");
	*(void **) (&rtlib_ocl.enqueueMapBuffer) 		= dlsym(handle, "clEnqueueMapBuffer");
	*(void **) (&rtlib_ocl.enqueueMapImage) 		= dlsym(handle, "clEnqueueMapImage");
	*(void **) (&rtlib_ocl.enqueueUnmapMemObject) 		= dlsym(handle, "clEnqueueUnmapMemObject");
	*(void **) (&rtlib_ocl.enqueueNDRangeKernel) 		= dlsym(handle, "clEnqueueNDRangeKernel");
	*(void **) (&rtlib_ocl.enqueueTask) 			= dlsym(handle, "clEnqueueTask");
	*(void **) (&rtlib_ocl.enqueueNativeKernel) 		= dlsym(handle, "clEnqueueNativeKernel");
	*(void **) (&rtlib_ocl.flush) 				= dlsym(handle, "clFlush");
	*(void **) (&rtlib_ocl.finish) 				= dlsym(handle, "clFinish");
#ifdef CL_API_SUFFIX__VERSION_1_2
	*(void **) (&rtlib_ocl.createSubDevices) 		= dlsym(handle, "clCreateSubDevices");
	*(void **) (&rtlib_ocl.retainDevice) 			= dlsym(handle, "clRetainDevice");
	*(void **) (&rtlib_ocl.releaseDevice) 			= dlsym(handle, "clReleaseDevice");
	*(void **) (&rtlib_ocl.getKernelArgInfo) 		= dlsym(handle, "clGetKernelArgInfo");
	*(void **) (&rtlib_ocl.compileProgram) 			= dlsym(handle, "clCompileProgram");
	*(void **) (&rtlib_ocl.linkProgram) 			= dlsym(handle, "clLinkProgram");
	*(void **) (&rtlib_ocl.unloadPlatformCompiler) 		= dlsym(handle, "clUnloadPlatformCompiler");
	*(void **) (&rtlib_ocl.createProgramWithBuiltInKernels) = dlsym(handle, "clCreateProgramWithBuiltInKernels");
	*(void **) (&rtlib_ocl.createImage) 			= dlsym(handle, "clCreateImage");
	*(void **) (&rtlib_ocl.enqueueFillBuffer) 		= dlsym(handle, "clEnqueueFillBuffer");
	*(void **) (&rtlib_ocl.enqueueFillImage) 		= dlsym(handle, "clEnqueueFillImage");
	*(void **) (&rtlib_ocl.enqueueMigrateMemObjects) 	= dlsym(handle, "clEnqueueMigrateMemObjects");
	*(void **) (&rtlib_ocl.enqueueMarkerWithWaitList) 	= dlsym(handle, "clEnqueueMarkerWithWaitList");
	*(void **) (&rtlib_ocl.enqueueBarrierWithWaitList) 	= dlsym(handle, "clEnqueueBarrierWithWaitList");
#endif
	// Command labels (for profiling output)
	ocl_cmd_str.emplace(CL_COMMAND_READ_BUFFER, "clEnqueueReadBuffer");
	ocl_cmd_str.emplace(CL_COMMAND_READ_BUFFER_RECT, "clEnqueueReadBufferRect");
	ocl_cmd_str.emplace(CL_COMMAND_WRITE_BUFFER, "clEnqueueWriteBuffer");
	ocl_cmd_str.emplace(CL_COMMAND_WRITE_BUFFER_RECT, "clEnqueueWriteBufferRect");
	ocl_cmd_str.emplace(CL_COMMAND_COPY_BUFFER, "clEnqueueCopyBuffer");
	ocl_cmd_str.emplace(CL_COMMAND_COPY_BUFFER_RECT, "clEnqueueCopyBufferRect");
	ocl_cmd_str.emplace(CL_COMMAND_READ_IMAGE, "clEnqueueReadImage");
	ocl_cmd_str.emplace(CL_COMMAND_WRITE_IMAGE, "clEnqueueWriteImage");
	ocl_cmd_str.emplace(CL_COMMAND_COPY_IMAGE, "clEnqueueCopyImage");
	ocl_cmd_str.emplace(CL_COMMAND_COPY_IMAGE_TO_BUFFER, "clEnqueueCopyImageToBuffer");
	ocl_cmd_str.emplace(CL_COMMAND_COPY_BUFFER_TO_IMAGE, "clEnqueueCopyBufferToImage");
	ocl_cmd_str.emplace(CL_COMMAND_MAP_BUFFER, "clEnqueueMapBuffer");
	ocl_cmd_str.emplace(CL_COMMAND_MAP_IMAGE, "clEnqueueMapImage");
	ocl_cmd_str.emplace(CL_COMMAND_UNMAP_MEM_OBJECT, "clEnqueueUnmapMemObject");
	ocl_cmd_str.emplace(CL_COMMAND_NDRANGE_KERNEL, "clEnqueueNDRangeKernel");
	ocl_cmd_str.emplace(CL_COMMAND_TASK, "clEnqueueTask");
	ocl_cmd_str.emplace(CL_COMMAND_NATIVE_KERNEL, "clEnqueueNativeKernel");

	// Initialize the list of all the devices
	rtlib_init_devices();
}

void rtlib_init_devices()
{
	cl_int status;
	char platform_name[128];
	cl_device_type dev_type = CL_DEVICE_TYPE_ALL;

	// By default, at the beginning, we haven't any device assigned
	rtlib_ocl_set_device(R_ID_ANY, R_ID_ANY, RTLIB_EXC_GWM_BLOCKED);

	// Get platform
	status = rtlib_ocl.getPlatformIDs(0, NULL, &rtlib_ocl.num_platforms);
	if (status != CL_SUCCESS) {
		logger->Error("OCL: Unable to find OpenCL platforms");
		return;
	}

	rtlib_ocl.platforms = (cl_platform_id *)
		malloc(sizeof (cl_platform_id) * rtlib_ocl.num_platforms);
	status = rtlib_ocl.getPlatformIDs(rtlib_ocl.num_platforms, rtlib_ocl.platforms, NULL);
	logger->Info("OCL: nr. of available platform(s): %d", rtlib_ocl.num_platforms);

	// Devices for each platform
	rtlib_ocl.devices = (cl_device_id **) malloc(sizeof (cl_device_id *) * rtlib_ocl.num_platforms);

	for (unsigned int plat_id = 0; plat_id < rtlib_ocl.num_platforms; ++plat_id) {
		cl_platform_id platform = rtlib_ocl.platforms[plat_id];
		status = rtlib_ocl.getPlatformInfo(
						platform,
						CL_PLATFORM_NAME,
						sizeof (platform_name),
						platform_name, NULL);
		logger->Notice("OCL: Platform %d = %s", plat_id, platform_name);

		// Get devices
		status = rtlib_ocl.getDeviceIDs(platform, dev_type, 0, NULL, &rtlib_ocl.num_devices);
		if (status != CL_SUCCESS) {
			logger->Error("OCL: Error [%d] in getting number of OpenCL devices", status);
			return;
		}

		rtlib_ocl.devices[plat_id] = (cl_device_id *)
			malloc(sizeof (cl_device_id) * rtlib_ocl.num_devices);
		status  = rtlib_ocl.getDeviceIDs(
						platform,
						dev_type,
						rtlib_ocl.num_devices,
						rtlib_ocl.devices[plat_id],
						NULL);
		if (status != CL_SUCCESS) {
			logger->Error("OCL: Error [%d] in getting OpenCL deviced list", status);
			return;
		}

		logger->Debug("OCL: OpenCL devices found: %u [descriptors size: %lu]",
			rtlib_ocl.num_devices, sizeof (cl_device_id));
		logger->Debug("OCL: Devices descriptors @%p", rtlib_ocl.devices);
		for (uint8_t dev_id = 0; dev_id < rtlib_ocl.num_devices; ++dev_id)
			logger->Debug("     Device #%02d @%p", dev_id, rtlib_ocl.devices[dev_id]);




	}
}

void rtlib_ocl_set_device(uint32_t platform_id, uint8_t device_id, RTLIB_ExitCode_t status)
{
	rtlib_ocl.platform_id = platform_id;
	rtlib_ocl.device_id   = device_id;
	rtlib_ocl.status      = status;
}

void rtlib_ocl_coll_event(cl_command_queue cmd_queue, cl_event * event,
			  void * addr)
{
	clRetainEvent(*event);

	// Collect events per command queue
	auto it = ocl_queues_prof.find(cmd_queue);
	if (it == ocl_queues_prof.end())
		ocl_queues_prof.emplace(cmd_queue, std::make_shared<RTLIB_OCL_QueueProf>());

	ocl_queues_prof[cmd_queue]->events.emplace(addr, *event);
}

void rtlib_ocl_prof_clean()
{
	ocl_queues_prof.clear();
}

void rtlib_ocl_flush_events()
{
	cl_command_queue cq;

	for (auto & it_cq : ocl_queues_prof) {
		QueueProfPtr_t stPtr = it_cq.second;
		cq = it_cq.first;
		stPtr->events.clear();
	}

	ocl_queues_prof.erase(cq);
}

void rtlib_ocl_prof_run(
			int8_t awm_id,
			OclEventsStatsMap_t & awm_ocl_events,
			int prof_level)
{
	cl_command_type cmd_type = 0;
	cl_int status;

	for (auto & it_cq : ocl_queues_prof) {
		cl_command_queue cq = it_cq.first;
		clFinish(cq);

		// Resume previously stored statistics
		QueueProfPtr_t stPtr = it_cq.second;
		if (awm_ocl_events[cq] != nullptr) {
			stPtr->cmd_prof  = awm_ocl_events[cq]->cmd_prof;
			stPtr->addr_prof = awm_ocl_events[cq]->addr_prof;
		}

		// Extract event profiling timings
		for (auto & it_ev : stPtr->events) {
			cl_event & ev(it_ev.second);
			status = clWaitForEvents(1, &ev);
			if (status != CL_SUCCESS)
				logger->Error("OCL: Error [%d] in clWaitForEvents", status);

			acc_command_event_info(
					stPtr, ev, cmd_type, it_ev.first, awm_id, prof_level);
		}

		rtlib_ocl_prof_save(cq, awm_ocl_events);
	}

	rtlib_ocl_prof_clean();
}

void rtlib_ocl_prof_save(
			 cl_command_queue cmd_queue,
			 OclEventsStatsMap_t & awm_ocl_events)
{
	auto it_cq = awm_ocl_events.find(cmd_queue);
	if (it_cq == awm_ocl_events.end()) {
		QueueProfPtr_t prof = ocl_queues_prof[cmd_queue];
		awm_ocl_events.emplace(cmd_queue, prof);
	}
	else {
		awm_ocl_events[cmd_queue] = ocl_queues_prof[cmd_queue];
	}
}

void acc_command_stats(
		       QueueProfPtr_t stPtr,
		       cl_command_type cmd_type,
		       double queued_time,
		       double submit_time,
		       double exec_time)
{
	AccArray_t accs;
	auto it_ct = stPtr->cmd_prof.find(cmd_type);

	if (it_ct == stPtr->cmd_prof.end()) {
		accs[CL_CMD_QUEUED_TIME](queued_time);
		accs[CL_CMD_SUBMIT_TIME](submit_time);
		accs[CL_CMD_EXEC_TIME](exec_time);
		stPtr->cmd_prof.emplace(cmd_type, accs);
		return;
	}

	it_ct->second[CL_CMD_QUEUED_TIME](queued_time);
	it_ct->second[CL_CMD_SUBMIT_TIME](submit_time);
	it_ct->second[CL_CMD_EXEC_TIME](exec_time);
}

void acc_address_stats(
		       QueueProfPtr_t stPtr,
		       void * addr,
		       double queued_time,
		       double submit_time,
		       double exec_time)
{
	AccArray_t accs;
	auto it_ca = stPtr->addr_prof.find(addr);

	if (it_ca == stPtr->addr_prof.end()) {
		accs[CL_CMD_QUEUED_TIME](queued_time);
		accs[CL_CMD_SUBMIT_TIME](submit_time);
		accs[CL_CMD_EXEC_TIME](exec_time);
		stPtr->addr_prof.emplace(addr, accs);
		return;
	}

	it_ca->second[CL_CMD_QUEUED_TIME](queued_time);
	it_ca->second[CL_CMD_SUBMIT_TIME](submit_time);
	it_ca->second[CL_CMD_EXEC_TIME](exec_time);
}

void dump_command_prof_info(
			    int8_t awm_id,
			    cl_command_type cmd_type,
			    double queued_time,
			    double submit_time,
			    double exec_time,
			    void * addr)
{
	FILE * dump_file;
	char buffer [100];
	snprintf(buffer, 100, OCL_PROF_FMT, OCL_PROF_OUTDIR,
		rtlib_services.Utils.GetUniqueID_String(), awm_id,
		ocl_cmd_str[cmd_type].c_str());
	dump_file = fopen(buffer, "a");

	if (! dump_file) {
		logger->Error("OCL: Error %s not open", buffer);
		return;
	}

	fprintf(dump_file, "%f %f %f %f %p\n",
		bbque_tmr.getElapsedTimeMs(),
		queued_time * 1e-06,
		submit_time * 1e-06,
		exec_time * 1e-06,
		addr);
	fclose(dump_file);
}

void acc_command_event_info(
			    QueueProfPtr_t stPtr,
			    cl_event event,
			    cl_command_type & cmd_type,
			    void * addr,
			    int8_t awm_id,
			    int prof_level)
{
	size_t return_bytes;
	cl_int status;
	cl_ulong ev_queued_time = (cl_ulong) 0;
	cl_ulong ev_submit_time = (cl_ulong) 0;
	cl_ulong ev_start_time  = (cl_ulong) 0;
	cl_ulong ev_end_time    = (cl_ulong) 0;
	// Extract event times
	clGetEventInfo(event, CL_EVENT_COMMAND_TYPE, sizeof (cl_command_type),
		&cmd_type, NULL);
	status = clGetEventProfilingInfo(
					event,
					CL_PROFILING_COMMAND_QUEUED,
					sizeof (cl_ulong),
					&ev_queued_time,
					&return_bytes);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in queued event profiling", status);
		return;
	}

	status = clGetEventProfilingInfo(
					event,
					CL_PROFILING_COMMAND_SUBMIT,
					sizeof (cl_ulong),
					&ev_submit_time,
					&return_bytes);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in submit event profiling", status);
		return;
	}

	status = clGetEventProfilingInfo(
					event,
					CL_PROFILING_COMMAND_START,
					sizeof (cl_ulong),
					&ev_start_time,
					&return_bytes);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in start event profiling", status);
		return;
	}

	status = clGetEventProfilingInfo(
					event,
					CL_PROFILING_COMMAND_END,
					sizeof (cl_ulong),
					&ev_end_time,
					&return_bytes);

	if (status != CL_SUCCESS) {
		logger->Error("OCL: Error [%d] in end event profiling", status);
		return;
	}

	// Accumulate event times for this command
	double queued_time = (double) (ev_submit_time - ev_queued_time);
	double submit_time = (double) (ev_start_time  - ev_submit_time);
	double exec_time   = (double) (ev_end_time    - ev_start_time);
	acc_command_stats(stPtr, cmd_type, queued_time, submit_time, exec_time);

	// Collects stats for command instances
	if (prof_level > 0) {
		acc_address_stats(stPtr, addr, queued_time, submit_time, exec_time);
		ocl_addr_cmd[addr] = cmd_type;
	}
	else
		addr = 0;

	// File dump
	dump_command_prof_info(
			awm_id, cmd_type, queued_time, submit_time, exec_time, addr);
}

cl_command_type rtlib_ocl_get_command_type(void * addr)
{
	cl_command_type cmd_type = CL_COMMAND_USER;

	auto it_ev = ocl_addr_cmd.find(addr);
	if (it_ev == ocl_addr_cmd.end()) {
		logger->Warn("OCL Unexpected missing command instance...");
		return cmd_type;
	}

	return it_ev->second;
}

#ifdef  __cplusplus
}
#endif
