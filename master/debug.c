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
   Ethernet interface for debugging purposes.
*/

/*****************************************************************************/

#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "globals.h"
#include "master.h"
#include "debug.h"

/*****************************************************************************/

// net_device functions
int ec_dbgdev_open(struct net_device *);
int ec_dbgdev_stop(struct net_device *);
int ec_dbgdev_tx(struct sk_buff *, struct net_device *);
struct net_device_stats *ec_dbgdev_stats(struct net_device *);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
/** Device operations for debug interfaces.
 */
static const struct net_device_ops ec_dbg_netdev_ops =
{
    .ndo_open = ec_dbgdev_open,
    .ndo_stop = ec_dbgdev_stop,
    .ndo_start_xmit = ec_dbgdev_tx,
    .ndo_get_stats = ec_dbgdev_stats,
};
#endif

/*****************************************************************************/

/** Debug interface constructor.
 *
 * Initializes the debug object, creates a net_device and registeres it.
 *
 * \retval  0 Success.
 * \retval <0 Error code.
 */
int ec_debug_init(
        ec_debug_t *dbg, /**< Debug object. */
        ec_device_t *device, /**< EtherCAT device. */
        const char *name /**< Interface name. */
        )
{
    dbg->device = device;
    dbg->registered = 0;
    dbg->opened = 0;

    memset(&dbg->stats, 0, sizeof(struct net_device_stats));

    if (!(dbg->dev =
          alloc_netdev(sizeof(ec_debug_t *), name,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
			NET_NAME_UNKNOWN,
#endif
			ether_setup))) {
        EC_MASTER_ERR(device->master, "Unable to allocate net_device"
                " for debug object!\n");
        return -ENODEV;
    }

    // initialize net_device
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    dbg->dev->netdev_ops = &ec_dbg_netdev_ops;
#else
    dbg->dev->open = ec_dbgdev_open;
    dbg->dev->stop = ec_dbgdev_stop;
    dbg->dev->hard_start_xmit = ec_dbgdev_tx;
    dbg->dev->get_stats = ec_dbgdev_stats;
#endif

    // initialize private data
    *((ec_debug_t **) netdev_priv(dbg->dev)) = dbg;

    return 0;
}

/*****************************************************************************/

/** Debug interface destructor.
 *
 * Unregisters the net_device and frees allocated memory.
 */
void ec_debug_clear(
        ec_debug_t *dbg /**< debug object */
        )
{
    ec_debug_unregister(dbg);
    free_netdev(dbg->dev);
}

/*****************************************************************************/

/** Register debug interface.
 */
void ec_debug_register(
        ec_debug_t *dbg, /**< debug object */
        const struct net_device *net_dev /**< 'Real' Ethernet device. */
        )
{
    int result;

    ec_debug_unregister(dbg);

    // use the Ethernet address of the physical device for the debug device
    memcpy(dbg->dev->dev_addr, net_dev->dev_addr, ETH_ALEN);

    // connect the net_device to the kernel
    if ((result = register_netdev(dbg->dev))) {
        EC_MASTER_WARN(dbg->device->master, "Unable to register net_device:"
                " error %i\n", result);
    } else {
        dbg->registered = 1;
    }
}

/*****************************************************************************/

/** Unregister debug interface.
 */
void ec_debug_unregister(
        ec_debug_t *dbg /**< debug object */
        )
{
    if (dbg->registered) {
        dbg->opened = 0;
        dbg->registered = 0;
        unregister_netdev(dbg->dev);
    }
}

/*****************************************************************************/

/** Sends frame data to the interface.
 */
void ec_debug_send(
        ec_debug_t *dbg, /**< debug object */
        const uint8_t *data, /**< frame data */
        size_t size /**< size of the frame data */
        )
{
    struct sk_buff *skb;

    if (!dbg->opened)
        return;

    // allocate socket buffer
    if (!(skb = dev_alloc_skb(size))) {
        dbg->stats.rx_dropped++;
        return;
    }

    // copy frame contents into socket buffer
    memcpy(skb_put(skb, size), data, size);

    // update device statistics
    dbg->stats.rx_packets++;
    dbg->stats.rx_bytes += size;

    // pass socket buffer to network stack
    skb->dev = dbg->dev;
    skb->protocol = eth_type_trans(skb, dbg->dev);
    skb->ip_summed = CHECKSUM_UNNECESSARY;
    netif_rx_ni(skb);
}

/******************************************************************************
 *  NET_DEVICE functions
 *****************************************************************************/

/** Opens the virtual network device.
 *
 * \return Always zero (success).
 */
int ec_dbgdev_open(
        struct net_device *dev /**< debug net_device */
        )
{
    ec_debug_t *dbg = *((ec_debug_t **) netdev_priv(dev));
    dbg->opened = 1;
    EC_MASTER_INFO(dbg->device->master, "Debug interface %s opened.\n",
            dev->name);
    return 0;
}

/*****************************************************************************/

/** Stops the virtual network device.
 *
 * \return Always zero (success).
 */
int ec_dbgdev_stop(
        struct net_device *dev /**< debug net_device */
        )
{
    ec_debug_t *dbg = *((ec_debug_t **) netdev_priv(dev));
    dbg->opened = 0;
    EC_MASTER_INFO(dbg->device->master, "Debug interface %s stopped.\n",
            dev->name);
    return 0;
}

/*****************************************************************************/

/** Transmits data via the virtual network device.
 *
 * \return Always zero (success).
 */
int ec_dbgdev_tx(
        struct sk_buff *skb, /**< transmit socket buffer */
        struct net_device *dev /**< EoE net_device */
        )
{
    ec_debug_t *dbg = *((ec_debug_t **) netdev_priv(dev));

    dev_kfree_skb(skb);
    dbg->stats.tx_dropped++;
    return 0;
}

/*****************************************************************************/

/** Gets statistics about the virtual network device.
 *
 * \return Statistics.
 */
struct net_device_stats *ec_dbgdev_stats(
        struct net_device *dev /**< debug net_device */
        )
{
    ec_debug_t *dbg = *((ec_debug_t **) netdev_priv(dev));
    return &dbg->stats;
}

/*****************************************************************************/
