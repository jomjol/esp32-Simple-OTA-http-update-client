#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_vfs_fat.h"

#include "jomjol_connect_wlan.h"
#include "server_ota_http.h"

#define WLAN_SSID       "WLAN-SSID"
#define WLAN_PASSPHRASE "PASSPHRASE"
#define WLAN_HOSTNAME   "SimpleOTA"


bool wlan_connected = false;


void Init_NVS()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
}

extern "C" void app_main() 
{
    printf("Init NVS und start WLAN\n");  
    Init_NVS(); 
    CheckOTAUpdate();
    wlan_connected = initialise_wifi(WLAN_SSID, WLAN_PASSPHRASE, WLAN_HOSTNAME, 3);   

    if (wlan_connected)
    {
        server = start_webserver();   
        register_server_main_uri(server);
    }
    else
    {
        printf("No connection --> no server !");
    }
}