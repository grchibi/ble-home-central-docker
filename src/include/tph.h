#ifndef TPH_H
#define TPH_H

/**
 * tph.h
 *
 *    2020/10/03
 */

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#define BME280_NAME_PREWORD "BME280_BEACON"
#define EIR_NAME_SHORT      0x08
#define EIR_NAME_COMPLETE   0x09
#define EIR_MANUFACTURER_DATA   0xff


/**
 * CLASS tph_initialization_error
 */

class tph_initialization_error : public std::runtime_error {
    public:
        tph_initialization_error(const std::string& msg) : std::runtime_error(msg) {}
};


/**
 * CLASS tph_data
 */

class tph_data {
	char _addr[19];
	char _name[31];

	std::string _dt;
	float _t, _p, _h;

	void _decode_advertisement_data(const char* src);
	void _initialize(const le_advertising_info& advinfo);

public:
	tph_data(void) : _addr{0}, _name{0}, _t(0), _p(0), _h(0) {}
	tph_data(const le_advertising_info& advinfo);
	tph_data(const tph_data& src);
	tph_data(tph_data&& src);
	~tph_data() {}

	std::string create_json_data(void);
	void debug_puts(void);
	bool is_valid(void);
	bool is_valid(std::set<std::string>& wh_list);
	void update(const le_advertising_info& advinfo);

	tph_data& operator=(const tph_data& src);
	tph_data& operator=(tph_data&& src);

};


/**
 * CLASS tph_datastore
 */

class tph_datastore {
	std::mutex _mtx;
	std::map<std::string, tph_data> _store;

public:
	typedef std::function<void(const std::string&, const std::string&)> func_type;

	void clear(void);
	void processIteratively(const func_type& func);
	void processIteratively(const func_type& func, std::set<std::string>& wh_list);
	std::unique_ptr<tph_data> store(const le_advertising_info& advinfo, bool over_write);

};


// end of tph.h

#endif
