#ifndef API_COMM_H
#define API_COMM_H

/**
 * api_comm.h
 *
 *    2020/10/19
 */

#include <set>
#include <stdexcept>
#include <string>

#include <curl/curl.h>
#include <poll.h>


/**
 * STRUCT
 */

typedef struct api_info {
	api_info(void): protocol("https"), host("127.0.0.1"), port("80"), path("/"), ctype("application/json") {};
    std::string protocol;
    std::string host;
    std::string port;
	std::string path;
    std::string ctype;
} api_info_t;


/**
 * CLASS api_comm
 */

class api_comm {
	bool _features_call_api = true;

	std::string _protocol = "https";
	std::string _host = "127.0.0.1";
	std::string _port = "443";
	std::string _path = "/";
	std::string _ctype = "application/json";

	int RETRY_MAX_COUNT = 5;
	int RETRY_SLEEP_SECS = 30;

	CURL* _curl_handle;
	struct curl_slist* _headers = nullptr;
	struct pollfd _fds_poll[2];

	void send_data(const char* json);

public:
	api_comm(int fd_sighup, int fd_read);
	~api_comm();

	void chg_settings(bool f_call_api, api_info_t& info);
	void start(void);

};


/**
 * CLASS tt_apicomm_exception
 */

class tt_apicomm_exception : public std::runtime_error {
    public:
        tt_apicomm_exception(const std::string& msg) : std::runtime_error(msg) {}
};


// end of api_comm.h

#endif
