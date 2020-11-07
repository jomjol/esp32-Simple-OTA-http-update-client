#include "jomjol_connect_wlan.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include <fstream>
#include <string>
#include <vector>

#include "Helper.h"


#include <string.h>

static const char *TAG = "jomjol_connect_wlan";

std::string ssid;
std::string passphrase;
std::string hostname;
std::string ipaddress;

std::string std_hostname = "watermeter";

#define BLINK_GPIO GPIO_NUM_33

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static int esp_max_retry = -1;             // -1: try connect forever (! ENDLESS LOOP POSSIBLE)




std::vector<std::string> ZerlegeZeile(std::string input, std::string _delimiter = "")
{
	std::vector<std::string> Output;
	std::string delimiter = " =,";
    if (_delimiter.length() > 0){
        delimiter = _delimiter;
    }

	input = trim(input, delimiter);
	size_t pos = findDelimiterPos(input, delimiter);
	std::string token;
	while (pos != std::string::npos) {
		token = input.substr(0, pos);
		token = trim(token, delimiter);
		Output.push_back(token);
		input.erase(0, pos + 1);
		input = trim(input, delimiter);
		pos = findDelimiterPos(input, delimiter);
	}
	Output.push_back(input);

	return Output;
}





void blinkstatus(int dauer, int _anzahl)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < _anzahl; ++i)
    {
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(dauer / portTICK_PERIOD_MS);
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(dauer / portTICK_PERIOD_MS);          
    }
}


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        blinkstatus(200, 5);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if ((esp_max_retry < 0) || (s_retry_num < esp_max_retry)) {
            blinkstatus(200, 5);
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        blinkstatus(1000, 3);
//        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
//        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool initialise_wifi(std::string _ssid, std::string _passphrase, std::string _hostname, int _try_reconnect)
{
    bool result = false;
    esp_max_retry = _try_reconnect;
    ssid = _ssid;
    passphrase = _passphrase;
    hostname = _hostname;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    tcpip_adapter_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));


    wifi_config_t wifi_config = { };
    strcpy((char*)wifi_config.sta.ssid, (const char*)ssid.c_str());
    strcpy((char*)wifi_config.sta.password, (const char*)passphrase.c_str());
//    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
//    wifi_config.sta.pmf_cfg.capable = true;
//    wifi_config.sta.pmf_cfg.required = false;

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
        * However these modes are deprecated and not advisable to be used. Incase your Access point
        * doesn't support WPA2, these mode can be enabled by commenting below line */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ssid.c_str(), passphrase.c_str());
        result = true;

        esp_err_t ret = tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA , hostname.c_str());
        if(ret != ESP_OK ){
        ESP_LOGE(TAG,"failed to set hostname:%d",ret);  
        }
        tcpip_adapter_ip_info_t ip_info;
        ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
        ipaddress = std::string(ip4addr_ntoa(&ip_info.ip));
        printf("IPv4 :  %s\n", ip4addr_ntoa(&ip_info.ip));
        printf("HostName :  %s\n", hostname.c_str());


    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ssid.c_str(), passphrase.c_str());
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
    return result;
}







void LoadWlanFromFile(std::string fn, std::string &_ssid, std::string &_passphrase, std::string &_hostname)
{
    std::string line = "";
    std::vector<std::string> zerlegt;
    _hostname = std_hostname;

    FILE* pFile;
    fn = FormatFileName(fn);
    pFile = fopen(fn.c_str(), "r");

    if (pFile == NULL)
        return;

    char zw[1024];
    fgets(zw, 1024, pFile);
    line = std::string(zw);

    while ((line.size() > 0) || !(feof(pFile)))
    {
//        printf("%s", line.c_str());
        zerlegt = ZerlegeZeile(line, "=");
        zerlegt[0] = trim(zerlegt[0], " ");

        if ((zerlegt.size() > 1) && (toUpper(zerlegt[0]) == "HOSTNAME")){
            _hostname = trim(zerlegt[1]);
            if ((_hostname[0] == '"') && (_hostname[_hostname.length()-1] == '"')){
                _hostname = _hostname.substr(1, _hostname.length()-2);
            }
        }

        if ((zerlegt.size() > 1) && (toUpper(zerlegt[0]) == "SSID")){
            _ssid = trim(zerlegt[1]);
            if ((_ssid[0] == '"') && (_ssid[_ssid.length()-1] == '"')){
                _ssid = _ssid.substr(1, _ssid.length()-2);
            }
        }

        if ((zerlegt.size() > 1) && (toUpper(zerlegt[0]) == "PASSWORD")){
            _passphrase = zerlegt[1];
            if ((_passphrase[0] == '"') && (_passphrase[_passphrase.length()-1] == '"')){
                _passphrase = _passphrase.substr(1, _passphrase.length()-2);
            }
        }

        if (fgets(zw, 1024, pFile) == NULL)
        {
            line = "";
        }
        else
        {
            line = std::string(zw);
        }
    }

    fclose(pFile);

    // Check if Hostname was empty in .ini if yes set to std_hostname
    if(_hostname.length() <= 0){
        _hostname = std_hostname;
    }
}





std::string getHostname(){
    return hostname;
}

std::string getIPAddress(){
    return ipaddress;
}

std::string getSSID(){
    return ssid;
}
