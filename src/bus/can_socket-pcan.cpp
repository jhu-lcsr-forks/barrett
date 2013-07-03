/*
	Copyright 2013 Barrett Technology <support@barrett.com>

	This file is part of libbarrett.

	This version of libbarrett is free software: you can redistribute it
	and/or modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This version of libbarrett is distributed in the hope that it will be
	useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this version of libbarrett.  If not, see
	<http://www.gnu.org/licenses/>.

	Further, non-binding information about licensing is available at:
	<http://wiki.barrett.com/libbarrett/wiki/LicenseNotes>
*/

/*
 * can_socket-pcan.cpp
 *
 *  Created on: Jun 27, 2013
 *      Author: dc
 */

#include <stdexcept>
#include <string>

#include <errno.h>
#include <fcntl.h>  // O_RDWR

#include <libpcan.h>

#include <boost/lexical_cast.hpp>

#include <barrett/os.h>
#include <barrett/thread/real_time_mutex.h>
#include <barrett/products/puck.h>
#include <barrett/bus/can_socket.h>


namespace barrett {
namespace bus {


namespace detail {
struct can_handle {
	typedef HANDLE handle_type;
	static const handle_type NULL_HANDLE;
	handle_type h;

	static const int TIMEOUT_US;

	can_handle() : h(NULL_HANDLE) {}
	bool isValid() const { return h != NULL_HANDLE; }
};

const can_handle::handle_type can_handle::NULL_HANDLE = NULL;
const int can_handle::TIMEOUT_US = CommunicationsBus::TIMEOUT * 1e6;
}


CANSocket::CANSocket() :
	mutex(), handle(new detail::can_handle)
{
}

CANSocket::CANSocket(int port) throw(std::runtime_error) :
	mutex(), handle(new detail::can_handle)
{
	open(port);
}

CANSocket::~CANSocket()
{
	close();
	delete handle;
	handle = NULL;
}


void CANSocket::open(int port) throw(std::logic_error, std::runtime_error)
{
	if (isOpen()) {
		(logMessage("CANSocket::%s(): This object is already associated with a CAN port.")
				% __func__).raise<std::logic_error>();
	}

	logMessage("CANSocket::open(%d) using PCAN driver") % port;

	int ret;

	std::string devname = "/dev/pcanusb" + boost::lexical_cast<std::string>(port);
	handle->h = LINUX_CAN_Open(devname.c_str(), O_RDWR);
	if ( !handle->isValid() ) {
		(logMessage("CANSocket::%s(): Could not open CAN port. LINUX_CAN_Open(): (%d) %s")
				% __func__ % errno % strerror(errno)).raise<std::runtime_error>();
	}

	ret = CAN_Init(handle->h, CAN_BAUD_1M, CAN_INIT_TYPE_ST);
	if (ret != 0) {
		(logMessage("CANSocket::%s(): Could not configure CAN port. CAN_Init(): (%d) %s")
				% __func__ % -ret % strerror(-ret)).raise<std::runtime_error>();
	}
	//CAN_MsgFilter
}

void CANSocket::close()
{
	if (isOpen()) {
		CAN_Close(handle->h);
		handle->h = detail::can_handle::NULL_HANDLE;
	}
}

bool CANSocket::isOpen() const
{
	return handle->isValid();
}

int CANSocket::send(int busId, const unsigned char* data, size_t len) const
{
	BARRETT_SCOPED_LOCK(mutex);

	TPCANMsg msg;
	msg.ID = busId;
	msg.MSGTYPE = MSGTYPE_STANDARD;
	msg.LEN = len;
	memcpy(msg.DATA, data, len);

	int ret = LINUX_CAN_Write_Timeout(handle->h, &msg, detail::can_handle::TIMEOUT_US);
	if (ret == 0) {
		return 0;
	} else if (ret == CAN_ERR_QXMTFULL) {
		logMessage("CANSocket::%s: LINUX_CAN_Write_Timeout(): "
				"data would block during non-blocking send (output buffer full)")
				% __func__;
		return 1;
	} else {
		logMessage("CANSocket::%s: LINUX_CAN_Write_Timeout(): "
				"returned error code %d")
				% __func__ % ret;
		return 2;
	}
}

int CANSocket::receiveRaw(int& busId, unsigned char* data, size_t& len, bool blocking) const
{
	BARRETT_SCOPED_LOCK(mutex);

	TPCANRdMsg rdMsg;
	int ret = LINUX_CAN_Read_Timeout(handle->h, &rdMsg, blocking ? detail::can_handle::TIMEOUT_US : 0);
	if (ret != 0) {
		if (ret == CAN_ERR_QRCVEMPTY) {
			if (blocking) {
				logMessage("CANSocket::%s: LINUX_CAN_Read_Timeout(): timed out")
						% __func__;
				return 2;
			} else {
				// When non-blocking, timeouts are expected
				return 1;
			}
		}

		logMessage("CANSocket::%s: LINUX_CAN_Read_Timeout(): "
				"returned error code %d")
				% __func__ % ret;
		return 2;
	}

	TPCANMsg* msg = &rdMsg.Msg;
	if (msg->MSGTYPE != MSGTYPE_STANDARD) {
		logMessage("CANSocket::%s: Bad message type (%d)")
				% __func__ % msg->MSGTYPE;
		return 2;
	}
	busId = msg->ID;
	len = msg->LEN;
	memcpy(data, msg->DATA, len);

	return 0;
}


}
}