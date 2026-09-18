/*
 * wifi_provision.c - phone friendly Wi-Fi configuration for the garage.
 *
 * Runs the Wi-Fi stack in AP+STA mode:
 *   - SoftAP "SCALE64_xxxx" (http://192.168.4.1) is always available and
 *     serves a small configuration page (scan, choose SSID, enter password),
 *   - the station connects to the stored router credentials,
 *   - a tiny DNS responder points every lookup at the SoftAP so phones pop
 *     up the captive portal automatically.
 *
 * Credentials live in NVS namespace "prov".
 */
#include "wifi_provision.h"

#include "app_config.h"
#include "app_state.h"
#include "motion_core.h"
#include "web_page.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

/* The configuration page is generated from src/index.html at build time. */

static const char *TAG = "WIFI";

#define NVS_NAMESPACE   "prov"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"

static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static bool         s_have_creds;
static bool         s_sta_connected;
static int          s_retry;
static char         s_ap_ssid[33];

/* ----------------------------------------------------------------- NVS ---- */
static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t len = ssid_len;
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &len) == ESP_OK) && ssid[0] != '\0';
    if (ok) {
        len = pass_len;
        if (nvs_get_str(h, NVS_KEY_PASS, pass, &len) != ESP_OK) {
            pass[0] = '\0';
        }
    }
    nvs_close(h);
    return ok;
}

static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = nvs_set_str(h, NVS_KEY_SSID, ssid)) == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* --------------------------------------------------------------- helpers -- */
static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    size_t n = strlen(src);
    if (n > dst_len - 1) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void url_decode(const char *in, char *out, size_t out_len)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < out_len; i++) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) &&
            isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = { in[i + 1], in[i + 2], '\0' };
            out[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[j++] = ' ';
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = '\0';
}

static bool form_value(const char *body, const char *key, char *out, size_t out_len)
{
    const size_t klen = strlen(key);
    const char  *p = body;

    while (p != NULL && *p != '\0') {
        const char *amp = strchr(p, '&');
        const size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', seg);

        if (eq != NULL && (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            char tmp[128];
            size_t vlen = seg - (size_t)(eq - p) - 1;
            if (vlen >= sizeof(tmp)) {
                vlen = sizeof(tmp) - 1;
            }
            memcpy(tmp, eq + 1, vlen);
            tmp[vlen] = '\0';
            url_decode(tmp, out, out_len);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static void json_escape(const char *in, char *out, size_t out_len)
{
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 7 < out_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c < 0x20) {
            j += (size_t)snprintf(out + j, out_len - j, "\\u%04x", c);
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

/* ----------------------------------------------------------- Wi-Fi events -- */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "SoftAP \"%s\" ready, open http://192.168.4.1", s_ap_ssid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_have_creds) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        if (s_have_creds && s_retry < PROV_STA_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "station disconnected, retry %d/%d", s_retry, PROV_STA_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "station not connected, setup AP still available");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        s_sta_connected = true;
        s_retry = 0;
        ESP_LOGI(TAG, "station got IP " IPSTR, IP2STR(&evt->ip_info.ip));
    }
}

/* ----------------------------------------------------------- HTTP handlers - */
static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, WEB_PAGE_HTML, WEB_PAGE_HTML_LEN);
}

static esp_err_t portal_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t status_get(httpd_req_t *req)
{
    app_snapshot_t s;
    app_state_snapshot(&s);

    char ssid[33] = "";
    char ip[16]   = "";
    int  rssi     = 0;
    if (s_sta_connected) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            memcpy(ssid, ap.ssid, sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
            rssi = ap.rssi;
        }
        esp_netif_ip_info_t ipi;
        if (s_sta_netif != NULL && esp_netif_get_ip_info(s_sta_netif, &ipi) == ESP_OK) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ipi.ip));
        }
    }

    char ssid_esc[96];
    json_escape(ssid, ssid_esc, sizeof(ssid_esc));

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
             "\"steps\":%lld,\"dir\":\"%s\"}",
             s_sta_connected ? "true" : "false", ssid_esc, ip, rssi,
             (long long)s.step_count,
             (s.dir == MOTOR_DIR_FORWARD) ? "FWD" : "REV");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t scan_get(httpd_req_t *req)
{
    const wifi_scan_config_t cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"aps\":[]}");
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 24) {
        n = 24;
    }
    wifi_ap_record_t *recs = (n > 0) ? calloc(n, sizeof(wifi_ap_record_t)) : NULL;
    if (recs != NULL) {
        esp_wifi_scan_get_ap_records(&n, recs);
    } else {
        n = 0;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "{\"aps\":[");
    for (uint16_t i = 0; i < n; i++) {
        char ssid_esc[96];
        json_escape((const char *)recs[i].ssid, ssid_esc, sizeof(ssid_esc));
        char item[160];
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                 (i == 0) ? "" : ",", ssid_esc, recs[i].rssi,
                 (recs[i].authmode != WIFI_AUTH_OPEN) ? 1 : 0);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_send_chunk(req, NULL, 0);   /* end chunked response */
    free(recs);

    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[256];
    int  total = req->content_len;
    if (total > (int)sizeof(body) - 1) {
        total = (int)sizeof(body) - 1;
    }
    if (total < 0) {
        total = 0;
    }

    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    char ssid[33] = "";
    char pass[65] = "";
    form_value(body, "ssid", ssid, sizeof(ssid));
    form_value(body, "pass", pass, sizeof(pass));

    httpd_resp_set_type(req, "application/json");
    if (ssid[0] == '\0') {
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing SSID\"}");
    }

    esp_err_t err = save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving credentials failed: %s", esp_err_to_name(err));
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"NVS write failed\"}");
    }

    /* (re)configure and connect the station */
    wifi_config_t wc = { 0 };
    copy_str((char *)wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    copy_str((char *)wc.sta.password, sizeof(wc.sta.password), pass);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    s_have_creds = true;
    s_retry = 0;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();

    ESP_LOGI(TAG, "saved Wi-Fi \"%s\", connecting", ssid);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = PROV_HTTP_PORT;
    cfg.stack_size       = PROV_HTTP_STACK;
    cfg.core_id          = PROV_TASK_CORE;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",                   .method = HTTP_GET,  .handler = root_get },
        { .uri = "/status",             .method = HTTP_GET,  .handler = status_get },
        { .uri = "/scan",               .method = HTTP_GET,  .handler = scan_get },
        { .uri = "/save",               .method = HTTP_POST, .handler = save_post },
        { .uri = "/generate_204",       .method = HTTP_GET,  .handler = portal_redirect },
        { .uri = "/hotspot-detect.html",.method = HTTP_GET,  .handler = portal_redirect },
        { .uri = "/ncsi.txt",           .method = HTTP_GET,  .handler = portal_redirect },
        { .uri = "/connecttest.txt",    .method = HTTP_GET,  .handler = portal_redirect },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    return ESP_OK;
}

/* ------------------------------------------------- captive portal DNS ------ */
static void dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(PROV_DNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "captive portal DNS on udp/%d", PROV_DNS_PORT);

    uint8_t rx[512];
    uint8_t tx[512];

    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, rx, sizeof(rx), 0,
                           (struct sockaddr *)&from, &from_len);
        if (len < 12 || len > (int)sizeof(tx) - 16) {
            continue;
        }

        uint32_t ap_ip = 0;
        esp_netif_ip_info_t ipi;
        if (s_ap_netif != NULL && esp_netif_get_ip_info(s_ap_netif, &ipi) == ESP_OK) {
            ap_ip = ipi.ip.addr;
        }

        memcpy(tx, rx, (size_t)len);
        tx[2] = 0x81;                       /* QR=1, RD=1 */
        tx[3] = 0x80;                       /* RA=1, RCODE=0 */
        tx[6] = 0x00; tx[7] = 0x01;         /* ANCOUNT = 1 */
        tx[8] = tx[9] = tx[10] = tx[11] = 0;

        int p = len;
        tx[p++] = 0xC0; tx[p++] = 0x0C;                 /* name -> question */
        tx[p++] = 0x00; tx[p++] = 0x01;                 /* TYPE A */
        tx[p++] = 0x00; tx[p++] = 0x01;                 /* CLASS IN */
        tx[p++] = 0x00; tx[p++] = 0x00;
        tx[p++] = 0x00; tx[p++] = 0x3C;                 /* TTL 60 s */
        tx[p++] = 0x00; tx[p++] = 0x04;                 /* RDLENGTH */
        tx[p++] = (ap_ip >> 0) & 0xFF;
        tx[p++] = (ap_ip >> 8) & 0xFF;
        tx[p++] = (ap_ip >> 16) & 0xFF;
        tx[p++] = (ap_ip >> 24) & 0xFF;

        sendto(sock, tx, (size_t)p, 0, (struct sockaddr *)&from, from_len);
    }
}

/* --------------------------------------------------------------- public --- */
esp_err_t wifi_provision_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_ap_netif  = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s_%02X%02X",
             PROV_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    const size_t ap_pass_len = strlen(PROV_AP_PASSWORD);

    wifi_config_t ap_cfg = { 0 };
    copy_str((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), s_ap_ssid);
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.channel        = PROV_AP_CHANNEL;
    ap_cfg.ap.max_connection = PROV_AP_MAX_STA;
    ap_cfg.ap.authmode       = (ap_pass_len > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (ap_pass_len > 0) {
        copy_str((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password),
                 PROV_AP_PASSWORD);
    }

    char ssid[33] = "";
    char pass[65] = "";
    s_have_creds = load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    if (s_have_creds) {
        wifi_config_t sta_cfg = { 0 };
        copy_str((char *)sta_cfg.sta.ssid, sizeof(sta_cfg.sta.ssid), ssid);
        copy_str((char *)sta_cfg.sta.password, sizeof(sta_cfg.sta.password), pass);
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    err = start_http_server();
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreatePinnedToCore(dns_task, "dns", PROV_DNS_STACK, NULL,
                                PROV_DNS_PRIORITY, NULL, PROV_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "dns task creation failed");
    }

    ESP_LOGI(TAG, "setup AP \"%s\" -> http://192.168.4.1", s_ap_ssid);
    if (s_have_creds) {
        ESP_LOGI(TAG, "using stored Wi-Fi \"%s\", connecting", ssid);
    } else {
        ESP_LOGI(TAG, "no stored Wi-Fi yet, join the AP and open the page");
    }
    return ESP_OK;
}
