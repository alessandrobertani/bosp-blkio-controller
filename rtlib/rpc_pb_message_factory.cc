// rpc_pb_message_factory.cc

#include "bbque/rtlib/rpc/pb_fifo/rpc_pb_message_factory.h"


namespace bbque { namespace rtlib {


void PBMessageFactory::pb_set_header(PB_rpc_msg & msg, uint32_t typ, uint32_t token, int32_t app_pid, uint8_t exc_id) {
    msg.mutable_hdr()->set_typ(typ);
    msg.mutable_hdr()->set_token(token);
    msg.mutable_hdr()->set_app_pid(app_pid);
    msg.mutable_hdr()->set_exc_id(exc_id);
}


void PBMessageFactory::struct_set_header(rpc_msg_header_t *struct_hdr, const PB_rpc_msg_header & pb_hdr) {
    struct_hdr->typ = (uint8_t)pb_hdr.typ();
    struct_hdr->token = pb_hdr.token();
    struct_hdr->app_pid = pb_hdr.app_pid();
    struct_hdr->exc_id = (uint8_t)pb_hdr.exc_id();
}


/*std::shared_ptr<PB_rpc_msg> MessageFactory::struct2pb(void *buffer) {
    rpc_msg_header_t *hdr = buffer

    if (hdr->typ == RPC_APP_EXIT || hdr->typ == RPC_EXC_CLEAR || hdr->typ == RPC_EXC_START || hdr->typ == RPC_EXC_STOP || hdr->typ == RPC_EXC_SCHEDULE) {
        return generic_msg(hdr->typ, hdr->token, hdr->app_pid, hdr->exc_id);
    } else if (hdr->typ == RPC_APP_PAIR) {
        rpc_msg_APP_PAIR_t *msg = buffer;
        return app_pair(hdr->token, hdr->app_pid, hdr->exc_id, msg->mjr_version, msg->mnr_version, msg->app_name);
    } else if (hdr->typ == RPC_EXC_REGISTER) {
        rpc_msg_EXC_REGISTER_t *msg = buffer;
        return exc_register(hdr->token, hdr->app_pid, hdr->exc_id, msg->exc_name, msg->recipe, msg->lang);
    } else if (hdr->typ == RPC_EXC_UNREGISTER) {
        rpc_msg_EXC_UNREGISTER_t *msg = buffer;
        return exc_unregister(hdr->token, hdr->app_pid, hdr->exc_id, msg->exc_name);
    } else if (hdr->typ == RPC_EXC_SET) {
        // TODO
    } else if (hdr->typ == RPC_EXC_RTNOTIFY) {
        rpc_msg_EXC_RTNOTIFYt *msg = buffer;
        return exc_rtnotify(hdr->token, hdr->app_pid, hdr->exc_id, msg->gap, msg->cusage, msg->ctime_ms);
    } else if (hdr->typ == RPC_BBQ_SYNCP_PRECHANGE) {
        rpc_msg_BBQ_SYNCP_PRECHANGE_t *msg = buffer;
        #ifdef CONFIG_BBQUE_CGROUPS_DISTRIBUTED_ACTUATION
        return bbq_syncp_prechange(hdr->token, hdr->app_pid, hdr->exc_id, msg->event, msg->awm, msg->cpu_ids, msg->cpu_ids_isolation, msg->mem_ids);
        #endif
        return bbq_syncp_prechange(hdr->token, hdr->app_pid, hdr->exc_id, msg->event, msg->awm, 0, 0, 0);
        // TODO subsequent systems
    } else if (hdr->typ ==)
}


std::shared_ptr<PB_rpc_msg> MessageFactory::generic_msg(uint32_t typ, uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = std::make_shared<PB_rpc_msg>();
    msg->mutable_hdr()->set_typ(typ);
    msg->mutable_hdr()->set_token(token);
    msg->mutable_hdr()->set_app_pid(app_pid);
    msg->mutable_hdr()->set_exc_id(exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::app_pair(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t mjr_version, uint32_t mnr_version, std::string app_name) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_APP_PAIR, token, app_pid, exc_id);
    msg->set_mjr_version(mjr_version);
    msg->set_mnr_version(mnr_version);
    msg->set_app_name(app_name);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::app_exit(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_APP_EXIT, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_register(uint32_t token, int32_t app_pid, uint8_t exc_id, std::string exc_name, std::string recipe, uint32_t lang) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_REGISTER, token, app_pid, exc_id);
    msg->set_exc_name(exc_name);
    msg->set_recipe(recipe);
    msg->set_lang(lang);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_unregister(uint32_t token, int32_t app_pid, uint8_t exc_id, std::string exc_name) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_UNREGISTER, token, app_pid, exc_id);
    msg->set_exc_name(exc_name);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_set(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_SET, token, app_pid, exc_id);
    return msg;
}

void MessageFactory::exc_set_add_constraint(std::shared_ptr<PB_rpc_msg> msg, uint32_t awm, uint32_t op, uint32_t type) {
    PB_constraint *constraint = msg->add_constraints();
    constraint->set_awm(awm);
    constraint->set_operation(op);
    constraint->set_type(type);
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_clear(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_CLEAR, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_rtnotify(uint32_t token, int32_t app_pid, uint8_t exc_id, int32_t gap, int32_t cusage, int32_t ctime_ms) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_RTNOTIFY, token, app_pid, exc_id);
    msg->set_gap(gap);
    msg->set_cusage(cusage);
    msg->set_ctime_ms(ctime_ms);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_start(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_START, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_stop(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_STOP, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::exc_schedule(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_EXC_SCHEDULE, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_prechange(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t event, int32_t awm, uint64_t cpu_ids, uint64_t cpu_ids_isolation, uint64_t mem_ids) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_SYNCP_PRECHANGE, token, app_pid, exc_id);
    msg->set_event(event);
    msg->set_awm(awm);
    msg->set_cpu_ids(cpu_ids);
    msg->set_cpu_ids_isolation(cpu_ids_isolation);
    msg->set_mem_ids(mem_ids);
    return msg;
}

void MessageFactory::bbq_syncp_prechange_system(std::shared_ptr<PB_rpc_msg> msg, int32_t sys_id, int32_t nr_cpus, int32_t nr_procs, int32_t r_proc, int32_t r_mem, int32_t r_gpu, int32_t r_acc, int32_t dev) {
    PB_rpc_msg_BBQ_SYNCP_PRECHANGE_SYSTEM *system = msg->add_systems();
    system->set_sys_id(sys_id);
    system->set_nr_cpus(nr_cpus);
    system->set_nr_procs(nr_procs);
    system->set_r_proc(r_proc);
    system->set_r_mem(r_mem);
    system->set_r_gpu(r_gpu);
    system->set_r_acc(r_acc);
    system->set_dev(dev);
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_prechange_resp(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t synclatency, uint32_t result) {
    std::shared_ptr<PB_rpc_msg> resp = generic_msg(RPC_BBQ_RESP, token, app_pid, exc_id);
    resp->set_synclatency(synclatency);
    resp->set_result(result);
    return resp;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_syncchange(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_SYNCP_SYNCCHANGE, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_syncchange_resp(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t result) {
    std::shared_ptr<PB_rpc_msg> resp = generic_msg(RPC_BBQ_RESP, token, app_pid, exc_id);
    resp->set_result(result);
    return resp;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_dochange(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_SYNCP_DOCHANGE, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_postchange(uint32_t token, int32_t app_pid, uint8_t exc_id) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_SYNCP_POSTCHANGE, token, app_pid, exc_id);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_syncp_postchange_resp(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t result) {
    std::shared_ptr<PB_rpc_msg> resp = generic_msg(RPC_BBQ_RESP, token, app_pid, exc_id);
    resp->set_result(result);
    return resp;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_stop(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t timeout) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_STOP_EXECUTION, token, app_pid, exc_id);
    msg->set_timeout(timeout);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_get_profile(uint32_t token, int32_t app_pid, uint8_t exc_id, bool is_ocl) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_GET_PROFILE, token, app_pid, exc_id);
    msg->set_is_ocl(is_ocl);
    return msg;
}

std::shared_ptr<PB_rpc_msg> MessageFactory::bbq_get_profile_resp(uint32_t token, int32_t app_pid, uint8_t exc_id, uint32_t exec_time, uint32_t mem_time) {
    std::shared_ptr<PB_rpc_msg> msg = generic_msg(RPC_BBQ_RESP, token, app_pid, exc_id);
    msg->set_exec_time(exec_time);
    msg->set_mem_time(mem_time);
    return msg;
}*/


} } // Close namespaces
