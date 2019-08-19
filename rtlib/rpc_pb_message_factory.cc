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


} } // Close namespaces
