#ifndef BLESCAN_H
#define BLESCAN_H

/**
 *  scanner.h
 *
 *    2020/09/12
 */

#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
//#include <stdexcept>

#include <poll.h>

#include "api_comm.h"
#include "tph.h"


namespace zushisa9tt::blehomecentral {

	/**
	 * STRUCT
	 */

	typedef struct svr_config {
		svr_config(void) {};
		std::set<std::string> white_list;
	} svr_config_t;


	/**
	 * CLASS central_worker
	 */

	class central_worker {
		int _fd_sig, _fd_w;
		std::set<std::string> _wh_list;

		std::unique_ptr<api_task_queue>& _task_queue;

	public:
		central_worker(int fd_sighup, int fd_write, std::unique_ptr<api_task_queue>& t_q):
		 _fd_sig(fd_sighup), _fd_w(fd_write), _task_queue(t_q) {}
		central_worker(const central_worker& rhs);
		~central_worker() {}

		void chg_central_settings(std::set<std::string>& whitelist);
		// OBSOLETE void operator()(struct pollfd (&fds)[2], tph_datastore& datastore, std::exception_ptr& ep);
		// OBSOLETE void operator()(tph_datastore& datastore, std::exception_ptr& ep);
		void operator()(std::exception_ptr& ep);

	};


	/**
	 * CLASS scheduler
	 */

	class scheduler {
		static constexpr int DURATION_SEC_OF_ACT = 300;

		std::condition_variable _cond;
		std::exception_ptr _ep_central, _ep_apicomm;
		std::mutex _mtx;
		int _pipe_notify_to_central[2];
		int _pipe_notify_to_apicomm[2];
		int _pipe_data_from_central[2];
		bool _rcv_sigint;
		std::thread /* OBSOLETE _th_ac,*/ _th_cl;
		// OBSOLETE apicomm_worker* _worker_ac;
		std::unique_ptr<central_worker> _worker_cl;

		std::condition_variable _pool_cond;
		std::mutex _task_mtx;
		std::unique_ptr<api_task_queue> _task_queue;
		std::unique_ptr<api_thread_pool> _api_thread_pool;

		int get_sec_for_alarm_00(void);
		void reset_pipes(void);
		void start_scanning_peripherals(void);
		void stop_scanning_peripherals(void);

	public:
		scheduler(void);
		~scheduler();

		void chg_apicomm_settings(api_config_t& s_conf, apis_map_t& apis);
		void chg_central_settings(svr_config_t& s_conf);
		void run(void);
		void sigint(void);

	};


};


/**
 * CLASS apicomm_worker (OBSOLETE)
 */

class apicomm_worker {
	std::unique_ptr<api_comm> _apicomm;
	int _fd_sig, _fd_r;

public:
	apicomm_worker(int fd_sighup, int fd_read);
	~apicomm_worker() {}

	void chg_apicomm_settings(bool f_call_api, zushisa9tt::blehomecentral::apis_map_t& a_info);
	//void operator()(struct pollfd (&fds)[1], std::exception_ptr& ep);
	void operator()(std::exception_ptr& ep);

};


/**
 * CLASS scanner (OBSOLETE)
 */

class scanner {
	int _pipe_notify_to_scanner[2];
	int _pipe_notify_to_sender[2];
	int _pipe_data_from_scanner[2];

public:
	scanner(void);
	~scanner();

	int getfd_of_notify_signal_4scanner() { return _pipe_notify_to_scanner[1]; }
	int getfd_of_notify_signal_4sender() { return _pipe_notify_to_sender[1]; }

	void run(void);

};


// end of scanner.h

#endif
