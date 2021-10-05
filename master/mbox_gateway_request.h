/******************************************************************************
 *
 *  $Id$
 *
 *  Copyright (C) 2019  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  ---
 *
 *  The license mentioned above concerns the source code only. Using the
 *  EtherCAT technology and brand is only permitted in compliance with the
 *  industrial property and similar rights of Beckhoff Automation GmbH.
 *
 *****************************************************************************/

/**
   \file
   EtherCAT Mailbox Gateway request structure.
*/

/*****************************************************************************/

#ifndef __EC_MBG_REQUEST_H__
#define __EC_MBG_REQUEST_H__

#include <linux/list.h>

#include "globals.h"

/*****************************************************************************/

/** EtherCAT Mailbox Gateway request.
 */
typedef struct {
    struct list_head list; /**< List item. */
    uint8_t *data; /**< Pointer to MBox request data. */
    size_t mem_size; /**< Size of MBox request data memory. */
    size_t data_size; /**< Size of MBox request data. */
    uint32_t response_timeout; /**< Maximum time in ms, the transfer is
                                 retried, if the slave does not respond. */
    ec_internal_request_state_t state; /**< Request state. */
    unsigned long jiffies_sent; /**< Jiffies, when the upload/download
                                     request was sent. */
    uint16_t error_code; /**< MBox Gateway error code. */
    uint8_t mbox_type; /**< Cached MBox type */
} ec_mbg_request_t;

/*****************************************************************************/

void ec_mbg_request_init(ec_mbg_request_t *);
void ec_mbg_request_clear(ec_mbg_request_t *);

int ec_mbg_request_alloc(ec_mbg_request_t *, size_t);
int ec_mbg_request_copy_data(ec_mbg_request_t *, const uint8_t *, size_t);
void ec_mbg_request_run(ec_mbg_request_t *);

/*****************************************************************************/

#endif
