/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 *   This file implements the OpenThread module initialization and state change handling.
 *
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_openthread_platform, CONFIG_OPENTHREAD_PLATFORM_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/version.h>
#include <zephyr/sys/check.h>
#include <zephyr/net/openthread.h>

#include "platform-zephyr.h"

#include <openthread/child_supervision.h>
#include <openthread/cli.h>
#include <openthread/ip6.h>
#include <openthread/link.h>
#include <openthread/link_raw.h>
#include <openthread/ncp.h>
#include <openthread/message.h>
#include <openthread/platform/diag.h>
#include <openthread/tasklet.h>
#include <openthread/thread.h>
#include <openthread/dataset.h>
#include <openthread/joiner.h>
#include <openthread-system.h>
#include <utils/uart.h>

#if defined(CONFIG_OPENTHREAD_NAT64_TRANSLATOR)
#include <openthread/nat64.h>
#endif /* CONFIG_OPENTHREAD_NAT64_TRANSLATOR */

#define OT_STACK_SIZE (CONFIG_OPENTHREAD_THREAD_STACK_SIZE)

#if defined(CONFIG_OPENTHREAD_THREAD_PREEMPTIVE)
#define OT_PRIORITY K_PRIO_PREEMPT(CONFIG_OPENTHREAD_THREAD_PRIORITY)
#else
#define OT_PRIORITY K_PRIO_COOP(CONFIG_OPENTHREAD_THREAD_PRIORITY)
#endif

#if defined(CONFIG_OPENTHREAD_NETWORK_NAME)
#define OT_NETWORK_NAME CONFIG_OPENTHREAD_NETWORK_NAME
#else
#define OT_NETWORK_NAME ""
#endif

#if defined(CONFIG_OPENTHREAD_CHANNEL)
#define OT_CHANNEL CONFIG_OPENTHREAD_CHANNEL
#else
#define OT_CHANNEL 0
#endif

#if defined(CONFIG_OPENTHREAD_PANID)
#define OT_PANID CONFIG_OPENTHREAD_PANID
#else
#define OT_PANID 0
#endif

#if defined(CONFIG_OPENTHREAD_XPANID)
#define OT_XPANID CONFIG_OPENTHREAD_XPANID
#else
#define OT_XPANID ""
#endif

#if defined(CONFIG_OPENTHREAD_NETWORKKEY)
#define OT_NETWORKKEY CONFIG_OPENTHREAD_NETWORKKEY
#else
#define OT_NETWORKKEY ""
#endif

#if defined(CONFIG_OPENTHREAD_JOINER_PSKD)
#define OT_JOINER_PSKD CONFIG_OPENTHREAD_JOINER_PSKD
#else
#define OT_JOINER_PSKD ""
#endif

#if defined(CONFIG_OPENTHREAD_PLATFORM_INFO)
#define OT_PLATFORM_INFO CONFIG_OPENTHREAD_PLATFORM_INFO
#else
#define OT_PLATFORM_INFO ""
#endif

#if defined(CONFIG_OPENTHREAD_POLL_PERIOD)
#define OT_POLL_PERIOD CONFIG_OPENTHREAD_POLL_PERIOD
#else
#define OT_POLL_PERIOD 0
#endif

#define ZEPHYR_PACKAGE_NAME "Zephyr"
#define PACKAGE_VERSION     KERNEL_VERSION_STRING

/* Global variables to store the OpenThread module context */
static otInstance *openthread_instance;
static struct k_mutex openthread_lock;
static struct k_work_q openthread_work_q;
static struct k_work openthread_work;
static sys_slist_t openthread_state_change_cbs;

K_KERNEL_STACK_DEFINE(ot_stack_area, OT_STACK_SIZE);

k_tid_t openthread_thread_id_get(void)
{
	return (k_tid_t)&openthread_work_q.thread;
}

static int ncp_hdlc_send(const uint8_t *buf, uint16_t len)
{
	otError err;

	err = otPlatUartSend(buf, len);
	if (err != OT_ERROR_NONE) {
		return 0;
	}

	return len;
}

static void openthread_process(struct k_work *work)
{
	ARG_UNUSED(work);

	openthread_mutex_lock();

	while (otTaskletsArePending(openthread_instance)) {
		otTaskletsProcess(openthread_instance);
	}

	otSysProcessDrivers(openthread_instance);

	openthread_mutex_unlock();
}

static void ot_joiner_start_handler(otError error, void *context)
{
	ARG_UNUSED(context);

	switch (error) {
	case OT_ERROR_NONE:
		LOG_INF("Join success");
		error = otThreadSetEnabled(openthread_instance, true);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to start the OpenThread network [%d]", error);
		}

		break;
	default:
		LOG_ERR("Join failed [%d]", error);
		break;
	}
}

static void ot_state_changed_handler(uint32_t flags, void *context)
{
	ARG_UNUSED(context);

	struct openthread_state_changed_callback *entry, *next;

	bool is_up = otIp6IsEnabled(openthread_instance);

	LOG_INF("State changed! Flags: 0x%08" PRIx32 " Current role: %s Ip6: %s", flags,
		otThreadDeviceRoleToString(otThreadGetDeviceRole(openthread_instance)),
		(is_up ? "up" : "down"));

	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&openthread_state_change_cbs, entry, next, node) {
		if (entry->openthread_state_changed_cb != NULL) {
			entry->openthread_state_changed_cb(flags, openthread_instance,
							   entry->user_data);
		}
	}
}

int openthread_state_change_callback_register(struct openthread_state_changed_callback *cb)
{
	CHECKIF(cb == NULL || cb->openthread_state_changed_cb == NULL) {
		return -EINVAL;
	}

	openthread_mutex_lock();
	sys_slist_append(&openthread_state_change_cbs, &cb->node);
	openthread_mutex_unlock();

	return 0;
}

void otTaskletsSignalPending(otInstance *instance)
{
	ARG_UNUSED(instance);

	k_work_submit_to_queue(&openthread_work_q, &openthread_work);
}

void otSysEventSignalPending(void)
{
	otTaskletsSignalPending(NULL);
}

int openthread_state_change_callback_unregister(struct openthread_state_changed_callback *cb)
{
	bool removed;

	CHECKIF(cb == NULL) {
		return -EINVAL;
	}

	openthread_mutex_lock();
	removed = sys_slist_find_and_remove(&openthread_state_change_cbs, &cb->node);
	openthread_mutex_unlock();

	if (!removed) {
		return -EALREADY;
	}

	return 0;
}

struct otInstance *openthread_get_default_instance(void)
{
	__ASSERT(openthread_instance, "OT instance is not initialized");
	return openthread_instance;
}

bool openthread_init(openthread_receive_cb rx_handler, void *context)
{
	struct k_work_queue_config q_cfg = {
		.name = "openthread",
		.no_yield = true,
	};
	otError err = OT_ERROR_NONE;

	/* Prevent multiple initializations */
	if (openthread_instance) {
		return true;
	}

	k_mutex_init(&openthread_lock);
	k_work_init(&openthread_work, openthread_process);

	openthread_mutex_lock();

	otSysInit(0, NULL);
	openthread_instance = otInstanceInitSingle();

	__ASSERT(openthread_instance, "OT instance initialization failed");

	if (IS_ENABLED(CONFIG_OPENTHREAD_SHELL)) {
		platformShellInit(openthread_instance);
	}

	if (IS_ENABLED(CONFIG_OPENTHREAD_COPROCESSOR)) {
		err = otPlatUartEnable();
		if (err != OT_ERROR_NONE) {
			LOG_ERR("Failed to enable UART: [%d]", err);
		}

		otNcpHdlcInit(openthread_instance, ncp_hdlc_send);
	} else {
		otIp6SetReceiveFilterEnabled(openthread_instance, true);

		__ASSERT(rx_handler != NULL, "Receive callback is not set");
		otIp6SetReceiveCallback(openthread_instance, rx_handler, context);

#if defined(CONFIG_OPENTHREAD_NAT64_TRANSLATOR)

		otIp4Cidr nat64_cidr;

		if (otIp4CidrFromString(CONFIG_OPENTHREAD_NAT64_CIDR, &nat64_cidr) ==
		    OT_ERROR_NONE) {
			if (otNat64SetIp4Cidr(openthread_instance, &nat64_cidr) != OT_ERROR_NONE) {
				LOG_ERR("Incorrect NAT64 CIDR");
				return false;
			}
		} else {
			LOG_ERR("Failed to parse NAT64 CIDR");
			return false;
		}

		otNat64SetReceiveIp4Callback(openthread_instance, rx_handler, context);

#endif /* CONFIG_OPENTHREAD_NAT64_TRANSLATOR */

		sys_slist_init(&openthread_state_change_cbs);
		err = otSetStateChangedCallback(openthread_instance, &ot_state_changed_handler,
						NULL);
		if (err != OT_ERROR_NONE) {
			LOG_ERR("Could not set state changed callback: %d", err);
			return false;
		}
	}

	openthread_mutex_unlock();

	/* Start work queue for the OpenThread module */
	k_work_queue_start(&openthread_work_q, ot_stack_area, K_KERNEL_STACK_SIZEOF(ot_stack_area),
			   OT_PRIORITY, &q_cfg);

	(void)k_work_submit_to_queue(&openthread_work_q, &openthread_work);

	return true;
}

int openthread_run(void)
{
	openthread_mutex_lock();
	otError error = OT_ERROR_NONE;

	if (otDatasetIsCommissioned(openthread_instance)) {
		/* OpenThread already has dataset stored - skip the
		 * configuration.
		 */
		LOG_DBG("OpenThread already commissioned.");
	} else if (IS_ENABLED(CONFIG_OPENTHREAD_JOINER_AUTOSTART)) {
		/* No dataset - initiate network join procedure. */
		LOG_DBG("Starting OpenThread join procedure.");

		error = otJoinerStart(openthread_instance, OT_JOINER_PSKD, NULL,
				      ZEPHYR_PACKAGE_NAME, OT_PLATFORM_INFO, PACKAGE_VERSION, NULL,
				      &ot_joiner_start_handler, NULL);

		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to start joiner [%d]", error);
		}

		goto exit;
	} else {
		/* No dataset - load the default configuration. */
		LOG_DBG("Loading OpenThread default configuration.");

		otExtendedPanId xpanid;
		otNetworkKey networkKey;

		error = otThreadSetNetworkName(openthread_instance, OT_NETWORK_NAME);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to set %s [%d]", "network name", error);
			goto exit;
		}

		error = otLinkSetChannel(openthread_instance, OT_CHANNEL);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to set %s [%d]", "channel", error);
			goto exit;
		}

		error = otLinkSetPanId(openthread_instance, OT_PANID);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to set %s [%d]", "PAN ID", error);
			goto exit;
		}

		net_bytes_from_str(xpanid.m8, 8, (char *)OT_XPANID);
		error = otThreadSetExtendedPanId(openthread_instance, &xpanid);
		if (error != OT_ERROR_NONE) {
			LOG_ERR("Failed to set %s [%d]", "ext PAN ID", error);
			goto exit;
		}

		if (strlen(OT_NETWORKKEY)) {
			net_bytes_from_str(networkKey.m8, OT_NETWORK_KEY_SIZE,
					   (char *)OT_NETWORKKEY);
			error = otThreadSetNetworkKey(openthread_instance, &networkKey);
			if (error != OT_ERROR_NONE) {
				LOG_ERR("Failed to set %s [%d]", "network key", error);
				goto exit;
			}
		}
	}

	LOG_INF("Network name: %s", otThreadGetNetworkName(openthread_instance));

	/* Start the network. */
	error = otThreadSetEnabled(openthread_instance, true);
	if (error != OT_ERROR_NONE) {
		LOG_ERR("Failed to start the OpenThread network [%d]", error);
	}

exit:

	openthread_mutex_unlock();

	return error == OT_ERROR_NONE ? 0 : -EIO;
}

int openthread_stop(void)
{
	otError error;

	if (IS_ENABLED(CONFIG_OPENTHREAD_COPROCESSOR)) {
		return 0;
	}

	openthread_mutex_lock();

	error = otThreadSetEnabled(openthread_instance, false);
	if (error == OT_ERROR_INVALID_STATE) {
		LOG_DBG("Openthread interface was not up [%d]", error);
	}

	openthread_mutex_unlock();

	return 0;
}

void openthread_mutex_lock(void)
{
	(void)k_mutex_lock(&openthread_lock, K_FOREVER);
}

int openthread_mutex_try_lock(void)
{
	return k_mutex_lock(&openthread_lock, K_NO_WAIT);
}

void openthread_mutex_unlock(void)
{
	(void)k_mutex_unlock(&openthread_lock);
}
