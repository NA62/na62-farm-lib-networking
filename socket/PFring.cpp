/*
 *
 * (C) 2007-09 - Luca Deri <deri@ntop.org>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "PFring.h"
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <errno.h>

#include <unistd.h>

namespace ntop {

/* *********************************************** */

PFring::PFring(pfring* _ring, char* _device_name, u_int32_t _snaplen,
		u_int32_t flags): ring(_ring), snaplen(_snaplen) {
	device_name = strdup(_device_name);
}

/* *********************************************** */

PFring::~PFring() {
	if (ring) {
		if (device_name)
			free(device_name);
		pfring_close(ring);
		ring = NULL;
	}
}

/* *********************************************** */

int PFring::add_bpf_filter(char *the_filter) {
	if (ring == NULL)
		return (-1);
	else
		return (pfring_set_bpf_filter(ring, the_filter));
}

/* *********************************************** */

int PFring::get_next_packet(struct pfring_pkthdr *hdr, char** pkt,
		u_int pkt_len, u_int8_t wait_for_incoming_packet) {
	if ((!ring) || (!hdr))
		return (-1);
	return (pfring_recv(ring, (u_char**) pkt, pkt_len, hdr,
			wait_for_incoming_packet));
}

/* *********************************************** */
int PFring::start_loop(pfringProcesssPacket looper) {
	return pfring_loop(ring, looper, (u_char*) NULL,
			1 /* wait_for_incoming_packet */);
}

/* *********************************************** */
int PFring::send_packet(char *pkt, u_int pkt_len, bool flush,
		bool active_poll) {
	if (ring == NULL)
		return (-1);

	if (unlikely(pkt_len > 1500 /* no jumbo frames! */))
		return -1;

	int rc = -1;
	while (rc < 0) {
		rc = pfring_send(ring, pkt, pkt_len, flush);

		if (!active_poll) {
			if (rc < 0) {
				/* Not enough space in buffer */
				usleep(100);
				pfring_poll(ring, 0);
			}
		}
	}
	return rc;
}

/* *********************************************** */

bool PFring::wait_for_packets(int msec) {
	struct pollfd pfd;
	int rc;

	/* Sleep when nothing is happening */
	pfd.fd = get_socket_id();
	pfd.events = POLLIN | POLLERR;
	pfd.revents = 0;

	errno = 0;
	rc = poll(&pfd, 1, msec);

	if (rc == -1)
		return (false);
	else
		return ((rc > 0) ? true : false);
}

} // namespace ntop
