/******************************************************************************
 * @file main.cpp
 * @brief Main application entry point for wave_rover_motor_driver
 * 
 ******************************************************************************/

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_now_comm.h"
#include "esp_now_comm_callbacks.h"
#include "wifi_manager.h"

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
#define TAG "MAIN"

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL VARIABLES DECLARATIONS                           */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL VARIABLES DEFINITIONS                            */
/*******************************************************************************/

/*******************************************************************************/
/*                     STATIC FUNCTION DECLARATIONS                            */
/*******************************************************************************/
/**
 * @brief Initialize all system components
 *
 * @details This function encapsulates all component initialization logic,
 *          keeping app_main clean and focused. 
 *
 * @return
 *      - ESP_OK on successful initialization of all components
 *      - Error code if any component fails to initialize
 */
static esp_err_t initialize_components(void);

/*******************************************************************************/
/*                             STATIC VARIABLES                                */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL FUNCTION DEFINITIONS                             */
/*******************************************************************************/

void app_main(void)
{
    /* Initialize all system components */
    esp_err_t ret = initialize_components();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Component initialization failed");
        return;
    }

    /* Main application loop */
    while (true) 
    {
        /* Log a periodic message to indicate device is operational */
        ESP_LOGI(TAG, "Main function, checking in...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/*******************************************************************************/
/*                     STATIC FUNCTION DEFINITIONS                             */
/*******************************************************************************/

static esp_err_t initialize_components(void)
{
    /******************************* NVS Flash *******************************/
    ESP_LOGI(TAG, "Initializing NVS Flash...");
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        /* NVS partition was truncated/corrupted - erase it and reinitialize */
        ESP_LOGW(TAG, "NVS partition corrupted/out of date, erasing...");
        nvs_err = nvs_flash_erase();
        if (nvs_err != ESP_OK) 
        {
            ESP_LOGI(TAG, "NVS erase failed: %s", esp_err_to_name(nvs_err));
        }
        else
        {
            nvs_err = nvs_flash_init();
        }
    }
    if (nvs_err != ESP_OK)
    {
        ESP_LOGI(TAG, "NVS initialization failed: %s", esp_err_to_name(nvs_err));
    }
    else
    {
        ESP_LOGI(TAG, "NVS Flash Initialized.");
    }

    /******************************* TCP/IP & Event Loop *******************************/
    ESP_LOGI(TAG, "Initializing network stack...");
    /* Initialize the TCP/IP stack (ESP-IDF uses lwIP for this) */
    /* ESP-NETIF (Network Interface) library provides an abstraction layer for the application on top of the TCP/IP stack. 
     * ESP-IDF currently implements ESP-NETIF for the lwIP TCP/IP stack only.
     * See documentation for details: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_netif.html 
     **/
    esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK)
    {
        ESP_LOGI(TAG, "Network interface initialization failed: %s", esp_err_to_name(netif_err));
    }
    
    /* Initialize and start the default system event loop. */
    /* This loop is used by various ESP-IDF components (e.g., WiFi, TCP/IP)
     * to post events and allows application code to register handlers
     * that react to these events asynchronously. It facilitates communication
     * between different parts of the system.
     **/
    esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_NO_MEM)
    {
        ESP_LOGI(TAG, "Event loop creation failed: %s", esp_err_to_name(event_loop_err));
    }
    /* Create a default WiFi station interface */
    /* The API creates esp_netif object with default WiFi station config,
     * attaches the netif to wifi and registers wifi handlers to the default event loop.
     * The return value is a pointer to the created esp_netif instance (not used here).
     * (default event loop needs to be created prior to calling this API)
     **/
    (void)esp_netif_create_default_wifi_sta(); 

    ESP_LOGI(TAG, "Network stack initialized.");

    /******************************* WiFi Initialization *******************************/
    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_err_t wifi_err = wifi_manager_init(NULL);
    if (wifi_err != ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi initialization failed: %s", esp_err_to_name(wifi_err)); // Log the error but continue execution
    }
    ESP_LOGI(TAG, "WiFi Initialized.");

    /************************ ESP-NOW Communication ***********************/
    /* Create configuration structure with callback function pointers
     * These callbacks will be invoked by the ESP-NOW component when
     * data is received or transmission completes
     */
    esp_now_comm_config_t config = 
    {
        .on_recv = on_data_recv_callback,    /* Called when data is received */
        .on_send = on_data_send_callback,    /* Called after send attempt completes */
        .mac_addr = {0}                      /* We don't know the MAC address yet (WiFi not initialized) */
    };

    /* Initialize the ESP-NOW communication component
     * This sets up WiFi in STA mode and initializes ESP-NOW protocol
     * The component will log the device MAC address upon successful initialization
     * After this call, we can add peers and send/receive data
     */
    esp_err_t ret = esp_now_comm_init(&config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW component: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Get current WiFi channel information (which is crucial for ESP-NOW communication because it operates on the same channel) */
    uint8_t primary_ch = 0;
    wifi_second_chan_t secondary_ch = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary_ch, &secondary_ch);
    ESP_LOGI(TAG, "Device operating on WiFi channel: %d", primary_ch);

    /* Add the MAC address of the wave_rover_driver device as ESP-NOW peer */
    uint8_t wave_rover_driver_mac[] = {0xD8, 0x13, 0x2A, 0x2F, 0x3C, 0xE4};
    ESP_LOGI(TAG, "Adding wave_rover_driver peer...");
    ret = esp_now_comm_add_peer(wave_rover_driver_mac);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Controller peer added successfully");
    
    ESP_LOGI(TAG, "All components initialized successfully");
    return ESP_OK;
}

