#include "server_ota_http.h"

#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "errno.h"

#include <sys/stat.h>

#include "server_help.h"

#include "esp_wifi.h"


httpd_handle_t server = NULL;  


#define BUFFSIZE 16384
#define HASH_LEN 32 /* SHA-256 digest length */

/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };


esp_err_t hello_main_handler(httpd_req_t *req)
{
    char filepath[50];
    struct stat file_stat;
    printf("uri: %s\n", req->uri);
    int _pos;

    char *base_path = (char*) req->user_ctx;
    std::string filetosend(base_path);

    const char *filename = get_path_from_uri(filepath, base_path,
                                             req->uri - 1, sizeof(filepath));    
    printf("1 uri: %s, filename: %s, filepath: %s\n", req->uri, filename, filepath);

    // Get handle to embedded file upload script 
    extern const unsigned char upload_script_start[] asm("_binary_upload_ota_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_ota_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    // Add file upload form and script which on execution sends a POST request to /upload 
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);
    
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t upload_post_handler(httpd_req_t *req)
{
    int remaining = req->content_len;
    ESP_LOGE(TAG, "File size : %d bytes", remaining);
    char *buf = (char*) malloc(BUFFSIZE);

    if (!buf) {
        ESP_LOGE(TAG, "Could not allocate memory!");
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Could not allocate buffer memory!");
        return ESP_FAIL;
    }

    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);


    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);

    int binary_file_length = 0;

    // deal with all receive packet 
    bool image_header_was_checked = false;
    int data_read;    


    ESP_LOGI(TAG, "Receiving file : ...");

    int received = 0;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */

        if ((data_read = httpd_req_recv(req, ota_write_data, MIN(remaining, BUFFSIZE))) <= 0) {
            if (data_read == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        if (image_header_was_checked == false) {
            esp_app_desc_t new_app_info;
            if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                // check current version with downloading
                memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                esp_app_desc_t running_app_info;
                if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                    ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                }

                const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                esp_app_desc_t invalid_app_info;
                if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                    ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                }

                // check current version with last invalid partition
                if (last_invalid_app != NULL) {
                    if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                        ESP_LOGW(TAG, "New version is the same as invalid version.");
                        ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                        ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                        return 0;
                    }
                }

                image_header_was_checked = true;

                err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                    return false;
                }
                ESP_LOGI(TAG, "esp_ota_begin succeeded");
            } else {
                ESP_LOGE(TAG, "received package is not fit len");
                return false;
            }
        }            
        err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
        if (err != ESP_OK) {
            return false;
        }
        binary_file_length += data_read;
        ESP_LOGD(TAG, "Written image length %d", binary_file_length);

//////////////////////////////

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= data_read;
    }



    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        }
        ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));

    }

    ESP_LOGI(TAG, "File reception complete");

    httpd_resp_sendstr(req, "File uploaded successfully - reboot in 5s!");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    esp_restart();
    return ESP_OK;
}



void register_server_main_uri(httpd_handle_t server)
{
    httpd_uri_t main_rest_handle = {
        .uri       = "/ota/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = hello_main_handler,
        .user_ctx  = (void*) "Hello World!"    // Pass server data as context
    };
    httpd_register_uri_handler(server, &main_rest_handle);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/ota/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = (void*) "Upload"    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);    

}



httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = { };

    config.task_priority      = tskIDLE_PRIORITY+5;
    config.stack_size         = 16384;                  
    config.core_id            = tskNO_AFFINITY;
    config.server_port        = 80;
    config.ctrl_port          = 32768;
    config.max_open_sockets   = 7;      
    config.max_uri_handlers   = 24;                       
    config.max_resp_headers   = 8;                        
    config.backlog_conn       = 5;                        
    config.lru_purge_enable   = false;                    
    config.recv_wait_timeout  = 5;                               
    config.send_wait_timeout  = 5;                            
    config.global_user_ctx = NULL;                        
    config.global_user_ctx_free_fn = NULL;                
    config.global_transport_ctx = NULL;                   
    config.global_transport_ctx_free_fn = NULL;           
    config.open_fn = NULL;                                
    config.close_fn = NULL;                               
    config.uri_match_fn = httpd_uri_match_wildcard;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    httpd_stop(server);
}


void disconnect_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

void connect_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}


static void print_sha256 (const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hash_print);
}


static bool diagnostic(void)
{
    return true;
}



void CheckOTAUpdate(void)
{
    ESP_LOGI(TAG, "Start CheckOTAUpdateCheck ...");
    printf("Start CheckOTAUpdateCheck ...\n");

    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for the partition table
    partition.address   = ESP_PARTITION_TABLE_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
    partition.type      = ESP_PARTITION_TYPE_DATA;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for the partition table: ");

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_err_t res_stat_partition = esp_ota_get_state_partition(running, &ota_state);
    switch (res_stat_partition)
    {
        case ESP_OK:
            printf("CheckOTAUpdate Partition: ESP_OK\n");
            if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
                if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                    // run diagnostic function ...
                    bool diagnostic_is_ok = diagnostic();
                    if (diagnostic_is_ok) {
                        ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                        printf("Diagnostics completed successfully! Continuing execution ...\n");
                        esp_ota_mark_app_valid_cancel_rollback();
                    } else {
                        ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                        printf("Diagnostics failed! Start rollback to the previous version ...\n");
                        esp_ota_mark_app_invalid_rollback_and_reboot();
                    }
                }
            }            
            break;
        case ESP_ERR_INVALID_ARG:
            printf("CheckOTAUpdate Partition: ESP_ERR_INVALID_ARG\n");
            break;
        case ESP_ERR_NOT_SUPPORTED:
            printf("CheckOTAUpdate Partition: ESP_ERR_NOT_SUPPORTED\n");
            break;
        case ESP_ERR_NOT_FOUND:
            printf("CheckOTAUpdate Partition: ESP_ERR_NOT_FOUND\n");
            break;
    }
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // run diagnostic function ...
            bool diagnostic_is_ok = diagnostic();
            if (diagnostic_is_ok) {
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                printf("Diagnostics completed successfully! Continuing execution ...\n");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                printf("Diagnostics failed! Start rollback to the previous version ...\n");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}



