#ifndef AIC8800_WLAN_AP_H
#define AIC8800_WLAN_AP_H

#include <stddef.h>
#include <stdint.h>
#include <wlan_mgnt.h>

rt_err_t aic8800_wlan_start_ap_with_ie(struct rt_wlan_info *info, const char *password,
                                      const uint8_t *ie, size_t len);

#endif
