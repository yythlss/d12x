/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date         Author          Notes
 * 2026-07-06   zequan.liang    hardware loopback test
 */

#include <getopt.h>
#include <string.h>
#include <rtthread.h>
#include <aic_core.h>

#define TEST_UART_PORT      "uart4"
#define TEST_UART_TIMEOUT   2000

static const char *g_test_str = "ArtInChip Uart Loopback Test\n";
#define TEST_STR_LEN        (sizeof("ArtInChip Uart Loopback Test\n") - 1)

static const char sopts[] = "u:t:p:h";
static const struct option lopts[] = {
    {"uart",    required_argument, NULL, 'u'},
    {"timeout", required_argument, NULL, 't'},
    {"port",    required_argument, NULL, 'p'},
    {"help",    no_argument,       NULL, 'h'},
    {0, 0, 0, 0}
};

static void test_uart_loopback_usage(char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("\t -u, --uart    Select a serial port. Default: uart4\n");
    printf("\t -p, --port    Same as -u\n");
    printf("\t -t, --timeout Timeout in ms. Default: 2000\n");
    printf("\t -h, --help\n");
    printf("\n");
    printf("Example: %s -u uart4 -t 2000\n", program);
}

int test_uart_loopback(int argc, char *argv[])
{
    int c;
    int timeout_ms = TEST_UART_TIMEOUT;
    int result = -RT_ERROR;
    char uart_name[RT_NAME_MAX] = TEST_UART_PORT;
    rt_device_t serial;
    rt_err_t ret;
    rt_tick_t deadline;
    char buf[128] = {0};
    char ch;
    int total = 0;

    optind = 0;
    while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (c) {
        case 'u':
        case 'p':
            rt_strncpy(uart_name, optarg, RT_NAME_MAX);
            break;
        case 't':
            timeout_ms = atoi(optarg);
            if (timeout_ms <= 0)
                timeout_ms = TEST_UART_TIMEOUT;
            break;
        case 'h':
        default:
            test_uart_loopback_usage(argv[0]);
            return 0;
        }
    }

    serial = rt_device_find(uart_name);
    if (!serial) {
        rt_kprintf("find %s failed\n", uart_name);
        return -RT_ERROR;
    }

    ret = rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (ret != RT_EOK) {
        rt_kprintf("open %s failed\n", uart_name);
        return -RT_ERROR;
    }

    /* drain stale RX data */
    while (rt_device_read(serial, 0, &ch, 1) > 0) ;

    rt_device_write(serial, 0, g_test_str, TEST_STR_LEN);

    /* read back with timeout */
    deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    while (total < TEST_STR_LEN && rt_tick_get() < deadline) {
        int n = rt_device_read(serial, 0, buf + total, TEST_STR_LEN - total);
        if (n > 0) {
            total += n;
            continue;
        }
        rt_thread_mdelay(5);
    }

    if (total == TEST_STR_LEN && strncmp(g_test_str, buf, TEST_STR_LEN) == 0) {
        printf("%s loopback test PASS\n", uart_name);
        result = 0;
    } else {
        buf[total] = '\0';
        printf("Send: %s", g_test_str);
        printf("Recv: %s\n", buf);
        printf("%s loopback test FAIL (got %d/%d)\n", uart_name, total, TEST_STR_LEN);
    }

    rt_device_close(serial);
    return result;
}

MSH_CMD_EXPORT(test_uart_loopback, ArtInChip Uart loopback test);
