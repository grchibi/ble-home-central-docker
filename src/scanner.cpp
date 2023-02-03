/**
 *  scanner.cpp
 *
 *    2020/09/12
 */

#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

#include <curl/curl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "yaml-cpp/yaml.h"

#include "scanner.h"
#include "ble_central.h"
#include "api_comm.h"

#include "tt_lib.h"


#define CONTINUOUS_RUNNING 1


using namespace std;


/**
 * scheduler
 */

scheduler::scheduler() : _ep_central(nullptr), _ep_apicomm(nullptr), _rcv_sigint(false) {
	if (pipe(_pipe_notify_to_central) == -1 || pipe(_pipe_notify_to_apicomm) == -1 || pipe(_pipe_data_from_central) == -1) {
		char msgbuff[128];
		strerror_r(errno, msgbuff, 128);
		throw runtime_error(string("Failed to initialize pipes. ") + msgbuff);
	}

	_worker_ac = new apicomm_worker(_pipe_notify_to_apicomm[0], _pipe_data_from_central[0]);
	_worker_cl = new central_worker(_pipe_notify_to_central[0], _pipe_data_from_central[1]);
}

scheduler::~scheduler() {
	delete _worker_ac;
	delete _worker_cl;

	close(_pipe_notify_to_central[0]);
	close(_pipe_notify_to_central[1]);

	close(_pipe_notify_to_apicomm[0]);
	close(_pipe_notify_to_apicomm[1]);

	close(_pipe_data_from_central[0]);
	close(_pipe_data_from_central[1]);
}

void scheduler::chg_apicomm_settings(svr_config_t& s_conf, apis_map_t& apis) {
	_worker_ac->chg_apicomm_settings(s_conf.features_api, apis);
}

int scheduler::get_sec_for_alarm_00() {
	auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
	tm* now_tm = localtime(&now);
	
	return ((60 - now_tm->tm_min - 1) * 60) + (60 - now_tm->tm_sec);
}

void scheduler::chg_central_settings(svr_config_t& s_conf) {
	_worker_cl->chg_central_settings(s_conf.while_list);
}

void scheduler::reset_pipes() {
	char tmp_buff[2];
    if (read(_pipe_notify_to_central[0], tmp_buff, sizeof(tmp_buff) - 1) < 0) {
        char msgbuff[128];
        strerror_r(errno, msgbuff, 128);
        tt_logger::instance().printf("SCHEDULER: Failed to read pipes(central). %s\n", msgbuff);
    }
	if (read(_pipe_notify_to_apicomm[0], tmp_buff, sizeof(tmp_buff) - 1) < 0) {
        char msgbuff[128];
        strerror_r(errno, msgbuff, 128);
        tt_logger::instance().printf("SCHEDULER: Failed to read pipes(apicomm). %s\n", msgbuff);
    }

    /*close(_pipe_notify_to_central[0]);
    close(_pipe_notify_to_central[1]);
	close(_pipe_notify_to_apicomm[0]);
	close(_pipe_notify_to_apicomm[1]);

    if (pipe(_pipe_notify_to_central) == -1 || pipe(_pipe_notify_to_apicomm) == -1) {
        char msgbuff[128];
        strerror_r(errno, msgbuff, 128);
        throw runtime_error(string("SCHEDULER: Failed to re-initialize pipes.") + msgbuff);
    }*/

    DEBUG_PUTS("SCHEDULER: reset pipes.");
}

void scheduler::sigint() {
	{
		lock_guard<mutex> lock(_mtx);
		_rcv_sigint = true;
	}

	_cond.notify_all();
}

void scheduler::run() {
	try {
		curl_global_init(CURL_GLOBAL_ALL);

		unique_lock<mutex> lock(_mtx);

		while (1) {
#if CONTINUOUS_RUNNING
			int sleep_sec = get_sec_for_alarm_00();

			DEBUG_PRINTF("SCHEDULER: falling into a sleep...%ds\n", sleep_sec);
			if (_cond.wait_for(lock, chrono::seconds(sleep_sec), [this]{ return _rcv_sigint; })) {
				tt_logger::instance().puts("SCHEDULER[INFO]: stopped waiting.");
				break;
			}

			DEBUG_PUTS("SCHEDULER: waked up!");
#endif
			start_scanning_peripherals();

			if (_cond.wait_for(lock, chrono::seconds(DURATION_SEC_OF_ACT), [this]{ return _rcv_sigint; })) {
				stop_scanning_peripherals();
				break;
			}

			DEBUG_PRINTF("SCHEDULER: timeout => %d sec.\n", DURATION_SEC_OF_ACT);
			stop_scanning_peripherals();

#if CONTINUOUS_RUNNING
			DEBUG_PUTS("SCHEDULER: next loop");
#else
			break;
#endif
		}

	} catch (exception& ex) {
		stop_scanning_peripherals();
		tt_logger::instance().printf("SCHEDULER[ERROR]: at scheduler::run() => %s\n", ex.what());
	}

	curl_global_cleanup();

	tt_logger::instance().puts("SCHEDULER[INFO]: Normally finished.");
}

void scheduler::start_scanning_peripherals() {
	DEBUG_PUTS("SCHEDULER: start scanning.");

	try {
		// move constructor
		_th_ac = thread(ref(*_worker_ac), ref(_ep_apicomm));
		_th_cl = thread(ref(*_worker_cl), ref(_ep_central));

	} catch (exception& ex) {
        tt_logger::instance().printf("SCHEDULER[ERROR]: at start_scanning_peripherals(): %s\n", ex.what());
    }
}

void scheduler::stop_scanning_peripherals() {
	DEBUG_PUTS("SCHEDULER: stop scanning.");

	write(_pipe_notify_to_central[1], "\x01", 1);
	write(_pipe_notify_to_apicomm[1], "\x01", 1);

	// wait for stopping the threads.
	if (_th_cl.joinable()) _th_cl.join();
	if (_th_ac.joinable()) _th_ac.join();

	// clear the pipe fd
    reset_pipes();

	try {
		if (_ep_apicomm != nullptr) rethrow_exception(_ep_apicomm);
	} catch (exception& ex) {
		tt_logger::instance().printf("SCHEDULER[ERROR]: from api_comm => %s\n", ex.what());
	}

	try {
		if (_ep_central != nullptr) rethrow_exception(_ep_central);
	} catch (exception& ex) {
		tt_logger::instance().printf("SCHEDULER[ERROR]: from central => %s\n", ex.what());
	}

	_ep_apicomm = _ep_central = nullptr;
}


/**
 * scanner (DEPRECATED)
 */

scanner::scanner() {
	if (pipe(_pipe_notify_to_scanner) == -1 || pipe(_pipe_notify_to_sender) == -1 || pipe(_pipe_data_from_scanner) == -1) {
		char msgbuff[128];
		strerror_r(errno, msgbuff, 128);
		throw runtime_error(string("Failed to initialize pipes. ") + msgbuff);
	}
}

scanner::~scanner() {
	close(_pipe_notify_to_scanner[0]);
	close(_pipe_notify_to_scanner[1]);

	close(_pipe_notify_to_sender[0]);
	close(_pipe_notify_to_sender[1]);

	close(_pipe_data_from_scanner[0]);
	close(_pipe_data_from_scanner[1]);
}

void scanner::run() {
	exception_ptr ep1 = nullptr, ep2 = nullptr;

	tph_datastore store;

	apicomm_worker ac_worker(_pipe_notify_to_sender[0], _pipe_data_from_scanner[0]);
	thread th_1(ref(ac_worker), ref(ep1));

	central_worker cl_worker(_pipe_notify_to_scanner[0], _pipe_data_from_scanner[1]);
	thread th_2(ref(cl_worker), ref(store), ref(ep2));

	th_2.join();
	th_1.join();

	try {
		if (ep1 != nullptr) rethrow_exception(ep1);
	} catch (exception& ex) {
		cerr << "[ERROR] at run(): thread1: " << ex.what() << endl;
	}

	try {
		if (ep2 != nullptr) rethrow_exception(ep2);
	} catch (exception& ex) {
		cerr << "[ERROR] at run(): thread2: " << ex.what() << endl;
	}

	cout << "Normally finished." << endl;
}


/**
 * central_worker
 */

void central_worker::chg_central_settings(std::set<std::string>& whitelist) {
	_wh_list = whitelist;
}

void central_worker::operator()(exception_ptr& ep) {
	ep = nullptr;
	tph_datastore store;

	try {
		ble_central central(_fd_sig, _fd_w);
		central.start_hci_scan(store, _wh_list);

	} catch (...) {
		ep = current_exception();
    }
}

void central_worker::operator()(tph_datastore& store, exception_ptr& ep) {
	ep = nullptr;

	try {
		ble_central central(_fd_sig, _fd_w);
		central.start_hci_scan(store, _wh_list);

	} catch (...) {
		ep = current_exception();
    }
}


/**
 * apicomm_worker
 */

apicomm_worker::apicomm_worker(int fd_sighup, int fd_read) : _fd_sig(fd_sighup), _fd_r(fd_read) {
	_apicomm = make_unique<api_comm>(_fd_sig, _fd_r);
}

void apicomm_worker::chg_apicomm_settings(bool f_call_api, apis_map_t& a_map) {
	_apicomm->chg_settings(f_call_api, a_map);
}

void apicomm_worker::operator()(exception_ptr& ep) {
	ep = nullptr;

	try {
		_apicomm->start();

	} catch (...) {
		ep = current_exception();
    }
}


/**
 * GLOBAL
 */

static scheduler svr_scheduler;

string get_exedirpath() {
	char tmp[PATH_MAX];

	ssize_t sz = readlink("/proc/self/exe", tmp, PATH_MAX);

	string exepath(tmp, (0 < sz) ? sz : 0);
	int slashPos = exepath.rfind("/");

	return exepath.substr(0, slashPos);
}

void initialize(svr_config_t& s_config, apis_map_t& a_map) {
	// read config file
	string cfg_path = get_exedirpath();
	cfg_path += "/config.yaml";

	YAML::Node config = YAML::LoadFile(cfg_path);

	/** parse api config **/

	// default api config
	api_info_t def_api;
	if (config["api"]) {
		if (config["api"]["protocol"]) {
			def_api.protocol = config["api"]["protocol"].as<string>();
		}
		if (config["api"]["host"]) {
			def_api.host = config["api"]["host"].as<string>();
		}
		if (config["api"]["port"]) {
			def_api.port = config["api"]["port"].as<string>();
		}
		if (config["api"]["path"]) {
			def_api.path = config["api"]["path"].as<string>();
		}
		if (config["api"]["ctype"]) {
			def_api.ctype = config["api"]["ctype"].as<string>();
		}
	}
	a_map.insert(make_pair(string("default"), vector<api_info_t>{def_api}));

	// parse apis config
	YAML::Node apis_node = config["apis"];
	if (apis_node.IsSequence()) {
		for (const auto api: apis_node) {
			if (api["dests"]) {	// multiple destination
				YAML::Node dests_node = api["dests"];
				if (dests_node.IsSequence()) {
					for (const auto dest: dests_node) {
						api_info_t apiinfo;
						if (dest["protocol"]) {
							apiinfo.protocol = dest["protocol"].as<string>();
						}
						if (dest["host"]) {
							apiinfo.host = dest["host"].as<string>();
						}
						if (dest["port"]) {
							apiinfo.port = dest["port"].as<string>();
						}
						if (dest["path"]) {
							apiinfo.path = dest["path"].as<string>();
						}
						if (dest["ctype"]) {
							apiinfo.ctype = dest["ctype"].as<string>();
						}

						a_map[api["key"].as<string>()].push_back(apiinfo);
					}
				}
			} else {	// single destination
				api_info_t apiinfo;
				if (api["protocol"]) {
					apiinfo.protocol = api["protocol"].as<string>();
				}
				if (api["host"]) {
					apiinfo.host = api["host"].as<string>();
				}
				if (api["port"]) {
					apiinfo.port = api["port"].as<string>();
				}
				if (api["path"]) {
					apiinfo.path = api["path"].as<string>();
				}
				if (api["ctype"]) {
					apiinfo.ctype = api["ctype"].as<string>();
				}

				if (api["key"]) a_map.insert(make_pair(api["key"].as<string>(), vector<api_info_t>{apiinfo}));
			}
		}
	}

	// parse features
    if (config["features"]) {
        if (config["features"]["api"]) {
            if (config["features"]["api"].as<string>() == "no")
                s_config.features_api = false;
        }
    }

    // parse whitelist
    if (config["whitelist"]) {
        YAML::Node wl_node =  config["whitelist"];
        for (auto ite = wl_node.begin(); ite != wl_node.end(); ite++) {
            s_config.while_list.insert(ite->as<std::string>().c_str());
        }
    }
}

void sigint_handler(int sig) {
	if (sig == SIGINT) {
		DEBUG_PUTS("GLOBAL: SIGINT RECEIVED.");
		svr_scheduler.sigint();
	}
}

int main(int argc, char** argv)
{
	struct sigaction sa = {};
    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

	try {
		svr_config_t s_config;
		apis_map_t a_map;

		initialize(s_config, a_map);

		svr_scheduler.chg_apicomm_settings(s_config, a_map);
		svr_scheduler.chg_central_settings(s_config);

		svr_scheduler.run();

	} catch (exception& ex) {
		tt_logger::instance().printf("Error at main(): %s\n", ex.what());
	}

	return 0;
}


// end of scanner.cpp
