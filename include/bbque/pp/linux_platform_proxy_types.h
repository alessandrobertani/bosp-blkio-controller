#ifndef LINUX_PLATFORM_PROXY_TYPES_H
#define LINUX_PLATFORM_PROXY_TYPES_H

#ifndef LINUX_PP_NAMESPACE
#error Please do not include directly this file, but you have to\
include linux_platform_proxy.h!
#endif

#include "bbque/app/schedulable.h"

#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH

//#include <netlink/libnetlink.h>
#include <netlink/netlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

#include <cstdint>
#include <memory>
#include <libcgroup.h>

/**
 * @brief The cgroup expected to assign resources to the BarbequeRTRM
 *
 * Resources which are assigned to BarbequeRTRM for resource management
 * are expected to be define under this control group.
 * @note this CGroup should be given both "task" and "admin" permissions to
 * the UID used to run the BarbequeRTRM
 */
#define BBQUE_PP_LINUX_CGROUP "user.slice"

/**
 * @brief The cgroup expected to define resources clusterization
 *
 * Resources assigned to BarbequeRTRM can be grouped into clusters, in a NUMA
 * machine a cluster usually correspond to a "node". The BarbequeRTRM will
 * consider this clusterization when scheduling applications by trying to keep
 * each application within a single cluster.
 */
#define BBQUE_PP_LINUX_RESOURCES BBQUE_PP_LINUX_CGROUP"/res"

/**
 * @brief The CGroup expected to define Clusters
 *
 * Resources managed by BarbequeRTRM are clusterized by grouping available
 * platform resources into CGroups which name start with this radix.
 */
#define BBQUE_PP_LINUX_CLUSTER "node"

#define BBQUE_PP_LINUX_FREEZER_STATE "/freezer.state"


namespace bbque {
namespace pp {

/**
 * @brief Resource assignment bindings on a Linux machine
 */
struct RLinuxBindings_t
{
	unsigned short node_id = 0; /** Computing node, e.g. processor */
	char *cpus = NULL; /** Processing elements / CPU cores assigned */
	char *mems = NULL; /** Memory nodes assigned */
	char *memb = NULL; /** Memory limits in bytes */
	uint_fast32_t amount_cpus = 0; /** Percentage of CPUs time assigned */
	int_fast64_t amount_memb = 0; /** Amount of socket MEMORY assigned (byte) */
	int_fast64_t amount_net_bw = 0; /** Amount of network bandwidth assigned (bps) */

	RLinuxBindings_t(const uint_fast8_t MaxCpusCount,
			const uint_fast8_t MaxMemsCount)
	{
		// 3 chars are required for each CPU/MEM resource if formatted
		// with syntax: "nn,". This allows for up-to 99 resources per
		// cluster
		if (MaxCpusCount) {
			cpus = new char[3 * MaxCpusCount]();
			cpus[0] = 0;
		}
		if (MaxMemsCount) {
			mems = new char[3 * MaxMemsCount]();
			mems[0] = 0;
		}
	}

	~RLinuxBindings_t()
	{
		delete [] cpus;
		delete [] mems;
		if (memb != NULL)
			delete memb;
	}
};

using RLinuxBindingsPtr_t = std::shared_ptr<RLinuxBindings_t>;

struct CGroupData_t : public bbque::utils::PluginData_t
{
	bbque::app::SchedPtr_t papp; /** The controlled application */
#define BBQUE_PP_LINUX_CGROUP_PATH_MAX 128 // "user.slice/res/12345:ABCDEF:00";
	char cgpath[BBQUE_PP_LINUX_CGROUP_PATH_MAX];
	struct cgroup *pcg;
	struct cgroup_controller *pc_cpu;
	struct cgroup_controller *pc_cpuset;
	struct cgroup_controller *pc_memory;
	struct cgroup_controller *pc_net_cls;
	struct cgroup_controller *pc_blkio;

	bool cfs_quota_available = false; /** Target system supports CFS quota management? */

	CGroupData_t(bbque::app::SchedPtr_t sched_app) :
	    bu::PluginData_t(LINUX_PP_NAMESPACE, "cgroup"),
	    papp(sched_app), pcg(NULL), pc_cpu(NULL),
	    pc_cpuset(NULL), pc_memory(NULL)
	{
		snprintf(cgpath, BBQUE_PP_LINUX_CGROUP_PATH_MAX,
			BBQUE_PP_LINUX_RESOURCES"/%s",
			papp->StrId());
	}

	CGroupData_t(const char *cgp) :
	    bu::PluginData_t(LINUX_PP_NAMESPACE, "cgroup"),
	    pcg(NULL), pc_cpu(NULL),
	    pc_cpuset(NULL), pc_memory(NULL)
	{
		snprintf(cgpath, BBQUE_PP_LINUX_CGROUP_PATH_MAX,
			"%s", cgp);
	}

	~CGroupData_t()
	{
		if (pcg != NULL) {
			// Removing kernel cgroup
			cgroup_delete_cgroup(pcg, 1);
			// Releasing libcgroup resources
			cgroup_free(&pcg);
		}
	}

};

using CGroupDataPtr_t = std::shared_ptr<CGroupData_t>;


#ifdef CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH
// TODO check this size (it seems too high)
#define MAX_MSG 16384 // 2^14

/**
 * @brief The netlink communication structure
 *	  It contains the file descriptors used in communication with the kernel
 *	  and the socket address
 */
typedef struct NetworkInfo
{
	struct rtnl_handle rth_1;
	struct rtnl_handle rth_2;
	struct sockaddr_nl kernel_addr;
} NetworkInfo_t;

typedef struct Requests
{
	struct nlmsghdr n;
	struct tcmsg t;
	char buf[MAX_MSG];
} NetworkKernelRequest_t;

#endif // CONFIG_BBQUE_LINUX_CG_NET_BANDWIDTH


} // namespace pp

} // namespace bbque

#endif // LINUX_PLATFORM_PROXY_TYPES_H
