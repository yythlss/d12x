/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Siyao Li <siyao.li@artinchip.com>
 */
#include "hal_gpai.h"

struct aic_gpai_ch aic_gpai_chs[] = {
#ifdef AIC_USING_GPAI0
    {
        .id = 0,
        .available = 1,
        .adc_acq = AIC_GPAI0_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI0,
#endif
        .obtain_data_mode = AIC_GPAI0_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI0_MODE,
#ifdef AIC_GPAI0_PERIOD_TIME
        .smp_period = AIC_GPAI0_PERIOD_TIME,
#endif
#ifdef AIC_GPAI_DRV_V11
        .fifo_depth = 8,
#else
        .fifo_depth = 32,
#endif
    },
#endif
#ifdef AIC_USING_GPAI1
    {
        .id = 1,
        .available = 1,
        .adc_acq = AIC_GPAI1_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI1,
#endif
        .obtain_data_mode = AIC_GPAI1_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI1_MODE,
#ifdef AIC_GPAI1_PERIOD_TIME
        .smp_period = AIC_GPAI1_PERIOD_TIME,
#endif
#ifdef AIC_GPAI_DRV_V11
        .fifo_depth = 8,
#else
        .fifo_depth = 32,
#endif
    },
#endif
#ifdef AIC_USING_GPAI2
    {
        .id = 2,
        .available = 1,
        .adc_acq = AIC_GPAI2_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI2,
#endif
        .obtain_data_mode = AIC_GPAI2_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI2_MODE,
#ifdef AIC_GPAI2_PERIOD_TIME
        .smp_period = AIC_GPAI2_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI3
    {
        .id = 3,
        .available = 1,
        .adc_acq = AIC_GPAI3_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI3,
#endif
        .obtain_data_mode = AIC_GPAI3_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI3_MODE,
#ifdef AIC_GPAI3_PERIOD_TIME
        .smp_period = AIC_GPAI3_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI4
    {
        .id = 4,
        .available = 1,
        .adc_acq = AIC_GPAI4_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI4,
#endif
        .obtain_data_mode = AIC_GPAI4_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI4_MODE,
#ifdef AIC_GPAI4_PERIOD_TIME
        .smp_period = AIC_GPAI4_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI5
    {
        .id = 5,
        .available = 1,
        .adc_acq = AIC_GPAI5_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI5,
#endif
        .obtain_data_mode = AIC_GPAI5_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI5_MODE,
#ifdef AIC_GPAI5_PERIOD_TIME
        .smp_period = AIC_GPAI5_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI6
    {
        .id = 6,
        .available = 1,
        .adc_acq = AIC_GPAI6_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI6,
#endif
        .obtain_data_mode = AIC_GPAI6_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI6_MODE,
#ifdef AIC_GPAI6_PERIOD_TIME
        .smp_period = AIC_GPAI6_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI7
    {
        .id = 7,
        .available = 1,
        .adc_acq = AIC_GPAI7_ADC_ACQ,
#ifdef AIC_GPAI_DRV_V20
        .dma_port_id = DMA_ID_GPAI7,
#endif
        .obtain_data_mode = AIC_GPAI7_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI7_MODE,
#ifdef AIC_GPAI7_PERIOD_TIME
        .smp_period = AIC_GPAI7_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI8
    {
        .id = 8,
        .available = 1,
        .adc_acq = AIC_GPAI8_ADC_ACQ,
        .dma_port_id = DMA_ID_GPAI8,
        .obtain_data_mode = AIC_GPAI8_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI8_MODE,
#ifdef AIC_GPAI8_PERIOD_TIME
        .smp_period = AIC_GPAI8_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI9
    {
        .id = 9,
        .available = 1,
        .adc_acq = AIC_GPAI9_ADC_ACQ,
        .dma_port_id = DMA_ID_GPAI9,
        .obtain_data_mode = AIC_GPAI9_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI9_MODE,
#ifdef AIC_GPAI9_PERIOD_TIME
        .smp_period = AIC_GPAI9_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI10
    {
        .id = 10,
        .available = 1,
        .adc_acq = AIC_GPAI10_ADC_ACQ,
        .dma_port_id = DMA_ID_GPAI10,
        .obtain_data_mode = AIC_GPAI10_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI10_MODE,
#ifdef AIC_GPAI10_PERIOD_TIME
        .smp_period = AIC_GPAI10_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
#ifdef AIC_USING_GPAI11
    {
        .id = 11,
        .available = 1,
        .adc_acq = AIC_GPAI11_ADC_ACQ,
        .dma_port_id = DMA_ID_GPAI11,
        .obtain_data_mode = AIC_GPAI11_OBTAIN_DATA_MODE,
        .mode = AIC_GPAI11_MODE,
#ifdef AIC_GPAI11_PERIOD_TIME
        .smp_period = AIC_GPAI11_PERIOD_TIME,
#endif
        .fifo_depth = 8,
    },
#endif
};
