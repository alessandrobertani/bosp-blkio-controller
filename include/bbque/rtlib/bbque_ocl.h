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

#ifndef BBQUE_OCL_H_
#define BBQUE_OCL_H_

#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <CL/cl.h>

#include "bbque/rtlib/bbque_rpc.h"

#define EVENT_RC_CONTROL(ev) \
	cl_event local_event; \
	if (ev == NULL) ev = &local_event;

#define OCL_PROF_OUTDIR BBQUE_PATH_TEMP
#define OCL_PROF_FMT    "%s/profOCL-%s-AWM%d-%s.dat"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RTLIB_OpenCL RTLIB_OpenCL_t;

typedef cl_int (*getPlatformIDs_t)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (*getPlatformInfo_t)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);
typedef cl_int (*getDeviceIDs_t)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (*getDeviceInfo_t)(cl_device_id, cl_device_info, size_t, void *, size_t *);
typedef cl_context (*createContext_t)(const cl_context_properties *, cl_uint, const cl_device_id *,
                                      void (CL_CALLBACK *)(const char *, const void *, size_t, void *), void *, cl_int *);
typedef cl_context (*createContextFromType_t)(const cl_context_properties *, cl_device_type,
                                              void (CL_CALLBACK *)(const char *, const void *, size_t, void *),
                                              void *, cl_int *);
typedef cl_int (*retainContext_t)(cl_context);
typedef cl_int (*releaseContext_t)(cl_context);
typedef cl_int (*getContextInfo_t)(cl_context, cl_context_info, size_t, void *, size_t *);
typedef cl_command_queue (*createCommandQueue_t)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
typedef cl_int (*retainCommandQueue_t)(cl_command_queue);
typedef cl_int (*releaseCommandQueue_t)(cl_command_queue);
typedef cl_int (*getCommandQueueInfo_t)(cl_command_queue, cl_command_queue_info, size_t, void *, size_t *);
typedef cl_mem (*createBuffer_t)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
typedef cl_mem (*createSubBuffer_t)(cl_mem, cl_mem_flags, cl_buffer_create_type, const void *, cl_int *);
typedef cl_int (*retainMemObject_t)(cl_mem);
typedef cl_int (*releaseMemObject_t)(cl_mem);
typedef cl_int (*getSupportedImageFormats_t)(cl_context, cl_mem_flags,
		cl_mem_object_type, cl_uint, cl_image_format *, cl_uint *);
typedef cl_int (*getMemObjectInfo_t)(cl_mem, cl_mem_info, size_t, void *, size_t *);
typedef cl_int (*getImageInfo_t)(cl_mem, cl_image_info, size_t, void *, size_t *);
typedef cl_int (*setMemObjectDestructorCallback_t)(cl_mem, void (CL_CALLBACK *)(cl_mem, void *), void *);
typedef cl_sampler (*createSampler_t)(cl_context, cl_bool, cl_addressing_mode, cl_filter_mode, cl_int *);
typedef cl_int (*retainSampler_t)(cl_sampler);
typedef cl_int (*releaseSampler_t)(cl_sampler);
typedef cl_int (*getSamplerInfo_t)(cl_sampler, cl_sampler_info, size_t, void *,size_t *);
typedef cl_program (*createProgramWithSource_t)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
typedef cl_program (*createProgramWithBinary_t)(cl_context, cl_uint, const cl_device_id *, const size_t *,
                                                const unsigned char **, cl_int *, cl_int *);
typedef cl_int (*retainProgram_t)(cl_program);
typedef cl_int (*releaseProgram_t)(cl_program);
typedef cl_int (*buildProgram_t)(cl_program, cl_uint, const cl_device_id *, const char *,
                                 void (CL_CALLBACK *)(cl_program, void *), void *);
typedef cl_int (*getProgramInfo_t)(cl_program, cl_program_info, size_t, void *, size_t *);
typedef cl_int (*getProgramBuildInfo_t)(cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);
typedef cl_kernel (*createKernel_t)(cl_program, const char *, cl_int *);
typedef cl_int (*createKernelsInProgram_t)(cl_program, cl_uint, cl_kernel *, cl_uint *);
typedef cl_int (*retainKernel_t)(cl_kernel);
typedef cl_int (*releaseKernel_t)(cl_kernel);
typedef cl_int (*setKernelArg_t)(cl_kernel, cl_uint, size_t, const void *);
typedef cl_int (*getKernelInfo_t)(cl_kernel, cl_kernel_info, size_t, void *, size_t *);
typedef cl_int (*getKernelWorkGroupInfo_t)(cl_kernel, cl_device_id,
		                           cl_kernel_work_group_info, size_t, void *, size_t *);
typedef cl_int (*waitForEvents_t)(cl_uint, const cl_event *);
typedef cl_int (*getEventInfo_t)(cl_event, cl_event_info, size_t, void *, size_t *);
typedef cl_event (*createUserEvent_t)(cl_context, cl_int *);
typedef cl_int (*retainEvent_t)(cl_event);
typedef cl_int (*releaseEvent_t)(cl_event);
typedef cl_int (*setUserEventStatus_t)(cl_event, cl_int);
typedef cl_int (*setEventCallback_t)(cl_event, cl_int, void (CL_CALLBACK *)(cl_event, cl_int, void *), void *);
typedef cl_int (*getEventProfilingInfo_t)(cl_event, cl_profiling_info, size_t, void *, size_t *);
typedef cl_int (*flush_t)(cl_command_queue);
typedef cl_int (*finish_t)(cl_command_queue);
typedef cl_int (*enqueueReadBuffer_t)(cl_command_queue, cl_mem, cl_bool, size_t,
				      size_t, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueReadBufferRect_t)(cl_command_queue, cl_mem, cl_bool,
		                          const size_t *, const size_t *, const size_t *,
                                          size_t, size_t, size_t, size_t,
                                          void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueWriteBuffer_t)(cl_command_queue, cl_mem, cl_bool,
				       size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueWriteBufferRect_t)(cl_command_queue, cl_mem, cl_bool,
		                           const size_t *, const size_t *, const size_t *, size_t, size_t, size_t, size_t,
		                           const void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueCopyBuffer_t)(cl_command_queue, cl_mem, cl_mem, size_t,
				      size_t, size_t, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueCopyBufferRect_t)(cl_command_queue, cl_mem, cl_mem,
		                          const size_t *, const size_t *, const size_t *, size_t, size_t, size_t, size_t,
		                          cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueReadImage_t)(cl_command_queue, cl_mem, cl_bool,
				     const size_t *, const size_t *, size_t, size_t, void *, cl_uint,
				     const cl_event *, cl_event *);
typedef cl_int (*enqueueWriteImage_t)(cl_command_queue, cl_mem, cl_bool,
				      const size_t *, const size_t *, size_t, size_t, const void *, cl_uint,
				      const cl_event *, cl_event *);
typedef cl_int (*enqueueCopyImage_t)(cl_command_queue, cl_mem, cl_mem,
				     const size_t *, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueCopyImageToBuffer_t)(cl_command_queue, cl_mem, cl_mem,
		                             const size_t *, const size_t *, size_t, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueCopyBufferToImage_t)(cl_command_queue, cl_mem, cl_mem,
		                             size_t, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);
typedef void * (*enqueueMapBuffer_t)(cl_command_queue, cl_mem, cl_bool,
				     cl_map_flags, size_t, size_t, cl_uint, const cl_event *, cl_event *, cl_int *);
typedef void * (*enqueueMapImage_t)(cl_command_queue, cl_mem, cl_bool,
				    cl_map_flags, const size_t *, const size_t *, size_t *, size_t *, cl_uint,
				    const cl_event *, cl_event *, cl_int *);
typedef cl_int (*enqueueUnmapMemObject_t)(cl_command_queue, cl_mem, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueNDRangeKernel_t)(cl_command_queue, cl_kernel, cl_uint,	const size_t *, const size_t *,
                                         const size_t *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueTask_t)(cl_command_queue, cl_kernel, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueNativeKernel_t)(cl_command_queue,
                                        void (CL_CALLBACK *)(void *), void *, size_t, cl_uint, const cl_mem *,
					const void **, cl_uint, const cl_event *, cl_event *);

#ifdef CL_API_SUFFIX__VERSION_1_2
typedef cl_int (*createSubDevices_t)(cl_device_id, const cl_device_partition_property *, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (*retainDevice_t)(cl_device_id);
typedef cl_int (*releaseDevice_t)(cl_device_id);
typedef cl_mem (*createImage_t)(cl_context, cl_mem_flags, const cl_image_format *, const cl_image_desc *, void *, cl_int *);
typedef cl_int (*getKernelArgInfo_t)(cl_kernel, cl_uint, cl_kernel_arg_info,  size_t, void *, size_t *);
typedef cl_int (*compileProgram_t)(cl_program, cl_uint, const cl_device_id *, const char *, cl_uint, const cl_program *, const char **,
				   void (CL_CALLBACK *)(cl_program, void *), void *);
typedef cl_program (*linkProgram_t)(cl_context, cl_uint, const cl_device_id *,	const char *, cl_uint, const cl_program *,
                                   void (CL_CALLBACK *)(cl_program, void *), void *, cl_int *);
typedef cl_int (*unloadPlatformCompiler_t)(cl_platform_id);
typedef cl_program (*createProgramWithBuiltInKernels_t)(cl_context, cl_uint, const cl_device_id *, const char *, cl_int *);
typedef cl_int (*enqueueFillBuffer_t)(cl_command_queue, cl_mem, const void *, size_t, size_t, size_t, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueFillImage_t)(cl_command_queue, cl_mem, const void *, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueMigrateMemObjects_t)(cl_command_queue, cl_uint, const cl_mem *, cl_mem_migration_flags, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*enqueueMarkerWithWaitList_t)(cl_command_queue, cl_uint,const cl_event *, cl_event *);
typedef cl_int (*enqueueBarrierWithWaitList_t)(cl_command_queue, cl_uint, const cl_event *, cl_event *);
#endif


struct RTLIB_OpenCL {
	getPlatformIDs_t  getPlatformIDs;
	getPlatformInfo_t getPlatformInfo;
	getDeviceIDs_t     getDeviceIDs;
	getDeviceInfo_t    getDeviceInfo;
	createContext_t         createContext;
	createContextFromType_t createContextFromType;
	retainContext_t         retainContext;
	releaseContext_t        releaseContext;
	getContextInfo_t        getContextInfo;
	createCommandQueue_t  createCommandQueue;
	retainCommandQueue_t  retainCommandQueue;
	releaseCommandQueue_t releaseCommandQueue;
	getCommandQueueInfo_t getCommandQueueInfo;
	createBuffer_t                   createBuffer;
	createSubBuffer_t                createSubBuffer;
	retainMemObject_t                retainMemObject;
	releaseMemObject_t               releaseMemObject;
	getSupportedImageFormats_t       getSupportedImageFormats;
	getMemObjectInfo_t               getMemObjectInfo;
	getImageInfo_t                   getImageInfo;
	setMemObjectDestructorCallback_t setMemObjectDestructorCallback;
	createSampler_t  createSampler;
	retainSampler_t  retainSampler;
	releaseSampler_t releaseSampler;
	getSamplerInfo_t getSamplerInfo;
	createProgramWithSource_t         createProgramWithSource;
	createProgramWithBinary_t         createProgramWithBinary;
	retainProgram_t                   retainProgram;
	releaseProgram_t                  releaseProgram;
	buildProgram_t                    buildProgram;
	getProgramInfo_t                  getProgramInfo;
	getProgramBuildInfo_t             getProgramBuildInfo;
	createKernel_t           createKernel;
	createKernelsInProgram_t createKernelsInProgram;
	retainKernel_t           retainKernel;
	releaseKernel_t          releaseKernel;
	setKernelArg_t           setKernelArg;
	getKernelInfo_t          getKernelInfo;
	getKernelWorkGroupInfo_t getKernelWorkGroupInfo;
	waitForEvents_t      waitForEvents;
	getEventInfo_t       getEventInfo;
	createUserEvent_t    createUserEvent;
	retainEvent_t        retainEvent;
	releaseEvent_t       releaseEvent;
	setUserEventStatus_t setUserEventStatus;
	setEventCallback_t   setEventCallback;
	getEventProfilingInfo_t getEventProfilingInfo;
	flush_t  flush;
	finish_t finish;
	enqueueReadBuffer_t          enqueueReadBuffer;
	enqueueReadBufferRect_t      enqueueReadBufferRect;
	enqueueWriteBuffer_t         enqueueWriteBuffer;
	enqueueWriteBufferRect_t     enqueueWriteBufferRect;
	enqueueCopyBuffer_t          enqueueCopyBuffer;
	enqueueCopyBufferRect_t      enqueueCopyBufferRect;
	enqueueReadImage_t           enqueueReadImage;
	enqueueWriteImage_t          enqueueWriteImage;
	enqueueCopyImage_t           enqueueCopyImage;
	enqueueCopyImageToBuffer_t   enqueueCopyImageToBuffer;
	enqueueCopyBufferToImage_t   enqueueCopyBufferToImage;
	enqueueMapBuffer_t           enqueueMapBuffer;
	enqueueMapImage_t            enqueueMapImage;
	enqueueUnmapMemObject_t      enqueueUnmapMemObject;
	enqueueNDRangeKernel_t       enqueueNDRangeKernel;
	enqueueTask_t                enqueueTask;
	enqueueNativeKernel_t        enqueueNativeKernel;

#ifdef CL_API_SUFFIX__VERSION_1_2
	createSubDevices_t createSubDevices;
	retainDevice_t     retainDevice;
	releaseDevice_t    releaseDevice;
	getKernelArgInfo_t                getKernelArgInfo;
	compileProgram_t                  compileProgram;
	linkProgram_t                     linkProgram;
	unloadPlatformCompiler_t          unloadPlatformCompiler;
	createProgramWithBuiltInKernels_t createProgramWithBuiltInKernels;
	createImage_t                createImage;
	enqueueFillBuffer_t          enqueueFillBuffer;
	enqueueFillImage_t           enqueueFillImage;
	enqueueMigrateMemObjects_t   enqueueMigrateMemObjects;
	enqueueMarkerWithWaitList_t  enqueueMarkerWithWaitList;
	enqueueBarrierWithWaitList_t enqueueBarrierWithWaitList;
#endif

	cl_uint num_platforms;      // Number of OpenCL platforms on the system
	cl_uint num_devices;        // Number of OpenCL devices to consider
	cl_platform_id * platforms; // OpenCL platforms ids
	cl_device_id  ** devices;   // Allocatable OpenCL devices ids
	int32_t platform_id;        // Assigned OpenCL platform ID
	int8_t device_id;           // Assigned OpenCL device ID

	/** Track the execution status */
	RTLIB_ExitCode_t status;
};

using bbque::rtlib::BbqueRPC;

void acc_command_event_info(QueueProfPtr_t, cl_event, cl_command_type &, void *, int8_t, int);
void acc_command_stats(QueueProfPtr_t, cl_command_type, double, double, double);
void acc_address_stats(QueueProfPtr_t, void *, double, double, double);
void dump_command_prof_info(int8_t, cl_command_type, double, double, double, void *);

void rtlib_ocl_init();
void rtlib_init_devices();
void rtlib_ocl_set_device(uint32_t platform_id, uint8_t device_id, RTLIB_ExitCode_t status);
void rtlib_ocl_flush_events();
void rtlib_ocl_coll_event(cl_command_queue, cl_event *, void *);
void rtlib_ocl_prof_save(cl_command_queue, OclEventsStatsMap_t &);
void rtlib_ocl_prof_clean();
void rtlib_ocl_prof_run(int8_t, OclEventsStatsMap_t &, int);
cl_command_type rtlib_ocl_get_command_type(void *);

/******************************************************************************
 * OpenCL wrapper functions                                                   *
 ******************************************************************************/

/* Platform API */
CL_API_ENTRY cl_int CL_API_CALL
clGetPlatformIDs(
	cl_uint,
	cl_platform_id *,
	cl_uint *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetPlatformInfo(
	cl_platform_id,
	cl_platform_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Device APIs */
CL_API_ENTRY  cl_int CL_API_CALL
clGetDeviceIDs(
	cl_platform_id,
	cl_device_type,
	cl_uint,
	cl_device_id *,
	cl_uint *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetDeviceInfo(
	cl_device_id,
	cl_device_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Context APIs  */
CL_API_ENTRY  cl_context CL_API_CALL
clCreateContext(
	const cl_context_properties *,
	cl_uint,
	const cl_device_id *,
	void (CL_CALLBACK *)(const char *, const void *, size_t, void *),
	void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_context CL_API_CALL
clCreateContextFromType(
	const cl_context_properties *,
	cl_device_type,
	void (CL_CALLBACK *)(const char *, const void *, size_t, void *),
	void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainContext(cl_context) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseContext(cl_context) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetContextInfo(
	cl_context,
	cl_context_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Command Queue APIs */
CL_API_ENTRY  cl_command_queue CL_API_CALL
clCreateCommandQueue(
	cl_context,
	cl_device_id,
	cl_command_queue_properties,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainCommandQueue(cl_command_queue) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseCommandQueue(cl_command_queue) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetCommandQueueInfo(
	cl_command_queue,
	cl_command_queue_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Memory Object APIs */
CL_API_ENTRY  cl_mem CL_API_CALL
clCreateBuffer(
	cl_context,
	cl_mem_flags,
	size_t,
	void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_mem CL_API_CALL
clCreateSubBuffer(
	cl_mem,
	cl_mem_flags,
	cl_buffer_create_type,
	const void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_1;


CL_API_ENTRY  cl_int CL_API_CALL
clRetainMemObject(cl_mem) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseMemObject(cl_mem) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetSupportedImageFormats(
	cl_context,
	cl_mem_flags,
	cl_mem_object_type,
	cl_uint,
	cl_image_format *,
	cl_uint *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetMemObjectInfo(
	cl_mem,
	cl_mem_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetImageInfo(
	cl_mem,
	cl_image_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clSetMemObjectDestructorCallback(
	cl_mem,
	void (CL_CALLBACK *)(cl_mem, void *), void *)
CL_API_SUFFIX__VERSION_1_1;

/* Sampler APIs */
CL_API_ENTRY  cl_sampler CL_API_CALL
clCreateSampler(
	cl_context,
	cl_bool,
	cl_addressing_mode,
	cl_filter_mode,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainSampler(cl_sampler) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseSampler(cl_sampler) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetSamplerInfo(
	cl_sampler,
	cl_sampler_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Program Object APIs  */
CL_API_ENTRY  cl_program CL_API_CALL
clCreateProgramWithSource(
	cl_context,
	cl_uint,
	const char **,
	const size_t *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_program CL_API_CALL
clCreateProgramWithBinary(
	cl_context,
	cl_uint,
	const cl_device_id *,
	const size_t *,
	const unsigned char **,
	cl_int *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;


CL_API_ENTRY  cl_int CL_API_CALL
clRetainProgram(cl_program) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseProgram(cl_program) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clBuildProgram(
	cl_program,
	cl_uint,
	const cl_device_id *,
	const char *,
	void (CL_CALLBACK *)(cl_program, void *),
	void *)
CL_API_SUFFIX__VERSION_1_0;


CL_API_ENTRY  cl_int CL_API_CALL
clGetProgramInfo(
	cl_program,
	cl_program_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetProgramBuildInfo(
	cl_program,
	cl_device_id,
	cl_program_build_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Kernel Object APIs */
CL_API_ENTRY  cl_kernel CL_API_CALL
clCreateKernel(
	cl_program,
	const char *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clCreateKernelsInProgram(
	cl_program,
	cl_uint,
	cl_kernel *,
	cl_uint *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainKernel(cl_kernel) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseKernel(cl_kernel) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clSetKernelArg(
	cl_kernel,
	cl_uint,
	size_t,
	const void *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetKernelInfo(
	cl_kernel,
	cl_kernel_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;


CL_API_ENTRY  cl_int CL_API_CALL
clGetKernelWorkGroupInfo(
	cl_kernel,
	cl_device_id,
	cl_kernel_work_group_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Event Object APIs */
CL_API_ENTRY  cl_int CL_API_CALL
clWaitForEvents(
	cl_uint,
	const cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clGetEventInfo(
	cl_event,
	cl_event_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_event CL_API_CALL
clCreateUserEvent(
	cl_context,
	cl_int *)
CL_API_SUFFIX__VERSION_1_1;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainEvent(cl_event) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseEvent(cl_event) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clSetUserEventStatus(
	cl_event,
	cl_int)
CL_API_SUFFIX__VERSION_1_1;

CL_API_ENTRY  cl_int CL_API_CALL
clSetEventCallback(
	cl_event,
	cl_int,
	void (CL_CALLBACK *)(cl_event, cl_int, void *),
	void *)
CL_API_SUFFIX__VERSION_1_1;

/* Profiling APIs */
CL_API_ENTRY  cl_int CL_API_CALL
clGetEventProfilingInfo(
	cl_event,
	cl_profiling_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_0;

/* Flush and Finish APIs */
CL_API_ENTRY  cl_int CL_API_CALL
clFlush(cl_command_queue) CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clFinish(cl_command_queue) CL_API_SUFFIX__VERSION_1_0;

/* Enqueued Commands APIs */
CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueReadBuffer(
	cl_command_queue,
	cl_mem,
	cl_bool,
	size_t,
	size_t,
	void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueReadBufferRect(
	cl_command_queue,
	cl_mem,
	cl_bool,
	const size_t *,
	const size_t *,
	const size_t *,
	size_t,
	size_t,
	size_t,
	size_t,
	void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_1;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueWriteBuffer(
	cl_command_queue,
	cl_mem,
	cl_bool,
	size_t,
	size_t,
	const void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueWriteBufferRect(
	cl_command_queue,
	cl_mem,
	cl_bool,
	const size_t *,
	const size_t *,
	const size_t *,
	size_t,
	size_t,
	size_t,
	size_t,
	const void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_1;


CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueCopyBuffer(
	cl_command_queue,
	cl_mem,
	cl_mem,
	size_t,
	size_t,
	size_t,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueCopyBufferRect(
	cl_command_queue,
	cl_mem,
	cl_mem,
	const size_t *,
	const size_t *,
	const size_t *,
	size_t,
	size_t,
	size_t,
	size_t,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_1;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueReadImage(
	cl_command_queue,
	cl_mem,
	cl_bool,
	const size_t *,
	const size_t *,
	size_t,
	size_t,
	void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueWriteImage(
	cl_command_queue,
	cl_mem,
	cl_bool,
	const size_t *,
	const size_t *,
	size_t,
	size_t,
	const void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;


CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueCopyImage(
	cl_command_queue,
	cl_mem,
	cl_mem,
	const size_t *,
	const size_t *,
	const size_t *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueCopyImageToBuffer(
	cl_command_queue,
	cl_mem,
	cl_mem,
	const size_t *,
	const size_t *,
	size_t,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueCopyBufferToImage(
	cl_command_queue,
	cl_mem,
	cl_mem,
	size_t,
	const size_t *,
	const size_t *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  void * CL_API_CALL
clEnqueueMapBuffer(
	cl_command_queue,
	cl_mem,
	cl_bool,
	cl_map_flags,
	size_t,
	size_t,
	cl_uint,
	const cl_event *,
	cl_event *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  void * CL_API_CALL
clEnqueueMapImage(
	cl_command_queue,
	cl_mem,
	cl_bool,
	cl_map_flags,
	const size_t *,
	const size_t *,
	size_t *,
	size_t *,
	cl_uint,
	const cl_event *,
	cl_event *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueUnmapMemObject(
	cl_command_queue,
	cl_mem,
	void *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;


CL_API_ENTRY cl_int CL_API_CALL
clEnqueueNDRangeKernel(
	cl_command_queue,
	cl_kernel,
	cl_uint,
	const size_t *,
	const size_t *,
	const size_t *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueTask(
	cl_command_queue,
	cl_kernel,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueNativeKernel(
	cl_command_queue,
	void (CL_CALLBACK *)(void *),
	void *,
	size_t,
	cl_uint,
	const cl_mem *,
	const void **,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_0;

/****************************************************************************
 * OpenCL specification version 1.2 additions                               *
 ****************************************************************************/
#ifdef CL_API_SUFFIX__VERSION_1_2

CL_API_ENTRY  cl_int CL_API_CALL
clGetKernelArgInfo(
	cl_kernel,
	cl_uint,
	cl_kernel_arg_info,
	size_t,
	void *,
	size_t *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clCreateSubDevices(
	cl_device_id,
	const cl_device_partition_property *,
	cl_uint,
	cl_device_id *,
	cl_uint *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clRetainDevice(cl_device_id) CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clReleaseDevice(cl_device_id) CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clCompileProgram(
	cl_program,
	cl_uint,
	const cl_device_id *,
	const char *,
	cl_uint,
	const cl_program *,
	const char **,
	void (CL_CALLBACK *)(cl_program, void *),
	void *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_program CL_API_CALL
clLinkProgram(
	cl_context,
	cl_uint,
	const cl_device_id *,
	const char *,
	cl_uint,
	const cl_program *,
	void (CL_CALLBACK *)(cl_program, void *),
	void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clUnloadPlatformCompiler(cl_platform_id) CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_program CL_API_CALL
clCreateProgramWithBuiltInKernels(
	cl_context,
	cl_uint,
	const cl_device_id *,
	const char *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_mem CL_API_CALL
clCreateImage(
	cl_context,
	cl_mem_flags,
	const cl_image_format *,
	const cl_image_desc *,
	void *,
	cl_int *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueMigrateMemObjects(
	cl_command_queue,
	cl_uint,
	const cl_mem *,
	cl_mem_migration_flags,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueMarkerWithWaitList(
	cl_command_queue,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueBarrierWithWaitList(
	cl_command_queue,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueFillImage(
	cl_command_queue,
	cl_mem,
	const void *,
	const size_t *,
	const size_t *,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_2;

CL_API_ENTRY  cl_int CL_API_CALL
clEnqueueFillBuffer(
	cl_command_queue,
	cl_mem,
	const void *,
	size_t,
	size_t,
	size_t,
	cl_uint,
	const cl_event *,
	cl_event *)
CL_API_SUFFIX__VERSION_1_2;

#endif //CL_API_SUFFIX__VERSION_1_2


#ifdef  __cplusplus
}
#endif

#endif // BBQUE_OCL_H_

