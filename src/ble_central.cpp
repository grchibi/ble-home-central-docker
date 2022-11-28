/**
 * ble_central.cpp
 *
 *    2020/10/09
 */

#include <iostream>

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ble_central.h"
#include "tph.h"
#include "tt_lib.h"


using namespace std;


ble_central::ble_central(int fd_sighup, int fd_write) {
    _fds_poll[0].fd = fd_sighup;
    _fds_poll[0].events = POLLIN;

    _fd_write = fd_write;

	open_device();
    if (_current_hci_state.state == hci_state::OPEN) {
        DEBUG_PUTS("BLE: OPENED BT DEVICE.");
    } else {
        DEBUG_PUTS("BLE: BT DEVICE NOT OPENED.");
    }
}

ble_central::~ble_central() {
    close_device();
    DEBUG_PUTS("BLE: CLOSED DEVICE.");
}

int ble_central::check_report_filter(uint8_t procedure, le_advertising_info* adv)
{
    uint8_t flags;

    // if no discovery procedure is set, all reports are treat as valid
    if (procedure == 0) return 1;

    // read flags AD type value from the advertising report if it exists
    if (read_flags(&flags, adv->data, adv->length)) return 0;

    switch (procedure) {
        case 'l':   // Limited Discovery Procedure
            if (flags & static_cast<uint8_t>(read_flag::LIMITED_MODE_BIT))
                return 1;
            break;
        case 'g':   // General Discovery Procedure
            if (flags & (static_cast<uint8_t>(read_flag::LIMITED_MODE_BIT) | static_cast<uint8_t>(read_flag::GENERAL_MODE_BIT)))
                return 1;
            break;
        default:
			tt_logger::instance().puts("BLE[ERROR] : unknown discovery procedure.");
    }

    return 0;
}

void ble_central::close_device() {
    if (0 <= _current_hci_state.device_handle)
        hci_close_dev(_current_hci_state.device_handle);
}

void ble_central::open_device() {
	_current_hci_state.device_id = hci_get_route(NULL);

    if ((_current_hci_state.device_handle = hci_open_dev(_current_hci_state.device_id)) < 0) {
		char msgbuff[128] = {0};
		strerror_r(errno, msgbuff, 128);
        throw tt_ble_exception(string("Could not open device: ") + msgbuff);
    }

    // set fd non-blocking
    /*int on = 1;
    if (ioctl(_current_hci_state.device_handle, FIONBIO, (char*)&on) < 0) {
		char msgbuff[128] = {0};
		strerror_r(errno, msgbuff, 128);
		throw tt_ble_exception(string("Could not set device to non-blocking: ") + msgbuff);
    }*/

    _current_hci_state.state = hci_state::OPEN;
}

void ble_central::scan_advertising_devices(tph_datastore& datastore, std::set<std::string>& wh_list, int dev_handle, uint8_t f_type)
{
    struct hci_filter of, nf;

    socklen_t olen = sizeof(of);
    if (getsockopt(dev_handle, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
		throw tt_ble_exception("Could not get socket options.");
    }

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);

    if (setsockopt(dev_handle, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
		throw tt_ble_exception("Could not set socket options.");
    }

	_fds_poll[1].fd = dev_handle;
	_fds_poll[1].events = POLLIN;

	int len;
    try {
		while (1) {
			if (poll(_fds_poll, 2, -1) < 0 && errno != EINTR) {
				char msgbuff[128] = {0};
				strerror_r(errno, msgbuff, 128);
				tt_logger::instance().printf("BLE[ERROR]: poll error occurred while scanning. %s\n", msgbuff);
				continue;
			}

			if (_fds_poll[0].revents & POLLIN) {
				DEBUG_PUTS("BLE: SIGNAL NOTIFIED");
				throw tt_ble_exception("read signal notify from pipe.");
			}

			if (_fds_poll[1].revents & POLLIN) {
            	evt_le_meta_event* meta;
            	unsigned char buff[HCI_MAX_EVENT_SIZE] = {0};
				
				if ((len = read(dev_handle, buff, sizeof(buff))) < 0) {
					if (errno == EAGAIN) {
						DEBUG_PUTS("BLE: EAGAIN");
						continue;
					} else if (errno == EINTR) {
						DEBUG_PUTS("BLE: read RECEIVED ANY SIGNAL");
						continue;
					} else {
                    	throw tt_ble_exception("read error.");
					}
				}

            	unsigned char* ptr = buff + (1 + HCI_EVENT_HDR_SIZE);
            	len -= (1 + HCI_EVENT_HDR_SIZE);

            	meta = (evt_le_meta_event*)ptr;
				if (meta->subevent != 0x02)
					throw tt_ble_exception("evt_le_meta_event.subevent != 0x02");

				// ignoring multiple reports
				le_advertising_info* adv_info = (le_advertising_info*)(meta->data + 1);

				if (check_report_filter(f_type, adv_info)) {
					unique_ptr<tph_data> tphdata = datastore.store(*adv_info, false);

					if (tphdata->is_valid(wh_list))	{	// has BME280 data.
						string jdata = tphdata->create_json_data();
						if (write(_fd_write, jdata.c_str(), jdata.size()) < 0) {
							char msgbuff[128] = {0};
							strerror_r(errno, msgbuff, 128);
							DEBUG_PRINTF("BLE: WRITE ERROR OCCURRED. %s\n", msgbuff);
							throw tt_ble_exception(string("write error. ") + msgbuff);
						}
						DEBUG_PUTS("BLE: WROTE DATA TO API COMM");
						//cout << tphdata.create_json_data() << endl;
					}
				}

				DEBUG_PUTS("BLE: RE-POLL(READ EVENTS)");

			} else {
				DEBUG_PUTS("BLE: RE-POLL(NO EVENTS)...");
			}
		}	// while loop

    } catch (tt_ble_exception& exp) {
		setsockopt(dev_handle, SOL_HCI, HCI_FILTER, &of, sizeof(of));
		throw exp;
	}

    setsockopt(dev_handle, SOL_HCI, HCI_FILTER, &of, sizeof(of));
}

int ble_central::read_flags(uint8_t *flags, const uint8_t *data, size_t size)
{
    size_t offset;

    if (!flags || !data)
        return -EINVAL;

    offset = 0;
    while (offset < size) {
        uint8_t len = data[offset];

        /* Check if it is the end of the significant part */
        if (len == 0)
            break;

        if (len + offset > size)
            break;

        uint8_t type = data[offset + 1];

        if (type == static_cast<uint8_t>(read_flag::AD_TYPE)) {
            *flags = data[offset + 2];
            return 0;
        }

        offset += 1 + len;
    }

    return -ENOENT;
}

void ble_central::start_hci_scan(tph_datastore& datastore, std::set<std::string>& wh_list) {
	uint8_t scan_type = 0x01;
    uint16_t interval = htobs(0x4000/*0x0010*/);		// N*0.625ms => 10240ms
    uint16_t window = htobs(0x0010);
    uint8_t own_type = LE_PUBLIC_ADDRESS;
    uint8_t filter_policy = 0x00;
    uint8_t filter_dup = 0x00;		// 0x01;
    uint8_t filter_type = 0;

    if (hci_le_set_scan_parameters(_current_hci_state.device_handle, scan_type, interval, window, own_type, filter_policy, 10000) < 0) {
		char msgbuff[128] = {0};
		strerror_r(errno, msgbuff, 128);
		tt_logger::instance().printf("BLE[WARN]: Failed to set scan parameters, %s\n", msgbuff);
    }

    if (hci_le_set_scan_enable(_current_hci_state.device_handle, 0x01, filter_dup, 10000) < 0) {
		char msgbuff[128] = {0};
		strerror_r(errno, msgbuff, 128);
		tt_logger::instance().printf("BLE[WARN]: Failed to enable scan, %s\n", msgbuff);
    }

    _current_hci_state.state = hci_state::SCANNING;
	DEBUG_PUTS("BLE: Scanning...");

	try {
		scan_advertising_devices(datastore, wh_list, _current_hci_state.device_handle, filter_type);

	} catch(tt_ble_exception& ex) {
		tt_logger::instance().printf("BLE[ERROR]: Could not receive advertising events, %s\n", ex.what());
    }

    if (hci_le_set_scan_enable(_current_hci_state.device_handle, 0x00, filter_dup, 10000) < 0) {
		char msgbuff[128] = {0};
		strerror_r(errno, msgbuff, 128);
        throw tt_ble_exception(string("Failed to disable scan: ") + msgbuff);
    }
}


// end of ble_central.cpp
