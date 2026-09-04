/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <rtconfig.h>

#include "aic_core.h"
#include "aic_log.h"

#include "hal_mailbox.h"
#include "rpmsg_lite.h"
#include "rpmsg_ns.h"
#include "rpmsg_aic.h"
#include "rpmsg_queue.h"

static struct rpmsg_lite_instance *g_pm_rpmsg = NULL;
static struct rpmsg_lite_endpoint *g_pm_ept = NULL;
static rpmsg_queue_handle g_pm_queue;
static char g_name[32] = "rpmsg-rtss";
static char rx_buf[256];

static void pm_rpmsg_lite_free(void)
{
    pr_info("%s: Deinit the RPMsg-lite instance\n", g_name);

    if (g_pm_queue) {
        rpmsg_queue_destroy(g_pm_rpmsg, g_pm_queue);
        g_pm_queue = NULL;
    }
    if (g_pm_ept && g_pm_rpmsg) {
        rpmsg_lite_destroy_ept(g_pm_rpmsg, g_pm_ept);
        g_pm_ept = NULL;
    }
    if (g_pm_rpmsg) {
        rpmsg_lite_deinit(g_pm_rpmsg);
        g_pm_rpmsg = NULL;
    }
}

static void pm_rpmsg_lite_thread(void *parameter)
{
    int ret = 0;
    uint32_t len;
    volatile uint32_t remote_addr;
    struct aic_rpmsg *msg;
    struct aic_rpmsg tx_msg;

    pr_info("Wait for the notify from master ...\n");
    while (!rpmsg_lite_is_link_up(g_pm_rpmsg)) {
        aicos_msleep(300);
        platform_check_interrupt();
    }
    pr_info("Yeah! The master is online, try announce ...\n");
    rpmsg_ns_announce(g_pm_rpmsg, g_pm_ept, g_name, 0);
    rpmsg_lite_enable_master_notify(g_pm_rpmsg, false);

    while (1)
    {
        ret = rpmsg_queue_recv(g_pm_rpmsg, g_pm_queue, (uint32_t *)&remote_addr, rx_buf, 256, &len, RL_BLOCK);
        if (ret)
        {
            pr_err("receive rpmsg error!\n");
            goto __err;
        }

        msg = (struct aic_rpmsg *)rx_buf;
        pr_info("cmd: 0x%x\n", msg->cmd);

        if (msg->cmd == RPMSG_CMD_REQ_STANDBY)
        {
            tx_msg.cmd = RPMSG_CMD_ACK;
            tx_msg.len = 0;
            tx_msg.seq = 0;
            ret = rpmsg_lite_send(g_pm_rpmsg, g_pm_ept, remote_addr, (char *)&tx_msg, sizeof(struct aic_rpmsg), RL_BLOCK);
            if (ret)
            {
                pr_err("reponse rpmsg error!\n");
                goto __err;
            }
            LOG_D("request sleep\n");
            /* request to enter suspend, the system will enter suspend in idle */
            rt_pm_release(0);
        }
    }

__err:
    pm_rpmsg_lite_free();
    aicos_thread_delete(NULL);
}

int pm_rpmsg_lite_init(void)
{
    void *shmem = NULL;
    aicos_thread_t thread;

    shmem = (void *)AIC_MAILBOX_SHMEM1_ADDR;
    pr_info("%s: Init RPMsg-lite as a remote ...\n", g_name);

    g_pm_rpmsg = rpmsg_lite_remote_init(shmem, 0, 0, g_name);
    if (!g_pm_rpmsg)
    {
        pr_err("%s: Failed to init RPMsg-lite instance\n", g_name);
        goto __exit;
    }

    g_pm_queue = rpmsg_queue_create(g_pm_rpmsg);
    if (!g_pm_queue)
    {
        pr_err("%s: Failed to create rpmsg queue\n", g_name);
        goto __exit;
    }

    g_pm_ept = rpmsg_lite_create_ept(g_pm_rpmsg, RL_REMOTE,
                                      rpmsg_queue_rx_cb, g_pm_queue);
    if (g_pm_ept == NULL)
    {
        pr_err("%s: Failed to init ept\n", g_name);
        goto __exit;
    }

    thread = aicos_thread_create("rpmsg_wait", 4096, 10, pm_rpmsg_lite_thread, NULL);
    if (!thread)
    {
        pr_err("create rpmsg_wait thread failed!\n");
        goto __exit;
    }

    return 0;
__exit:
    pm_rpmsg_lite_free();
    return -1;
}

INIT_APP_EXPORT(pm_rpmsg_lite_init);
