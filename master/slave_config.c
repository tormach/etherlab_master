/******************************************************************************
 *
 *  $Id$
 *
 *  Copyright (C) 2006-2012  Florian Pose, Ingenieurgemeinschaft IgH
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
 *  vim: expandtab
 *
 *****************************************************************************/

/**
   \file
   EtherCAT slave configuration methods.
*/

/*****************************************************************************/

#include <linux/module.h>
#include <linux/slab.h>

#include "globals.h"
#include "master.h"
#include "voe_handler.h"
#include "flag.h"

#include "slave_config.h"

/*****************************************************************************/

/** Slave configuration constructor.
 *
 * See ecrt_master_slave_config() for the usage of the \a alias and \a
 * position parameters.
 */
void ec_slave_config_init(
        ec_slave_config_t *sc, /**< Slave configuration. */
        ec_master_t *master, /**< EtherCAT master. */
        uint16_t alias, /**< Slave alias. */
        uint16_t position, /**< Slave position. */
        uint32_t vendor_id, /**< Expected vendor ID. */
        uint32_t product_code /**< Expected product code. */
        )
{
    unsigned int i;

    sc->master = master;

    sc->alias = alias;
    sc->position = position;
    sc->vendor_id = vendor_id;
    sc->product_code = product_code;
    sc->watchdog_divider = 0; // use default
    sc->allow_overlapping_pdos = 0; // default not allowed
    sc->watchdog_intervals = 0; // use default

    sc->slave = NULL;

    for (i = 0; i < EC_MAX_SYNC_MANAGERS; i++)
        ec_sync_config_init(&sc->sync_configs[i]);

    sc->used_fmmus = 0;
    sc->dc_assign_activate = 0x0000;
    sc->dc_sync[0].cycle_time = 0U;
    sc->dc_sync[1].cycle_time = 0;
    sc->dc_sync[0].shift_time = 0U;
    sc->dc_sync[1].shift_time = 0;

    INIT_LIST_HEAD(&sc->sdo_configs);
    INIT_LIST_HEAD(&sc->sdo_requests);
    INIT_LIST_HEAD(&sc->reg_requests);
    INIT_LIST_HEAD(&sc->voe_handlers);
    INIT_LIST_HEAD(&sc->soe_configs);
    INIT_LIST_HEAD(&sc->flags);

    ec_coe_emerg_ring_init(&sc->emerg_ring, sc);
}

/*****************************************************************************/

/** Slave configuration destructor.
 *
 * Clears and frees a slave configuration object.
 */
void ec_slave_config_clear(
        ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    unsigned int i;
    ec_sdo_request_t *req, *next_req;
    ec_voe_handler_t *voe, *next_voe;
    ec_reg_request_t *reg, *next_reg;
    ec_soe_request_t *soe, *next_soe;
    ec_flag_t *flag, *next_flag;

    ec_slave_config_detach(sc);

    // Free sync managers
    for (i = 0; i < EC_MAX_SYNC_MANAGERS; i++)
        ec_sync_config_clear(&sc->sync_configs[i]);

    // free all SDO configurations
    list_for_each_entry_safe(req, next_req, &sc->sdo_configs, list) {
        list_del(&req->list);
        ec_sdo_request_clear(req);
        kfree(req);
    }

    // free all SDO requests
    list_for_each_entry_safe(req, next_req, &sc->sdo_requests, list) {
        list_del(&req->list);
        ec_sdo_request_clear(req);
        kfree(req);
    }

    // free all register requests
    list_for_each_entry_safe(reg, next_reg, &sc->reg_requests, list) {
        list_del(&reg->list);
        ec_reg_request_clear(reg);
        kfree(reg);
    }

    // free all VoE handlers
    list_for_each_entry_safe(voe, next_voe, &sc->voe_handlers, list) {
        list_del(&voe->list);
        ec_voe_handler_clear(voe);
        kfree(voe);
    }

    // free all SoE configurations
    list_for_each_entry_safe(soe, next_soe, &sc->soe_configs, list) {
        list_del(&soe->list);
        ec_soe_request_clear(soe);
        kfree(soe);
    }

    // free all flags
    list_for_each_entry_safe(flag, next_flag, &sc->flags, list) {
        list_del(&flag->list);
        ec_flag_clear(flag);
        kfree(flag);
    }

    ec_coe_emerg_ring_clear(&sc->emerg_ring);
}

/*****************************************************************************/

/** Prepares an FMMU configuration.
 *
 * Configuration data for the FMMU is saved in the slave config structure and
 * is written to the slave during the configuration. The FMMU configuration
 * is done in a way, that the complete data range of the corresponding sync
 * manager is covered. Seperate FMMUs are configured for each domain. If the
 * FMMU configuration is already prepared, the function does nothing and
 * returns with success.
 *
 * \retval >=0 Success, logical offset byte address.
 * \retval  <0 Error code.
 */
int ec_slave_config_prepare_fmmu(
        ec_slave_config_t *sc, /**< Slave configuration. */
        ec_domain_t *domain, /**< Domain. */
        uint8_t sync_index, /**< Sync manager index. */
        ec_direction_t dir /**< PDO direction. */
        )
{
    unsigned int i;
    ec_fmmu_config_t *fmmu;

    // FMMU configuration already prepared?
    for (i = 0; i < sc->used_fmmus; i++) {
        fmmu = &sc->fmmu_configs[i];
        if (fmmu->domain == domain && fmmu->sync_index == sync_index)
            return fmmu->logical_domain_offset;
    }

    if (sc->used_fmmus == EC_MAX_FMMUS) {
        EC_CONFIG_ERR(sc, "FMMU limit reached!\n");
        return -EOVERFLOW;
    }

    fmmu = &sc->fmmu_configs[sc->used_fmmus];

    ec_lock_down(&sc->master->master_sem);
    ec_fmmu_config_init(fmmu, sc, domain, sync_index, dir);

#if 0 //TODO overlapping PDOs
    // Overlapping PDO Support from 4751747d4e6d
    // FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
    // parent code does not call ec_fmmu_config_domain
    // FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME FIXME
    fmmu_logical_start_address = domain->tx_size;
    tx_size = fmmu->data_size;

    // FIXME is it enough to take only the *previous* FMMU into account?

    // FIXME Need to qualify allow_overlapping_pdos with slave->sii.general_flags.enable_not_lrw

    if (sc->allow_overlapping_pdos && sc->used_fmmus > 0) {
        prev_fmmu = &sc->fmmu_configs[sc->used_fmmus - 1];
        if (fmmu->dir != prev_fmmu->dir && prev_fmmu->tx_size != 0) {
            // prev fmmu has opposite direction
            // and is not already paired with prev-prev fmmu
            old_prev_tx_size = prev_fmmu->tx_size;
            prev_fmmu->tx_size = max(fmmu->data_size, prev_fmmu->data_size);
            domain->tx_size += prev_fmmu->tx_size - old_prev_tx_size;
            tx_size = 0;
            fmmu_logical_start_address = prev_fmmu->logical_domain_offset;
        }
    }

    ec_fmmu_config_domain(fmmu, domain, fmmu_logical_start_address, tx_size);
    // Overlapping PDO Support from 4751747d4e6d
#endif

    sc->used_fmmus++;
    ec_lock_up(&sc->master->master_sem);

    return fmmu->logical_domain_offset;
}

/*****************************************************************************/

/** Attaches the configuration to the addressed slave object.
 *
 * \retval  0 Success.
 * \retval <0 Error code.
 */
int ec_slave_config_attach(
        ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    ec_slave_t *slave;

    if (sc->slave)
        return 0; // already attached

    if (!(slave = ec_master_find_slave(
                    sc->master, sc->alias, sc->position))) {
        EC_CONFIG_DBG(sc, 1, "Failed to find slave for configuration.\n");
        return -ENOENT;
    }

    if (slave->config) {
        EC_CONFIG_DBG(sc, 1, "Failed to attach configuration. Slave %s-%u"
                " already has a configuration!\n",
                ec_device_names[slave->device_index!=0], slave->ring_position);
        return -EEXIST;
    }

    if (!slave->sii_image) {
        EC_CONFIG_DBG(sc, 1, "Slave cannot access its SII data!\n");
        return -EAGAIN;
    }

    if (
#ifdef EC_IDENT_WILDCARDS
            sc->vendor_id != 0xffffffff &&
#endif
            slave->sii_image->sii.vendor_id != sc->vendor_id
       ) {
        EC_CONFIG_DBG(sc, 1, "Slave %s-%u has no matching vendor ID (0x%08X)"
                " for configuration (0x%08X).\n",
                ec_device_names[slave->device_index!=0], slave->ring_position,
                slave->sii_image->sii.vendor_id, sc->vendor_id);
        return -EINVAL;
    }

    if (
#ifdef EC_IDENT_WILDCARDS
            sc->product_code != 0xffffffff &&
#endif
            slave->sii_image->sii.product_code != sc->product_code
       ) {
        EC_CONFIG_DBG(sc, 1, "Slave %s-%u has no matching product code (0x%08X)"
                " for configuration (0x%08X).\n",
                ec_device_names[slave->device_index!=0], slave->ring_position,
                slave->sii_image->sii.product_code, sc->product_code);
        return -EINVAL;
    }

    // attach slave
    slave->config = sc;
    sc->slave = slave;

    EC_CONFIG_DBG(sc, 1, "Attached slave %s-%u.\n",
                ec_device_names[slave->device_index!=0], slave->ring_position);
    return 0;
}

/*****************************************************************************/

/** Detaches the configuration from a slave object.
 */
void ec_slave_config_detach(
        ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    if (sc->slave) {
        ec_reg_request_t *reg;

        sc->slave->config = NULL;

        // invalidate processing register request
        list_for_each_entry(reg, &sc->reg_requests, list) {
            if (sc->slave->fsm.reg_request == reg) {
                sc->slave->fsm.reg_request = NULL;
                EC_SLAVE_WARN(sc->slave, "Aborting register request,"
                        " slave is detaching.\n");
                reg->state = EC_INT_REQUEST_FAILURE;
                wake_up_all(&sc->slave->master->request_queue);
                break;
            }
        }

        sc->slave = NULL;
    }
}

/*****************************************************************************/

/** Loads the default PDO assignment from the slave object.
 */
void ec_slave_config_load_default_sync_config(ec_slave_config_t *sc)
{
    uint8_t sync_index;
    ec_sync_config_t *sync_config;
    const ec_sync_t *sync;

    if (!sc->slave)
        return;

    for (sync_index = 0; sync_index < EC_MAX_SYNC_MANAGERS; sync_index++) {
        sync_config = &sc->sync_configs[sync_index];
        if ((sync = ec_slave_get_sync(sc->slave, sync_index))) {
            sync_config->dir = ec_sync_default_direction(sync);
            if (sync_config->dir == EC_DIR_INVALID)
                EC_SLAVE_WARN(sc->slave,
                        "SM%u has an invalid direction field!\n", sync_index);
            ec_pdo_list_copy(&sync_config->pdos, &sync->pdos);
        }
    }
}

/*****************************************************************************/

/** Loads the default mapping for a PDO from the slave object.
 */
void ec_slave_config_load_default_mapping(
        const ec_slave_config_t *sc,
        ec_pdo_t *pdo
        )
{
    unsigned int i;
    const ec_sync_t *sync;
    const ec_pdo_t *default_pdo;

    if (!sc->slave)
        return;

    EC_CONFIG_DBG(sc, 1, "Loading default mapping for PDO 0x%04X.\n",
            pdo->index);

    if (!sc->slave->sii_image) {
        EC_CONFIG_DBG(sc, 1, "Slave cannot access its SII data!\n");
        return;
    }

    // find PDO in any sync manager (it could be reassigned later)
    for (i = 0; i < sc->slave->sii_image->sii.sync_count; i++) {
        sync = &sc->slave->sii_image->sii.syncs[i];

        list_for_each_entry(default_pdo, &sync->pdos.list, list) {
            if (default_pdo->index != pdo->index)
                continue;

            if (default_pdo->name) {
                EC_CONFIG_DBG(sc, 1, "Found PDO name \"%s\".\n",
                        default_pdo->name);

                // take PDO name from assigned one
                ec_pdo_set_name(pdo, default_pdo->name);
            }

            // copy entries (= default PDO mapping)
            if (ec_pdo_copy_entries(pdo, default_pdo))
                return;

            if (sc->master->debug_level) {
                const ec_pdo_entry_t *entry;
                list_for_each_entry(entry, &pdo->entries, list) {
                    EC_CONFIG_DBG(sc, 1, "Entry 0x%04X:%02X.\n",
                            entry->index, entry->subindex);
                }
            }

            return;
        }
    }

    EC_CONFIG_DBG(sc, 1, "No default mapping found.\n");
}

/*****************************************************************************/

/** Get the number of SDO configurations.
 *
 * \return Number of SDO configurations.
 */
unsigned int ec_slave_config_sdo_count(
        const ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    const ec_sdo_request_t *req;
    unsigned int count = 0;

    list_for_each_entry(req, &sc->sdo_configs, list) {
        count++;
    }

    return count;
}

/*****************************************************************************/

/** Finds an SDO configuration via its position in the list.
 *
 * Const version.
 *
 * \return Search result, or NULL.
 */
const ec_sdo_request_t *ec_slave_config_get_sdo_by_pos_const(
        const ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    const ec_sdo_request_t *req;

    list_for_each_entry(req, &sc->sdo_configs, list) {
        if (pos--)
            continue;
        return req;
    }

    return NULL;
}

/*****************************************************************************/

/** Get the number of IDN configurations.
 *
 * \return Number of SDO configurations.
 */
unsigned int ec_slave_config_idn_count(
        const ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    const ec_soe_request_t *req;
    unsigned int count = 0;

    list_for_each_entry(req, &sc->soe_configs, list) {
        count++;
    }

    return count;
}

/*****************************************************************************/

/** Finds an IDN configuration via its position in the list.
 *
 * Const version.
 *
 * \return Search result, or NULL.
 */
const ec_soe_request_t *ec_slave_config_get_idn_by_pos_const(
        const ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    const ec_soe_request_t *req;

    list_for_each_entry(req, &sc->soe_configs, list) {
        if (pos--)
            continue;
        return req;
    }

    return NULL;
}

/*****************************************************************************/

/** Get the number of feature flags.
 *
 * \return Number of feature flags.
 */
unsigned int ec_slave_config_flag_count(
        const ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    const ec_flag_t *flag;
    unsigned int count = 0;

    list_for_each_entry(flag, &sc->flags, list) {
        count++;
    }

    return count;
}

/*****************************************************************************/

/** Finds a flag via its position in the list.
 *
 * Const version.
 *
 * \return Search result, or NULL.
 */
const ec_flag_t *ec_slave_config_get_flag_by_pos_const(
        const ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    const ec_flag_t *flag;

    list_for_each_entry(flag, &sc->flags, list) {
        if (pos--)
            continue;
        return flag;
    }

    return NULL;
}

/*****************************************************************************/

/** Finds a CoE handler via its position in the list.
 *
 * \return Search result, or NULL.
 */
ec_sdo_request_t *ec_slave_config_find_sdo_request(
        ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    ec_sdo_request_t *req;

    list_for_each_entry(req, &sc->sdo_requests, list) {
        if (pos--)
            continue;
        return req;
    }

    return NULL;
}

/*****************************************************************************/

/** Finds a register handler via its position in the list.
 *
 * \return Search result, or NULL.
 */
ec_reg_request_t *ec_slave_config_find_reg_request(
        ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    ec_reg_request_t *reg;

    list_for_each_entry(reg, &sc->reg_requests, list) {
        if (pos--)
            continue;
        return reg;
    }

    return NULL;
}

/*****************************************************************************/

/** Finds a VoE handler via its position in the list.
 *
 * \return Search result, or NULL.
 */
ec_voe_handler_t *ec_slave_config_find_voe_handler(
        ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int pos /**< Position in the list. */
        )
{
    ec_voe_handler_t *voe;

    list_for_each_entry(voe, &sc->voe_handlers, list) {
        if (pos--)
            continue;
        return voe;
    }

    return NULL;
}

/*****************************************************************************/

/** Expires any requests that have been started on a detached slave.
 */
void ec_slave_config_expire_disconnected_requests(
        ec_slave_config_t *sc /**< Slave configuration. */
        )
{
    ec_sdo_request_t *sdo_req;
    ec_reg_request_t *reg_req;

    if (sc->slave) {
        return;
    }

    list_for_each_entry(sdo_req, &sc->sdo_requests, list) {
        if (sdo_req->state == EC_INT_REQUEST_QUEUED ||
                sdo_req->state == EC_INT_REQUEST_BUSY) {
            EC_CONFIG_DBG(sc, 1,
                    "Aborting SDO request; no slave attached.\n");
            sdo_req->state = EC_INT_REQUEST_FAILURE;
        }
    }

    list_for_each_entry(reg_req, &sc->reg_requests, list) {
        if (reg_req->state == EC_INT_REQUEST_QUEUED ||
                reg_req->state == EC_INT_REQUEST_BUSY) {
            EC_CONFIG_DBG(sc, 1,
                    "Aborting register request; no slave attached.\n");
            reg_req->state = EC_INT_REQUEST_FAILURE;
        }
    }
}

/*****************************************************************************/

/** Finds a flag.
 *
 * \return Search result, or NULL.
 */
ec_flag_t *ec_slave_config_find_flag(
        ec_slave_config_t *sc, /**< Slave configuration. */
        const char *key /**< Flag key. */
        )
{
    if (sc) {
        ec_flag_t *flag;
        list_for_each_entry(flag, &sc->flags, list) {
            if (!strcmp(flag->key, key)) {
                return flag;
            }
        }
    }

    return NULL;
}

/******************************************************************************
 *  Application interface
 *****************************************************************************/

int ecrt_slave_config_sync_manager(ec_slave_config_t *sc, uint8_t sync_index,
        ec_direction_t dir, ec_watchdog_mode_t watchdog_mode)
{
    ec_sync_config_t *sync_config;

    EC_CONFIG_DBG(sc, 1, "ecrt_slave_config_sync_manager(sc = 0x%p,"
            " sync_index = %u, dir = %i, watchdog_mode = %i)\n",
            sc, sync_index, dir, watchdog_mode);

    if (sync_index >= EC_MAX_SYNC_MANAGERS) {
        EC_CONFIG_ERR(sc, "Invalid sync manager index %u!\n", sync_index);
        return -ENOENT;
    }

    if (dir != EC_DIR_OUTPUT && dir != EC_DIR_INPUT) {
        EC_CONFIG_ERR(sc, "Invalid direction %u!\n", (unsigned int) dir);
        return -EINVAL;
    }

    sync_config = &sc->sync_configs[sync_index];
    sync_config->dir = dir;
    sync_config->watchdog_mode = watchdog_mode;
    return 0;
}

/*****************************************************************************/

void ecrt_slave_config_watchdog(ec_slave_config_t *sc,
        uint16_t divider, uint16_t intervals)
{
    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, divider = %u, intervals = %u)\n",
            __func__, sc, divider, intervals);

    sc->watchdog_divider = divider;
    sc->watchdog_intervals = intervals;
}

/*****************************************************************************/

void ecrt_slave_config_overlapping_pdos(ec_slave_config_t *sc,
        uint8_t allow_overlapping_pdos )
{
    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, allow_overlapping_pdos = %u)\n",
                __func__, sc, allow_overlapping_pdos);

    sc->allow_overlapping_pdos = allow_overlapping_pdos;
}

/*****************************************************************************/

int ecrt_slave_config_pdo_assign_add(ec_slave_config_t *sc,
        uint8_t sync_index, uint16_t pdo_index)
{
    ec_pdo_t *pdo;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, sync_index = %u, "
            "pdo_index = 0x%04X)\n", __func__, sc, sync_index, pdo_index);

    if (sync_index >= EC_MAX_SYNC_MANAGERS) {
        EC_CONFIG_ERR(sc, "Invalid sync manager index %u!\n", sync_index);
        return -EINVAL;
    }

    ec_lock_down(&sc->master->master_sem);

    pdo = ec_pdo_list_add_pdo(&sc->sync_configs[sync_index].pdos, pdo_index);
    if (IS_ERR(pdo)) {
        ec_lock_up(&sc->master->master_sem);
        return PTR_ERR(pdo);
    }
    pdo->sync_index = sync_index;

    ec_slave_config_load_default_mapping(sc, pdo);

    ec_lock_up(&sc->master->master_sem);
    return 0;
}

/*****************************************************************************/

void ecrt_slave_config_pdo_assign_clear(ec_slave_config_t *sc,
        uint8_t sync_index)
{
    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, sync_index = %u)\n",
            __func__, sc, sync_index);

    if (sync_index >= EC_MAX_SYNC_MANAGERS) {
        EC_CONFIG_ERR(sc, "Invalid sync manager index %u!\n", sync_index);
        return;
    }

    ec_lock_down(&sc->master->master_sem);
    ec_pdo_list_clear_pdos(&sc->sync_configs[sync_index].pdos);
    ec_lock_up(&sc->master->master_sem);
}

/*****************************************************************************/

int ecrt_slave_config_pdo_mapping_add(ec_slave_config_t *sc,
        uint16_t pdo_index, uint16_t entry_index, uint8_t entry_subindex,
        uint8_t entry_bit_length)
{
    uint8_t sync_index;
    ec_pdo_t *pdo = NULL;
    ec_pdo_entry_t *entry;
    int retval = 0;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, "
            "pdo_index = 0x%04X, entry_index = 0x%04X, "
            "entry_subindex = 0x%02X, entry_bit_length = %u)\n",
            __func__, sc, pdo_index, entry_index, entry_subindex,
            entry_bit_length);

    for (sync_index = 0; sync_index < EC_MAX_SYNC_MANAGERS; sync_index++)
        if ((pdo = ec_pdo_list_find_pdo(
                        &sc->sync_configs[sync_index].pdos, pdo_index)))
            break;

    if (pdo) {
        ec_lock_down(&sc->master->master_sem);
        entry = ec_pdo_add_entry(pdo, entry_index, entry_subindex,
                entry_bit_length);
        ec_lock_up(&sc->master->master_sem);
        if (IS_ERR(entry))
            retval = PTR_ERR(entry);
    } else {
        EC_CONFIG_ERR(sc, "PDO 0x%04X is not assigned.\n", pdo_index);
        retval = -ENOENT;
    }

    return retval;
}

/*****************************************************************************/

void ecrt_slave_config_pdo_mapping_clear(ec_slave_config_t *sc,
        uint16_t pdo_index)
{
    uint8_t sync_index;
    ec_pdo_t *pdo = NULL;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, pdo_index = 0x%04X)\n",
            __func__, sc, pdo_index);

    for (sync_index = 0; sync_index < EC_MAX_SYNC_MANAGERS; sync_index++)
        if ((pdo = ec_pdo_list_find_pdo(
                        &sc->sync_configs[sync_index].pdos, pdo_index)))
            break;

    if (pdo) {
        ec_lock_down(&sc->master->master_sem);
        ec_pdo_clear_entries(pdo);
        ec_lock_up(&sc->master->master_sem);
    } else {
        EC_CONFIG_WARN(sc, "PDO 0x%04X is not assigned.\n", pdo_index);
    }
}

/*****************************************************************************/

int ecrt_slave_config_pdos(ec_slave_config_t *sc,
        unsigned int n_syncs, const ec_sync_info_t syncs[])
{
    int ret;
    unsigned int i, j, k;
    const ec_sync_info_t *sync_info;
    const ec_pdo_info_t *pdo_info;
    const ec_pdo_entry_info_t *entry_info;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, n_syncs = %u, syncs = 0x%p)\n",
            __func__, sc, n_syncs, syncs);

    if (!syncs)
        return 0;

    for (i = 0; i < n_syncs; i++) {
        sync_info = &syncs[i];

        if (sync_info->index == (uint8_t) EC_END)
            break;

        if (sync_info->index >= EC_MAX_SYNC_MANAGERS) {
            EC_CONFIG_ERR(sc, "Invalid sync manager index %u!\n",
                    sync_info->index);
            return -ENOENT;
        }

        ret = ecrt_slave_config_sync_manager(sc, sync_info->index,
                sync_info->dir, sync_info->watchdog_mode);
        if (ret)
            return ret;

        ecrt_slave_config_pdo_assign_clear(sc, sync_info->index);

        if (sync_info->n_pdos && sync_info->pdos) {

            for (j = 0; j < sync_info->n_pdos; j++) {
                pdo_info = &sync_info->pdos[j];

                ret = ecrt_slave_config_pdo_assign_add(
                        sc, sync_info->index, pdo_info->index);
                if (ret)
                    return ret;

                ecrt_slave_config_pdo_mapping_clear(sc, pdo_info->index);

                if (pdo_info->n_entries && pdo_info->entries) {
                    for (k = 0; k < pdo_info->n_entries; k++) {
                        entry_info = &pdo_info->entries[k];

                        ret = ecrt_slave_config_pdo_mapping_add(sc,
                                pdo_info->index, entry_info->index,
                                entry_info->subindex,
                                entry_info->bit_length);
                        if (ret)
                            return ret;
                    }
                }
            }
        }
    }

    return 0;
}

/*****************************************************************************/

int ecrt_slave_config_reg_pdo_entry(
        ec_slave_config_t *sc,
        uint16_t index,
        uint8_t subindex,
        ec_domain_t *domain,
        unsigned int *bit_position
        )
{
    uint8_t sync_index;
    const ec_sync_config_t *sync_config;
    unsigned int bit_offset, bit_pos;
    ec_pdo_t *pdo;
    ec_pdo_entry_t *entry;
    int sync_offset;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "subindex = 0x%02X, domain = 0x%p, bit_position = 0x%p)\n",
            __func__, sc, index, subindex, domain, bit_position);

    for (sync_index = 0; sync_index < EC_MAX_SYNC_MANAGERS; sync_index++) {
        sync_config = &sc->sync_configs[sync_index];
        bit_offset = 0;

        list_for_each_entry(pdo, &sync_config->pdos.list, list) {
            list_for_each_entry(entry, &pdo->entries, list) {
                if (entry->index != index || entry->subindex != subindex) {
                    bit_offset += entry->bit_length;
                } else {
                    bit_pos = bit_offset % 8;
                    if (bit_position) {
                        *bit_position = bit_pos;
                    } else if (bit_pos) {
                        EC_CONFIG_ERR(sc, "PDO entry 0x%04X:%02X does"
                                " not byte-align.\n", index, subindex);
                        return -EFAULT;
                    }

                    sync_offset = ec_slave_config_prepare_fmmu(
                            sc, domain, sync_index, sync_config->dir);
                    if (sync_offset < 0)
                        return sync_offset;

                    return sync_offset + bit_offset / 8;
                }
            }
        }
    }

    EC_CONFIG_ERR(sc, "PDO entry 0x%04X:%02X is not mapped.\n",
           index, subindex);
    return -ENOENT;
}

/*****************************************************************************/

int ecrt_slave_config_reg_pdo_entry_pos(
        ec_slave_config_t *sc,
        uint8_t sync_index,
        unsigned int pdo_pos,
        unsigned int entry_pos,
        ec_domain_t *domain,
        unsigned int *bit_position
        )
{
    const ec_sync_config_t *sync_config;
    unsigned int bit_offset, pp, ep;
    ec_pdo_t *pdo;
    ec_pdo_entry_t *entry;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, sync_index = %u, pdo_pos = %u,"
            " entry_pos = %u, domain = 0x%p, bit_position = 0x%p)\n",
            __func__, sc, sync_index, pdo_pos, entry_pos,
            domain, bit_position);

    if (sync_index >= EC_MAX_SYNC_MANAGERS) {
        EC_CONFIG_ERR(sc, "Invalid syncmanager position %u.\n", sync_index);
        return -EINVAL;
    }

    sync_config = &sc->sync_configs[sync_index];
    bit_offset = 0;
    pp = 0;

    list_for_each_entry(pdo, &sync_config->pdos.list, list) {
        ep = 0;
        list_for_each_entry(entry, &pdo->entries, list) {
            if (pp != pdo_pos || ep != entry_pos) {
                bit_offset += entry->bit_length;
            } else {
                unsigned int bit_pos = bit_offset % 8;
                int sync_offset;

                if (bit_position) {
                    *bit_position = bit_pos;
                } else if (bit_pos) {
                    EC_CONFIG_ERR(sc, "PDO entry 0x%04X:%02X does"
                            " not byte-align.\n",
                            pdo->index, entry->subindex);
                    return -EFAULT;
                }

                sync_offset = ec_slave_config_prepare_fmmu(
                        sc, domain, sync_index, sync_config->dir);
                if (sync_offset < 0)
                    return sync_offset;

                return sync_offset + bit_offset / 8;
            }
            ep++;
        }
        pp++;
    }

    EC_CONFIG_ERR(sc, "PDO entry specification %u/%u/%u out of range.\n",
           sync_index, pdo_pos, entry_pos);
    return -ENOENT;
}

/*****************************************************************************/

void ecrt_slave_config_dc(ec_slave_config_t *sc, uint16_t assign_activate,
        uint32_t sync0_cycle_time, int32_t sync0_shift_time,
        uint32_t sync1_cycle_time, int32_t sync1_shift_time)
{
    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, assign_activate = 0x%04X,"
            " sync0_cycle = %u, sync0_shift = %i,"
            " sync1_cycle = %u, sync1_shift = %i\n",
            __func__, sc, assign_activate, sync0_cycle_time, sync0_shift_time,
            sync1_cycle_time, sync1_shift_time);

    sc->dc_assign_activate = assign_activate;
    sc->dc_sync[0].cycle_time = sync0_cycle_time;
    sc->dc_sync[0].shift_time = sync0_shift_time;
    if (sync0_cycle_time > 0)
    {
        sc->dc_sync[1].shift_time = (sync1_cycle_time + sync1_shift_time) %
                sync0_cycle_time;
              
        if ((sync1_cycle_time + sync1_shift_time) < sc->dc_sync[1].shift_time) {
            EC_CONFIG_ERR(sc, "Slave Config DC results in a negative "
                    "sync1 cycle.  Resetting to zero cycle and shift time\n");

            sc->dc_sync[1].cycle_time = 0;
            sc->dc_sync[1].shift_time = 0;
        } else {
            sc->dc_sync[1].cycle_time = (sync1_cycle_time + sync1_shift_time) -
                    sc->dc_sync[1].shift_time;
        }
    } else {
        sc->dc_sync[1].cycle_time = 0;
        sc->dc_sync[1].shift_time = 0;
    }
}

/*****************************************************************************/

int ecrt_slave_config_sdo(ec_slave_config_t *sc, uint16_t index,
        uint8_t subindex, const uint8_t *data, size_t size)
{
    ec_slave_t *slave = sc->slave;
    ec_sdo_request_t *req;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "subindex = 0x%02X, data = 0x%p, size = %zu)\n",
            __func__, sc, index, subindex, data, size);

    if (slave && slave->sii_image && !(slave->sii_image->sii.mailbox_protocols & EC_MBOX_COE)) {
        EC_CONFIG_WARN(sc, "Attached slave does not support CoE!\n");
    }

    if (!(req = (ec_sdo_request_t *)
          kmalloc(sizeof(ec_sdo_request_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate memory for"
                " SDO configuration!\n");
        return -ENOMEM;
    }

    ec_sdo_request_init(req);
    ecrt_sdo_request_index(req, index, subindex);

    ret = ec_sdo_request_copy_data(req, data, size);
    if (ret < 0) {
        ec_sdo_request_clear(req);
        kfree(req);
        return ret;
    }

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&req->list, &sc->sdo_configs);
    ec_lock_up(&sc->master->master_sem);
    return 0;
}

/*****************************************************************************/

int ecrt_slave_config_sdo8(ec_slave_config_t *sc, uint16_t index,
        uint8_t subindex, uint8_t value)
{
    uint8_t data[1];

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "subindex = 0x%02X, value = %u)\n",
            __func__, sc, index, subindex, (unsigned int) value);

    EC_WRITE_U8(data, value);
    return ecrt_slave_config_sdo(sc, index, subindex, data, 1);
}

/*****************************************************************************/

int ecrt_slave_config_sdo16(ec_slave_config_t *sc, uint16_t index,
        uint8_t subindex, uint16_t value)
{
    uint8_t data[2];

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "subindex = 0x%02X, value = %u)\n",
            __func__, sc, index, subindex, value);

    EC_WRITE_U16(data, value);
    return ecrt_slave_config_sdo(sc, index, subindex, data, 2);
}

/*****************************************************************************/

int ecrt_slave_config_sdo32(ec_slave_config_t *sc, uint16_t index,
        uint8_t subindex, uint32_t value)
{
    uint8_t data[4];

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "subindex = 0x%02X, value = %u)\n",
            __func__, sc, index, subindex, value);

    EC_WRITE_U32(data, value);
    return ecrt_slave_config_sdo(sc, index, subindex, data, 4);
}

/*****************************************************************************/

int ecrt_slave_config_complete_sdo(ec_slave_config_t *sc, uint16_t index,
        const uint8_t *data, size_t size)
{
    ec_slave_t *slave = sc->slave;
    ec_sdo_request_t *req;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, index = 0x%04X, "
            "data = 0x%p, size = %zu)\n", __func__, sc, index, data, size);

    if (slave && !(slave->sii_image->sii.mailbox_protocols & EC_MBOX_COE)) {
        EC_CONFIG_WARN(sc, "Attached slave does not support CoE!\n");
    }

    if (!(req = (ec_sdo_request_t *)
          kmalloc(sizeof(ec_sdo_request_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate memory for"
                " SDO configuration!\n");
        return -ENOMEM;
    }

    ec_sdo_request_init(req);
    ecrt_sdo_request_index_complete(req, index);

    ret = ec_sdo_request_copy_data(req, data, size);
    if (ret < 0) {
        ec_sdo_request_clear(req);
        kfree(req);
        return ret;
    }

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&req->list, &sc->sdo_configs);
    ec_lock_up(&sc->master->master_sem);
    return 0;
}

/*****************************************************************************/

int ecrt_slave_config_emerg_size(ec_slave_config_t *sc, size_t elements)
{
    return ec_coe_emerg_ring_size(&sc->emerg_ring, elements);
}

/*****************************************************************************/

int ecrt_slave_config_emerg_pop(ec_slave_config_t *sc, uint8_t *target)
{
    return ec_coe_emerg_ring_pop(&sc->emerg_ring, target);
}

/*****************************************************************************/

int ecrt_slave_config_emerg_clear(ec_slave_config_t *sc)
{
    return ec_coe_emerg_ring_clear_ring(&sc->emerg_ring);
}

/*****************************************************************************/

int ecrt_slave_config_emerg_overruns(ec_slave_config_t *sc)
{
    return ec_coe_emerg_ring_overruns(&sc->emerg_ring);
}

/*****************************************************************************/

/** Same as ecrt_slave_config_create_sdo_request(), but with ERR_PTR() return
 * value.
 */
ec_sdo_request_t *ecrt_slave_config_create_sdo_request_err(
        ec_slave_config_t *sc, uint16_t index, uint8_t subindex, uint8_t complete, size_t size)
{
    ec_sdo_request_t *req;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, "
            "index = 0x%04X, subindex = 0x%02X, complete = %u, size = %zu)\n",
            __func__, sc, index, subindex, complete, size);

    if (!(req = (ec_sdo_request_t *)
                kmalloc(sizeof(ec_sdo_request_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate SDO request memory!\n");
        return ERR_PTR(-ENOMEM);
    }

    ec_sdo_request_init(req);
    if (complete) {
        ecrt_sdo_request_index_complete(req, index);
    }
    else {
        ecrt_sdo_request_index(req, index, subindex);
    }
    ret = ec_sdo_request_alloc(req, size);
    if (ret < 0) {
        ec_sdo_request_clear(req);
        kfree(req);
        return ERR_PTR(ret);
    }

    // prepare data for optional writing
    memset(req->data, 0x00, size);
    req->data_size = size;

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&req->list, &sc->sdo_requests);
    ec_lock_up(&sc->master->master_sem);

    return req;
}

/*****************************************************************************/

ec_sdo_request_t *ecrt_slave_config_create_sdo_request(
        ec_slave_config_t *sc, uint16_t index, uint8_t subindex, size_t size)
{
    ec_sdo_request_t *s = ecrt_slave_config_create_sdo_request_err(sc, index,
            subindex, 0, size);
    return IS_ERR(s) ? NULL : s;
}

/*****************************************************************************/

ec_sdo_request_t *ecrt_slave_config_create_sdo_request_complete(
        ec_slave_config_t *sc, uint16_t index, size_t size)
{
    ec_sdo_request_t *s = ecrt_slave_config_create_sdo_request_err(sc, index,
            0, 1, size);
    return IS_ERR(s) ? NULL : s;
}

/*****************************************************************************/

/** Same as ecrt_slave_config_create_reg_request(), but with ERR_PTR() return
 * value.
 */
ec_reg_request_t *ecrt_slave_config_create_reg_request_err(
        ec_slave_config_t *sc, size_t size)
{
    ec_reg_request_t *reg;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, size = %zu)\n",
            __func__, sc, size);

    if (!(reg = (ec_reg_request_t *)
                kmalloc(sizeof(ec_reg_request_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate register request memory!\n");
        return ERR_PTR(-ENOMEM);
    }

    ret = ec_reg_request_init(reg, size);
    if (ret) {
        kfree(reg);
        return ERR_PTR(ret);
    }

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&reg->list, &sc->reg_requests);
    ec_lock_up(&sc->master->master_sem);

    return reg;
}

/*****************************************************************************/

ec_reg_request_t *ecrt_slave_config_create_reg_request(
        ec_slave_config_t *sc, size_t size)
{
    ec_reg_request_t *reg =
        ecrt_slave_config_create_reg_request_err(sc, size);
    return IS_ERR(reg) ? NULL : reg;
}

/*****************************************************************************/

/** Same as ecrt_slave_config_create_voe_handler(), but with ERR_PTR() return
 * value.
 */
ec_voe_handler_t *ecrt_slave_config_create_voe_handler_err(
        ec_slave_config_t *sc, size_t size)
{
    ec_voe_handler_t *voe;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, size = %zu)\n", __func__, sc, size);

    if (!(voe = (ec_voe_handler_t *)
                kmalloc(sizeof(ec_voe_handler_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate VoE request memory!\n");
        return ERR_PTR(-ENOMEM);
    }

    ret = ec_voe_handler_init(voe, sc, size);
    if (ret < 0) {
        kfree(voe);
        return ERR_PTR(ret);
    }

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&voe->list, &sc->voe_handlers);
    ec_lock_up(&sc->master->master_sem);

    return voe;
}

/*****************************************************************************/

ec_voe_handler_t *ecrt_slave_config_create_voe_handler(
        ec_slave_config_t *sc, size_t size)
{
    ec_voe_handler_t *voe = ecrt_slave_config_create_voe_handler_err(sc,
            size);
    return IS_ERR(voe) ? NULL : voe;
}

/*****************************************************************************/

void ecrt_slave_config_state(const ec_slave_config_t *sc,
        ec_slave_config_state_t *state)
{
    state->online = sc->slave ? 1 : 0;
    if (state->online) {
        state->operational =
            sc->slave->current_state == EC_SLAVE_STATE_OP
            && !sc->slave->force_config;
        state->al_state = sc->slave->current_state;
        state->error_flag = sc->slave->error_flag ? 1 : 0;
        state->ready = ec_fsm_slave_is_ready(&sc->slave->fsm) ? 1 : 0;
        state->position = sc->slave->ring_position;
    } else {
        state->operational = 0;
        state->al_state = EC_SLAVE_STATE_UNKNOWN;
        state->error_flag = 0;
        state->ready = 0;
        state->position = (uint16_t) -1;
    }
}

/*****************************************************************************/

int ecrt_slave_config_idn(ec_slave_config_t *sc, uint8_t drive_no,
        uint16_t idn, ec_al_state_t state, const uint8_t *data,
        size_t size)
{
    ec_slave_t *slave = sc->slave;
    ec_soe_request_t *req;
    int ret;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, drive_no = %u, idn = 0x%04X, "
            "state = %u, data = 0x%p, size = %zu)\n",
            __func__, sc, drive_no, idn, state, data, size);

    if (drive_no > 7) {
        EC_CONFIG_ERR(sc, "Invalid drive number %u!\n",
                (unsigned int) drive_no);
        return -EINVAL;
    }

    if (state != EC_AL_STATE_PREOP && state != EC_AL_STATE_SAFEOP) {
        EC_CONFIG_ERR(sc, "AL state for IDN config"
                " must be PREOP or SAFEOP!\n");
        return -EINVAL;
    }

    if (slave && slave->sii_image && !(slave->sii_image->sii.mailbox_protocols & EC_MBOX_SOE)) {
        EC_CONFIG_WARN(sc, "Attached slave does not support SoE!\n");
    }

    if (!(req = (ec_soe_request_t *)
          kmalloc(sizeof(ec_soe_request_t), GFP_KERNEL))) {
        EC_CONFIG_ERR(sc, "Failed to allocate memory for"
                " IDN configuration!\n");
        return -ENOMEM;
    }

    ec_soe_request_init(req);
    ec_soe_request_set_drive_no(req, drive_no);
    ec_soe_request_set_idn(req, idn);
    req->al_state = state;

    ret = ec_soe_request_copy_data(req, data, size);
    if (ret < 0) {
        ec_soe_request_clear(req);
        kfree(req);
        return ret;
    }

    ec_lock_down(&sc->master->master_sem);
    list_add_tail(&req->list, &sc->soe_configs);
    ec_lock_up(&sc->master->master_sem);
    return 0;
}

/*****************************************************************************/

int ecrt_slave_config_flag(ec_slave_config_t *sc, const char *key,
        int32_t value)
{
    ec_flag_t *flag;

    EC_CONFIG_DBG(sc, 1, "%s(sc = 0x%p, key = %s, value = %i)\n",
            __func__, sc, key, value);


    flag = ec_slave_config_find_flag(sc, key);
    if (flag) {
        flag->value = value; // overwrite value
    }
    else { // new flag
        int ret;

        if (!(flag = (ec_flag_t *) kmalloc(sizeof(ec_flag_t), GFP_KERNEL))) {
            EC_CONFIG_ERR(sc, "Failed to allocate memory for flag!\n");
            return -ENOMEM;
        }

        ret = ec_flag_init(flag, key, value);
        if (ret) {
            kfree(flag);
            return ret;
        }

        ec_lock_down(&sc->master->master_sem);
        list_add_tail(&flag->list, &sc->flags);
        ec_lock_up(&sc->master->master_sem);
    }
    return 0;
}

/*****************************************************************************/

/** \cond */

EXPORT_SYMBOL(ecrt_slave_config_sync_manager);
EXPORT_SYMBOL(ecrt_slave_config_watchdog);
EXPORT_SYMBOL(ecrt_slave_config_pdo_assign_add);
EXPORT_SYMBOL(ecrt_slave_config_pdo_assign_clear);
EXPORT_SYMBOL(ecrt_slave_config_pdo_mapping_add);
EXPORT_SYMBOL(ecrt_slave_config_pdo_mapping_clear);
EXPORT_SYMBOL(ecrt_slave_config_pdos);
EXPORT_SYMBOL(ecrt_slave_config_reg_pdo_entry);
EXPORT_SYMBOL(ecrt_slave_config_reg_pdo_entry_pos);
EXPORT_SYMBOL(ecrt_slave_config_dc);
EXPORT_SYMBOL(ecrt_slave_config_sdo);
EXPORT_SYMBOL(ecrt_slave_config_sdo8);
EXPORT_SYMBOL(ecrt_slave_config_sdo16);
EXPORT_SYMBOL(ecrt_slave_config_sdo32);
EXPORT_SYMBOL(ecrt_slave_config_complete_sdo);
EXPORT_SYMBOL(ecrt_slave_config_emerg_size);
EXPORT_SYMBOL(ecrt_slave_config_emerg_pop);
EXPORT_SYMBOL(ecrt_slave_config_emerg_clear);
EXPORT_SYMBOL(ecrt_slave_config_emerg_overruns);
EXPORT_SYMBOL(ecrt_slave_config_create_sdo_request);
EXPORT_SYMBOL(ecrt_slave_config_create_sdo_request_complete);
EXPORT_SYMBOL(ecrt_slave_config_create_voe_handler);
EXPORT_SYMBOL(ecrt_slave_config_create_reg_request);
EXPORT_SYMBOL(ecrt_slave_config_state);
EXPORT_SYMBOL(ecrt_slave_config_idn);
EXPORT_SYMBOL(ecrt_slave_config_flag);

/** \endcond */

/*****************************************************************************/
