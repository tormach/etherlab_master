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

/** \file
 * EtherCAT FMMU configuration methods.
 */

/*****************************************************************************/

#include "globals.h"
#include "slave_config.h"
#include "master.h"

#include "fmmu_config.h"

/*****************************************************************************/

/** FMMU configuration constructor.
 *
 * Inits an FMMU configuration, sets the logical start address and adds the
 * process data size for the mapped PDOs of the given direction to the domain
 * data size.
 */
void ec_fmmu_config_init(
        ec_fmmu_config_t *fmmu, /**< EtherCAT FMMU configuration. */
        ec_slave_config_t *sc, /**< EtherCAT slave configuration. */
        ec_domain_t *domain, /**< EtherCAT domain. */
        uint8_t sync_index, /**< Sync manager index to use. */
        ec_direction_t dir /**< PDO direction. */
        )
{
    INIT_LIST_HEAD(&fmmu->list);
    fmmu->sc = sc;
    fmmu->sync_index = sync_index;
    fmmu->dir = dir;

    fmmu->logical_domain_offset = 0;
    fmmu->data_size = 0;

    ec_domain_add_fmmu_config(domain, fmmu);
}

void ec_fmmu_set_domain_offset_size(
        ec_fmmu_config_t *fmmu, /**< EtherCAT FMMU configuration. */
        uint32_t logical_domain_offset, /**< Logical offset address
            relative to domain->logical_base_address. */
        unsigned data_size /**< Covered PDO size. */
        )
{
    fmmu->logical_domain_offset = logical_domain_offset;
    fmmu->data_size = data_size;
}

/*****************************************************************************/

/** Initializes an FMMU configuration page.
 *
 * The referenced memory (\a data) must be at least EC_FMMU_PAGE_SIZE bytes.
 */
void ec_fmmu_config_page(
        const ec_fmmu_config_t *fmmu, /**< EtherCAT FMMU configuration. */
        const ec_sync_t *sync, /**< Sync manager. */
        uint8_t *data /**> Configuration page memory. */
        )
{
    EC_CONFIG_DBG(fmmu->sc, 1, "FMMU: LogOff 0x%08X, Size %3u,"
            " PhysAddr 0x%04X, SM%u, Dir %s\n",
            fmmu->logical_domain_offset, fmmu->data_size,
            sync->physical_start_address, fmmu->sync_index,
            fmmu->dir == EC_DIR_INPUT ? "in" : "out");

    EC_WRITE_U32(data,      fmmu->domain->logical_base_address +
        fmmu->logical_domain_offset);
    EC_WRITE_U16(data + 4,  fmmu->data_size); // size of fmmu
    EC_WRITE_U8 (data + 6,  0x00); // logical start bit
    EC_WRITE_U8 (data + 7,  0x07); // logical end bit
    EC_WRITE_U16(data + 8,  sync->physical_start_address);
    EC_WRITE_U8 (data + 10, 0x00); // physical start bit
    EC_WRITE_U8 (data + 11, fmmu->dir == EC_DIR_INPUT ? 0x01 : 0x02);
    EC_WRITE_U16(data + 12, 0x0001); // enable
    EC_WRITE_U16(data + 14, 0x0000); // reserved
}

/*****************************************************************************/
