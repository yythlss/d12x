/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BT UART transport adapter for bt_test tool.
 * Reuses NimBLE's HCI UART (rtthread_uart_tx / LPKG_NIMBLE_HCI_UART_DEVICE_NAME).
 */

#include "aic_plat_log.h"
#include "rtos_port.h"

#if defined(CONFIG_AIC8800_BT_SUPPORT) && defined(LPKG_NIMBLE_HCI_USING_RTT_UART)

extern void rtthread_uart_tx(const uint8_t *buf, size_t len);

#define BT_HCI_QUEUE_SIZE   (2048)
#define BT_HCI_QUEUE_DEPTH  (10)

rtos_queue bt_driver_server_queue = NULL;

int lbh_uart_open(void)
{
    if (bt_driver_server_queue)
        return 0;

    if (rtos_queue_create(BT_HCI_QUEUE_SIZE, BT_HCI_QUEUE_DEPTH,
                          &bt_driver_server_queue, "bt_drv_q")) {
        AIC_LOG_PRINTF("bt_driver_server_queue create fail\n");
        return -1;
    }
    return 0;
}

int hci_send(char *data, int len)
{
    if (!data || len <= 0)
        return -1;
    rtthread_uart_tx((const uint8_t *)data, (size_t)len);
    return 0;
}

int send_to_server(unsigned char *buffer, int len)
{
    if (!buffer || len <= 0)
        return -1;
    rtthread_uart_tx((const uint8_t *)buffer, (size_t)len);
    return 0;
}

#endif /* CONFIG_AIC8800_BT_SUPPORT && LPKG_NIMBLE_HCI_USING_RTT_UART */
