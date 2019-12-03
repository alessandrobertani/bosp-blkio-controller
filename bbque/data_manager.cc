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
#include <csignal>
#include <unistd.h>
#include <stdlib.h>
#include <cstring>
#include <algorithm>

#include "bbque/utils/utility.h"
#include "bbque/utils/timer.h"
#include "bbque/data_manager.h"
#include "bbque/config.h"
#include "bbque/platform_manager.h"

#define DATA_MANAGER_NAMESPACE "bq.dm"
#define MODULE_NAMESPACE DATA_MANAGER_NAMESPACE

// The prefix for configuration file attributes
#define MODULE_CONFIG "DataManager"

#define BBQUE_DM_DEFAULT_SLEEP_TIME  1000
#define BBQUE_DM_DEFAULT_SERVER_PORT 30200

namespace bbque {

using namespace bbque::stat;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::archive;
using namespace boost::serialization;
namespace bd = bbque::data;
namespace br = bbque::res;
namespace po = boost::program_options;


#define LOAD_CONFIG_OPTION(name, type, var, default) \
	opts_desc.add_options() \
		(MODULE_CONFIG "." name, po::value<type>(&var)->default_value(default), "");


DataManager & DataManager::GetInstance() {
	static DataManager instance;
	return instance;
}

DataManager::DataManager() : Worker(),
		cfm(ConfigurationManager::GetInstance()),
		ra(ResourceAccounter::GetInstance()),
		am(ApplicationManager::GetInstance()),
		is_terminating(false) {

	logger = bu::Logger::GetLogger(MODULE_NAMESPACE);
	logger->Debug("Publisher setup...");
	sleep_time = BBQUE_DM_DEFAULT_SLEEP_TIME;
	Setup(BBQUE_MODULE_NAME("dm.pub"), MODULE_NAMESPACE".pub");
	archive_path_rate = std::string(BBQUE_DM_ARCHIVE_PREFIX) + "rate";
	archive_path_event = std::string(BBQUE_DM_ARCHIVE_PREFIX) + "event";

	try {
		po::options_description opts_desc("Data Manager options");
		LOAD_CONFIG_OPTION("server_port", uint32_t, server_port, BBQUE_DM_DEFAULT_SERVER_PORT);
		LOAD_CONFIG_OPTION("client_attempts", uint16_t, max_client_attempts, MAX_SUB_COMM_FAILURE);
		po::variables_map opts_vm;
		cfm.ParseConfigurationFile(opts_desc, opts_vm);
	}
	catch(boost::program_options::invalid_option_value ex) {
		logger->Error("Errors in configuration file [%s]", ex.what());
	}

	RestoreSubscribers();

	logger->Info("Publisher start...");
	Start();

	// Setting the subscription server thread
	logger->Debug("Subscription server thread start...");
	subscription_server = std::thread(&DataManager::SubscriptionHandler, this);
	subscription_server.detach();

	// Setting the event handler thread
	logger->Debug("Event handler thread start...");
	event_handler = std::thread(&DataManager::EventHandler, this);
	event_handler.detach();
}

DataManager::~DataManager() {

}

void DataManager::_PreTerminate() {
	logger->Debug("Worker[%s]: Pre-termination...", name.c_str());
	is_terminating = true;
	{
		std::unique_lock<std::mutex> subs_lock(subscribers_mtx);
		std::unique_lock<std::mutex> events_lock(events_mtx);
		subscribers_on_rate.clear();
		subscribers_on_event.clear();
		event_queue.clear();
		subs_cv.notify_all();
		event_cv.notify_all();
	}
	// Send signal to server
	assert(subscription_server_tid != 0);
	::kill(subscription_server_tid, SIGUSR1);
	// Send signal to event handler
	assert(event_handler_tid != 0);
	::kill(event_handler_tid, SIGUSR1);
}

void DataManager::_PostTerminate() {

}

void DataManager::PrintSubscribers(){
	// Subscribers on rate
	for (auto s : subscribers_on_rate){
		logger->Debug("Subscribers on rate: ip: %s port: %d deadline: %d filter: %s period: %d",
			s->ip_address.c_str(),
			s->port_num,
			s->period_deadline_ms,
			s->subscription.filter.to_string().c_str(),
			s->subscription.period_ms);
	}

	// Subscribers on event
	for (auto s : subscribers_on_event){
		logger->Debug("Subscribers on event: ip: %s port: %d event: %s",
			s->ip_address.c_str(),
			s->port_num,
			s->subscription.event.to_string().c_str());
	}
}

SubscriberPtr_t DataManager::FindSubscriber(
		SubscriberPtr_t & sub_to_find,
		SubscriberPtrList_t & subscribers){
	for (auto sub: subscribers){
		if (*sub.get() == *sub_to_find.get())
			return sub;
	}
	return nullptr;
}

void DataManager::CheckpointSubscribers() {
		logger->Debug("Storing all subscribers...");

		std::ofstream ofs_rate(archive_path_rate);
		std::ofstream ofs_event(archive_path_event);
		text_oarchive oa_rate(ofs_rate);
		text_oarchive oa_event(ofs_event);

		// Checkpointing subscribers on rate
		if (!subscribers_on_rate.empty()){
			oa_rate << subscribers_on_rate;
			logger->Info("CheckpointSubscribers: subscribers on rate stored");
		} else {
			remove(archive_path_rate.c_str());
		}
		// Checkpointing subscribers on event
		if (!subscribers_on_event.empty()){
			oa_event << subscribers_on_event;
			logger->Info("CheckpointSubscribers: subscribers on event stored");
		} else {
			remove(archive_path_event.c_str());
		}

}

void DataManager::RestoreSubscribers(){
	logger->Debug("Loading all stored subscribers...");

	std::ifstream ifs_rate(archive_path_rate);
	std::ifstream ifs_event(archive_path_event);
	// Loading subscribers on rate
	if (ifs_rate.good()) {
		try {
			boost::archive::text_iarchive ia_rate(ifs_rate);
			ia_rate >> subscribers_on_rate;
		}
		catch(std::exception & ex) {
			logger->Error("RestoreSubscribers: exception [%s]", ex.what());
			return;
		}

		assert(!subscribers_on_rate.empty());
		logger->Info("RestoreSubscribers: stored subscribers on rate loaded!");
	} else {
		logger->Info("RestoreSubscribers: previous subscribers on rate not found!");
	}

	// Loading subscribers on event
	if (ifs_event.good()) {
		try {
			boost::archive::text_iarchive ia_event(ifs_event);
			ia_event >> subscribers_on_event;
		}
		catch(std::exception & ex) {
			logger->Error("RestoreSubscribers: exception [%s]", ex.what());
			return;
		}

		assert(!subscribers_on_event.empty());
		logger->Info("RestoreSubscribers: stored subscribers on event loaded!");
	} else {
		logger->Info("RestoreSubscribers: previous subscribers on event not found!");
	}

}

/*******************************************************************/
/*                      Subscription handling                      */
/*******************************************************************/

void DataManager::SubscriptionHandler() {
	/* ---------------- Server setup ---------------- */

	// Getting the id of the subscription server thread
	subscription_server_tid = gettid();
	logger->Info("SubscriptionHandler: starting server [tid=%d]... ",
		subscription_server_tid);

	// Local address settings
	io_service ios;
	ip::tcp::endpoint endpoint =
		ip::tcp::endpoint(ip::tcp::v4(), server_port);
	ip::tcp::acceptor acceptor(ios);

	// Creating the socket
	try {
		logger->Debug("SubscriptionHandler: opening socket @<%s:%d>...",
			ip_address.c_str(), server_port);
		acceptor.open(endpoint.protocol());
		acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
		acceptor.bind(endpoint);
	} catch(boost::exception const& ex){
		if (!done && !is_terminating)
			logger->Error("SubscriptionHandler: error during socket creation");
		return;
	}

	/* ----------------------------------------------------------- */
	/* ------------------ Subscription handling ------------------ */

	// Incoming subscription message
	bbque::stat::subscription_message_t sub_msg;
	logger->Debug("SubscriptionHandler: ready");

	while (!done) {
		ip::tcp::iostream stream;

		// Subscription receiving waiting
		try {
			acceptor.listen();
			acceptor.accept(*stream.rdbuf());
		} catch(boost::exception const& ex) {
			if (!done && !is_terminating)
				logger->Error("SubscriptionHandler: error during socket receiving");
			break;
		}

		// Receiving subscription
		std::string client_addr;
		try {
			client_addr = stream.rdbuf()->remote_endpoint().address().to_string();
			boost::archive::text_iarchive archive(stream);
			archive >> sub_msg;
		} catch(boost::exception const& ex) {
			if (!done && !is_terminating)
				logger->Error("SubscriptionHandler: error during subscription receiving");
			break;
		}

		// Subscription information
		Subscription subscription_info(
			bd::sub_bitset_t(sub_msg.filter),
			bd::sub_bitset_t(sub_msg.event),
			sub_msg.period_ms);

		// Subscriber
		SubscriberPtr_t subscriber = boost::make_shared<Subscriber>
			(client_addr, (uint32_t) sub_msg.port_num, subscription_info);

		// Logging messages
		logger->Info("SubscriptionHandler: client <%s:%d>",
			client_addr.c_str(), sub_msg.port_num);
		logger->Debug("SubscriptionHandler: message size: %u", sizeof(sub_msg));
		logger->Info("Client subscriber:");
		logger->Info("\tPort: %d",subscriber->port_num);
		logger->Info("\tFilter: %s",
			data::sub_bitset_t(subscriber->subscription.filter).to_string().c_str());
		logger->Info("\tEvent: %s",
			data::sub_bitset_t(subscriber->subscription.event).to_string().c_str());
		logger->Info("\tPeriod: %d ms",subscriber->subscription.period_ms);
		logger->Info("\tMode: %d",sub_msg.mode);

		std::unique_lock<std::mutex> subs_lock(subscribers_mtx);

		if (sub_msg.mode == 0)
			Subscribe(subscriber, sub_msg.event != 0);
		else
			Unsubscribe(subscriber, sub_msg.event != 0);
	}

	// Closing the socket
	try {
		acceptor.close();
	} catch(boost::exception const& ex) {
		if (!done && !is_terminating)
			logger->Error("SubscriptionHandler: error during closing socket");
	}
}


void DataManager::Subscribe(SubscriberPtr_t subscr, bool event_based) {
	logger->Debug("Subscribe: client <%s:%d>",
			subscr->ip_address.c_str(),subscr->port_num);

	if (event_based) { // If event-based subscription
		auto sub = FindSubscriber(subscr, subscribers_on_event);
		// If the client is already a subscriber just update its event filter
		if (sub != nullptr) {
			logger->Debug("Subscribe: <%s> current events: %s",
				subscr->ip_address.c_str(),
				sub->subscription.event.to_string().c_str());

			sub->subscription.event |= subscr->subscription.event;
			logger->Debug("Subscribe: <%s> updated events: %s",
				subscr->ip_address.c_str(),
				sub->subscription.event.to_string().c_str());
		}
		else {
			// If new just add it to the list
			subscribers_on_event.push_back(subscr);
		}
	}
	else { // If rate-based subscription
		auto sub = FindSubscriber(subscr, subscribers_on_rate);
		// If the client is already a subscriber just update its filter and rate
		if (sub != nullptr) {
			logger->Debug("Subscribe: <%s> current filter: %s period: %d ms",
				subscr->ip_address.c_str(),
				sub->subscription.filter.to_string().c_str(),
				sub->subscription.period_ms);

			sub->subscription.filter |= subscr->subscription.filter;
			sub->subscription.period_ms = subscr->subscription.period_ms;
			sub->period_deadline_ms     = sub->subscription.period_ms;
			logger->Debug("Subscribe: <%s> updated filter: %s period: %d m",
				subscr->ip_address.c_str(),
				sub->subscription.filter.to_string().c_str(),
				sub->subscription.period_ms);
		}
		else {
			subscribers_on_rate.push_back(subscr);
			subs_cv.notify_one();
		}
		subscribers_on_rate.sort();
	}

	// Storing the actual subscribers
	CheckpointSubscribers();

	PrintSubscribers();
}


void DataManager::Unsubscribe(SubscriberPtr_t subscr, bool event_based) {
	logger->Info("Unsubscribe: client <%s:%d>",
		subscr->ip_address.c_str(), subscr->port_num);

	if (event_based) {
		auto sub = FindSubscriber(subscr, subscribers_on_event);
		if (sub != nullptr) {
			sub->subscription.event &= ~(subscr->subscription.event);
			if (sub->subscription.event.none())
				subscribers_on_event.remove(sub);
		}
		else
			logger->Error("Unsubscribe: client not found!");
	}
	else {
		auto sub = FindSubscriber(subscr, subscribers_on_rate);
		if (sub != nullptr){
			sub->subscription.filter &= ~(subscr->subscription.filter);
			if (sub->subscription.filter.none())
				subscribers_on_rate.remove(sub);
			subscribers_on_rate.sort();
		}
		else
			logger->Error("Unsubscribe: client not found!");
	}

	// Storing the actual subscribers
	CheckpointSubscribers();

	PrintSubscribers();
}


/*******************************************************************/
/*                       Publishing handling                       */
/*******************************************************************/

void DataManager::Task() {
	logger->Debug("Task: starting worker...");
	while (!done) {
		std::unique_lock<std::mutex> subs_lock(subscribers_mtx);
		while (subscribers_on_rate.empty())
			subs_cv.wait(subs_lock);
		subs_lock.unlock();
		PublishOnRate();
		logger->Debug("Task: sleeping for %d...", sleep_time);
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
	}
}

void DataManager::NotifyUpdate(status_event_t event) {
	logger->Notice("NotifyUpdate: update notified: %d",event);
	std::unique_lock<std::mutex> events_lock(events_mtx);
	event_queue.push_back(event);
	event_cv.notify_one();
}

void DataManager::EventHandler() {
	event_handler_tid = gettid();
	logger->Info("EventHandler: starting handler..,.[tid=%d]", event_handler_tid);
	std::unique_lock<std::mutex> events_lock(events_mtx, std::defer_lock);

	while (!done) {
		std::vector<status_event_t> events_vec;
		events_lock.lock();
		while (event_queue.empty())
			event_cv.wait(events_lock);
		for (auto & event: event_queue)
			events_vec.push_back(event);
		event_queue.clear();
		events_lock.unlock();
		// Avoiding blocking function due to long execution time
		// of PublishOnEvent
		for (const auto & event: events_vec) {
			logger->Debug("EventHandler: publishing for event %d", event);
			PublishOnEvent(event);
		}
	}
}

void DataManager::PublishOnEvent(status_event_t event){
	ExitCode_t result;
	std::list<SubscriberPtr_t> push_list;
	std::unique_lock<std::mutex> subs_lock(subscribers_mtx, std::defer_lock);
	UpdateData(); // Update resources and applications data

	subs_lock.lock();

	for (const auto s : subscribers_on_event) {
		// If event matched
		if((s->subscription.event & bd::sub_bitset_t(event)) == bd::sub_bitset_t(event)){
			push_list.push_back(s);
		}
	}

	// Publishing information
	for(auto s: push_list){
		result = Push(s);
		if(result == ERR_CLIENT_TIMEOUT){
			logger->Error("PublishOnEvent: attempts limit reached, the client is removed");
			continue;
		} else if (result != OK){
			logger->Error("PublishOnEvent: error in publishing to <%s:%d>",
				s->ip_address.c_str(),s->port_num);
		} else {
			logger->Info("PublishOnEvent: published information to <%s:%d>",
				s->ip_address.c_str(),s->port_num);
		}
	}
}

void DataManager::PublishOnRate(){
	uint16_t tmp_sleep_time;
	ExitCode_t result;
	utils::Timer tmr;
	std::list<SubscriberPtr_t> push_list;

	std::unique_lock<std::mutex> subs_lock(subscribers_mtx, std::defer_lock);
	tmp_sleep_time = subscribers_on_rate.back()->subscription.period_ms;
	UpdateData(); // Update resources and applications data

	subs_lock.lock();
	tmr.start();

	for (const auto s: subscribers_on_rate) {
		// Update deadline after sleep
		s->period_deadline_ms = s->period_deadline_ms - actual_sleep_time;
		logger->Debug("PublishOnRate: subscriber: <%s:%d> next_deadline: %d",
			s->ip_address.c_str(), s->port_num, s->period_deadline_ms);

		// Deadline passed => push the updated information
		if (s->period_deadline_ms <= 0) {
			push_list.push_back(s);
			// Reset the deadline
			s->period_deadline_ms = s->subscription.period_ms;
			logger->Debug("PublishOnRate: subscriber: <%s:%d> updated next_deadline: %d",
				s->ip_address.c_str(), s->port_num, s->period_deadline_ms);
		}
		// Calculating the earlier sleep time the sleep time with the earlier
		if(s->period_deadline_ms < tmp_sleep_time)
			tmp_sleep_time = s->period_deadline_ms;
	}

	// Publishing information
	for(auto s: push_list){
		result = Push(s);
		if(result == ERR_CLIENT_TIMEOUT){
			logger->Error("PublishOnRate: attempts limit reached, the client is removed");
			continue;
		} else if (result != OK){
			logger->Error("PublishOnRate: error in publishing to <%s:%d>",
				s->ip_address.c_str(),s->port_num);
		} else {
			logger->Info("PublishOnRate: published information to <%s:%d>",
				s->ip_address.c_str(),s->port_num);
		}
	}
	tmr.stop();

	// Updating the sleep time
	sleep_time = tmp_sleep_time - static_cast<uint16_t>(tmr.getElapsedTimeMs());
	actual_sleep_time = tmp_sleep_time;

	// Sort subscribers by deadline in ascending order
	subscribers_on_rate.sort();
}

DataManager::ExitCode_t DataManager::Push(SubscriberPtr_t sub) {
	status_message_t updated_status;
	updated_status.ts = static_cast<uint32_t>(utils::Timer::getTimestamp());
	updated_status.n_app_status_msgs = 0;
	updated_status.n_res_status_msgs = 0;
	logger->Debug("Push: timestamp=%d", updated_status.ts);

	// Subscriber has "resource" subscription?
	if ((sub->subscription.filter & bd::sub_bitset_t(FILTER_RESOURCE)) ==
		bd::sub_bitset_t(FILTER_RESOURCE)) {
		logger->Debug("Push: adding resources info for subscriber <%s>...",
			sub->ip_address.c_str());

		updated_status.n_res_status_msgs = num_resources;
		for (auto res_stat: res_stats)
			updated_status.res_status_msgs.push_back(res_stat);

		// Debug logging
		for (auto res_stat : updated_status.res_status_msgs) {
			logger->Debug("Resource: id=%ld, occupancy=%d, load=%d, power=%d, temp=%d",
				res_stat.id,
				res_stat.occupancy,
				res_stat.load,
				res_stat.power,
				res_stat.temp);
		}
	}

	// Subscriber filter has "application" subscription?
	if ((sub->subscription.filter & bd::sub_bitset_t(FILTER_APPLICATION)) ==
		bd::sub_bitset_t(FILTER_APPLICATION)) {
		logger->Debug("Push: adding applications info for subscriber <%s>...",
			sub->ip_address.c_str());

		updated_status.n_app_status_msgs = num_applications;
		for (auto app_stat : app_stats)
			updated_status.app_status_msgs.push_back(app_stat);

		// Debug logging
		for (auto app_stat : updated_status.app_status_msgs) {
			logger->Debug("Push: Application: id=%d, name=%s, nr_tasks=%d, mapping=%d",
				app_stat.id,
				app_stat.name.c_str(),
				app_stat.n_task,
				app_stat.mapping);
			for(auto task_stat : app_stat.tasks){
				logger->Debug("Push: Task: id=%d, THR:%d, CT:%d, mapping=%d, n_threads=%d",
				task_stat.id,
				task_stat.throughput,
				task_stat.completion_time,
				task_stat.mapping,
				task_stat.n_threads);
			}
		}
	}

	// TCP socket for data messages
	tcp::iostream client_sock(sub->ip_address, std::to_string(sub->port_num));
	if (!client_sock) {
		logger->Error("Push: cannot connect to <%s:%d>",
			sub->ip_address.c_str(), sub->port_num);
		sub->comm_failures++;
		if(sub->comm_failures > max_client_attempts){
			// Unsubscribing the unreachable client
			Unsubscribe(sub,sub->subscription.event != 0);
			return ERR_CLIENT_TIMEOUT;
		}
		return ERR_CLIENT_COMM;
	}
	sub->comm_failures = 0;

	// Message serialization and transmission
	text_oarchive archive(client_sock);
	try {
		archive << updated_status;  // Send
	} catch(boost::exception const& ex){
		logger->Error("Boost archive exception");
		return ERR_CLIENT_COMM;
	}

	return OK;
}

res_bitset_t DataManager::BuildResourceBitset(br::ResourcePathPtr_t resource_path) {
	res_bitset_t res_bitset = 0;
	for(auto resource_identifier: resource_path->GetIdentifiers()){
		switch(resource_identifier->Type()){
			case res::ResourceType::SYSTEM:
				res_bitset |= static_cast<uint64_t>(resource_identifier->ID()) << BBQUE_DCI_OFFSET_SYS;
				break;
			case res::ResourceType::GROUP:
				res_bitset |= static_cast<uint64_t>(resource_identifier->ID()) << BBQUE_DCI_OFFSET_GRP;
				break;
			case res::ResourceType::CPU:
			case res::ResourceType::GPU:
			case res::ResourceType::ACCELERATOR:
			case res::ResourceType::MEMORY:
			case res::ResourceType::NETWORK_IF:
				res_bitset |=
					static_cast<uint64_t>(resource_identifier->Type()) << BBQUE_DCI_OFFSET_UNIT_TYPE;
				res_bitset |= static_cast<uint64_t>(resource_identifier->ID()) << BBQUE_DCI_OFFSET_UNIT_ID;
				break;
			case res::ResourceType::PROC_ELEMENT:
				res_bitset |= static_cast<uint64_t>(1) << BBQUE_DCI_OFFSET_PE_TYPE;
				res_bitset |= static_cast<uint64_t>(resource_identifier->ID()) << BBQUE_DCI_OFFSET_PE_ID;
				break;
			default:
				break;
		}
	}
	return res_bitset;
}

void DataManager::UpdateData(){

	UpdateResourcesData();

	UpdateApplicationsData();

	logger->Info("UpdateData: completed");
}

void DataManager::UpdateResourcesData(){
	logger->Debug("UpdateData: resources status...");

	// Taking data from Resource accounter and Power Manager
	std::set<br::ResourcePtr_t> resource_set = ra.GetResourceSet();
	num_resources = resource_set.size();

	// Updating resource status list
	res_stats.clear();
	for (auto & resource_ptr : resource_set) {
		br::ResourcePathPtr_t resource_path = resource_ptr->Path();
		logger->Debug("UpdateData: <%lu>: used=%lu  unreserved=%lu total=%lu",
			BuildResourceBitset(resource_path),
			resource_ptr->Used(),
			resource_ptr->Unreserved(),
			resource_ptr->Total());

#ifdef CONFIG_BBQUE_PM
		logger->Debug("UpdateData: <%ld>: temp=%d freq=%d power=%2.2f",
			BuildResourceBitset(resource_path),
			static_cast<uint32_t>(
				resource_ptr->GetPowerInfo(PowerManager::InfoType::TEMPERATURE, br::Resource::ValueType::INSTANT)),
			static_cast<uint32_t>(
				resource_ptr->GetPowerInfo(PowerManager::InfoType::FREQUENCY, br::Resource::ValueType::INSTANT)),
			static_cast<uint32_t>(
				resource_ptr->GetPowerInfo(PowerManager::InfoType::POWER, br::Resource::ValueType::INSTANT)));
#endif // CONFIG_BBQUE_PM

		resource_status_t temp_res;
		temp_res.id = BuildResourceBitset(resource_path);
		temp_res.model = resource_ptr->Model();
		temp_res.occupancy = static_cast<uint8_t>(
			(float(resource_ptr->Used()) / resource_ptr->Total()) * 100 );
#ifdef CONFIG_BBQUE_PM
		temp_res.load = static_cast<uint8_t>(
			resource_ptr->GetPowerInfo(PowerManager::InfoType::LOAD, br::Resource::ValueType::INSTANT));
		temp_res.power = static_cast<uint32_t>(
			resource_ptr->GetPowerInfo(PowerManager::InfoType::POWER, br::Resource::ValueType::INSTANT));
		temp_res.temp = static_cast<uint32_t>(
			resource_ptr->GetPowerInfo(PowerManager::InfoType::TEMPERATURE, br::Resource::ValueType::INSTANT));
#endif // CONFIG_BBQUE_PM
		res_stats.push_back(temp_res);
	}
}

void DataManager::UpdateApplicationsData(){

	logger->Debug("UpdateData: applications status...");
	app_stats.clear();

	num_applications = am.AppsCount(ApplicationStatusIF::RUNNING);

	// Per-application information
	AppsUidMapIt app_it;
	AppPtr_t app_ptr = am.GetFirst(ApplicationStatusIF::RUNNING, app_it);

	while(app_ptr != nullptr) {
		app_status_t temp_app;

		temp_app.id   = app_ptr->Pid();
		temp_app.name = app_ptr->Name();
		temp_app.state = app_ptr->State();
		auto temp_awm = app_ptr->CurrentAWM();
		logger->Debug("UpdateData: app = <%d>, state = <%s>, current AWM = <%s:%d>",
		              temp_app.id, ApplicationStatusIF::StateStr(app_ptr->State()));

		if (temp_awm == nullptr) {
			logger->Debug("UpdateData: app = <%d>, no AWM", temp_app.id);
			continue;
		}

		logger->Debug("UpdateData: app = <%d>, current AWM = <%s:%d>",
	                      temp_app.id, temp_awm->Name().c_str(), temp_awm->Id());

		// Application resource assignment
		res::ResourceAssignmentMapPtr_t temp_res_bind_ptr =
		        temp_awm->GetResourceBinding();

		auto & temp_res_bind = *(temp_res_bind_ptr.get());
		std::list<res_bitset_t> app_res_list;
		uint32_t res_counter = 0;
		uint32_t mapped_sys_id;

		// Resource bitset for all the assigned resources
		for(auto bind : temp_res_bind) {
			res::ResourcePtrList_t res_list = bind.second->GetResourcesList();
			logger->Debug("UpdateData: resource request = <%s>",
			              bind.first->ToString().c_str());
			for(auto res : res_list) {
				mapped_sys_id = res->Path()->GetID(res::ResourceType::SYSTEM);
				app_res_list.push_back(BuildResourceBitset(res->Path()));
				logger->Debug("UpdateData: resource mapped = <%s>",
				              res->Path()->ToString().c_str());
				res_counter++;
			}
		}

		// Getting the assigned resources
		temp_app.n_mapping = res_counter;
		temp_app.mapping   = app_res_list;

#ifdef CONFIG_BBQUE_TG_PROG_MODEL

		// Per-task information
		auto temp_tg = app_ptr->GetTaskGraph();
		if (temp_tg == nullptr) {
			logger->Warn("UpdateData: <%s-%s>: no task-graph",
		              temp_app.name.c_str(), app_ptr->StrId());
			continue;
		}

		temp_app.n_task = temp_tg->TaskCount();
		logger->Debug("UpdateData: <%s-%s>: n_task=%d",
		              temp_app.name.c_str(), app_ptr->StrId(), temp_app.n_task);

		// List of tasks
		auto tasks_map = temp_tg->Tasks();
		int mapped_cluster = temp_tg->GetCluster();

		for(auto task : tasks_map){
			task_status_t temp_task;

			// Task Id
			TaskPtr_t task_ptr = task.second;
			temp_task.id = task_ptr->Id();

			// Performance data
			task_ptr->GetProfiling(temp_task.throughput, temp_task.completion_time);
			logger->Debug("UpdateData: performance { THR=%d, CT=%d }",
				temp_task.throughput, temp_task.completion_time);

			// Resource mapping
			int mapped_proc = task_ptr->GetAssignedProcessor();
			temp_task.n_threads = task_ptr->GetAssignedCoresCount(); // # threads

			// Build complete resource path
			std::string assigned_res_path =
				FindResourceAssigned(temp_awm->GetResourceBinding(),
					mapped_sys_id, mapped_cluster, mapped_proc);
			if(assigned_res_path.size() != 0){
				br::ResourcePathPtr_t mapping_path = ra.GetPath(assigned_res_path);
				if (mapping_path == nullptr) {
					logger->Warn("UpdateData: <%s> is not a valid path",
						assigned_res_path.c_str());
				}
				else {
					logger->Debug("UpdateData: Task <%d>:<%s>",
					temp_task.id, assigned_res_path.c_str());
					temp_task.mapping = BuildResourceBitset(mapping_path);
				}
			} else {
				logger->Error("UpdateData: no resource assignment found for task %d",
					temp_task.id);
			}

			temp_app.tasks.push_back(temp_task);
		}
#endif
		app_stats.push_back(temp_app);

		app_ptr = am.GetNext(ApplicationStatusIF::RUNNING, app_it);
	}
}

std::string DataManager::FindResourceAssigned(
	res::ResourceAssignmentMapPtr_t res_map_ptr,
	int mapped_sys_id,
	int mapped_cluster,
	int mapped_proc) const{

	auto & res_map = *(res_map_ptr.get());
	for(auto bind : res_map){
		res::ResourcePtrList_t res_list = bind.second->GetResourcesList();
		for(auto res : res_list){
			auto res_path = res->Path();
			//logger->Error("FindResourceAssigned: sys%d.grp%d.cpu%d - %s",
			//	mapped_sys_id, mapped_cluster, mapped_proc, res->Path().c_str());
			//logger->Error("Type: <%s>", br::GetResourceTypeString(res_path->Type(-1)));
			// Considering only path without memory assignment
			if (res_path->GetID(res::ResourceType::GROUP) == mapped_cluster &&
					res_path->GetID(res_path->Type(-1)) == mapped_proc && 
					!res_path->IncludesType(res::ResourceType::MEMORY)){
				std::string res_path_str(std::string("sys")
							+ std::to_string(mapped_sys_id)
							+".grp"
							+ std::to_string(mapped_cluster)
							+ "."
							+ br::GetResourceTypeString(res_path->Type(-1))
							+ std::to_string(mapped_proc));
				logger->Debug("FindResourceAssigned: %s", res_path_str.c_str());
				return res_path_str;
			}
		}
	}
	return std::string();
}

} // namespace bbque

