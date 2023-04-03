/**
 * api_comm.cpp
 *
 *    2020/10/19
 */

#include <iostream>
#include <sstream>

#include <string.h>
#include <unistd.h>

#include "nlohmann/json.hpp"

#include "api_comm.h"

#include "tt_lib.h"


using namespace std;


namespace zushisa9tt::blehomecentral {

	/**
	 * CLASS api_comm
	 */

	api_comm::api_comm(int idx, api_config_t& a_conf, apis_map_t& a_map) : _myno(idx), _apis_map(a_map) {
		_curl_handle = curl_easy_init();
		if (!_curl_handle)
			throw tt_apicomm_exception("failed to initialize Curl handle.");

		_headers = curl_slist_append(_headers, "Content-Type: application/json");

		chg_settings(a_conf);
	}

	api_comm::~api_comm() {
		if (_curl_handle) {
			curl_slist_free_all(_headers);
			curl_easy_cleanup(_curl_handle);
		}
	}

	void api_comm::chg_settings(api_config_t& a_conf) {
		_api_retry_duration_secs = a_conf.api_retry_duration_secs;
		_api_retry_max_count = a_conf.api_retry_max_count;
		_features_call_api = a_conf.features_api;	// call api ?

		// make the default API destination
		api_info_t def_api;
		_apis_map.insert(make_pair(string("default"), std::vector<api_info_t>{def_api}));
	}

	void api_comm::send_data(const char* json) {
		if (!_curl_handle) {
			tt_logger::instance().printf("API_COMM %d[INFO]: Received => %s\n", _myno, json);
			tt_logger::instance().printf("API_COMM %d[WARN] at send_data(): curl handle is null.\n", _myno);
			return;
		} else if (!_features_call_api)	{
			tt_logger::instance().printf("API_COMM %d[INFO]: Received => %s\n", _myno, json);
			tt_logger::instance().printf("API_COMM %d[WARN] at send_data(): the feature of calling api is unavailable.\n", _myno);
			return;
		}

		// obtain the destination
		string device_id;
		nlohmann::json json_obj;
		try {
			json_obj = nlohmann::json::parse(json);
			device_id = (json_obj["tph_register"]["dsrc"]).get<std::string>();

			if (_apis_map.find(device_id) == _apis_map.end()) {	// not found
				device_id = "default";
			}
		} catch (exception& ex) {
			tt_logger::instance().printf("API_COMM %d[ERROR]: at send_data() => %s\n", _myno, ex.what());
			tt_logger::instance().printf("API_COMM %d[ERROR]: Received and parse => %s\n", _myno, json);
			tt_logger::instance().printf("API_COMM %d[WARN] at send_data(): default API setting is used.\n", _myno);
			device_id = "default";
		}

		std::vector<api_info_t> destination_infos_v = _apis_map[device_id];
		for (const auto destination_info: destination_infos_v) {
			std::stringstream ss;
			ss << destination_info.protocol << "://" << destination_info.host << ":" << destination_info.port << destination_info.path;
			string url = ss.str();
			tt_logger::instance().printf("API_COMM %d[INFO]: Destination URL(%s) => %s\n", _myno, device_id.c_str(), url.c_str());

			curl_easy_setopt(_curl_handle, CURLOPT_URL, url.c_str());
			curl_easy_setopt(_curl_handle, CURLOPT_HTTPHEADER, _headers);
			curl_easy_setopt(_curl_handle, CURLOPT_POST, 1);
			curl_easy_setopt(_curl_handle, CURLOPT_POSTFIELDS, json);
			curl_easy_setopt(_curl_handle, CURLOPT_POSTFIELDSIZE, strlen(json));
			curl_easy_setopt(_curl_handle, CURLOPT_CONNECTTIMEOUT, 5);

			unsigned int cnt = 0;
			while (cnt < _api_retry_max_count) {
				CURLcode ret = curl_easy_perform(_curl_handle);

				if (ret == CURLE_OK) {
					long resp_code = -1;
					ret = curl_easy_getinfo(_curl_handle, CURLINFO_RESPONSE_CODE, &resp_code);
					if (ret == CURLE_OK && resp_code == 200) {
						tt_logger::instance().printf("API_COMM %d[INFO] at send_data(): 200 OK.\n", _myno);
						break;
					} else {
						tt_logger::instance().printf("API_COMM %d[ERROR] at send_data(): http response code %ld was returned. retrying...\n", _myno, resp_code);
					}

				} else {
					tt_logger::instance().printf("API_COMM %d[ERROR] at send_data(): %s\n", _myno, curl_easy_strerror(ret));
				}

				sleep(_api_retry_duration_secs);
				cnt++;
			}
			if (_api_retry_max_count <= cnt) tt_logger::instance().printf("API_COMM %d[ERROR] at send_data(): the sending data was aborted.\n", _myno);
		}	
	}


	/**
	 * CLASS apicomm_worker
	 */

	apicomm_worker::apicomm_worker(api_config_t& a_conf, apis_map_t& a_map, std::condition_variable& p_cond, bool& prun, std::mutex& t_mtx, std::unique_ptr<api_task_queue>& t_q):
	 _api_conf(a_conf), _apis_map(a_map), _pool_cond(p_cond), _pool_running(prun), _task_mtx(t_mtx), _task_queue(t_q) {
	 }
	
	apicomm_worker::apicomm_worker(const apicomm_worker& rhs):
	 _api_conf(rhs._api_conf), _apis_map(rhs._apis_map), _pool_cond(rhs._pool_cond), _pool_running(rhs._pool_running), _task_mtx(rhs._task_mtx), _task_queue(rhs._task_queue) {
		// DEBUG_PUTS("apicomm_worker's copy constructor is called.");
	}

	void apicomm_worker::operator()(int idx) {
		try {
			api_comm api(idx, _api_conf, _apis_map);

			while (1) {
				std::string json_str;

				{
					std::unique_lock<mutex> lock(_task_mtx);

					_pool_cond.wait(lock, [this]{ return !_pool_running || !_task_queue->empty(); });
					if (!_pool_running) {
						DEBUG_PRINTF("APICOMM_WORKER %d: stopped waiting.\n", idx);
						break;
					}

					json_str = _task_queue->front_pop(lock);
				}

				/** WORK WORK WORK **/
				if (!json_str.empty()) {
					DEBUG_PRINTF("APICOMM_WORKER %d: popped => [%s]\n", idx, json_str.c_str());
					api.send_data(json_str.c_str());
				} else {
					tt_logger::instance().printf("APICOMM_WORKER %d[WARN]: popped string was null. skipped.\n", idx);
				}
			}

		} catch (exception& ex) {
			tt_logger::instance().printf("APICOMM_WORKER %d[ERROR]: from api_comm::start() => %s\n", idx, ex.what());
		}

		DEBUG_PRINTF("APICOMM_WORKER %d: stopped.\n", idx);
	}


	/**
	 * CLASS api_task_queue
	 */

	std::string api_task_queue::front_pop(std::unique_lock<std::mutex>& lk) {
		std::string result;

		{
			if (!lk.owns_lock()) {
				tt_logger::instance().puts("API_TASK_QUEUE[ERROR]: at front_pop() => not own the lock!");
				throw tt_apicomm_exception("at api_task_queue.front_pop() => not own the lock!");
			}

			result = std::move(_queue.front());
			_queue.pop();
		}

		return result;
	}

	void api_task_queue::push_task(const std::string& jstr) {
		{
			std::lock_guard<mutex> lock(_mtx);

			_queue.push(jstr);
		}

		_cond.notify_one();
	}


	/**
	 * CLASS api_thread_pool
	 */

	api_thread_pool::api_thread_pool(std::condition_variable& t_cond, std::mutex& t_mtx, std::unique_ptr<api_task_queue>& t_q) :
	 _cond(t_cond), _mtx(t_mtx), _queue(t_q), _running(false) {}

	void api_thread_pool::chg_settings(api_config_t& a_conf, apis_map_t& a_map) {
		_api_conf = a_conf;

		_apis_map.clear();
		_apis_map.insert(a_map.begin(), a_map.end());
	}

	void api_thread_pool::run() {
		tt_logger::instance().puts("API_THREAD_POOL[INFO]: called run()");

		{
			lock_guard<mutex> lock(_mtx);
			_running = true;
		}

		for (unsigned int idx = 0; idx < _api_conf.api_thread_pool_capacity; idx++) {
			apicomm_worker worker(_api_conf, _apis_map, _cond, _running, _mtx, _queue);
			_api_workers.emplace_back(worker, idx);
		}
	}

	void api_thread_pool::stop() {
		{
			lock_guard<mutex> lock(_mtx);
			_running = false;
		}

		_cond.notify_all();

		for (thread& th : _api_workers) {
			if (th.joinable()) th.join();
		}

		tt_logger::instance().puts("API_THREAD_POOL [INFO]: all threads stopped.");
	}
	
}	// namespace zushisa9tt::blehomecentral


/**
 * CLASS api_comm (OBSOLETE)
 */

api_comm::api_comm(int fd_sighup, int fd_read) {
	_curl_handle = curl_easy_init();
	if (!_curl_handle)
		throw zushisa9tt::blehomecentral::tt_apicomm_exception("failed to initialize Curl handle.");

	_headers = curl_slist_append(_headers, "Content-Type: application/json");

	_fds_poll[0].fd = fd_sighup;
	_fds_poll[0].events = POLLIN;
	_fds_poll[1].fd = fd_read;
	_fds_poll[1].events = POLLIN;

	zushisa9tt::blehomecentral::api_info_t def_api;
	_apis_map.insert(make_pair(string("default"), vector<zushisa9tt::blehomecentral::api_info_t>{def_api}));
}

api_comm::~api_comm() {
	if (_curl_handle) {
		curl_slist_free_all(_headers);
		curl_easy_cleanup(_curl_handle);
	}
}

void api_comm::chg_settings(bool f_call_api, zushisa9tt::blehomecentral::apis_map_t& a_map) {
	_features_call_api = f_call_api;	// call api ?
	_apis_map = a_map;	// api config
}

void api_comm::send_data(const char* json) {
	if (!_curl_handle) {
		tt_logger::instance().puts("API_COMM[WARN] at send_data(): curl handle is null.");
		tt_logger::instance().printf("API_COMM[WARN]: Received => %s\n", json);
		return;
	} else if (!_features_call_api)	{
		tt_logger::instance().puts("API_COMM[WARN] at send_data(): the feature of calling api is unavailable.");
		tt_logger::instance().printf("API_COMM[WARN]: Received => %s\n", json);
		return;
	}

	// obtain the destination
	string device_id;
	nlohmann::json json_obj;
	try {
		json_obj = nlohmann::json::parse(json);
		device_id = (json_obj["tph_register"]["dsrc"]).get<std::string>();
		if (_apis_map.find(device_id) == _apis_map.end()) {	// not found
			device_id = "default";
		}
	} catch (exception& ex) {
		tt_logger::instance().printf("API_COMM[ERROR]: at send_data() => %s\n", ex.what());
		tt_logger::instance().printf("API_COMM[ERROR]: Received and parse => %s\n", json);
		tt_logger::instance().puts("API_COMM[WARN] at send_data(): default API setting is used.");
		device_id = "default";
	}
	std::vector<zushisa9tt::blehomecentral::api_info_t> &destination_infos_v = _apis_map[device_id];

	for (const auto destination_info: destination_infos_v) {
		string url = destination_info.protocol + "://" + destination_info.host + ":" + destination_info.port + destination_info.path;
		tt_logger::instance().printf("API_COMM[INFO]: Destination URL(%s) => %s\n", device_id.c_str(), url.c_str());

		curl_easy_setopt(_curl_handle, CURLOPT_URL, url.c_str());
		curl_easy_setopt(_curl_handle, CURLOPT_HTTPHEADER, _headers);
		curl_easy_setopt(_curl_handle, CURLOPT_POST, 1);
		curl_easy_setopt(_curl_handle, CURLOPT_POSTFIELDS, json);
		curl_easy_setopt(_curl_handle, CURLOPT_POSTFIELDSIZE, strlen(json));
		curl_easy_setopt(_curl_handle, CURLOPT_CONNECTTIMEOUT, 5);

		int cnt = 0;
		while (cnt < RETRY_MAX_COUNT) {
			CURLcode ret = curl_easy_perform(_curl_handle);

			if (ret == CURLE_OK) {
				long resp_code = -1;
				ret = curl_easy_getinfo(_curl_handle, CURLINFO_RESPONSE_CODE, &resp_code);
				if (ret == CURLE_OK && resp_code == 200) {
					tt_logger::instance().puts("API_COMM[INFO] at send_data(): 200 OK.");
					break;
				} else {
					tt_logger::instance().printf("API_COMM[ERROR] at send_data(): http response code %ld was returned. retrying...", resp_code);
				}

			} else {
				tt_logger::instance().printf("API_COMM[ERROR] at send_data(): %s\n", curl_easy_strerror(ret));
			}

			sleep(RETRY_SLEEP_SECS);
			cnt++;
		}
	}
}

void api_comm::start() {
    try {
		while (1) {
			if (poll(_fds_poll, 2, -1) < 0 && errno != EINTR) {
				char msgbuff[128] = {0};
				strerror_r(errno, msgbuff, 128);
				tt_logger::instance().printf("poll error occurred in api_comm. %s\n", msgbuff);
				continue;
			}

			if (_fds_poll[0].revents & POLLIN) {
				DEBUG_PUTS("API_COMM: SIGNAL NOTIFIED");
				throw zushisa9tt::blehomecentral::tt_apicomm_exception("read signal notify from pipe.");
			}

			if (_fds_poll[1].revents & POLLIN) {
				char jd_buff[256] = {0};

				int len = 0;
				if ((len = read(_fds_poll[1].fd, jd_buff, sizeof(jd_buff))) < 0) {
                    if (errno == EAGAIN) {
                        DEBUG_PUTS("API_COMM: EAGAIN");
                        continue;
                    } else if (errno == EINTR) {
                        DEBUG_PUTS("API_COMM: read RECEIVED ANY SIGNAL");
                        continue;
                    } else {
                        throw zushisa9tt::blehomecentral::tt_apicomm_exception("read error.");
                    }
                }

				DEBUG_PRINTF("API_COMM: Received => %s\n", jd_buff);
				send_data(jd_buff);
			}

			DEBUG_PUTS("API_COMM: RE-POLL(NO EVENTS)...");
		}	// while loop

    } catch (zushisa9tt::blehomecentral::tt_apicomm_exception& exp) {
		throw exp;
	}
}


// end of api_comm.cpp
