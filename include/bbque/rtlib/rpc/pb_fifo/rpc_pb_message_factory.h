// rpc_pb_message_factory.h

#ifndef AOS_PROJECT_MESSAGE_FACTORY_H
#define AOS_PROJECT_MESSAGE_FACTORY_H

//#include <memory>
#include "rpc_messages.pb.h"
#include "bbque/rtlib/rpc/rpc_messages.h"


namespace bbque { namespace rtlib {


class PBMessageFactory {

public:

    static void pb_set_header(PB_rpc_msg & msg, uint32_t typ, uint32_t token, int32_t app_pid, uint8_t exc_id);

    static void struct_set_header(rpc_msg_header_t *struct_hdr, const PB_rpc_msg_header & pb_hdr);

    #define RPC_STRUCT_HDR(msg) \
        (uint8_t)msg.hdr().typ(), \
        msg.hdr().token(), \
        msg.hdr().app_pid(), \
        (uint8_t)msg.hdr().exc_id()

    //static rpc_msg_header_t p2s_rpc_msg_header(PB_rpc_msg_header & hdr);

    //static rpc_msg_BBQ_GET_PROFILE_t p2s_rpc_msg_BBQ_GET_PROFILE(PB_rpc_msg & msg);

    /*static std::shared_ptr<PB_rpc_msg> struct2pb(rpc_msg_header_t *hdr);

    static std::shared_ptr<PB_rpc_msg> generic_msg(uint32_t typ,
                                                   uint32_t token,
                                                   int32_t app_pid,
                                                   uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> app_pair(uint32_t token,
                                                int32_t app_pid,
                                                uint8_t exc_id,
                                                uint32_t mjr_version,
                                                uint32_t mnr_version,
                                                std::string app_name);

    static std::shared_ptr<PB_rpc_msg> app_exit(uint32_t token,
                                                int32_t app_pid,
                                                uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> exc_register(uint32_t token,
                                                    int32_t app_pid,
                                                    uint8_t exc_id,
                                                    std::string exc_name,
                                                    std::string recipe,
                                                    uint32_t lang);

    static std::shared_ptr<PB_rpc_msg> exc_unregister(uint32_t token,
                                                      int32_t app_pid,
                                                      uint8_t exc_id,
                                                      std::string exc_name);

    static std::shared_ptr<PB_rpc_msg> exc_set(uint32_t token,
                                               int32_t app_pid,
                                               uint8_t exc_id);

    static void exc_set_add_constraint(std::shared_ptr<PB_rpc_msg> msg,
                                       uint32_t awm,
                                       uint32_t op,
                                       uint32_t type);

    static std::shared_ptr<PB_rpc_msg> exc_clear(uint32_t token,
                                                 int32_t app_pid,
                                                 uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> exc_rtnotify(uint32_t token,
                                                    int32_t app_pid,
                                                    uint8_t exc_id,
                                                    int32_t gap,
                                                    int32_t cusage,
                                                    int32_t ctime_ms);

    static std::shared_ptr<PB_rpc_msg> exc_start(uint32_t token,
                                                 int32_t app_pid,
                                                 uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> exc_stop(uint32_t token,
                                                int32_t app_pid,
                                                uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> exc_schedule(uint32_t token,
                                                    int32_t app_pid,
                                                    uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_prechange(uint32_t token,
                                                           int32_t app_pid,
                                                           uint8_t exc_id,
                                                           uint32_t event,
                                                           int32_t awm,
                                                           uint64_t cpu_ids,
                                                           uint64_t cpu_ids_isolation,
                                                           uint64_t mem_ids);

    void bbq_syncp_prechange_system(std::shared_ptr<PB_rpc_msg> msg,
                                    int32_t sys_id,
                                    int32_t nr_cpus,
                                    int32_t nr_procs,
                                    int32_t r_proc,
                                    int32_t r_mem,
                                    int32_t r_gpu,
                                    int32_t r_acc,
                                    int32_t dev);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_prechange_resp(uint32_t token,
                                                                int32_t app_pid,
                                                                uint8_t exc_id,
                                                                uint32_t synclatency,
                                                                uint32_t result);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_syncchange(uint32_t token,
                                                            int32_t app_pid,
                                                            uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_syncchange_resp(uint32_t token,
                                                                 int32_t app_pid,
                                                                 uint8_t exc_id,
                                                                 uint32_t result);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_dochange(uint32_t token,
                                                          int32_t app_pid,
                                                          uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_postchange(uint32_t token,
                                                            int32_t app_pid,
                                                            uint8_t exc_id);

    static std::shared_ptr<PB_rpc_msg> bbq_syncp_postchange_resp(uint32_t token,
                                                                 int32_t app_pid,
                                                                 uint8_t exc_id,
                                                                 uint32_t result);

    static std::shared_ptr<PB_rpc_msg> bbq_stop(uint32_t token,
                                                int32_t app_pid,
                                                uint8_t exc_id,
                                                uint32_t timeout);

    static std::shared_ptr<PB_rpc_msg> bbq_get_profile(uint32_t token,
                                                       int32_t app_pid,
                                                       uint8_t exc_id,
                                                       bool is_ocl);

    static std::shared_ptr<PB_rpc_msg> bbq_get_profile_resp(uint32_t token,
                                                            int32_t app_pid,
                                                            uint8_t exc_id,
                                                            uint32_t exec_time,
                                                            uint32_t mem_time);*/

};

} } // Close namespaces


#endif //AOS_PROJECT_MESSAGE_FACTORY_H
