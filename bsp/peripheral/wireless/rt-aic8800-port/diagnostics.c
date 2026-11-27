#include <rtthread.h>

#if defined(RT_USING_FINSH) && defined(FINSH_USING_MSH)
#include <finsh.h>
#include <stdlib.h>
#include <string.h>

#include "general_wireless_pm.h"
#include "rt_aic8800/rtthread.h"
#include "rt_aic8800/sdio.h"
#include "fmacfw_8800d80_u02.h"

static struct rt_aic8800_boot_result firmware_boot;
static struct rt_aic8800_vif_result ap_probe;
static rt_bool_t vif_probe_attempted;

static const struct rt_aic8800_d80_radio_config d213_radio = {
    .tx_power_levels = {
        1,
        21, 21, 21, 21, 21, 21, 21, 21, 19, 19, 17, 17,
        21, 21, 21, 21, 17, 17, 16, 16, 15, 0,
        21, 21, 21, 21, 17, 17, 16, 16, 15, 15, 14, 14,
        0x80, 0x80, 0x80, 0x80, 18, 18, 18, 18, 16, 16, 15, 15,
        18, 18, 18, 18, 16, 16, 15, 15, 14, 14,
        18, 18, 18, 18, 16, 16, 14, 14, 13, 13, 12, 12
    }
};

static void aic8800_print_version(void)
{
    const struct rt_aic8800_version *version = &firmware_boot.version;

    rt_kprintf("FMAC lmac=0x%08x features=0x%08x max_sta=%u max_vif=%u\n",
               (unsigned int)version->lmac_version, (unsigned int)version->features,
               version->max_stations, version->max_vifs);
}

static int aic8800_boot(rt_bool_t radio)
{
    if (firmware_boot.upload_started)
    {
        rt_kprintf("Power-cycle the module before another firmware boot attempt\n");
        return -RT_EBUSY;
    }

    struct rt_mmcsd_host *host = aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID);
    int result = radio ?
        rt_aic8800_sdio_boot_d80_radio(host, fmacfw_8800d80_u02, sizeof(fmacfw_8800d80_u02),
                                     &d213_radio, &firmware_boot) :
        rt_aic8800_sdio_boot_d80(host, fmacfw_8800d80_u02, sizeof(fmacfw_8800d80_u02),
                               &firmware_boot);
    const struct rt_aic8800_rom_probe *mailbox = &firmware_boot.mailbox;

    rt_kprintf("FMAC stage=%s rc=%d cleanup=%d uploaded=%u/%u request=0x%04x address=0x%08x\n",
               mailbox->stage, result, mailbox->cleanup_result,
               (unsigned int)firmware_boot.uploaded, (unsigned int)sizeof(fmacfw_8800d80_u02),
               mailbox->request_id, (unsigned int)mailbox->request_address);
    rt_kprintf("FMAC RX bytes=%u type=0x%02x id=0x%04x offset=%u credits=0x%02x status=0x%02x\n",
               mailbox->response_length, mailbox->response_type, mailbox->response_id,
               mailbox->payload_offset, mailbox->credits, mailbox->status);
    if (firmware_boot.start_app_confirmed)
        rt_kprintf("DBG_START_APP_CFM word=0x%08x\n", (unsigned int)firmware_boot.start_app_word);
    if (radio)
        rt_kprintf("FMAC radio calibrated=%u\n", firmware_boot.radio_ready);
    if (result == RT_EOK)
    {
        aic8800_print_version();
        rt_kprintf("MM_VERSION_CFM:");
        for (size_t index = 0; index < sizeof(firmware_boot.version_payload); ++index)
            rt_kprintf(" %02x", firmware_boot.version_payload[index]);
        rt_kprintf("\n");
    }
    return result;
}

static int aic8800_vif(rt_bool_t scan, const char *ssid)
{
    struct rt_aic8800_vif_result probe;
    const size_t ssid_length = ssid ? strlen(ssid) : 0;
    const char *label = ssid ? "JOIN" : (scan ? "SCAN" : "VIF");

    if (ssid && (!ssid_length || ssid_length > 32))
    {
        rt_kprintf("SSID must contain 1 to 32 bytes\n");
        return -RT_EINVAL;
    }
    if (!firmware_boot.runtime_ready)
    {
        rt_kprintf("Run aic8800 boot%s first\n", ssid ? " rf" : "");
        return -RT_EINVAL;
    }
    if (ssid && !firmware_boot.radio_ready)
    {
        rt_kprintf("Power-cycle and run aic8800 boot rf before join\n");
        return -RT_EINVAL;
    }
    if (vif_probe_attempted)
    {
        rt_kprintf("Power-cycle the module before repeating VIF setup\n");
        return -RT_EBUSY;
    }
    vif_probe_attempted = RT_TRUE;
    struct rt_mmcsd_host *host = aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID);
    int result;

    if (ssid)
        result = rt_aic8800_sdio_open_join_probe(host, &firmware_boot, ssid, ssid_length, &probe);
    else if (scan)
        result = rt_aic8800_sdio_scan_probe(host, &firmware_boot, &probe);
    else
        result = rt_aic8800_sdio_vif_probe(host, &firmware_boot, &probe);

    rt_kprintf("%s stage=%s rc=%d cleanup=%d request=0x%04x response=0x%04x fw_status=%u sta=%u ap=%u\n",
               label, probe.mailbox.stage, result, probe.mailbox.cleanup_result,
               probe.mailbox.request_id, probe.mailbox.response_id, probe.firmware_status,
               probe.station_index, probe.ap_index);
    if (scan || ssid)
        rt_kprintf("SCAN accepted=%u completed=%u frames=%u malformed=%u firmware_count=%u\n",
                   probe.scan_accepted, probe.scan_completion_received,
                   probe.scan_frames, probe.scan_malformed, probe.scan_firmware_count);
    if (ssid)
    {
        if (probe.target_frequency)
            rt_kprintf("JOIN target=%02x:%02x:%02x:%02x:%02x:%02x frequency=%u MHz rssi=%d dBm\n",
                       probe.target_bssid[0], probe.target_bssid[1], probe.target_bssid[2],
                       probe.target_bssid[3], probe.target_bssid[4], probe.target_bssid[5],
                       probe.target_frequency, probe.target_rssi);
        rt_kprintf("JOIN accepted=%u completed=%u assoc_status=%u peer=%u channel_context=%u\n",
                   probe.association_accepted, probe.association_completed, probe.association_status,
                   probe.peer_index, probe.channel_context);
    }
    return result;
}

static int aic8800_ap_start(const char *ssid, const char *channel_arg)
{
    char *end;
    const unsigned long channel = strtoul(channel_arg, &end, 10);
    const size_t length = strlen(ssid);

    if (end == channel_arg || *end || channel < 1 || channel > 11 || !length || length > 32)
    {
        rt_kprintf("AP requires an SSID of 1 to 32 bytes and a channel from 1 to 11\n");
        return -RT_EINVAL;
    }
    if (!firmware_boot.radio_ready || !firmware_boot.runtime_ready)
    {
        rt_kprintf("Run aic8800 boot rf after power-cycling the module\n");
        return -RT_EINVAL;
    }
    if (vif_probe_attempted)
    {
        rt_kprintf("Power-cycle the module before repeating VIF setup\n");
        return -RT_EBUSY;
    }

    vif_probe_attempted = RT_TRUE;
    int result = rt_aic8800_sdio_open_ap_start(aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID),
                                             &firmware_boot, ssid, length, (uint8_t)channel,
                                             &ap_probe);
    rt_kprintf("AP stage=%s rc=%d cleanup=%d request=0x%04x response=0x%04x fw_status=%u\n",
               ap_probe.mailbox.stage, result, ap_probe.mailbox.cleanup_result,
               ap_probe.mailbox.request_id, ap_probe.mailbox.response_id,
               ap_probe.firmware_status);
    if (ap_probe.ap_beacon_confirmed)
        rt_kprintf("AP beacon confirmation status=%u\n", ap_probe.ap_beacon_status);
    if (result != RT_EOK && ap_probe.mailbox.response_id)
        rt_kprintf("AP RX record_payload=%u parameters=%u vif=%u\n",
                   ap_probe.mailbox.response_record_length,
                   ap_probe.mailbox.response_parameter_length, ap_probe.ap_index);
    if (result == RT_EOK)
        rt_kprintf("AP SSID=%s channel=%u vif=%u channel_context=%u bcmc=%u\n",
                   ssid, (unsigned int)channel, ap_probe.ap_index, ap_probe.channel_context,
                   ap_probe.ap_broadcast_index);
    return result;
}

static int aic8800_ap_stop(void)
{
    if (!ap_probe.ap_remove_pending)
    {
        rt_kprintf("No AP VIF to stop\n");
        return -RT_EINVAL;
    }

    int result = rt_aic8800_sdio_open_ap_stop(aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID), &ap_probe);
    rt_kprintf("AP stage=%s rc=%d request=0x%04x response=0x%04x vif_pending=%u\n",
               ap_probe.mailbox.stage, result, ap_probe.mailbox.request_id,
               ap_probe.mailbox.response_id, ap_probe.ap_remove_pending);
    return result;
}

static int aic8800_ap_monitor(const char *seconds_arg)
{
    char *end;
    const unsigned long seconds = strtoul(seconds_arg, &end, 10);
    struct rt_aic8800_ap_rx_stats stats = {0};

    if (end == seconds_arg || *end || seconds < 1 || seconds > 30)
        return -RT_EINVAL;
    if (!ap_probe.ap_stop_pending || ap_probe.ap_broadcast_index == UINT8_MAX)
    {
        rt_kprintf("Start the AP before monitoring its RX FIFO\n");
        return -RT_EINVAL;
    }

    rt_kprintf("AP monitoring for %u s, connect the client now\n", (unsigned int)seconds);
    int result = rt_aic8800_sdio_open_ap_monitor(aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID),
                                               &ap_probe, (unsigned int)seconds * 1000U,
                                               &stats);
    rt_kprintf("AP RX rc=%d auth=%u assoc_req=%u data=%u station_loss=%u other=%u malformed=%u\n",
               result, (unsigned int)stats.auth_frames, (unsigned int)stats.assoc_frames,
               (unsigned int)stats.data_frames, (unsigned int)stats.station_losses,
               (unsigned int)stats.other_records, (unsigned int)stats.malformed_records);
    rt_kprintf("AP AUTH TX submitted=%u errors=%u confirmed=%u acked=%u\n",
               (unsigned int)stats.auth_tx, (unsigned int)stats.auth_tx_errors,
               (unsigned int)stats.auth_tx_confirmed, (unsigned int)stats.auth_tx_acked);
    if (stats.data_frames)
        rt_kprintf("AP RX last data station=%u\n", stats.last_data_station);
    return result;
}

static int aic8800_rom(void)
{
    struct rt_aic8800_rom_probe probe;
    int result = rt_aic8800_sdio_rom_probe(aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID), &probe);

    rt_kprintf("ROM stage=%s rc=%d cleanup=%d address=0x%08x credits=0x%02x status=0x%02x reply_offset=%u\n",
               probe.stage, result, probe.cleanup_result, (unsigned int)probe.request_address,
               probe.credits, probe.status, probe.payload_offset);
    if (probe.response_length)
        rt_kprintf("ROM RX bytes=%u type=0x%02x id=0x%04x\n",
                   probe.response_length, probe.response_type, probe.response_id);
    if (result == RT_EOK)
    {
        rt_kprintf("ROM chip_word=0x%08x flag_word=0x%08x chip_id=%x chip_sub_id=%x\n",
                   (unsigned int)probe.chip_word, (unsigned int)probe.flag_word,
                   (unsigned int)((probe.chip_word >> 16) & 0xffU),
                   (unsigned int)(probe.flag_word & 0xffU));
    }
    return result;
}

static int aic8800(int argc, char **argv)
{
    struct rt_aic8800_sdio_snapshot snapshot;
    int result;

    if (argc == 2 && strcmp(argv[1], "info") == 0)
    {
        rt_kprintf("rt-aic8800 SDIO diagnostics, SDMC%d, expected F1 %04x:%04x\n",
                   AIC_WIFI_SDMC_ID, RT_AIC8800_D80_SDIO_VENDOR, RT_AIC8800_D80_SDIO_PRODUCT);
        if (firmware_boot.runtime_ready)
            aic8800_print_version();
        else
            rt_kprintf("silicon revision and firmware version: unavailable\n");
        return RT_EOK;
    }

    if (argc == 2 && strcmp(argv[1], "rom") == 0)
        return aic8800_rom();

    if (argc == 2 && strcmp(argv[1], "boot") == 0)
        return aic8800_boot(RT_FALSE);

    if (argc == 3 && strcmp(argv[1], "boot") == 0 && strcmp(argv[2], "rf") == 0)
        return aic8800_boot(RT_TRUE);

    if (argc == 2 && strcmp(argv[1], "vif") == 0)
        return aic8800_vif(RT_FALSE, RT_NULL);

    if (argc == 2 && strcmp(argv[1], "scan") == 0)
        return aic8800_vif(RT_TRUE, RT_NULL);

    if (argc == 3 && strcmp(argv[1], "join") == 0)
        return aic8800_vif(RT_FALSE, argv[2]);

    if (argc == 5 && strcmp(argv[1], "ap") == 0 && strcmp(argv[2], "start") == 0)
        return aic8800_ap_start(argv[3], argv[4]);

    if (argc == 3 && strcmp(argv[1], "ap") == 0 && strcmp(argv[2], "stop") == 0)
        return aic8800_ap_stop();

    if (argc == 4 && strcmp(argv[1], "ap") == 0 && strcmp(argv[2], "monitor") == 0)
        return aic8800_ap_monitor(argv[3]);

    if (argc != 2 || strcmp(argv[1], "sdio") != 0)
    {
        rt_kprintf("usage: aic8800 info | sdio | rom | boot [rf] | vif | scan | join <open-ssid>\n");
        rt_kprintf("       aic8800 ap start <open-ssid> <channel 1-11> | ap monitor <seconds 1-30> | ap stop\n");
        return -RT_EINVAL;
    }

    result = rt_aic8800_sdio_snapshot(aic_sdmc_get_rthost(AIC_WIFI_SDMC_ID), &snapshot);
    if (result != RT_EOK)
    {
        rt_kprintf("SDMC%d snapshot unavailable, rc=%d\n", AIC_WIFI_SDMC_ID, result);
        return result;
    }

    rt_kprintf("SDMC%d CCCR revision register=0x%02x IOEx=0x%02x IORx=0x%02x\n",
               AIC_WIFI_SDMC_ID, snapshot.cccr_revision, snapshot.io_enable, snapshot.io_ready);
    for (size_t index = 0; index < snapshot.function_count; ++index)
    {
        const struct rt_aic8800_sdio_function *function = &snapshot.functions[index];

        rt_kprintf("F%u %04x:%04x max_block=%u\n", function->number,
                   function->manufacturer, function->product,
                   (unsigned int)function->max_block_size);
    }
    return RT_EOK;
}
MSH_CMD_EXPORT(aic8800, AIC8800 SDIO and ROM diagnostics);
#endif
