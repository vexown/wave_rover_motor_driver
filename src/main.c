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
#include "esp_ota_ops.h"
#include "esp_now_comm.h"
#include "esp_now_comm_callbacks.h"
#include "wifi_manager.h"
#include "motor_control.h"
#include "ota_manager.h"

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
#define TAG "MAIN"

/* Firmware version is now defined via CMake (see root CMakeLists.txt)
 * No need to manually update this in multiple places!
 * 
 * To change version:
 * 1. Update PROJECT_VER in root CMakeLists.txt
 * 2. Rebuild project
 * 
 * The version is automatically:
 * - Embedded in app descriptor (esp_app_desc_t)
 * - Available as FIRMWARE_VERSION macro
 * - Used by OTA manager for version checking
 */
#ifndef FIRMWARE_VERSION
#error "FIRMWARE_VERSION not defined! Check CMakeLists.txt"
#endif

/* OTA web server port */
#define OTA_SERVER_PORT 8080

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

/* MAC address of the wave_rover_driver controller */
const uint8_t wave_rover_driver_mac[6] = {0xD8, 0x13, 0x2A, 0x2F, 0x3C, 0xE4};

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

/**
 * @brief Verify system health before marking OTA firmware as valid
 *
 * @details Called after initialization to ensure all critical systems
 *          are operational before committing to new OTA firmware.
 *          Add your own system checks here.
 *
 * @return
 *      - true if all systems operational
 *      - false if any critical check fails
 */
static bool verify_system_health(void);

/**
 * @brief OTA status callback for progress monitoring
 *
 * @param[in] status Current OTA status
 * @param[in] progress Upload/flash progress (0-100)
 */
static void ota_status_callback(ota_status_t status, int progress);

/*******************************************************************************/
/*                             STATIC VARIABLES                                */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL FUNCTION DEFINITIONS                             */
/*******************************************************************************/

void app_main(void)
{
    ESP_LOGI(TAG, "═══════════════════════════════════════════════");
    ESP_LOGI(TAG, "   Wave Rover Motor Driver");
    ESP_LOGI(TAG, "   Firmware Version: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "═══════════════════════════════════════════════");

    /* Initialize all system components */
    esp_err_t ret = initialize_components();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Component initialization failed");
        return;
    }

    /* Verify system health and mark OTA firmware as valid if all checks pass */
    if (verify_system_health())
    {
        ESP_LOGI(TAG, "✓ All systems operational");
        
        /* Mark this firmware as valid (prevents rollback after OTA update) */
        ret = ota_manager_mark_valid();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "✓ Firmware validated and committed");
        }
        else if (ret == ESP_ERR_OTA_ROLLBACK_INVALID_STATE)
        {
            ESP_LOGD(TAG, "Firmware already validated (not from OTA)");
        }
        else
        {
            ESP_LOGW(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGE(TAG, "✗ System health check failed!");
        ESP_LOGE(TAG, "⚠ NOT marking firmware as valid - will rollback on reboot");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "   Device Ready");
    ESP_LOGI(TAG, "   OTA Interface: http://<device-ip>:%d", OTA_SERVER_PORT);
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

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
    ESP_LOGI(TAG, "Adding wave_rover_driver peer...");
    ret = esp_now_comm_add_peer(wave_rover_driver_mac);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Controller peer added successfully");

    /******************************* Motor Control *******************************/
    ESP_LOGI(TAG, "Initializing motor control...");
    ret = motor_control_init();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize motor control: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Motor control initialized successfully");

    /******************************* OTA Manager *******************************/
    ESP_LOGI(TAG, "Initializing OTA Manager...");
    
    /* Configure OTA manager with default settings */
    ota_manager_config_t ota_config = OTA_MANAGER_CONFIG_DEFAULT();
    ota_config.server_port = OTA_SERVER_PORT;
    ota_config.current_version = FIRMWARE_VERSION;
    ota_config.verify_signature = true;  // Enable ECDSA signature verification
    ota_config.status_callback = ota_status_callback;
    
    ret = ota_manager_init(&ota_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize OTA manager: %s", esp_err_to_name(ret));
        /* Continue execution - OTA is non-critical for basic operation */
    }
    else
    {
        ESP_LOGI(TAG, "OTA Manager initialized successfully");
    }
    
    ESP_LOGI(TAG, "All components initialized successfully");
    return ESP_OK;
}

static bool verify_system_health(void)
{
    /* Add your own system health checks here
     *
     * Examples:
     * - Check if WiFi is connected
     * - Verify motor controllers are responding
     * - Test ESP-NOW communication
     * - Validate critical peripherals
     * - Check available heap memory
     *
     * Return false if ANY critical check fails
     */
    
    /* Example check: Verify sufficient free heap memory */
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < 50000) 
    {
        ESP_LOGE(TAG, "Health check failed: Low memory (%u bytes free)", free_heap);
        return false;
    }
    
    /* Example check: Verify WiFi is initialized */
    wifi_mode_t wifi_mode;
    if (esp_wifi_get_mode(&wifi_mode) != ESP_OK)
    {
        ESP_LOGE(TAG, "Health check failed: WiFi not initialized");
        return false;
    }
    
    /* Add more checks as needed for your application */
    
    ESP_LOGI(TAG, "System health checks passed (Free heap: %u bytes)", free_heap);
    return true;
}

static void ota_status_callback(ota_status_t status, int progress)
{
    /* This callback is invoked during OTA operations to report progress */
    switch (status)
    {
        case OTA_STATUS_IDLE:
            ESP_LOGD(TAG, "OTA: Idle");
            break;
            
        case OTA_STATUS_RECEIVING:
            ESP_LOGI(TAG, "OTA: Receiving firmware (%d%%)", progress);
            /* Example: Update LED to show progress */
            break;
            
        case OTA_STATUS_VERIFYING_SIGNATURE:
            ESP_LOGI(TAG, "OTA: Verifying signature...");
            break;
            
        case OTA_STATUS_VERIFYING_VERSION:
            ESP_LOGI(TAG, "OTA: Verifying version...");
            break;
            
        case OTA_STATUS_FLASHING:
            ESP_LOGI(TAG, "OTA: Flashing firmware (%d%%)", progress);
            break;
            
        case OTA_STATUS_SUCCESS:
            ESP_LOGI(TAG, "OTA: Update successful! Rebooting...");
            break;
            
        case OTA_STATUS_ERROR_SIGNATURE:
            ESP_LOGE(TAG, "OTA: Signature verification failed!");
            break;
            
        case OTA_STATUS_ERROR_VERSION:
            ESP_LOGE(TAG, "OTA: Version check failed (downgrade attempt)");
            break;
            
        case OTA_STATUS_ERROR_PARTITION:
            ESP_LOGE(TAG, "OTA: Partition operation failed");
            break;
            
        case OTA_STATUS_ERROR_SIZE:
            ESP_LOGE(TAG, "OTA: Firmware too large for partition");
            break;
            
        case OTA_STATUS_ERROR_NETWORK:
            ESP_LOGE(TAG, "OTA: Network/upload error");
            break;
            
        default:
            ESP_LOGE(TAG, "OTA: Unknown error");
            break;
    }
}

