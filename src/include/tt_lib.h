#ifndef TT_LIB_H
#define TT_LIB_H

/**
 * tt_lib.h
 *
 *    2020/10/03
 */

#include <iostream>
#include <mutex>
#include <string>
#include <cstdio>
#include <vector>

#ifdef DEBUG_BUILD
#define DEBUG_PUTS(str) tt_logger::instance().puts(str);
#define DEBUG_PRINTF(fmt, ...) tt_logger::instance().printf(fmt, __VA_ARGS__);
#else
#define DEBUG_PUTS(str)
#define DEBUG_PRINTF(fmt, ...)
#endif


template <typename ... Args>
std::string format(const std::string& fmt, Args ... args)
{
	size_t len = std::snprintf(nullptr, 0, fmt.c_str(), args ...);
	std::vector<char> buf(len + 1);
	std::snprintf(&buf[0], len + 1, fmt.c_str(), args ...);
	return std::string(&buf[0], &buf[0] + len);
}


class tt_logger {
	std::mutex _mutex;

	tt_logger() = default;
	~tt_logger() = default;
	tt_logger(const tt_logger&) = delete;
	tt_logger& operator=(const tt_logger&) = delete;

public:
	static tt_logger& instance() {
		static tt_logger _instance;
		return _instance;
	}

	template <typename ... Args>
	void printf(const char* const fmt, Args const & ... args) noexcept {
		::printf(fmt, args ...);
	}

	void puts(const char* const msg) {
		std::lock_guard<std::mutex> lock(_mutex);
		std::cout << msg << std::endl;
	}

};


// end of tt_lib.h

#endif
