/******************************************************************************
 *
 *  $Id$
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
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
   Mailbox functionality.
*/

/*****************************************************************************/

#include <linux/slab.h>
#include <linux/delay.h>

#include "mailbox.h"
#include "datagram.h"
#include "master.h"

/*****************************************************************************/

/**
   Prepares a mailbox-send datagram.
   \return Pointer to mailbox datagram data, or ERR_PTR() code.
*/

uint8_t *ec_slave_mbox_prepare_send(const ec_slave_t *slave, /**< slave */
                                    ec_datagram_t *datagram, /**< datagram */
                                    uint8_t type, /**< mailbox protocol */
                                    size_t size /**< size of the data */
                                    )
{
    size_t total_size;
    int ret;

    if (unlikely(!slave->sii_image)) {
        EC_SLAVE_ERR(slave, "Slave cannot verify if mailbox communication"
                " is supported!\n");
        return ERR_PTR(-EAGAIN);
    }

    if (unlikely(!slave->sii_image->sii.mailbox_protocols)) {
        EC_SLAVE_ERR(slave, "Slave does not support mailbox"
                " communication!\n");
        return ERR_PTR(-EPROTONOSUPPORT);
    }

    total_size = EC_MBOX_HEADER_SIZE + size;

    if (unlikely(total_size > slave->configured_rx_mailbox_size)) {
        EC_SLAVE_ERR(slave, "Data size (%zu) does not fit in mailbox (%u)!\n",
                total_size, slave->configured_rx_mailbox_size);
        return ERR_PTR(-EOVERFLOW);
    }

    ret = ec_datagram_fpwr(datagram, slave->station_address,
            slave->configured_rx_mailbox_offset,
            slave->configured_rx_mailbox_size);
    if (ret) {
        return ERR_PTR(ret);
    }

    EC_WRITE_U16(datagram->data,     size); // mailbox service data length
    EC_WRITE_U16(datagram->data + 2, slave->station_address); // station addr.
    EC_WRITE_U8 (datagram->data + 4, 0x00); // channel & priority
    EC_WRITE_U8 (datagram->data + 5, type); // underlying protocol type

    return datagram->data + EC_MBOX_HEADER_SIZE;
}

/*****************************************************************************/

/**
   Prepares a datagram for checking the mailbox state.
   \todo Determine sync manager used for receive mailbox
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_prepare_check(const ec_slave_t *slave, /**< slave */
                                ec_datagram_t *datagram /**< datagram */
                                )
{
    int ret = ec_datagram_fprd(datagram, slave->station_address, 0x808, 8);
    if (ret)
        return ret;

    ec_datagram_zero(datagram);
    return 0;
}

/*****************************************************************************/

/**
   Processes a mailbox state checking datagram.
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_check(const ec_datagram_t *datagram /**< datagram */)
{
    return EC_READ_U8(datagram->data + 5) & 8 ? 1 : 0;
}

/*****************************************************************************/

/**
   Prepares a datagram to fetch mailbox data.
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_prepare_fetch(const ec_slave_t *slave, /**< slave */
                                ec_datagram_t *datagram /**< datagram */
                                )
{
    int ret = ec_datagram_fprd(datagram, slave->station_address,
            slave->configured_tx_mailbox_offset,
            slave->configured_tx_mailbox_size);
    if (ret)
        return ret;

    ec_datagram_zero(datagram);
    return 0;
}

/*****************************************************************************/

/**
   Mailbox error codes.
*/

const ec_code_msg_t mbox_error_messages[] = {
    {0x00000001, "MBXERR_SYNTAX"},
    {0x00000002, "MBXERR_UNSUPPORTEDPROTOCOL"},
    {0x00000003, "MBXERR_INVAILDCHANNEL"},
    {0x00000004, "MBXERR_SERVICENOTSUPPORTED"},
    {0x00000005, "MBXERR_INVALIDHEADER"},
    {0x00000006, "MBXERR_SIZETOOSHORT"},
    {0x00000007, "MBXERR_NOMOREMEMORY"},
    {0x00000008, "MBXERR_INVALIDSIZE"},
    {}
};

/*****************************************************************************/

/** Processes received mailbox data.
 *
 * \return Pointer to the received data, or ERR_PTR() code.
 */
uint8_t *ec_slave_mbox_fetch(const ec_slave_t *slave, /**< slave */
                             ec_mbox_data_t *response_data, /**< response data */
                             uint8_t *type, /**< expected mailbox protocol */
                             size_t *size /**< size of the received data */
                             )
{
    size_t data_size;

    if (!response_data->data) {
        EC_SLAVE_ERR(slave, "No mailbox response data received!\n");
        return ERR_PTR(-EPROTO);
    }

    data_size = EC_READ_U16(response_data->data);

    if (data_size + EC_MBOX_HEADER_SIZE > slave->configured_tx_mailbox_size) {
        EC_SLAVE_ERR(slave, "Corrupt mailbox response received!\n");
        ec_print_data(response_data->data, slave->configured_tx_mailbox_size);
        return ERR_PTR(-EPROTO);
    }

#if 0
    if (slave->master->debug_level) {
        EC_SLAVE_DBG(slave, 1, "Mailbox data:\n");
        ec_print_data(datagram->data, EC_MBOX_HEADER_SIZE + data_size);
    }
#endif

    *type = EC_READ_U8(response_data->data + 5) & 0x0F;
    *size = data_size;

    if (*type == 0x00) {
        const ec_code_msg_t *mbox_msg;
        uint16_t code = EC_READ_U16(response_data->data + 8);

        EC_SLAVE_ERR(slave, "Mailbox error response received - ");

        for (mbox_msg = mbox_error_messages; mbox_msg->code; mbox_msg++) {
            if (mbox_msg->code != code)
                continue;
            printk(KERN_CONT "Code 0x%04X: \"%s\".\n",
                    mbox_msg->code, mbox_msg->message);
            break;
        }

        if (!mbox_msg->code) {
            printk(KERN_CONT "Unknown error reply code 0x%04X.\n", code);
        }

        if (slave->master->debug_level && data_size > 0) {
            ec_print_data(response_data->data + EC_MBOX_HEADER_SIZE, data_size);
        }

        return ERR_PTR(-EPROTO);
    }

    return response_data->data + EC_MBOX_HEADER_SIZE;
}

/*****************************************************************************/
