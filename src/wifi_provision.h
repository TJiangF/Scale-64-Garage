/*
 * wifi_provision.h - SoftAP + web page configuration for the local Wi-Fi.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialises NVS, the network stack, Wi-Fi in AP+STA mode, the HTTP
 * configuration server and the captive portal DNS responder.
 * Returns ESP_OK once the setup AP is up (station may still be connecting).
 */
esp_err_t wifi_provision_start(void);

#ifdef __cplusplus
}
#endif
