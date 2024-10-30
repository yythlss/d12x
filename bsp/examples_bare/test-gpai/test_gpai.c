/*
 * Copyright (c) 2022-2024, Artinchip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Li Siyao <siyao.li@artinchip.com>
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <console.h>
#include <getopt.h>

#include "hal_adcim.h"
#include "hal_gpai.h"

/* The default voltages are set to D21x->3.0V, D31x、D12x->2.5V */
#define AIC_GPAI_DEFAULT_VOLTAGE        3
#define AIC_GPAI_ADC_MAX_VAL            0xFFF
#define AIC_GPAI_VOLTAGE_ACCURACY       10000
#define AIC_GPAI_DEFAULT_SAMPLES_NUM    100

static float g_def_voltage = AIC_GPAI_DEFAULT_VOLTAGE;
static int g_sample_num = AIC_GPAI_DEFAULT_SAMPLES_NUM;

static void cmd_gpai_usage(void)
{
    printf("Compile time: %s %s\n", __DATE__, __TIME__);
    printf("Usage: test_gpai [options]\n");
    printf("test_gpai read <channel_id>         : Select one channel in [0, %d], default is 0\n", AIC_GPAI_CH_NUM - 1);
    printf("test_gpai modify <default_voltage>  : Modify default voltage\n");
    printf("test_gpai set <samples_number>      : Set the number of samples,default is 100\n");
    printf("test_gpai help                      : Get this help\n");
    printf("\n");
    printf("Example: test_gpai read 4\n");
}

static int test_gpai_init(int ch)
{
    static int inited = 0;
    struct aic_gpai_ch *chan;

    if (!inited) {
        hal_adcim_probe();
        hal_gpai_clk_init();
        inited = 1;
    }

    hal_gpai_set_ch_num(AIC_GPAI_CH_NUM);
    chan = hal_gpai_ch_is_valid(ch);
    if (!chan)
        return -1;

    aich_gpai_enable(1);
    hal_gpai_clk_get(chan);
    aich_gpai_ch_init(chan, chan->pclk_rate);
    return 0;
}

static void cmd_gpai_set(int argc, char **argv)
{
    g_def_voltage = strtod(argv[1], NULL);
    if (g_def_voltage < 0) {
        printf("Please input valid default voltage\n");
        return;
    }
    printf("Successfully set the default voltage\n");
}


static int cmd_gpai_read(int argc, char **argv)
{
    u16 value;
    u32 ch;
    u32 cal_param;
    int cnt = 0;
    int voltage = 0;
    struct aic_gpai_ch *chan;
    int scale = AIC_GPAI_VOLTAGE_ACCURACY;
    int sample_cnt = g_sample_num;
    ch = strtod(argv[1], NULL);

    if ((ch < 0) || (ch >= AIC_GPAI_CH_NUM)) {
        printf("Invalid channel No.%d\n", ch);
        return -1;
    }

    cal_param = hal_adcim_auto_calibration();
    test_gpai_init(ch);

    chan = hal_gpai_ch_is_valid(ch);
    chan->complete = aicos_sem_create(0);
    aicos_request_irq(GPAI_IRQn, aich_gpai_isr, 0, NULL, NULL);

    while(sample_cnt) {
        aich_gpai_read(chan, &value, AIC_GPAI_TIMEOUT);
        cnt++;
        if (value) {
            voltage = hal_adcim_adc2voltage(&value, cal_param,
                                            AIC_GPAI_VOLTAGE_ACCURACY,
                                            g_def_voltage);
            printf("[%d] ch %d: %d\n", cnt, ch, value);
            printf("voltage : %d.%04d v\n", voltage / scale, voltage % scale);
        }
        sample_cnt--;
    }

    return 0;
}

static int cmd_test_gpai(int argc, char *argv[])
{
    if (argc < 3) {
        cmd_gpai_usage();
        return 0;
    }

    if (!strcmp(argv[1], "read")) {
        cmd_gpai_read(argc - 1, &argv[1]);
        return 0;
    }

    if (!strcmp(argv[1], "set")) {
        g_sample_num = atoi(argv[2]);
        if (g_sample_num <= 0)
            printf("Please set the number of samples\n");
        return 0;
    }

    if (!strcmp(argv[1], "modify")) {
        cmd_gpai_set(argc - 1, &argv[1]);
        return 0;
    }

    cmd_gpai_usage();

    return 0;
}

CONSOLE_CMD(test_gpai, cmd_test_gpai,  "GPAI test example");
