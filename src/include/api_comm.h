#ifndef API_COMM_H
#define API_COMM_H

/**
 * api_comm.h
 *
 *    2020/10/19
 */

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <poll.h>


namespace zushisa9tt::blehomecentral {
	
	/**
	 * CONSTEXPR
	 */

	constexpr unsigned int DEFAULT_API_RETRY_DURATION_SECS = 10;
	constexpr unsigned int DEFAULT_API_RETRY_MAX_COUNT = 2;
	constexpr unsigned int DEFAULT_API_THREAD_POOL_CAPACITY = 2;


	/**
	 * STRUCT
	 */

	typedef struct api_config {
		api_config(void) {};
		unsigned int api_retry_duration_secs = DEFAULT_API_RETRY_DURATION_SECS;
		unsigned int api_retry_max_count = DEFAULT_API_RETRY_MAX_COUNT;
		unsigned int api_thread_pool_capacity = DEFAULT_API_THREAD_POOL_CAPACITY;
		bool features_api = true;
	} api_config_t;


	typedef struct api_info {
		api_info(void): protocol("http"), host("127.0.0.1"), port("80"), path("/"), ctype("application/json") {};
		std::string protocol;
		std::string host;
		std::string port;
		std::string path;
		std::string ctype;
	} api_info_t;

	typedef std::map<std::string, std::vector<api_info_t>> apis_map_t;


	/**
	 * CLASS tt_apicomm_exception
	 */

	class tt_apicomm_exception : public std::runtime_error {
		public:
			tt_apicomm_exception(const std::string& msg) : std::runtime_error(msg) {}
	};


	/**
	 * CLASS api_comm
	 */

	class api_comm {
		int _myno;

		apis_map_t _apis_map;

		unsigned int _api_retry_duration_secs;
		unsigned int _api_retry_max_count;
		bool _features_call_api;

		CURL* _curl_handle;
		struct curl_slist* _headers = nullptr;

		void chg_settings(api_config_t& a_conf);

	public:
		api_comm(int idx, api_config_t& a_conf, apis_map_t& a_map);
		~api_comm();

		void send_data(const char* json);
		
	};

	/**
	 * CLASS api_task_queue
	 */

	class api_task_queue {
		std::condition_variable& _cond;
		std::mutex& _mtx;
		std::queue<std::string> _queue;

	public:
		api_task_queue(std::condition_variable& t_cond, std::mutex& t_mtx): _cond(t_cond), _mtx(t_mtx) {}
		~api_task_queue() {}

		bool empty() { return _queue.empty(); }
		std::string front_pop(std::unique_lock<std::mutex>& lk);
		void push_task(const std::string& jstr);

	};


	/** 
	 * CLASS api_thread_pool
	 */

	class api_thread_pool {
		std::condition_variable& _cond;
		std::mutex& _mtx;
		std::unique_ptr<api_task_queue>& _queue;
		bool _running;

		api_config_t _api_conf;
		apis_map_t _apis_map;

		std::vector<std::thread> _api_workers;

	public:
		api_thread_pool(std::condition_variable& t_cond, std::mutex& t_mtx, std::unique_ptr<api_task_queue>& t_q);
		~api_thread_pool() {}

		void chg_settings(api_config_t& a_conf, apis_map_t& a_map);
		void run(void);
		void stop(void);

	};


	/**
	 * CLASS apicomm_worker
	 */

	class apicomm_worker {
		api_config_t _api_conf;
		apis_map_t _apis_map;

		std::condition_variable& _pool_cond;
		bool& _pool_running;
		std::mutex& _task_mtx;
		std::unique_ptr<api_task_queue>& _task_queue;

	public:
		apicomm_worker(api_config_t& a_conf, apis_map_t& a_map, std::condition_variable& p_cond, bool& p_run, std::mutex& t_mtx, std::unique_ptr<api_task_queue>& t_q);
		apicomm_worker(const apicomm_worker& rhs);
		~apicomm_worker() {}

		void operator()(int idx);

	};

};	// namespace zushisa9tt::blehomecentral


/**
 * CLASS api_comm (OBSOLETE)
 */

class api_comm {
	bool _features_call_api = true;

	zushisa9tt::blehomecentral::apis_map_t _apis_map;

	int RETRY_MAX_COUNT = 2;
	int RETRY_SLEEP_SECS = 10;

	CURL* _curl_handle;
	struct curl_slist* _headers = nullptr;
	struct pollfd _fds_poll[2];

	void send_data(const char* json);

public:
	api_comm(int fd_sighup, int fd_read);
	~api_comm();

	void chg_settings(bool f_call_api, zushisa9tt::blehomecentral::apis_map_t& a_map);
	void start(void);

};


// end of api_comm.h

#endif
