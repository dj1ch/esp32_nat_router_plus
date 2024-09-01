/**
 * @author Jaya Satish
 *
 *@copyright Copyright (c) 2023
 *Licensed under MIT
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_eap_client.h>
#include <esp_netif.h>
#include <esp_err.h>

#include "router_globals.h"
#include "initialization.h"
#include "wifi_init.h"
#include "wifi_event_handler.h"
#include "router_handler.h"
#include "mac_generator.h"

static const char *TAG = "wifi_handler";

esp_netif_t *wifiAP;
esp_netif_t *wifiSTA;

bool has_static_ip = false;

esp_err_t esp_eap_client_set_identity(const unsigned char *identity, int len);
esp_err_t esp_eap_client_set_username(const unsigned char *username, int len);
esp_err_t esp_eap_client_set_password(const unsigned char *password, int len);
esp_err_t esp_eap_client_enable(void);

//-----------------------------------------------------------------------------
// initiating wifi setup
void wifi_init()
{
    esp_log_level_set("wifi", ESP_LOG_NONE);
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifiAP = esp_netif_create_default_wifi_ap();
    wifiSTA = esp_netif_create_default_wifi_sta();

    esp_netif_ip_info_t ipInfo_sta;
    if ((strlen(ssid) > 0) && (strlen(static_ip) > 0) && (strlen(subnet_mask) > 0) && (strlen(gateway_addr) > 0))
    {
        has_static_ip = true;
        my_ip = ipInfo_sta.ip.addr = ipaddr_addr(static_ip);
        ipInfo_sta.gw.addr = ipaddr_addr(gateway_addr);
        ipInfo_sta.netmask.addr = ipaddr_addr(subnet_mask);
        esp_netif_dhcpc_stop(wifiSTA); // Don't run a DHCP client
        esp_netif_set_ip_info(wifiSTA, &ipInfo_sta);
        apply_portmap_tab();
    }

    my_ap_ip = ipaddr_addr(ap_ip);
    esp_netif_ip_info_t ipInfo_ap;
    ipInfo_ap.ip.addr = my_ap_ip;
    ipInfo_ap.gw.addr = my_ap_ip;
    IP4_ADDR(&ipInfo_ap.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(wifiAP); // stop before setting IP for WifiAP
    esp_netif_set_ip_info(wifiAP, &ipInfo_ap);
    esp_netif_dhcps_start(wifiAP);
    wifi_events_register_init();
    custom_mac_generator();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* ESP WIFI CONFIG */
    wifi_config_t wifi_config = {0};
    wifi_config_t ap_config = {
        .ap = {
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 10,
            .beacon_interval = 100,
            .pairwise_cipher = WIFI_CIPHER_TYPE_CCMP}};

    strlcpy((char *)ap_config.sta.ssid, ap_ssid, sizeof(ap_config.sta.ssid));
    if (strlen(ap_passwd) < 8)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    else
    {
        strlcpy((char *)ap_config.sta.password, ap_passwd, sizeof(ap_config.sta.password));
    }

    if (strlen(ssid) > 0)
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        // Set SSID
        strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        // Set password
        if (strlen(ent_username) == 0)
        {
            ESP_LOGI(TAG, "STA regular connection");
            strlcpy((char *)wifi_config.sta.password, passwd, sizeof(wifi_config.sta.password));
        }
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        if (strlen(ent_username) != 0 && strlen(ent_identity) != 0)
        {
            ESP_LOGI(TAG, "STA enterprise connection");
            if (strlen(ent_username) != 0 && strlen(ent_identity) != 0)
            {
                esp_eap_client_set_identity((uint8_t *)ent_identity, strlen(ent_identity)); // provide identity
            }
            else
            {
                esp_eap_client_set_identity((uint8_t *)ent_username, strlen(ent_username));
            }
            esp_eap_client_set_username((uint8_t *)ent_username, strlen(ent_username)); // provide username
            esp_eap_client_set_password((uint8_t *)passwd, strlen(passwd));             // provide password
            esp_eap_client_enable();
        }

        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    }
    // Enable DNS (offer) for dhcp server
    esp_netif_dhcp_option_id_t dhcps_dns_value = ESP_NETIF_DHCP_CLIENT;
    esp_netif_dhcps_option(wifiAP, ESP_NETIF_DHCP_SERVER, ESP_NETIF_DHCP_CLIENT, &dhcps_dns_value, sizeof(dhcps_dns_value));

    ESP_ERROR_CHECK(esp_wifi_start());

    if (strlen(ssid) > 0)
    {
        ESP_LOGI(TAG, "wifi_init_apsta finished.");
        ESP_LOGI(TAG, "connect to ap SSID: %s ", ssid);
    }
    else
    {
        ESP_LOGI(TAG, "wifi_init_ap with default finished.");
    }
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, JOIN_TIMEOUT_MS / portTICK_PERIOD_MS);
}

//-----------------------------------------------------------------------------