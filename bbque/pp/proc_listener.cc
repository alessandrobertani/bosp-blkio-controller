/*
 * Copyright (C) 2016  Politecnico di Milano
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

#include "bbque/pp/proc_listener.h"
#include <cstdlib>
#include <linux/cn_proc.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <sstream>
#include <fstream>

#define MODULE_NAMESPACE "bq.pp.linux_ps"

namespace bbque {

ProcessListener & ProcessListener::GetInstance() {
	static ProcessListener instance;
	return instance;
}

std::string ProcessListener::GetProcName(int pid) {
	std::stringstream ss;
	std::string pname("");
	ss << "/proc/" << pid << "/comm";
	try {
		std::ifstream ifs(ss.str());
		ifs >> pname;
		ifs.close();
	}
	catch(std::exception & ex) {
		logger->Debug("GetProcName: %s", ex.what());
	}
	return pname;
}

ProcessListener::ProcessListener():
		prm(ProcessManager::GetInstance()) {
	sock = -1;
	buffSize = getpagesize();
	buf = new char[buffSize];
	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	logger->Info("Linux Process Listener Started");

	// Setup the worker thread (calling Task())
	Worker::Setup(BBQUE_MODULE_NAME("pp.linux_ps"), MODULE_NAMESPACE);

	/*
	 * Initialization of Linux Connector Channel
	 */
	//Socket creation (Datagram)
	sock = socket (PF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
		NETLINK_CONNECTOR);
	//Socket Binding
	sockaddr_nl addr;
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid(); //current process's pid
	addr.nl_groups = CN_IDX_PROC;
	bind(sock, (struct sockaddr *)&addr, sizeof addr);
	if (errno == EPERM){
		logger->Error("Linux Process Listener does not have the proper"
			" permission to connect the socket");
	}
	/*
	 * The proc connector doesn't send any messages until a process has
	 * subscribed to it. So we have to send a subscription message.
	 * Sending that subscription message also involves embedded a message,
	 * inside a message inside a message.
	 *
	 * Note that the proc connector interface is built on the more generic
	 * connector API, which itself is built on the generic netlink API
	 * Since we’re nesting a proc connector operation message inside a
	 * connector message inside a netlink message, it’s easiest to use a
	 * struct iovec for this kind of thing.
	 */

	iovec iov[3];//We need 3 data vectors
	char nlmsghdrbuf[NLMSG_LENGTH (0)];
	nlmsghdr *_nlmsghdr = (nlmsghdr*)nlmsghdrbuf; //netlink msg header
	cn_msg cn_msg; //Process Events connector message
	enum proc_cn_mcast_op op; //proc connector -> operation (listen/ignore)
	//Setting up the netlink msg
	_nlmsghdr->nlmsg_len = NLMSG_LENGTH (sizeof cn_msg + sizeof op);
	_nlmsghdr->nlmsg_type = NLMSG_DONE;
	_nlmsghdr->nlmsg_flags = 0;
	_nlmsghdr->nlmsg_seq = 0;
	_nlmsghdr->nlmsg_pid = getpid ();
	iov[0].iov_base = nlmsghdrbuf;
	iov[0].iov_len = NLMSG_LENGTH (0);
	//Setting up Process Events connector
	cn_msg.id.idx = CN_IDX_PROC;
	cn_msg.id.val = CN_VAL_PROC;
	cn_msg.seq = 0;
	cn_msg.ack = 0;
	cn_msg.len = sizeof op;
	iov[1].iov_base = &cn_msg;
	iov[1].iov_len = sizeof cn_msg;
	//Setting up register subscription message
	op = PROC_CN_MCAST_LISTEN;
	iov[2].iov_base = &op;
	iov[2].iov_len = sizeof op;
	//Sending the subscription message
	ssize_t ret = writev(sock, iov, 3);
	if (ret < 0) {
		logger->Error("Linux proc connector subscription failed");
	}
}

ProcessListener::~ProcessListener() {
	Terminate();
	if (sock!=-1)
		close(sock);
	delete[] buf;
}

void ProcessListener::Task() {
	/*
	 * Now we need to read the stream of messages. Just like the message we sent,
	 * the stream of messages we receive are actually netlink messages,
	 * and inside those netlink messages are connector messages,
	 * and inside those are proc connector messages.
	 */
	msghdr msghdr;
	sockaddr_nl addr;
	iovec iov[1];
	ssize_t len;

	msghdr.msg_name = &addr;
	msghdr.msg_namelen = sizeof addr;
	msghdr.msg_iov = iov;
	msghdr.msg_iovlen = 1;
	msghdr.msg_control = NULL;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	iov[0].iov_base = buf;
	iov[0].iov_len = buffSize;
	/*
	 * netlink allows arbitrary processes to send messages to each other,4
	 * so we need to make sure the message actually comes from the kernel;
	 * otherwise you have a potential security vulnerability.
	 * recvmsg lets us receive the sender address as well as the data.
	 */
	while (!done) {
		len = recvmsg (sock, &msghdr, 0);
		if (len<=0)
			continue;
		/*
		 * So now we have a netlink message package from the kernel,
		 * this may contain multiple individual netlink messages
		 * (it doesn’t, but it may). So we iterate over those.
		 */
		for (struct nlmsghdr *_nlmsghdr = (struct nlmsghdr *)buf;
			NLMSG_OK (_nlmsghdr, len);
			_nlmsghdr = NLMSG_NEXT (_nlmsghdr, len)){
			/*
			* Ignore No-Op messages
			*/
			if ((_nlmsghdr->nlmsg_type == NLMSG_ERROR) ||
				(_nlmsghdr->nlmsg_type == NLMSG_NOOP))
				continue;
			/*
			 * Inside each individual netlink message is a connector
			 * message, we extract that and make sure it comes from
			 * the proc connector system.
			 */
			cn_msg *_cn_msg = (cn_msg *)(NLMSG_DATA (_nlmsghdr));
			if ((_cn_msg->id.idx != CN_IDX_PROC) ||
				(_cn_msg->id.val != CN_VAL_PROC))
				continue;
			/*
			 * Now we can safely extract the proc connector message;
			 * this is a struct proc_event that we haven’t seen before.
			 * It’s quite a large structure definition, since it contains a
			 * union for each of the different possible message types.
			 */
			proc_event * e = (proc_event *)_cn_msg->data;
			// Event Processing
			switch (e->what){
			case proc_event::PROC_EVENT_EXEC:
				logger->Debug("Event : [ EXEC, pid: %i, name: %s ]",
					e->event_data.exec.process_pid,
					GetProcName(e->event_data.exec.process_pid).c_str());
				prm.NotifyStart(
					GetProcName(e->event_data.exec.process_pid),
					e->event_data.exec.process_pid);
				break;
			case proc_event::PROC_EVENT_EXIT:
				logger->Debug("Event : [ EXIT, pid: %i, name: %s, "
						"exit code: %i ]",
					e->event_data.exec.process_pid,
					GetProcName(e->event_data.exec.process_pid).c_str(),
					e->event_data.exit.exit_code);
				prm.NotifyExit(
					GetProcName(e->event_data.exec.process_pid),
					e->event_data.exec.process_pid);
				break;
			default:
				break;
			}
		}
	}
}

} // namespace bbque

