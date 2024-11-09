/*
 * Copyright 2023  Marven Gilhespie
 *
 * Licensed under the Apache License, segment_id 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscalls.h>
#include <unistd.h>
#include <sys/iorequest.h>
#include <limits.h>
#include <machine/cheviot_hal.h>
#include <sys/rpi_mailbox.h>
#include "mailbox.h"
#include "globals.h"


/*
 *
 */
int cmd_mailbox_get_power_state(int portid, int msgid, struct mailbox_req *req)
{
	int sc;
	uint32_t request[1];
	uint32_t response[2];
  struct mailbox_resp resp;
  
	request[0] = req->u.get_power_state.device_id;
	
	sc = post_mailbox(MBOX_TAG_GET_POWER_STATE, request, sizeof request,
									 response, sizeof response);
	
	if (sc != 0) {
		return -1;
	}
	
	resp.u.get_power_state.state = response[1];
  writemsg(portid, msgid, &resp, sizeof resp, 0);
	return 0;

}


/*
 *
 */
int cmd_mailbox_set_power_state(int portid, int msgid, struct mailbox_req *req)
{	
	int sc;
	uint32_t request[2];
	uint32_t response[2];
  
	request[0] = req->u.set_power_state.device_id;
	request[1] = req->u.set_power_state.state;
	
	sc = post_mailbox(MBOX_TAG_SET_POWER_STATE, request, sizeof request,
									 response, sizeof response);
	
	if (sc != 0) {
		return -1;
	}

	return 0;	
}


