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

#include "bbque/config.h"

#include "bbque/pp/linux_platform_proxy.h"
#include "bbque/res/binder.h"
#include "bbque/res/resource_path.h"
#include "bbque/utils/assert.h"
#include "bbque/utils/iofs.h"
#include "bbque/utils/utility.h"

#include <boost/program_options.hpp>

#ifdef CONFIG_BBQUE_RELIABILITY
#include <criu/criu.h>
#endif

#include <cstring>
#include <fstream>
#include <libcgroup.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/version.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <sstream>
#include <unistd.h>

#ifdef CONFIG_BBQUE_WM
#include "bbque/power_monitor.h"
#endif

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
#include "bbque/energy_monitor.h"
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
#include <asm/types.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/pkt_sched.h>
#include <linux/pkt_cls.h>
#include <linux/rtnetlink.h>
#include <netlink/ll_map.h>

#define NET_MAX_BANDWIDTH 1000000000000LL
#define Q_HANDLE (0x100000)
#define F_HANDLE (1)

#endif

#define BBQUE_PP_LINUX_SILOS BBQUE_PP_LINUX_CGROUP"/silos"

#define BBQUE_PP_LINUX_CPUS_PARAM           "cpuset.cpus"
#define BBQUE_PP_LINUX_CPUP_PARAM           "cpu.cfs_period_us"
#define BBQUE_PP_LINUX_CPUQ_PARAM           "cpu.cfs_quota_us"
#define BBQUE_PP_LINUX_MEMN_PARAM           "cpuset.mems"
#define BBQUE_PP_LINUX_MEMB_PARAM           "memory.limit_in_bytes"
#define BBQUE_PP_LINUX_CPU_EXCLUSIVE_PARAM  "cpuset.cpu_exclusive"
#define BBQUE_PP_LINUX_MEM_EXCLUSIVE_PARAM  "cpuset.mem_exclusive"
#define BBQUE_PP_LINUX_PROCS_PARAM          "cgroup.procs"
#define BBQUE_PP_LINUX_NETCLS_PARAM         "net_cls.classid"
#define BBQUE_PP_LINUX_R_IO_PARAM			"blkio.throttle.read_bps_device"
#define BBQUE_PP_LINUX_W_IO_PARAM			"blkio.throttle.write_bps_device"

#define BBQUE_PP_LINUX_SYS_MEMINFO "/proc/meminfo"

// The default CFS bandwidth period [us]
#define BBQUE_PP_LINUX_CPUP_DEFAULT  100000
#define BBQUE_PP_LINUX_CPUP_MAX      1000000

// Checking for kernel version requirements

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
#error Linux kernel >= 2.6.34 required by the Platform Integration Layer
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
#warning CPU Quota management disabled, Linux kernel >= 3.2 required
#endif

#define MODULE_CONFIG "LinuxPlatformProxy"

namespace bb = bbque;
namespace br = bbque::res;
namespace po = boost::program_options;


namespace bbque {
namespace pp {

LinuxPlatformProxy * LinuxPlatformProxy::GetInstance()
{
	static LinuxPlatformProxy * instance;
	if (instance == nullptr)
		instance = new LinuxPlatformProxy();
	return instance;
}

LinuxPlatformProxy::LinuxPlatformProxy() :
    controller("cpuset"),
    refreshMode(false)
{
	//---------- Get a logger module
	logger = bu::Logger::GetLogger(LINUX_PP_NAMESPACE);
	assert(logger);

	this->platform_id = BBQUE_PP_LINUX_PLATFORM_ID;
	this->hardware_id = BBQUE_TARGET_HARDWARE_ID; // Defined via Kconfig

	//---------- Loading configuration
	this->LoadConfiguration();

	//---------- Init the CGroups
	if (BBQUE_UNLIKELY(this->InitCGroups() != PLATFORM_OK)) {
		// We have to throw the exception: without cgroups the object should not
		// be created!
		throw new std::runtime_error("Unable to construct LinuxPlatformProxy, "
					"InitCGroups() failed.");
	}

#ifdef CONFIG_BBQUE_RELIABILITY
	InitReliabilitySupport();
#endif

#ifdef CONFIG_TARGET_ARM_BIG_LITTLE
	InitCoresType();
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
	InitNetworkManagement();
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO
	InitIODevInfo();
#endif

}

LinuxPlatformProxy::~LinuxPlatformProxy()
{
	logger->Info("LinuxPlatformProxy: destroyed");
}


#ifdef CONFIG_TARGET_ARM_BIG_LITTLE

void LinuxPlatformProxy::InitCoresType()
{
	std::stringstream ss;
	ss.str(BBQUE_BIG_LITTLE_HP);
	std::string first_core_str, last_core_str;

	size_t sep_pos = ss.str().find_first_of("-", 0);
	first_core_str = ss.str().substr(0, sep_pos);
	uint16_t first, last;
	if (!first_core_str.empty()) {
		first = std::stoi(first_core_str);
		logger->Debug("InitCoresType: first big core: %d", first);
		last_core_str = ss.str().substr(++sep_pos, std::string::npos);
		last = std::stoi(last_core_str);
		logger->Debug("InitCoresType: last big core: %d", last);
	}

	BBQUE_RID_TYPE core_id;
	for (core_id = first; core_id <= last; ++core_id) {
		logger->Debug("InitCoresType: %d is high-performance", core_id);
		high_perf_cores[core_id] = true;
	}

	//	while (std::getline(ss, core_str, ',')) {	}
}

#endif // CONFIG_TARGET_ARM_BIG_LITTLE


#ifdef CONFIG_BBQUE_RELIABILITY

void LinuxPlatformProxy::InitReliabilitySupport()
{
	//	char criu_exec_path[] = BBQUE_CRIU_BINARY_PATH);
	//	char criu_serv_addr[] = BBQUE_PATH_VAR;
	boost::filesystem::perms prms(boost::filesystem::owner_all);
	prms |= boost::filesystem::others_read;
	prms |= boost::filesystem::group_read;
	prms |= boost::filesystem::group_write;

	// Checkpoint image path
	image_prefix_dir += "/linux";
	logger->Info("Reliability: checkpoint images directory:  %s",
		image_prefix_dir.c_str());

	if (!boost::filesystem::exists(image_prefix_dir)) {
		if (boost::filesystem::create_directories(image_prefix_dir)) {
			logger->Debug("Reliability: "
				"checkpoint images directory created");
		}
		else
			logger->Error("Reliability: "
				"checkpoint images directory not created");
	}
	boost::filesystem::permissions(image_prefix_dir, prms);


	// Freezer initialization
	freezer_prefix_dir += "/linux";
	logger->Info("Reliability: freezer interfaces path: %s",
		freezer_prefix_dir.c_str());
	if (!boost::filesystem::exists(freezer_prefix_dir)) {
		if (boost::filesystem::create_directory(freezer_prefix_dir)) {
			logger->Debug("Reliability: freezer created");
		}
		else
			logger->Error("Reliability: freezer created");
	}
	boost::filesystem::permissions(freezer_prefix_dir, prms);


	// CRIU initialization
	int c_ret = criu_init_opts();
	if (c_ret == 0) {
		logger->Info("Reliability: CRIU successfully initialized");
		//	criu_set_service_address(BBQUE_PATH_VAR);
		logger->Info("Reliability: CRIU service address = [%s]",
			BBQUE_PATH_VAR);

		criu_set_service_binary(BBQUE_CRIU_BINARY_PATH);
		logger->Info("Reliability: CRIU service binary = [%s]",
			BBQUE_CRIU_BINARY_PATH);
	}
	else {
		logger->Error("Reliability: CRIU initialization failed [ret=%d]",
			c_ret);
	}
}
#endif // CONFIG_BBQUE_RELIABILITY


#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH

void LinuxPlatformProxy::InitNetworkManagement()
{
	bbque_assert(0 == rtnl_open(&network_info.rth_1, 0));
	bbque_assert(0 == rtnl_open(&network_info.rth_2, 0));
	memset(&network_info.kernel_addr, 0, sizeof (network_info.kernel_addr));
	network_info.kernel_addr.nl_family = AF_NETLINK;
	logger->Debug("NetworkManagement: sockets to kernel initialized.");
}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::MakeQDisk(int if_index)
{
	char k[16];
	NetworkKernelRequest_t req;

	memset(&req, 0, sizeof (req));
	memset(&k, 0, sizeof (k));

	req.n.nlmsg_len   = NLMSG_LENGTH(sizeof (struct tcmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_CREATE;
	req.n.nlmsg_type  = RTM_NEWQDISC;
	req.t.tcm_family  = AF_UNSPEC;
	req.t.tcm_parent  = TC_H_ROOT;
	req.t.tcm_handle  = Q_HANDLE;

	strncpy(k, "htb", sizeof (k) - 1);
	addattr_l(&req.n, sizeof (req), TCA_KIND, k, strlen(k) + 1);
	HTBParseOpt(&req.n);

	req.t.tcm_ifindex = if_index;

	int err = rtnl_talk(&network_info.kernel_addr, &network_info.rth_2,
			&req.n, 0, 0, NULL, NULL, NULL);
	if (err < 0) {
		if (EEXIST == errno) {
			// That's good, someone else added the QDisk
			return PLATFORM_OK;
		}

		logger->Error("MakeQDisk: Kernel communication failed "
			"[%d] (%s).", errno, strerror(errno));
		return PLATFORM_GENERIC_ERROR;
	}

	return PLATFORM_OK;

}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::MakeCLS(int if_index)
{
	char k[16];
	NetworkKernelRequest_t req;

	memset(&req, 0, sizeof (req));
	memset(k, 0, sizeof (k));
	memset(&req, 0, sizeof (req));

	req.n.nlmsg_len   = NLMSG_LENGTH(sizeof (struct tcmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | (NLM_F_REPLACE | NLM_F_CREATE);
	req.n.nlmsg_type  = RTM_NEWTFILTER;
	req.t.tcm_family  = AF_UNSPEC;
	req.t.tcm_parent  = Q_HANDLE;

	uint32_t protocol = 8; // 8 = ETH_P_IP
	uint32_t prio     = 10;
	req.t.tcm_info    = TC_H_MAKE(prio << 16, protocol);

	// &kind
	strncpy(k, "cgroup", sizeof (k) - 1);
	addattr_l(&req.n, sizeof (req), TCA_KIND, k, strlen(k) + 1);
	CGParseOpt(F_HANDLE, &req.n);

	// if index
	req.t.tcm_ifindex = if_index;

	//try to talk
	int err = rtnl_talk(&network_info.kernel_addr, &network_info.rth_2,
			&req.n, 0, 0, NULL, NULL, NULL);
	if (BBQUE_UNLIKELY(err < 0)) {
		if (EEXIST == errno) {
			// That's good, someone else added the QDisk
			return PLATFORM_OK;
		}

		logger->Error("MakeCLS: Kernel communication failed "
			"[%d] (%s).", errno, strerror(errno));
		return PLATFORM_GENERIC_ERROR;
	}
	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::CGParseOpt(long handle, struct nlmsghdr *n)
{
	struct tcmsg *t = (struct tcmsg *) NLMSG_DATA(n);
	struct rtattr *tail;
	void * p1;
	void * p2;
	t->tcm_handle = handle;

	tail = (struct rtattr*) (((void*) n) + NLMSG_ALIGN(n->nlmsg_len));
	addattr_l(n, MAX_MSG, TCA_OPTIONS, NULL, 0);

	p1 = n;
	p2 = tail;
	tail->rta_len = ((char *) p1) - ((char *) p2) + n->nlmsg_len;
	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::HTBParseOpt(struct nlmsghdr *n)
{
	struct tc_htb_glob opt; //in pkt_sched.h
	struct rtattr *tail;
	memset(&opt, 0, sizeof (opt));
	opt.rate2quantum = 10;
	opt.version = 3;

	opt.defcls = 1;

	tail = NLMSG_TAIL(n);
	addattr_l(n, 1024, TCA_OPTIONS, NULL, 0);
	addattr_l(n, 2024, TCA_HTB_INIT, &opt, NLMSG_ALIGN(sizeof (opt)));
	tail->rta_len = (char *) NLMSG_TAIL(n) - (char *) tail;
	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::HTBParseClassOpt(unsigned rate, struct nlmsghdr *n)
{
	struct tc_htb_opt opt;
	unsigned short mpu = 0;
	unsigned short overhead = 0;
	struct rtattr *tail;

	memset(&opt, 0, sizeof (opt));

	//*rate = (bps * s->scale) / 8.; //empirically determined
	//{ "KBps",	8000. },
	opt.rate.rate = rate * 125;

	opt.ceil = opt.rate;

	opt.ceil.overhead = overhead;
	opt.rate.overhead = overhead;

	opt.ceil.mpu = mpu;
	opt.rate.mpu = mpu;

	tail = NLMSG_TAIL(n);
	addattr_l(n, 1024, TCA_OPTIONS, NULL, 0);
	addattr_l(n, 2024, TCA_HTB_PARMS, &opt, sizeof (opt));
	tail->rta_len = (char *) NLMSG_TAIL(n) - (char *) tail;
	return PLATFORM_OK;
}

#endif

#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO

void LinuxPlatformProxy::InitIODevInfo()
{
	const PlatformDescription *pd;

	try {
		pd = &this->GetPlatformDescription();
	}
	catch (const std::runtime_error& e) {
		UNUSED(e);
		logger->Fatal("ScanPlatformDescription: PlatformDescription object missing");
	}

	for(const auto & sys_entry : pd->GetSystemsAll()) {
		auto sys = sys_entry.second;

		for(const auto storage : sys.GetStoragesAll()) {
			if(storage -> GetId() == 0) MakeNewIODev(storage->GetDev());
		}
	}

	logger->Debug("InitIODevInfo: found %d storage device(s).", this->dev_info.size());
	for (const auto device : this->dev_info) {
		logger->Debug("InitIODevInfo: %s", 
						device->dev.c_str());
	}
}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::MakeNewIODev(std::string const & dev)
{
	// Creates a new element and adds it to the vector this -> dev_info.
	IODevInfoPtr_t new_info = std::make_shared<IODevInfo_t>(dev);
	this->dev_info.push_back(new_info);

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::AddDevicePath(br::ResourcePathPtr_t resource_path)
{
	// Search the first available bandwidth path and set it to resource_path.
	for(auto & dev: this->dev_info) {
		if(dev->r_bw_path == NULL){
			dev->r_bw_path = resource_path;
			logger->Debug("AddDevicePath: set %s as read bw path of %s", resource_path->ToString().c_str(), dev->dev.c_str());
			return LinuxPlatformProxy::PLATFORM_OK;
		}

		if(dev->w_bw_path == NULL){
			dev->w_bw_path = resource_path;
			logger->Debug("AddDevicePath: set %s as write bw path of %s", resource_path->ToString().c_str(), dev->dev.c_str());
			return LinuxPlatformProxy::PLATFORM_OK;
		}
	}

	return LinuxPlatformProxy::PLATFORM_GENERIC_ERROR;
}

std::string LinuxPlatformProxy::GetDevFromPath(br::ResourcePathPtr_t const & resource_path)
{
	for(auto dev : this->dev_info) {
		if (!dev->r_bw_path->ToString().compare(resource_path->ToString()) 
		 || !dev->w_bw_path->ToString().compare(resource_path->ToString())) {
			return dev->dev;
		}
	}
	return "";
}

#endif

bool
LinuxPlatformProxy::IsHighPerformance(bbque::res::ResourcePathPtr_t const & path) const
{
#ifdef CONFIG_TARGET_ARM_BIG_LITTLE

	BBQUE_RID_TYPE core_id =
		path->GetID(bbque::res::ResourceType::PROC_ELEMENT);
	if (core_id >= 0 && core_id <= BBQUE_TARGET_CPU_CORES_NUM) {
		logger->Debug("IsHighPerformance: <%s> = %d",
			path->ToString().c_str(), high_perf_cores[core_id]);
		return high_perf_cores[core_id];
	}
	logger->Error("IsHighPerformance: cannot find process element ID in <%s>",
		path->ToString().c_str());
#else
	(void) path;
#endif
	return false;
}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::Setup(SchedPtr_t papp) noexcept
{
	ExitCode_t result = PLATFORM_OK;
	CGroupDataPtr_t pcgd;

	RLinuxBindingsPtr_t prlb = std::make_shared<RLinuxBindings_t>(
		MaxCpusCount, MaxMemsCount);

	// Setup a new CGroup data for this application
	result = GetCGroupData(papp, pcgd);
	if (BBQUE_UNLIKELY(result != PLATFORM_OK)) {
		logger->Error("Setup: [%s] CGroup initialization FAILED "
			"(Error: CGroupData setup)", papp->StrId());
		return result;
	}

	// Setup the kernel CGroup with an empty resources assignement
	SetupCGroup(pcgd, prlb, false, false);

	// Reclaim application resource, thus moving this app into the silos
	result = this->ReclaimResources(papp);
	if (BBQUE_UNLIKELY(result != PLATFORM_OK)) {
		logger->Error("Setup: [%s] CGroup initialization FAILED "
			"(Error: failed moving app into silos)", papp->StrId());
		return result;
	}

	return result;
}

void LinuxPlatformProxy::LoadConfiguration() noexcept
{
	po::options_description opts_desc("Linux Platform Proxy Options");
	opts_desc.add_options()
		(MODULE_CONFIG ".cfs_bandwidth.margin_pct",
		po::value<int> (&cfs_margin_pct)->default_value(0),
		"The safety margin [%] to add for CFS bandwidth enforcement");
	opts_desc.add_options()
		(MODULE_CONFIG ".cfs_bandwidth.threshold_pct",
		po::value<int> (&cfs_threshold_pct)->default_value(100),
		"The threshold [%] under which we enable CFS bandwidth enforcement");
	po::variables_map opts_vm;
	ConfigurationManager::GetInstance().
		ParseConfigurationFile(opts_desc, opts_vm);

	// Range check
	cfs_margin_pct = std::min(std::max(cfs_margin_pct, 0), 100);
	cfs_threshold_pct = std::min(std::max(cfs_threshold_pct, 0), 100);

	// Force threshold to be NOT lower than (100 - margin)
	if (cfs_threshold_pct < cfs_margin_pct)
		cfs_threshold_pct = 100 - cfs_margin_pct;

	logger->Info("LoadConfiguration: CFS bandwidth control, margin %d, threshold: %d",
		cfs_margin_pct, cfs_threshold_pct);
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::Release(SchedPtr_t papp)
{
	// Release CGroup plugin data
	// ... thus releasing the corresponding control group
	logger->Debug("Release: releasing platform-specific data [%s]", papp->StrId());
	papp->ClearPluginData(LINUX_PP_NAMESPACE);

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::ReclaimResources(SchedPtr_t papp) noexcept
{
	RLinuxBindingsPtr_t prlb(new RLinuxBindings_t(MaxCpusCount, MaxMemsCount));
	// FIXME: update once a better SetAttributes support is available
	//CGroupDataPtr_t pcgd(new CGroupData_t);
	CGroupDataPtr_t pcgd;
	int error;

	logger->Debug("ReclaimResources: CGroup resource claiming START");

	// Move this app into "silos" CGroup
	cgroup_set_value_uint64(psilos->pc_cpuset,
				BBQUE_PP_LINUX_PROCS_PARAM,
				papp->Pid());

	// Configure the CGroup based on resource bindings
	logger->Info("ReclaimResources: [%s] => SILOS[%s]",
		papp->StrId(), psilos->cgpath);
	error = cgroup_modify_cgroup(psilos->pcg);
	if (BBQUE_UNLIKELY(error)) {
		logger->Error("ReclaimResources: CGroup resource reclaiming FAILED "
			"(Error: libcgroup, kernel cgroup update "
			"[%d: %s]", errno, strerror(errno));
		return PLATFORM_MAPPING_FAILED;
	}

	logger->Debug("ReclaimResources: CGroup resource claiming DONE!");

	return PLATFORM_OK;
}

void LinuxPlatformProxy::Exit()
{

#ifdef CONFIG_BBQUE_RELIABILITY

	// Remove checkpoint image path
	if (boost::filesystem::exists(image_prefix_dir)) {
		if (boost::filesystem::remove_all(image_prefix_dir))
			logger->Info("Reliability: "
				"image directory [%s] removed",
				image_prefix_dir.c_str());
		else
			logger->Error("Reliability: "
				"cannot remove image directory [%s]",
				image_prefix_dir.c_str());
	}
	// Remove freezer directory
	if (boost::filesystem::exists(freezer_prefix_dir)) {
		if (!boost::filesystem::remove_all(freezer_prefix_dir))
			logger->Info("Reliability: "
				"freezer directory [%s] removed",
				freezer_prefix_dir.c_str());
		else
			logger->Error("Reliability: "
				"cannot remove freezer directory [%s]",
				freezer_prefix_dir.c_str());
	}
#endif

	logger->Info("Exit: LinuxPP terminated");
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::MapResources(SchedPtr_t papp,
				 ResourceAssignmentMapPtr_t pres,
				 bool excl) noexcept
{
	ResourceAccounter &ra = ResourceAccounter::GetInstance();
	RViewToken_t rvt = ra.GetScheduledView();

	// FIXME: update once a better SetAttributes support is available
	CGroupDataPtr_t pcgd;
	ExitCode_t result;

	logger->Debug("MapResources: CGroup resource mapping START");

	// Get a reference to the CGroup data
	result = GetCGroupData(papp, pcgd);
	if (BBQUE_UNLIKELY(result != PLATFORM_OK))
		return result;

	// Get the set of assigned (bound) CPU
	br::ResourceBitset cpus(br::ResourceBinder::GetMask(
							pres,
							br::ResourceType::CPU));
	BBQUE_RID_TYPE cpu_id = cpus.FirstSet();
	if (BBQUE_UNLIKELY(cpu_id < 0)) {
		// No resources for LinuxPP
		logger->Warn("MapResources: Missing binding to nodes/CPUs");
		return PLATFORM_OK;
	}

	// Map resources for each node (e.g., CPU)
	RLinuxBindingsPtr_t prlb = std::make_shared<RLinuxBindings_t>
		(MaxCpusCount, MaxMemsCount);
	for (; cpu_id <= cpus.LastSet(); ++cpu_id) {
		logger->Debug("MapResources: CGroup resource mapping CPU node [%d]",
			cpu_id);
		if (!cpus.Test(cpu_id)) continue;

		// Node resource mapping
		result = GetResourceMapping(papp, pres, prlb, cpu_id, rvt);
		if (BBQUE_UNLIKELY(result != PLATFORM_OK)) {
			logger->Error("MapResources: binding parsing FAILED");
			return PLATFORM_MAPPING_FAILED;
		}

		// Configure the CGroup based on resource bindings
		result = SetupCGroup(pcgd, prlb, excl, true);
		if (BBQUE_UNLIKELY(result != PLATFORM_OK)) {
			logger->Error("MapResources: Set CGroups FAILED");
			return PLATFORM_MAPPING_FAILED;
		}
	}

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
	result = SetCGNetworkBandwidth(papp, pcgd, pres, prlb);
	if (PLATFORM_OK != result) {
		logger->Warn("MapResources: unable to enforce Network Bandwidth [%d],"
			" ignoring...", result);
	}

#endif


#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	logger->Debug("MapResources: Distributed actuation: retrieving masks and ranking");

	br::ResourceBitset proc_elements =
		br::ResourceBinder::GetMask(
					pres,
					br::ResourceType::PROC_ELEMENT,
					br::ResourceType::CPU,
					R_ID_ANY,
					papp, rvt);

	br::ResourceBitset mem_nodes =
		br::ResourceBinder::GetMask(
					pres,
					br::ResourceType::MEMORY,
					br::ResourceType::CPU,
					R_ID_ANY,
					papp, rvt);

	// Processing elements that have been allocated exclusively
	br::ResourceBitset proc_elements_exclusive = proc_elements;
	proc_elements_exclusive.Reset();

	for (BBQUE_RID_TYPE pe_id = proc_elements.FirstSet();
	pe_id <= proc_elements.LastSet();
	pe_id++) {

		// Skip if this processing element is not allocated to this app
		if (!proc_elements.Test(pe_id))
			continue;

		// Getting a reference to the resource status
		std::string path = "sys.cpu.pe" + std::to_string(pe_id);
		br::ResourcePtr_t resource = *(ra.GetResources(path).begin());

		// If allocation is exclusive, set the bit
		if (resource->ApplicationsCount(rvt) == 1)
			proc_elements_exclusive.Set(pe_id);

	}

	logger->Debug("MapResources: [%d] pes %s (isolated %s), mems %s",
		papp->Pid(),
		proc_elements.ToString().c_str(),
		proc_elements_exclusive.ToString().c_str(),
		mem_nodes.ToString().c_str());
	papp->SetCGroupSetupData(
				proc_elements.ToULong(),
				mem_nodes.ToULong(),
				proc_elements_exclusive.ToULong());
#endif // CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION

	return PLATFORM_OK;
}


#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::SetCGNetworkBandwidth(SchedPtr_t papp, CGroupDataPtr_t pcgd,
					  ResourceAssignmentMapPtr_t pres,
					  RLinuxBindingsPtr_t prlb)
{
	ResourceAccounter &ra = ResourceAccounter::GetInstance();
	std::stringstream sstream_PID;
	int res;

	// net_cls.classid attribute has must be written as an hexadecimal string
	// of the shape AAAABBBB. The handles correspond to traffic shapping class
	// handles. AAAA being the major part of the handle and must be the same for
	// all classes having the same parent. BBBB being the minor part,
	// a class-specific identifier. More information in the "tc" documentation.
	// Here, the major handle number is 0x10 and correspond to the parent qdisc
	// handle and will be the same for all classes. The minor handle number is
	// the process PID
	sstream_PID << std::hex << papp->Pid();
	std::string PID("0x10" + sstream_PID.str());

	cgroup_add_value_string(pcgd->pc_net_cls,
				BBQUE_PP_LINUX_NETCLS_PARAM, PID.c_str());
	res = cgroup_modify_cgroup(pcgd->pcg);
	if (res) {
		logger->Error("SetCGNetworkBandwidth: CGroup NET_CLS resource mapping FAILED "
			"(Error: libcgroup, kernel cgroup update "
			"[%d: %s])", errno, strerror(errno));
		return PLATFORM_MAPPING_FAILED;
	}

	br::ResourceBitset net_ifs(
				br::ResourceBinder::GetMask(
							pres,
							br::ResourceType::NETWORK_IF));
	auto interface_id = net_ifs.FirstSet();
	if (interface_id < 0) {
		logger->Error("SetCGNetworkBandwidth: Missing binding to network interfaces");
		return PLATFORM_MAPPING_FAILED;
	}

	for (; interface_id <= net_ifs.LastSet(); ++interface_id) {
		logger->Debug("SetCGNetworkBandwidth: CGroup resource mapping interface [%d]",
			interface_id);
		if (!net_ifs.Test(interface_id)) continue;

		logger->Debug("SetCGNetworkBandwidth: CLASS handle %d,"
			" bandwith %d, interface : %d",
			papp->Pid(), prlb->amount_net_bw, interface_id);

		int64_t assigned_net_bw = prlb->amount_net_bw;
		if (assigned_net_bw < 0) {
			assigned_net_bw = ra.Total("sys0.net" +
						std::to_string(interface_id));
		}
		MakeNetClass(papp->Pid(), assigned_net_bw, interface_id);
	}

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::MakeNetClass(AppPid_t handle, unsigned rate, int if_index)
{
	NetworkKernelRequest_t req;
	char k[16];

	memset(&req, 0, sizeof (req));
	memset(k, 0, sizeof (k));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof (struct tcmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_CREATE;
	req.n.nlmsg_type = RTM_NEWTCLASS;
	req.t.tcm_family = AF_UNSPEC;

	req.t.tcm_handle = handle;
	req.t.tcm_parent = Q_HANDLE;

	strncpy(k, "htb", sizeof (k) - 1);
	addattr_l(&req.n, sizeof (req), TCA_KIND, k, strlen(k) + 1);
	HTBParseClassOpt(rate, &req.n);

	req.t.tcm_ifindex = if_index;

	if (rtnl_talk(&network_info.kernel_addr, &network_info.rth_1, &req.n, 0,
		0, NULL, NULL, NULL) < 0) {
		return PLATFORM_GENERIC_ERROR;
	}

	return PLATFORM_OK;
}

#endif // CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::GetResourceMapping(SchedPtr_t papp,
				       ResourceAssignmentMapPtr_t assign_map,
				       RLinuxBindingsPtr_t prlb,
				       BBQUE_RID_TYPE node_id,
				       br::RViewToken_t rvt) noexcept
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	// CPU core set
	br::ResourceBitset core_ids(
				br::ResourceBinder::GetMask(assign_map,
							br::ResourceType::PROC_ELEMENT,
							br::ResourceType::CPU,
							node_id,
							papp, rvt));
	if (strlen(prlb->cpus) > 0)
		strcat(prlb->cpus, ",");
	strncat(prlb->cpus, +core_ids.ToStringCG().c_str(), 3 * MaxCpusCount);
	logger->Debug("GetResourceMapping: cpu[%d] cores: { %s }",
		node_id, prlb->cpus);

	// Memory nodes
	br::ResourceBitset mem_ids(
				br::ResourceBinder::GetMask(
							assign_map,
							br::ResourceType::PROC_ELEMENT,
							br::ResourceType::MEMORY,
							node_id,
							papp, rvt));
	if (mem_ids.Count() == 0)
		strncpy(prlb->mems, memory_ids_all.c_str(), memory_ids_all.length());
	else
		strncpy(prlb->mems, mem_ids.ToStringCG().c_str(), 3 * MaxMemsCount);
	logger->Debug("GetResourceMapping: cpu[%d] mems : { %s }",
		node_id, prlb->mems);

	// CPU quota
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	prlb->amount_cpus += ra.GetAssignedAmount(
						assign_map,
						papp, rvt,
						br::ResourceType::PROC_ELEMENT,
						br::ResourceType::CPU,
						node_id);
#else
	prlb->amount_cpus = -1;
#endif
	logger->Debug("GetResourceMapping: cpu[%d] quota: { %d }",
		node_id, prlb->amount_cpus);

	// Memory amount
#ifdef CONFIG_BBQUE_LINUX_CG_MEMORY
	uint64_t memb = ra.GetAssignedAmount(
					assign_map,
					papp, rvt,
					br::ResourceType::MEMORY,
					br::ResourceType::CPU);
	if (memb > 0)
		prlb->amount_memb = memb;
#else
	prlb->amount_memb = -1;
#endif
	logger->Debug("GetResourceMapping: cpu[%d] memb: { %lld }",
		node_id, prlb->amount_memb);

	// Network bandwidth
#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
	uint64_t netb = ra.GetAssignedAmount(
					assign_map,
					papp, rvt,
					br::ResourceType::NETWORK_IF,
					br::ResourceType::SYSTEM);
	if (netb > 0)
		prlb->amount_net_bw = netb;
#else
	prlb->amount_net_bw = -1;
#endif
	logger->Debug("GetResourceMapping: cpu[%d] network bandwidth: { %lld }",
		node_id, prlb->amount_net_bw);

#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO
	for (auto dev : dev_info) {
		uint64_t r_bw = ra.GetAssignedAmount(
						assign_map,
						papp, rvt,
						dev->r_bw_path);

		uint64_t w_bw =  ra.GetAssignedAmount(
						assign_map,
						papp, rvt,
						dev->w_bw_path);

		if(r_bw > 0) {
			prlb -> read_devs.push_back(std::make_pair(dev->r_bw_path, r_bw));
			logger->Debug("GetResourceMapping: cpu[%d] read bandwidth for %s: { %lld }",
				node_id, dev->r_bw_path->ToString().c_str(), r_bw);
		}

		if(w_bw > 0) {
			prlb -> write_devs.push_back(std::make_pair(dev->w_bw_path, w_bw));
			logger->Debug("GetResourceMapping: cpu[%d] write bandwidth for %s: { %lld }",
				node_id, dev->w_bw_path->ToString().c_str(), w_bw);
		}
	}
#endif

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::Refresh() noexcept
{
	logger->Info("Refresh: Updating CGroups resources description...");
	refreshMode = true;
	return this->ScanPlatformDescription();
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::LoadPlatformData() noexcept
{
	logger->Info("LoadPlatformData: Starting...");
	return this->ScanPlatformDescription();
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::ScanPlatformDescription() noexcept
{
	const PlatformDescription *pd;

	try {
		pd = &this->GetPlatformDescription();
	}
	catch (const std::runtime_error& e) {
		UNUSED(e);
		logger->Fatal("ScanPlatformDescription: PlatformDescription object missing");
		return PLATFORM_LOADING_FAILED;
	}

	this->memory_ids_all = "";

	for (const auto & sys_entry : pd->GetSystemsAll()) {
		auto sys = sys_entry.second;

		logger->Debug("ScanPlatformDescription: [%s@%s] CPUs...",
			sys.GetHostname().c_str(), sys.GetNetAddress().c_str());
		for (const auto cpu : sys.GetCPUsAll()) {
			ExitCode_t result = this->RegisterCPU(cpu, sys.IsLocal());
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("ScanPlatformDescription: register CPU %d failed", cpu.GetId());
				return result;
			}

#ifdef CONFIG_BBQUE_ENERGY_MONITOR
			std::string strpath = cpu.GetPath()
				+ std::string(".")
				+ br::GetResourceTypeString(br::ResourceType::PROC_ELEMENT);
			RegisterForEnergyMonitoring(strpath);
#endif
		}

		logger->Debug("ScanPlatformDescription: [%s@%s] Memories...",
			sys.GetHostname().c_str(),
			sys.GetNetAddress().c_str());
		for (const auto mem : sys.GetMemoriesAll()) {
			ExitCode_t result = this->RegisterMEM(*mem, sys.IsLocal());
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("ScanPlatformDescription: MEM %d "
					"registration failed",
					mem->GetId());
				return result;
			}

			if (sys.IsLocal()) {
				logger->Debug("ScanPlatformDescription: [%s@%s] "
					"is LOCAL",
					sys.GetHostname().c_str(),
					sys.GetNetAddress().c_str());

				this->memory_ids_all += std::to_string(mem->GetId()) + ',';
			}
		}

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
		for (const auto net : sys.GetNetworkIFsAll()) {
			ExitCode_t result = this->RegisterNET(*net, sys.IsLocal());
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Error("ScanPlatformDescription: network "
					"interface %d (%s) registration failed [%d]",
					net->GetId(),
					net->GetName().c_str(),
					result);
				return result;
			}
		}
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO
		logger->Debug("ScanPlatformDescription: [%s@%s] IO storage...",
			sys.GetHostname().c_str(),
			sys.GetNetAddress().c_str());
		for (const auto storage : sys.GetStoragesAll()) {
			ExitCode_t result = this->RegisterIODev(*storage, sys.IsLocal());
			if (BBQUE_UNLIKELY(PLATFORM_OK != result)) {
				logger->Fatal("ScanPlatformDescription: STORAGE %d "
					"registration failed",
					storage->GetId());
				return result;
			}
		}
#endif

	}

	// Build the default string for the CGroups
	if (!this->memory_ids_all.empty())
		this->memory_ids_all.pop_back();
	logger->Debug("ScanPlatformDescription: Memory nodes = {%s}",
		memory_ids_all.c_str());

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::RegisterCPU(const PlatformDescription::CPU &cpu,
				bool is_local) noexcept
{

	ResourceAccounter & ra(ResourceAccounter::GetInstance());

	for (const auto pe : cpu.GetProcessingElementsAll()) {
		auto pe_type = pe.GetPartitionType();
		if ((PlatformDescription::MDEV == pe_type)
		|| (PlatformDescription::SHARED == pe_type)) {
			const std::string resource_path = pe.GetPath();
			const int share = pe.GetShare();
			logger->Debug("RegisterCPU: <%s>: total=%d",
				resource_path.c_str(), share);

			if (refreshMode) {
				ra.UpdateResource(resource_path, "", share);
			}
			else {
				ra.RegisterResource(resource_path, "", share);
				if (is_local) {
					logger->Debug("RegisterCPU: <%s>: is local",
						resource_path.c_str());
					InitPowerInfo(resource_path, pe.GetId());
				}
			}
		}
	}


	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::RegisterMEM(const PlatformDescription::Memory &mem,
				bool is_local) noexcept
{

	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	UNUSED(is_local);

	std::string resource_path = mem.GetPath();
	const auto q_bytes = mem.GetQuantity();
	logger->Debug("RegisterMEM: Registration of <%s>: %lu Kb", resource_path.c_str(), q_bytes);

	if (refreshMode) {
		ra.UpdateResource(resource_path, "", q_bytes);
	}
	else {
		ra.RegisterResource(resource_path, "", q_bytes);
	}
	logger->Debug("RegisterMEM: Registration of <%s> successfully performed", resource_path.c_str());

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::RegisterNET(const PlatformDescription::NetworkIF &net,
				bool is_local) noexcept
{

	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	UNUSED(is_local);

	std::string resource_path = net.GetPath();
	logger->Debug("RegisterNET: Registration of netif %d <%s>",
		net.GetId(), net.GetName().c_str());

	uint64_t bw;
	try {
		bw = GetNetIFBandwidth(net.GetName());
	}
	catch (std::runtime_error &e) {
		logger->Error("RegisterNET: Unable to get the Bandwidth of %s: %s",
			net.GetName().c_str(), e.what());
		return PLATFORM_GENERIC_ERROR;
	}

	if (refreshMode) {
		ra.UpdateResource(resource_path, "", bw);
	}
	else {
		ra.RegisterResource(resource_path, "", bw);
	}

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
	// Now we have to register the kernel-level

	int interface_idx = if_nametoindex(net.GetName().c_str());
	logger->Debug("RegisterNET: netif %d (%s) has the kernel index %d", net.GetId(),
		net.GetName().c_str(), interface_idx);
	if (PLATFORM_OK != MakeQDisk(interface_idx)) {
		logger->Error("RegisterNET: MakeQDisk FAILED on device #%d (%s)",
			net.GetId(), net.GetName().c_str());
		return PLATFORM_GENERIC_ERROR;
	}
	if (PLATFORM_OK != MakeCLS(interface_idx)) {
		logger->Error("RegisterNET: MakeCLS FAILED on device #%d (%s)",
			net.GetId(), net.GetName().c_str());
		return PLATFORM_GENERIC_ERROR;
	}
#endif

	return PLATFORM_OK;
}

uint64_t LinuxPlatformProxy::GetNetIFBandwidth(const std::string &ifname) const
{
	int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (!sock) {
		throw std::runtime_error("Unable to create socket");
	}

	struct ifreq ifr;
	struct ethtool_cmd edata;

	strncpy(ifr.ifr_name, ifname.c_str(), sizeof (ifr.ifr_name));
	ifr.ifr_data = (caddr_t) & edata;
	edata.cmd = ETHTOOL_GSET;

	int err = ioctl(sock, SIOCETHTOOL, &ifr);
	if (err) {
		logger->Error("GetNetIFBandwidth: ioctl FAILED");
		return 0;
	}

	return ((uint64_t) edata.speed) * 1000000ULL;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::RegisterIODev(const PlatformDescription::IO &io_dev,
				  bool is_local) noexcept
{

	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	UNUSED(is_local);

	std::string resource_path = io_dev.GetPath();
	const auto bps = io_dev.GetBandwidth();
	logger->Debug("RegisterIODev: Registration of <%s>: %lu Kb",
		resource_path.c_str(), bps);

	if (refreshMode) {
		ra.UpdateResource(resource_path, "", bps);
	}
	else {
		// May save the returned resource path pointers to use them in MapResources
		bbque::res::ResourcePtr_t res_ptr = ra.RegisterResource(resource_path, "", bps);
#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO
		AddDevicePath(res_ptr->Path());
#endif
	}
	logger->Debug("RegisterIODev: Registration of <%s> successfully performed",
		resource_path.c_str());

	return PLATFORM_OK;
}

void LinuxPlatformProxy::InitPowerInfo(std::string const & resource_path, BBQUE_RID_TYPE core_id)
{

#ifdef CONFIG_TARGET_ARM_BIG_LITTLE
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	br::ResourcePtr_t rsrc(ra.GetResource(resource_path));
	if (high_perf_cores[core_id]) {
		rsrc->SetModel("ARM Cortex A15");
	}
	else {
		rsrc->SetModel("ARM Cortex A7");
		logger->Info("InitPowerInfo: [%s] CPU model = %s",
			rsrc->Path()->ToString().c_str(), rsrc->Model().c_str());
	}
#else
	UNUSED(core_id);
#endif


#ifdef CONFIG_BBQUE_WM
	PowerMonitor & wm(PowerMonitor::GetInstance());
	wm.Register(resource_path);
	logger->Debug("InitPowerInfo: [%s] registered for monitoring", resource_path.c_str());
#endif

}

#ifdef CONFIG_BBQUE_ENERGY_MONITOR

void LinuxPlatformProxy::RegisterForEnergyMonitoring(std::string const & resource_path)
{
	ResourceAccounter & ra(ResourceAccounter::GetInstance());
	auto resource_path_ptr = ra.GetPath(resource_path);
	if (resource_path_ptr == nullptr) {
		logger->Error("RegisterForEnergyMonitoring: invalid path specified <%s>",
			resource_path.c_str());
		return;
	}

	logger->Info("RegisterForEnergyMonitoring: <%s>...", resource_path.c_str());
	EnergyMonitor & eym(EnergyMonitor::GetInstance());
	eym.RegisterResource(resource_path_ptr);
}

#endif

/******************************************************************************
 * cgroup manipulation
 ******************************************************************************/

LinuxPlatformProxy::ExitCode_t LinuxPlatformProxy::InitCGroups() noexcept
{
	ExitCode_t pp_result;
	char *mount_path = NULL;
	int cg_result;

	// Init the Control Group Library
	cg_result = cgroup_init();
	if (BBQUE_UNLIKELY(cg_result)) {
		logger->Error("InitCGroups: CGroup Library initializaton FAILED! "
			"(Error: %d - %s)", cg_result, cgroup_strerror(cg_result));
		return PLATFORM_INIT_FAILED;
	}

	cg_result = cgroup_get_subsys_mount_point(controller, &mount_path);
	if (BBQUE_UNLIKELY(cg_result)) {
		logger->Error("InitCGroups: CGroup Library mountpoint lookup FAILED! "
			"(Error: %d - %s)", cg_result, cgroup_strerror(cg_result));
		return PLATFORM_GENERIC_ERROR;
	}
	logger->Info("InitCGroups: controller [%s] mounted at [%s]",
		controller, mount_path);


	// TODO: check that the "bbq" cgroup already existis
	// TODO: check that the "bbq" cgroup has CPUS and MEMS
	// TODO: keep track of overall associated resources => this should be done
	// by exploting the LoadPlatformData low-level method
	// TODO: update the MaxCpusCount and MaxMemsCount

	// Build "silos" CGroup to host blocked applications
	pp_result = BuildSilosCG(psilos);
	if (BBQUE_UNLIKELY(pp_result)) {
		logger->Error("InitCGroups: Silos CGroup setup FAILED!");
		return PLATFORM_GENERIC_ERROR;
	}

	free(mount_path);

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::BuildSilosCG(CGroupDataPtr_t &pcgd) noexcept
{
	RLinuxBindingsPtr_t prlb(new RLinuxBindings_t(
						MaxCpusCount, MaxMemsCount));
	ExitCode_t result;
	int error;

	logger->Debug("BuildSilosCG: Building SILOS CGroup...");

	// Build new CGroup data
	pcgd = CGroupDataPtr_t(new CGroupData_t(BBQUE_PP_LINUX_SILOS));
	result = BuildCGroup(pcgd);
	if (BBQUE_UNLIKELY(result != PLATFORM_OK))
		return result;

	// Setting up silos (limited) resources, just to run the RTLib
	sprintf(prlb->cpus, "0");
	sprintf(prlb->mems, "0");

	// Configuring silos constraints
	cgroup_set_value_string(pcgd->pc_cpuset, BBQUE_PP_LINUX_CPUS_PARAM, prlb->cpus);
	cgroup_set_value_string(pcgd->pc_cpuset, BBQUE_PP_LINUX_MEMN_PARAM, prlb->mems);

	// Updating silos constraints
	logger->Info("BuildSilosCG: Updating kernel CGroup [%s]", pcgd->cgpath);
	error = cgroup_modify_cgroup(pcgd->pcg);
	if (BBQUE_UNLIKELY(error)) {
		logger->Error("BuildSilosCG: CGroup resource mapping FAILED "
			"(Error: libcgroup, kernel cgroup update "
			"[%d: %s]", errno, strerror(errno));
		return PLATFORM_MAPPING_FAILED;
	}

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::BuildCGroup(CGroupDataPtr_t &pcgd) noexcept
{

	logger->Debug("BuildCGroup: building cgroup [%s]...", pcgd->cgpath);

	// Setup CGroup path for this application
	pcgd->pcg = cgroup_new_cgroup(pcgd->cgpath);
	if (BBQUE_UNLIKELY(!pcgd->pcg)) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, \"cgroup\" creation)");
		return PLATFORM_MAPPING_FAILED;
	}

	// cpuset controller
	pcgd->pc_cpuset = cgroup_add_controller(pcgd->pcg, "cpuset");
	if (BBQUE_UNLIKELY(!pcgd->pc_cpuset)) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, [cpuset] \"controller\" "
			"creation failed)");
		return PLATFORM_MAPPING_FAILED;
	}
	else {
		logger->Debug("BuildCGroup: added cpuset controller");
	}

#ifdef CONFIG_BBQUE_LINUX_CG_MEMORY
	// memory controller
	pcgd->pc_memory = cgroup_add_controller(pcgd->pcg, "memory");
	if (BBQUE_UNLIKELY(!pcgd->pc_memory)) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, [memory] \"controller\" "
			"creation failed)");
		return PLATFORM_MAPPING_FAILED;
	}
	else {
		logger->Debug("BuildCGroup: added memory controller");
	}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	// cpu controller
	pcgd->pc_cpu = cgroup_add_controller(pcgd->pcg, "cpu");
	if (BBQUE_UNLIKELY(!pcgd->pc_cpu)) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, [cpu] \"controller\" "
			"creation failed)");
		return PLATFORM_MAPPING_FAILED;
	}
	else {
		logger->Debug("BuildCGroup: added cpu controller");
	}

#endif

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
	// network interface controller
	pcgd->pc_net_cls = cgroup_add_controller(pcgd->pcg, "net_cls");
	if (!pcgd->pc_net_cls) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, [net_cls] \"controller\" "
			"creation failed)");
		return PLATFORM_MAPPING_FAILED;
	}
	else {
		logger->Info("BuildCGroup: added net_cls controller");
	}
#endif

#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO
	pcgd->pc_blkio = cgroup_add_controller(pcgd->pcg, "blkio");
	if (!pcgd->pc_blkio) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, [blkio] \"controller\" "
			"creation failed)");
		return PLATFORM_MAPPING_FAILED;
	}
	else {
		logger->Debug("BuildCGroup: added blkio controller");
	}
#endif

	// Create the kernel-space CGroup
	// NOTE: the current libcg API is quite confuse and unclear
	// regarding the "ignore_ownership" second parameter
	logger->Debug("BuildCGroup: cgroup [%s] created", pcgd->cgpath);
	int result = cgroup_create_cgroup(pcgd->pcg, 0);
	if (BBQUE_UNLIKELY(result && errno)) {
		logger->Error("BuildCGroup: CGroup resource mapping FAILED "
			"(Error: libcgroup, kernel cgroup creation "
			"[%d: %s]", errno, strerror(errno));
		cgroup_delete_cgroup(pcgd->pcg, 1); // As suggested by doc
		return PLATFORM_MAPPING_FAILED;
	}

	// Unfortunately we cannot use pcgd->pcg to check if the "cpu" controller
	// is available or not, so we have to open a new cgroup in order to check
	// this
	struct cgroup *temp_cg = cgroup_new_cgroup(pcgd->cgpath);
	assert(temp_cg != NULL);
	if (BBQUE_UNLIKELY(0 != cgroup_get_cgroup(temp_cg))) {
		logger->Error("BuildCGroup: Cannot re-open CGroup [%s], continuing with cpu quota"
			"disabled", pcgd->cgpath);
		pcgd->cfs_quota_available = false;
		return PLATFORM_OK;
	}

	struct cgroup_controller *temp_cgc = cgroup_get_controller(temp_cg, "cpu");
	for (int i = 0; i < cgroup_get_value_name_count(temp_cgc); i++) {
		const char* cg_c_name = cgroup_get_value_name(temp_cgc, i);
		if (0 == strcmp(cg_c_name, "cpu.cfs_quota_us")) {
			pcgd->cfs_quota_available = true;
			return PLATFORM_OK;
		}
	}

	pcgd->cfs_quota_available = false;

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::GetCGroupData(SchedPtr_t papp, CGroupDataPtr_t &pcgd) noexcept
{
	ExitCode_t result;
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	logger->Warn("Distributed cgroup actuation: cgroup will be written by "
		"the EXC itself.");
	return PLATFORM_OK;
#endif
	// Loop-up for application control group data
	pcgd = std::static_pointer_cast<CGroupData_t>(
		papp->GetPluginData(LINUX_PP_NAMESPACE, "cgroup")
		);
	if (pcgd)
		return PLATFORM_OK;

	// A new CGroupData must be setup for this app
	result = BuildAppCG(papp, pcgd);
	if (BBQUE_UNLIKELY(result != PLATFORM_OK))
		return result;

	// Keep track of this control group
	// FIXME check return value otherwise multiple BuildCGroup could be
	// called for the same application
	papp->SetPluginData(pcgd);

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::SetupCGroup(CGroupDataPtr_t & pcgd,
				RLinuxBindingsPtr_t prlb,
				bool excl,
				bool move) noexcept
{
#ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
	logger->Warn("SetupCGroup: Distributed cgroup actuation: cgroup will be setup by "
		"the EXC itself.");
	return PLATFORM_OK;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	int64_t cpus_quota = -1; // NOTE: use "-1" for no quota assignement
#endif
	int result;

	/**********************************************************************
	 *    CPUSET Controller
	 **********************************************************************/
#if 0
	// Setting CPUs as EXCLUSIVE if required
	if (excl) {
		cgroup_set_value_string(pcgd->pc_cpuset,
					BBQUE_PP_LINUX_CPU_EXCLUSIVE_PARAM, "1");
	}
#else
	excl = false;
#endif

	// Set the assigned CPUs
	cgroup_set_value_string(pcgd->pc_cpuset,
				BBQUE_PP_LINUX_CPUS_PARAM,
				prlb->cpus ? prlb->cpus : "");

	// Set the assigned memory NODE (only if we have at least one CPUS)
	if (prlb->cpus[0]) {
		cgroup_set_value_string(pcgd->pc_cpuset,
					BBQUE_PP_LINUX_MEMN_PARAM,
					prlb->mems);

		logger->Debug("SetupCGroup: CPUSET for [%s]: {cpus [%c: %s], mems[%s]}",
			pcgd->papp->StrId(),
			excl ? 'E' : 'S',
			prlb->cpus, prlb->mems);
	}
	else {
		logger->Debug("SetupCGroup: CPUSET for [%s]: {cpus [NONE], mems[NONE]}",
			pcgd->papp->StrId());
	}


	/**********************************************************************
	 *    MEMORY Controller
	 **********************************************************************/

#ifdef CONFIG_BBQUE_LINUX_CG_MEMORY
	assert(prlb->amount_memb >= -1);

	char quota[] = "9223372036854775807";
	// Set the assigned MEMORY amount
	if (prlb->amount_memb > 0)
		sprintf(quota, "%lu", prlb->amount_memb);
	else
		sprintf(quota, "-1");

	cgroup_set_value_string(pcgd->pc_memory, BBQUE_PP_LINUX_MEMB_PARAM, quota);

	logger->Debug("SetupCGroup: MEMORY for [%s]: {bytes_limit [%s]}",
		pcgd->papp->StrId(), quota);
#endif

	/**********************************************************************
	 *    CPU Quota Controller
	 **********************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)

	uint32_t cfs_period_us = BBQUE_PP_LINUX_CPUP_MAX;
	char const *cfs_c = std::to_string(cfs_period_us).c_str();

	if (BBQUE_LIKELY(pcgd->cfs_quota_available)) {
		bool quota_enforcing = true;
		// Set the default CPU bandwidth period
		cgroup_set_value_string(pcgd->pc_cpu, BBQUE_PP_LINUX_CPUP_PARAM, cfs_c);

		// Set the assigned CPU bandwidth amount
		// NOTE: if a quota is NOT assigned we have amount_cpus="0", but this
		// is not acceptable by the CFS controller, which requires a negative
		// number to remove any constraint.
		assert(cpus_quota == -1);
		if (!prlb->amount_cpus) {
			quota_enforcing = false;
		}

		// CFS quota to enforced is
		// assigned + (margin * #PEs)
		cpus_quota = prlb->amount_cpus;
		cpus_quota += ((cpus_quota / 100) + 1) * cfs_margin_pct;
		if ((cpus_quota % 100) > cfs_threshold_pct) {
			logger->Warn("SetupCGroup: CFS (quota+margin) %d > %d "
				"threshold, enforcing disabled",
				cpus_quota, cfs_threshold_pct);
			quota_enforcing = false;
		}

		if (quota_enforcing) {
			cpus_quota = (cfs_period_us / 100) * prlb->amount_cpus;
			cgroup_set_value_int64(
					pcgd->pc_cpu,
					BBQUE_PP_LINUX_CPUQ_PARAM,
					cpus_quota);

			logger->Debug("SetupCGroup: CPU for [%s]: {period [%s], quota [%lu]}",
				pcgd->papp->StrId(),
				cfs_c,
				cpus_quota);
		}
		else {
			logger->Debug("SetupCGroup: CPU for [%s]: {period [%s], quota [-]}",
				pcgd->papp->StrId(),
				cfs_c);
		}
	}
	else {
		logger->Warn("SetupCGroup: CFS quota enforcement not supported by the kernel");
	}
#endif

	/**********************************************************************
	 *    Block I/O Controller
	 **********************************************************************/
#ifdef CONFIG_BBQUE_LINUX_CG_BLKIO

	char quota[30];
	for (auto r_dev : prlb->read_devs) {
		br::ResourcePathPtr_t dev_path = r_dev.first;
		int_fast64_t amount = r_dev.second;

		std::string dev = GetDevFromPath(dev_path);

		// Set the assigned READ BANDWIDTH amount
		if (dev.compare(""))
			sprintf(quota, "%s %lu", dev.c_str(), amount << 20); // The bandwidth is stored in MB/s, the blkio parameter is in Bps.
		else {
			logger->Warn("SetupCGroup: dev not found for %s, search returned %s", dev_path->ToString().c_str(), dev.c_str());
			return PLATFORM_MAPPING_FAILED;
		}

		cgroup_set_value_string(pcgd->pc_blkio, BBQUE_PP_LINUX_R_IO_PARAM, quota);

		logger->Debug("SetupCGroup: READ BANDWIDTH for [%s]: {maj:min bytes_limit [%s] bps}",
			pcgd->papp->StrId(), quota);
	}

	for (auto w_dev : prlb->write_devs) {
		br::ResourcePathPtr_t dev_path = w_dev.first;
		int_fast64_t amount = w_dev.second;

		std::string dev = GetDevFromPath(dev_path);

		// Set the assigned WRITE BANDWIDTH amount
		if (dev.compare(""))
			sprintf(quota, "%s %lu", dev.c_str(), amount << 20); // The bandwidth is stored in MB/s, the blkio parameter is in Bps.
		else {
			logger->Warn("SetupCGroup: dev not found for %s, search returned %s", dev_path->ToString().c_str(), dev.c_str());
			return PLATFORM_MAPPING_FAILED;
		}

		cgroup_set_value_string(pcgd->pc_blkio, BBQUE_PP_LINUX_W_IO_PARAM, quota);

		logger->Debug("SetupCGroup: WRITE BANDWIDTH for [%s]: {maj:min bytes_limit [%s] bps}",
			pcgd->papp->StrId(), quota);
	}
#endif

	/**********************************************************************
	 *    CGroup Configuration
	 **********************************************************************/

	logger->Debug("SetupCGroup: Updating cgroup [%s]", pcgd->cgpath);
	bbque_assert(pcgd->pcg);
	result = cgroup_modify_cgroup(pcgd->pcg);
	if (BBQUE_UNLIKELY(result)) {
		logger->Error("SetupCGroup: cgroup resource mapping FAILED "
			"(Error: libcgroup, kernel cgroup update "
			"[%d: %s])", errno, strerror(errno));
		return PLATFORM_MAPPING_FAILED;
	}

	/* If a task has not been assigned, we are done */
	if (!move)
		return PLATFORM_OK;

	/**********************************************************************
	 *    CGroup Task Assignment
	 **********************************************************************/
	// NOTE: task assignement must be done AFTER CGroup configuration, to
	// ensure all the controller have been properly setup to manage the
	// task. Otherwise a task could be killed if being assigned to a
	// CGroup not yet configure.

	logger->Info("SetupCGroup: [%s] => {cpus [%s: %ld], mems[%s: %ld B]}",
		pcgd->papp->StrId(),
		prlb->cpus,
		prlb->amount_cpus,
		prlb->mems,
		prlb->amount_memb);
	for (auto r_dev : prlb->read_devs){
		logger->Info("SetupCGroup: [%s] => {read_bw [%s: %ld MB/s]}",
			pcgd->papp->StrId(),
			r_dev.first->ToString().c_str(),
			r_dev.second);
	}

	for (auto w_dev : prlb->write_devs){
		logger->Info("SetupCGroup: [%s] => {write_bw [%s: %ld MB/s]}",
			pcgd->papp->StrId(),
			w_dev.first->ToString().c_str(),
			w_dev.second);
	}
	
	cgroup_set_value_uint64(
				pcgd->pc_cpuset, BBQUE_PP_LINUX_PROCS_PARAM, pcgd->papp->Pid());

	logger->Debug("SetupCGroup: Updating cgroup [%s]", pcgd->cgpath);
	result = cgroup_modify_cgroup(pcgd->pcg);
	if (BBQUE_UNLIKELY(result)) {
		logger->Error("SetupCGroup: cgroup resource mapping FAILED "
			"(Error: libcgroup, kernel cgroup update "
			"[%d: %s])", errno, strerror(errno));
		return PLATFORM_MAPPING_FAILED;
	}

	return PLATFORM_OK;
}

LinuxPlatformProxy::ExitCode_t
LinuxPlatformProxy::BuildAppCG(SchedPtr_t papp, CGroupDataPtr_t &pcgd) noexcept
{
	// Build new CGroup data for the specified application
	pcgd = CGroupDataPtr_t(new CGroupData_t(papp));
	return BuildCGroup(pcgd);
}


#ifdef CONFIG_BBQUE_RELIABILITY

ReliabilityActionsIF::ExitCode_t
LinuxPlatformProxy::Dump(app::SchedPtr_t psched)
{
	logger->Debug("Dump: [%s] checkpoint [pid=%d]... (user=%d)",
		psched->StrId(), psched->Pid(), getuid());

	std::string image_dir(ApplicationPath(
					image_prefix_dir,
					psched->Pid(),
					psched->Name()));
	if (!boost::filesystem::exists(image_dir)) {
		logger->Debug("Dump: [%s] creating directory [%s]",
			psched->StrId(), image_dir.c_str());
		boost::filesystem::create_directory(image_dir);
	}

	// CRIU image directory
	int fd = open(image_dir.c_str(), O_DIRECTORY);
	if (fd < 0) {
		logger->Warn("Dump: [%s] image directory [%s] not accessible",
			psched->StrId(), image_dir.c_str());
		perror("CRIU");
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}
	else {
		logger->Debug("Dump: [%s] image directory [%s] open",
			psched->StrId(), image_dir.c_str());
	}

	// CRIU settings
	criu_set_images_dir_fd(fd);
	criu_set_log_level(4);
	criu_set_log_file("dump.log");
	criu_set_pid(psched->Pid());
	criu_set_leave_running(true);
	criu_set_shell_job(true);

	// Checkpoint image dump
	int c_ret = criu_dump();
	switch (c_ret) {
	case -EBADE:
		logger->Error("Dump: [%s] CRIU: RPC error", psched->StrId());
		break;
	case -ECONNREFUSED:
		logger->Error("Dump: [%s] CRIU: connection refused (%s)",
			psched->StrId(),
			strerror(c_ret));
		break;
	case -ECOMM:
		logger->Error("Dump: [%s] CRIU: unable to send/receive "
			"messages (%s)",
			psched->StrId(),
			strerror(c_ret));
		break;
	case -EBADMSG:
		logger->Error("Dump: [%s] CRIU: unexpected response",
			psched->StrId());
		break;
	}

	if (c_ret < 0) {
		logger->Error("Dump: [%s] checkpointing failed", psched->StrId());
		close(fd);
		return ReliabilityActionsIF::ExitCode_t::ERROR_UNKNOWN;
	}

	close(fd);
	logger->Info("Dump: [%s] checkpoint done [image_dir=%s]",
		psched->StrId(), image_dir.c_str());
	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
LinuxPlatformProxy::Restore(uint32_t pid, std::string exe_name)
{
	// Retrieve checkpoint image directory
	std::string image_dir(image_prefix_dir
			+ "/" + std::to_string(pid)
			+ "_" + exe_name);

	logger->Debug("Restore: [pid=%d] recovering checkpoint from = [%s]",
		pid, image_dir.c_str());

	if (!boost::filesystem::exists(image_dir)) {
		logger->Debug("Restore: [pid=%d] missing directory [%s]",
			pid, image_dir.c_str());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}

	int fd = open(image_dir.c_str(), O_DIRECTORY);
	if (fd < 0) {
		logger->Warn("Restore: [pid=%d] image directory [%s] not accessible",
			pid, image_dir.c_str());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}
	else {
		logger->Debug("Restore: [pid=%d] image directory [%s] open",
			pid, image_dir.c_str());
	}

	// CRIU settings
	criu_set_images_dir_fd(fd);
	criu_set_log_level(4);
	criu_set_log_file("restore.log");
	criu_set_pid(pid);
	criu_set_ext_unix_sk(true);
	criu_set_tcp_established(true);
	criu_set_evasive_devices(true);
	criu_set_file_locks(true);
	//	criu_set_shell_job(true);

	logger->Info("Restore: [pid=%d] restoring...", pid);

	// Do restore
	int c_ret = criu_restore_child();
	if (c_ret < 0) {
		logger->Error("Restore: [pid=%d] error=%d", pid, c_ret);
		return ReliabilityActionsIF::ExitCode_t::ERROR_UNKNOWN;
	}

	// Wait for the restored process to be back to run
	logger->Info("Restore: [pid=%d] waiting for restore...", pid);
	unsigned short int nr_attempts = 10;
	bool resumed = false;
	do {
		if (kill(pid, 0)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			nr_attempts--;
			logger->Debug("Restore: [pid=%d] attempts left = %d",
				pid, nr_attempts);
			resumed = false;
		}
		else {
			resumed = true;
		}
		logger->Debug("Restore: [pid=%d] resumed checked = %d",
			pid, resumed);
	}
	while (!resumed && (nr_attempts > 0));

	if (!resumed) {
		logger->Error("Restore: [pid=%d] failed", pid);
		return ReliabilityActionsIF::ExitCode_t::ERROR_UNKNOWN;
	}
	logger->Info("Restore: [pid=%d] execution resumed", pid);

	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
LinuxPlatformProxy::Freeze(app::SchedPtr_t psched)
{
	std::string freezer_dir(ApplicationPath(freezer_prefix_dir,
						psched->Pid(),
						psched->Name()));
	logger->Debug("Freeze: [%s] freezer directory = [%s]",
		psched->StrId(), freezer_dir.c_str());

	if (!boost::filesystem::exists(freezer_dir)) {
		logger->Debug("Freeze: [%s] creating directory [%s]",
			psched->StrId(), freezer_dir.c_str());
		boost::filesystem::create_directory(freezer_dir);
	}

	// add the task to the freeze
	std::string freezer_tasks(freezer_dir + "/cgroup.procs");
	std::ofstream tasks_ofs(freezer_tasks, std::ofstream::out);
	try {
		tasks_ofs << psched->Pid();
		tasks_ofs.close();
	}
	catch (...) {
		tasks_ofs.close();
		logger->Error("Freeze: [%s] filesystem error", psched->StrId());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}

	// change state to frozen
	std::string freezer_attr(freezer_dir + BBQUE_PP_LINUX_FREEZER_STATE);
	std::ofstream fstate_ofs(freezer_attr, std::ofstream::out);
	try {
		fstate_ofs << "FROZEN";
		fstate_ofs.close();
	}
	catch (...) {
		fstate_ofs.close();
		logger->Error("Freeze: [%s] filesystem error", psched->StrId());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}

	return ReliabilityActionsIF::ExitCode_t::OK;
}

ReliabilityActionsIF::ExitCode_t
LinuxPlatformProxy::Thaw(app::SchedPtr_t psched)
{
	std::string freezer_dir(ApplicationPath(freezer_prefix_dir,
						psched->Pid(),
						psched->Name()));
	logger->Debug("Thaw: [%s] freezer directory = [%s]",
		psched->StrId(), freezer_dir.c_str());

	if (!boost::filesystem::exists(freezer_dir)) {
		logger->Error("Thaw: [%s] not frozen", psched->StrId());
		return ReliabilityActionsIF::ExitCode_t::ERROR_PROCESS_ID;
	}

	std::string freezer_attr(freezer_dir + BBQUE_PP_LINUX_FREEZER_STATE);
	std::ofstream fofs(freezer_attr, std::ofstream::out);
	try {
		fofs << "THAWED";
		fofs.close();
	}
	catch (...) {
		fofs.close();
		logger->Error("Thaw: [%s] filesystem error", psched->StrId());
		return ReliabilityActionsIF::ExitCode_t::ERROR_FILESYSTEM;
	}

	return ReliabilityActionsIF::ExitCode_t::OK;
}

#endif

} // namespace pp
} // namespace bbque
