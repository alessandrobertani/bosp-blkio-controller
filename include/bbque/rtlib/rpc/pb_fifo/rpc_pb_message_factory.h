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

};

} } // Close namespaces


#endif //AOS_PROJECT_MESSAGE_FACTORY_H
