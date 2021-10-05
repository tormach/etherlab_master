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
   EtherCAT domain structure.
*/

/*****************************************************************************/

#ifndef __EC_DOMAIN_H__
#define __EC_DOMAIN_H__

#include <linux/list.h>

#include "globals.h"
#include "datagram.h"
#include "master.h"
#include "fmmu_config.h"

/*****************************************************************************/

/** EtherCAT domain.
 *
 * Handles the process data and the therefore needed datagrams of a certain
 * group of slaves.
 */
struct ec_domain
{
    struct list_head list; /**< List item. */
    ec_master_t *master; /**< EtherCAT master owning the domain. */
    unsigned int index; /**< Index (just a number). */

    struct list_head fmmu_configs; /**< FMMU configurations contained. */
    size_t data_size; /**< Size of the process data. */
    uint8_t *data; /**< Memory for the process data. */
    ec_origin_t data_origin; /**< Origin of the \a data memory. */
    uint32_t logical_base_address; /**< Logical offset address of the
                                     process data. */
    struct list_head datagram_pairs; /**< Datagrams pairs (main/backup) for
                                       process data exchange. */
    uint16_t working_counter[EC_MAX_NUM_DEVICES]; /**< Last working counter
                                                values. */
    uint16_t expected_working_counter; /**< Expected working counter. */
    unsigned int working_counter_changes; /**< Working counter changes
                                             since last notification. */
    unsigned int redundancy_active; /**< Non-zero, if redundancy is in use. */
    unsigned long notify_jiffies; /**< Time of last notification. */
    uint32_t offset_used[EC_DIR_COUNT]; /**< Next available domain offset of
        PDO, by direction */
    const ec_slave_config_t *sc_in_work; /**< slave_config which is actively
        being registered in this domain
        (i.e. ecrt_slave_config_reg_pdo_entry() ) */
};

/*****************************************************************************/

void ec_domain_init(ec_domain_t *, ec_master_t *, unsigned int);
void ec_domain_clear(ec_domain_t *);

void ec_domain_add_fmmu_config(ec_domain_t *, ec_fmmu_config_t *);
int ec_domain_finish(ec_domain_t *, uint32_t);

unsigned int ec_domain_fmmu_count(const ec_domain_t *);
const ec_fmmu_config_t *ec_domain_find_fmmu(const ec_domain_t *, unsigned int);

/*****************************************************************************/

#endif
