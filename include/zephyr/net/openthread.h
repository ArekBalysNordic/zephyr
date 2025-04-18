/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief OpenThread stack public header
 */

#ifndef ZEPHYR_INCLUDE_NET_OPENTHREAD_H_
#define ZEPHYR_INCLUDE_NET_OPENTHREAD_H_

/**
 * @brief OpenThread stack public header
 * @defgroup openthread OpenThread stack
 * @since 1.11
 * @version 0.8.0
 * @ingroup ieee802154
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/kernel/thread.h>

#include <openthread/instance.h>
#include <openthread/message.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @cond INTERNAL_HIDDEN
 */
/**
 * @brief Type of pkt_list
 */
struct pkt_list_elem {
	struct net_pkt *pkt;
};

/**
 * @brief OpenThread l2 private data.
 */
struct openthread_context {
	/** @deprecated Pointer to OpenThread stack instance. This is deprecated and will be removed
	 * in a future release. This field must not be used outside anymore.
	 */
	__deprecated otInstance *instance;

	/** Pointer to OpenThread network interface */
	struct net_if *iface;

	/** Index indicates the head of pkt_list ring buffer */
	uint16_t pkt_list_in_idx;

	/** Index indicates the tail of pkt_list ring buffer */
	uint16_t pkt_list_out_idx;

	/** Flag indicates that pkt_list is full */
	uint8_t pkt_list_full;

	/** Array for storing net_pkt for OpenThread internal usage */
	struct pkt_list_elem pkt_list[CONFIG_OPENTHREAD_PKT_LIST_SIZE];

	/** @deprecated A mutex to protect API calls from being preempted. This is deprecated and
	 * will be removed in a future release. This field must not be used outside anymore.
	 */
	__deprecated struct k_mutex api_lock;

	/** @deprecated A work queue for all OpenThread activity. This is deprecated and will be
	 * removed in a future release. This field must not be used outside anymore.
	 */
	__deprecated struct k_work_q work_q;

	/** @deprecated Work object for OpenThread internal usage. This is deprecated and will be
	 * removed in a future release. This field must not be used outside anymore.
	 */
	__deprecated struct k_work api_work;

	/** @deprecated A list for state change callbacks. This is deprecated and will be removed in
	 * a future release.
	 */
	sys_slist_t state_change_cbs;
};
/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @brief The common callback type for receiving IPv4 (translated by NAT64) and IPv6 datagrams.
 *
 * This callback is called when a datagram is received.
 *
 * @param aMessage The message to receive.
 * @param aContext The context to pass to the callback.
 */
typedef void (*openthread_receive_cb)(otMessage *aMessage, void *aContext);

/** OpenThread state change callback  */

/**
 * @brief OpenThread state change callback structure
 *
 * Used to register a callback in the callback list. As many
 * callbacks as needed can be added as long as each of them
 * are unique pointers of struct openthread_state_changed_cb.
 * Beware such structure should not be allocated on stack.
 */
struct openthread_state_changed_callback {
	/**
	 * @brief Callback for notifying configuration or state changes.
	 *
	 * @param flags as per OpenThread otStateChangedCallback() aFlags parameter.
	 *        See https://openthread.io/reference/group/api-instance#otstatechangedcallback
	 * @param instance the OpenThread instance the callback is registered with.
	 * @param user_data Data to pass to the callback.
	 */
	void (*openthread_state_changed_cb)(otChangedFlags flags, struct otInstance *instance,
					    void *user_data);

	/** User data if required */
	void *user_data;

	/**
	 * Internally used field for list handling
	 *  - user must not directly modify
	 */
	sys_snode_t node;
};

/**
 * @deprecated use @ref openthread_state_changed_callback instead.
 *
 * @brief OpenThread state change callback structure
 *
 * Used to register a callback in the callback list. As many
 * callbacks as needed can be added as long as each of them
 * are unique pointers of struct openthread_state_changed_cb.
 * Beware such structure should not be allocated on stack.
 */
struct openthread_state_changed_cb {
	/**
	 * @brief Callback for notifying configuration or state changes.
	 *
	 * @param flags as per OpenThread otStateChangedCallback() aFlags parameter.
	 *        See https://openthread.io/reference/group/api-instance#otstatechangedcallback
	 * @param ot_context the OpenThread context the callback is registered with.
	 * @param user_data Data to pass to the callback.
	 */
	void (*state_changed_cb)(otChangedFlags flags, struct openthread_context *ot_context,
				 void *user_data);

	/** User data if required */
	void *user_data;

	/**
	 * Internally used field for list handling
	 *  - user must not directly modify
	 */
	sys_snode_t node;
};

/**
 * @brief Registers callbacks which will be called when certain configuration
 * or state changes occur within OpenThread.
 *
 * @param cb callback struct to register.
 */
int openthread_state_change_callback_register(struct openthread_state_changed_callback *cb);

/**
 * @brief Unregisters OpenThread configuration or state changed callbacks.
 *
 * @param cb callback struct to unregister.
 */
int openthread_state_change_callback_unregister(struct openthread_state_changed_callback *cb);

/**
 * @deprecated use @ref openthread_platform_state_changed_cb_register from modules/openthread
 * instead.
 *
 * @brief Registers callbacks which will be called when certain configuration
 * or state changes occur within OpenThread.
 *
 * @param ot_context the OpenThread context to register the callback with.
 * @param cb callback struct to register.
 */
__deprecated int openthread_state_changed_cb_register(struct openthread_context *ot_context,
						      struct openthread_state_changed_cb *cb);

/**
 * @deprecated use @ref openthread_platform_state_changed_cb_unregister from modules/openthread
 * instead.
 *
 * @brief Unregisters OpenThread configuration or state changed callbacks.
 *
 * @param ot_context the OpenThread context to unregister the callback from.
 * @param cb callback struct to unregister.
 */
__deprecated int openthread_state_changed_cb_unregister(struct openthread_context *ot_context,
							struct openthread_state_changed_cb *cb);

/**
 * @brief Get OpenThread thread identification.
 */
k_tid_t openthread_thread_id_get(void);

/**
 * @brief Get pointer to default OpenThread context.
 *
 * @retval !NULL On success.
 * @retval NULL  On failure.
 */
struct openthread_context *openthread_get_default_context(void);

/**
 * @brief Get pointer to default OpenThread instance.
 *
 * @retval !NULL On success.
 * @retval NULL  On failure.
 */
struct otInstance *openthread_get_default_instance(void);

/**
 * @brief Initialize the OpenThread module.
 *
 * This function:
 * - Initializes the OpenThread module.
 * - Creates an OpenThread single instance.
 * - Starts the shell.
 * - Enables the UART and NCP HDLC for coprocessor purposes.
 * - Initializes the NAT64 translator.
 * - Creates a work queue for the OpenThread module.
 * - Initializes the state change callback list.
 *
 * @note This function is automatically called by Zephyr's networking layer.
 * If you want to initialize the OpenThread independently, call this function
 * in your application init code.
 *
 * @param rx_handler The receive callback for the OpenThread module.
 * @param context The context to pass to the callback.
 * @return true if initialization succeeded, false otherwise.
 */
bool openthread_init(openthread_receive_cb rx_handler, void *context);

/**
 * @brief Runs the OpenThread network.
 *
 * @details Prepares the OpenThread network and enables it.
 * Depends on active settings: it uses stored network configuration,
 * start joining procedure or uses default network configuration. Additionally
 * when the device is MTD, it sets the SED mode to properly attach the network.
 */
int openthread_run(void);

/**
 * @brief Disables the OpenThread network.
 */
int openthread_stop(void);

/**
 * @deprecated use @ref openthread_run instead.
 *
 * @brief Starts the OpenThread network.
 *
 * @details Depends on active settings: it uses stored network configuration,
 * start joining procedure or uses default network configuration. Additionally
 * when the device is MTD, it sets the SED mode to properly attach the network.
 *
 * @param ot_context
 */
__deprecated int openthread_start(struct openthread_context *ot_context);

/**
 * @brief Lock internal mutex before accessing OpenThread API.
 *
 * @details OpenThread API is not thread-safe, therefore before accessing any
 * API function, it's needed to lock the internal mutex, to prevent the
 * OpenThread thread from preempting the API call.
 */
void openthread_mutex_lock(void);

/**
 * @brief Try to lock internal mutex before accessing OpenThread API.
 *
 * @details This function behaves like openthread_mutex_lock() provided that
 * the internal mutex is unlocked. Otherwise, it exists immediately and returns
 * a negative value.
 */
int openthread_mutex_try_lock(void);

/**
 * @brief Unlock internal mutex after accessingOpenThread API.
 */
void openthread_mutex_unlock(void);

/**
 * @deprecated use @ref openthread_mutex_lock.
 *
 * @brief Lock internal mutex before accessing OT API.
 *
 * @details OpenThread API is not thread-safe, therefore before accessing any
 * API function, it's needed to lock the internal mutex, to prevent the
 * OpenThread thread from preempting the API call.
 *
 * @param ot_context Context to lock.
 */
__deprecated void openthread_api_mutex_lock(struct openthread_context *ot_context);

/**
 * @deprecated use @ref openthread_mutex_try_lock instead.
 *
 * @brief Try to lock internal mutex before accessing OT API.
 *
 * @details This function behaves like openthread_api_mutex_lock() provided that
 * the internal mutex is unlocked. Otherwise, it exists immediately and returns
 * a negative value.
 *
 * @param ot_context Context to lock.
 * @retval 0  On success.
 * @retval <0 On failure.
 */
__deprecated int openthread_api_mutex_try_lock(struct openthread_context *ot_context);

/**
 * @deprecated use @ref openthread_mutex_unlock instead.
 *
 * @brief Unlock internal mutex after accessing OT API.
 *
 * @param ot_context Context to unlock.
 */
__deprecated void openthread_api_mutex_unlock(struct openthread_context *ot_context);

/** @cond INTERNAL_HIDDEN */

#define OPENTHREAD_L2_CTX_TYPE struct openthread_context

/** @endcond */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_NET_OPENTHREAD_H_ */
