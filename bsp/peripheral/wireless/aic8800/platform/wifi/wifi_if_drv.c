/*
 * Copyright (c) 2023, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/pin.h>
#include <drivers/sdio.h>
#include <drivers/mmcsd_card.h>
#include <drivers/mmcsd_core.h>
#include <rtdevice.h>
#include <rtthread.h>

#include "sdio_func.h"
#include "sdio_port.h"
#include "aic_plat_log.h"
#include "rtos_port.h"
#include "plat_port.h"
#include "wifi_port.h"
#include "general_wireless_pm.h"

#define AIC8800_SDIO_READY_TIMEOUT_MS 2000

struct rt_mmcsd_card *g_wifi_if_sdio = NULL;
struct sdio_func g_wifi_if_sdio_funcs[SDIOM_MAX_FUNCS];
static struct rt_mutex wifi_init_lock;
static rt_bool_t wifi_driver_registered;
static int wifi_init_result = -RT_EEMPTY;

static int wifi_if_sdio_lock_init(void)
{
    return rt_mutex_init(&wifi_init_lock, "aicsdio", RT_IPC_FLAG_PRIO);
}
INIT_COMPONENT_EXPORT(wifi_if_sdio_lock_init);

int aic8800_reset(void)
{
    return 0;
}

int aic8800_power_on(void)
{
    return 0;
}

int aic8800_power_off(void)
{
    return 0;
}

static rt_int32_t wifi_if_sdio_probe(struct rt_mmcsd_card *card)
{
    int idx;
    if (card->host != aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID))
        return -RT_ERROR;
    for (idx = 0; idx < SDIOM_MAX_FUNCS; idx++) {
        if (!card->sdio_function[idx])
            return -RT_ERROR;
    }
    AIC_LOG_PRINTF("%s: card_type=%d, host_flags=0x%x\n", __func__, card->card_type, card->host->flags);
    g_wifi_if_sdio = card;
    for (idx = 0; idx < SDIOM_MAX_FUNCS; idx++) {
        g_wifi_if_sdio_funcs[idx].num    = card->sdio_function[idx]->num;
        g_wifi_if_sdio_funcs[idx].vendor = card->sdio_function[idx]->manufacturer;
        g_wifi_if_sdio_funcs[idx].device = card->sdio_function[idx]->product;
        g_wifi_if_sdio_funcs[idx].drv_priv = card;
        AIC_LOG_PRINTF("%s: id:%d, num:%d manid:%x product:%x\n", __func__, idx,
            card->sdio_function[idx]->num, card->sdio_function[idx]->manufacturer, card->sdio_function[idx]->product);
    }

    return RT_EOK;
}

static rt_int32_t wifi_if_sdio_remove(struct rt_mmcsd_card *card)
{
    AIC_LOG_PRINTF("%s: card_type=%d, host_flags=0x%x\n", __func__, card->card_type, card->host->flags);
    if (wifi_init_result == RT_EOK)
        aicwf_sdio_remove(&g_wifi_if_sdio_funcs[0]);
    g_wifi_if_sdio = NULL;
    return 0;
}

#if defined(CONFIG_AIC8800D80_SUPPORT)
struct rt_sdio_device_id wifi_if_sdio_ids_d80[]= {
    { 0, 0xc8a1, 0x0082},
};
#endif

#if defined(CONFIG_AIC8800DC_SUPPORT) || defined(CONFIG_AIC8800DW_SUPPORT)
struct rt_sdio_device_id wifi_if_sdio_ids_dcdw[]= {
    { 0, 0xc8a1, 0xc08d},
};
#endif

struct rt_sdio_driver wifi_if_sdio_drv = {
    "aicwfsdio",
    wifi_if_sdio_probe,
    wifi_if_sdio_remove,
    NULL,
};

static int wifi_if_sdio_wait_ready(const struct rt_sdio_device_id *id)
{
    struct rt_mmcsd_host *host = aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID);
    if (!host)
        return -RT_ERROR;

    rt_tick_t started = rt_tick_get();
    rt_tick_t timeout = rt_tick_from_millisecond(AIC8800_SDIO_READY_TIMEOUT_MS);
    do {
        mmcsd_host_lock(host);
        struct rt_mmcsd_card *card = host->card;
        rt_bool_t ready = card && card->sdio_function_num >= SDIOM_MAX_FUNCS - 1 &&
                          card->sdio_function[1] &&
                          card->sdio_function[1]->manufacturer == id->manufacturer &&
                          card->sdio_function[1]->product == id->product;
        mmcsd_host_unlock(host);
        if (ready)
            return RT_EOK;
        rt_thread_mdelay(20);
    } while (rt_tick_get() - started < timeout);

    return -RT_ETIMEOUT;
}

static int wifi_if_sdio_register(void)
{
    int chip_id = CONFIG_CHIPID_SELECT;
    if (0) {}
    #if defined(CONFIG_AIC8800D80_SUPPORT)
    else if (chip_id == PRODUCT_ID_AIC8800D80) {
        wifi_if_sdio_drv.id = wifi_if_sdio_ids_d80;
    }
    #endif
    #if defined(CONFIG_AIC8800DC_SUPPORT) || defined(CONFIG_AIC8800DW_SUPPORT)
    else if ((chip_id == PRODUCT_ID_AIC8800DC) || (chip_id == PRODUCT_ID_AIC8800DW)) {
        wifi_if_sdio_drv.id = wifi_if_sdio_ids_dcdw;
    }
    #endif
    else {
        AIC_LOG_PRINTF("invalid chip_id = %d\n", chip_id);
        return -1;
    }
    int ret = wifi_if_sdio_wait_ready(wifi_if_sdio_drv.id);
    if (ret != RT_EOK) {
        AIC_LOG_PRINTF("AIC8800 SDIO%d not ready: rc=%d\n", AIC_WIFI_SDMC_ID, ret);
        return ret;
    }

    AIC_LOG_PRINTF("AIC8800 SDIO%d ready, product=0x%x\n",
                   AIC_WIFI_SDMC_ID, wifi_if_sdio_drv.id->product);
    ret = sdio_register_driver(&wifi_if_sdio_drv);
    wifi_driver_registered = ret != -RT_ENOMEM;
    if (ret != RT_EOK) {
        AIC_LOG_PRINTF("AIC8800 SDIO driver registration rc=%d\n", ret);
        return ret;
    }

    ret = aicwf_sdio_probe(&g_wifi_if_sdio_funcs[0]);
    AIC_LOG_PRINTF("AIC8800 firmware init rc=%d\n", ret);
    return ret;
}

int wifi_if_sdio_init(void)
{
    int ret = rt_mutex_take(&wifi_init_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK)
        return ret;
    if (!wifi_driver_registered)
        wifi_init_result = wifi_if_sdio_register();
    ret = wifi_init_result;
    rt_mutex_release(&wifi_init_lock);
    return ret;
}

int wifi_if_sdio_deinit(void)
{
    int ret = rt_mutex_take(&wifi_init_lock, RT_WAITING_FOREVER);
    if (ret != RT_EOK)
        return ret;
    if (wifi_driver_registered) {
        ret = sdio_unregister_driver(&wifi_if_sdio_drv);
        if (ret == RT_EOK) {
            wifi_driver_registered = RT_FALSE;
            wifi_init_result = -RT_EEMPTY;
        }
    }
    rt_mutex_release(&wifi_init_lock);
    return ret;
}

#ifdef RT_USING_PM
static struct rt_mmcsd_host *aic8800_pm_get_host(void)
{
    return aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID);
}

static int aic8800_pm_suspend(rt_uint8_t mode)
{
    return 0;
}

static int aic8800_pm_resume(rt_uint8_t mode)
{
    return wifi_if_sdio_deinit();
}

struct aic_wlan_chip_pm_ops g_aic8800_pm_ops = {
    .suspend   = aic8800_pm_suspend,
    .resume    = aic8800_pm_resume,
    .get_host  = aic8800_pm_get_host,
};
#endif

