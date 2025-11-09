/******************************************************************************
 * @file ota_integration_example.c
 * @brief Example showing how to integrate OTA Manager component
 *
 * @details This example demonstrates:
 *          1. Proper initialization sequence (NVS → WiFi → OTA)
 *          2. Firmware validation after OTA update
 *          3. Optional status callback for custom behavior
 *          4. Error handling
 *          5. Version management using CMakeLists.txt
 *
 *          Copy relevant sections to your main.c
 *
 ******************************************************************************/

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "ota_manager.h"  // OTA Manager component

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
#define TAG "MAIN"

/* WiFi Configuration - REPLACE WITH YOUR CREDENTIALS */
#define WIFI_SSID      "YourWiFiSSID"
#define WIFI_PASSWORD  "YourWiFiPassword"

/* Firmware Version - Define via CMakeLists.txt (recommended)
 * 
 * BEST PRACTICE: Define version in root CMakeLists.txt:
 *   set(PROJECT_VER "1.0.0")
 *   add_compile_definitions(FIRMWARE_VERSION="${PROJECT_VER}")
 * 
 * For PlatformIO, use platformio.ini:
 *   build_flags = -DFIRMWARE_VERSION=\"1.0.0\"
 * 
 * Or use the extract_version.py script to read from CMakeLists.txt
 */
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"  /* Fallback if not set by build system */
#endif

/* OTA Server Port */
#define OTA_SERVER_PORT 8080

/*******************************************************************************/
/*                     STATIC FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/**
 * @brief Initialize NVS flash storage
 * 
 * @return ESP_OK on success
 */
static esp_err_t init_nvs(void);

/**
 * @brief Initialize WiFi in Station mode
 * 
 * @return ESP_OK on success
 */
static esp_err_t init_wifi(void);

/**
 * @brief WiFi event handler
 * 
 * @param[in] arg User data (unused)
 * @param[in] event_base Event base
 * @param[in] event_id Event ID
 * @param[in] event_data Event data
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

/**
 * @brief OTA status callback for custom handling (OPTIONAL)
 * 
 * @details The OTA manager provides comprehensive logging internally.
 *          This callback is ONLY needed for custom behavior such as:
 *          - Updating LEDs to show progress
 *          - Sending notifications to a remote server
 *          - Triggering custom actions on specific events
 * 
 *          You can safely set status_callback to NULL if you don't need
 *          custom behavior - the component logs everything internally.
 * 
 * @param[in] status Current OTA status
 * @param[in] progress Upload/flash progress (0-100)
 */
static void ota_status_callback(ota_status_t status, int progress);

/**
 * @brief Check if all critical systems are operational
 * 
 * @details Called before marking firmware as valid. Add your own checks here.
 * 
 * @return true if all systems OK, false otherwise
 */
static bool verify_system_health(void);

/*******************************************************************************/
/*                             STATIC VARIABLES                                */
/*******************************************************************************/

/** Flag indicating WiFi connection status */
static bool g_wifi_connected = false;

/*******************************************************************************/
/*                     GLOBAL FUNCTION DEFINITIONS                             */
/*******************************************************************************/

void app_main(void)
{
    ESP_LOGI(TAG, "═══════════════════════════════════════════════");
    ESP_LOGI(TAG, "   ESP32 OTA Example");
    ESP_LOGI(TAG, "   Firmware Version: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "═══════════════════════════════════════════════");

    /* ═══════════════════════════════════════════════════════════════════
     * STEP 1: Initialize NVS (Required for WiFi and OTA)
     * ═══════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "[1/4] Initializing NVS...");
    
    esp_err_t ret = init_nvs();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "✓ NVS initialized");

    /* ═══════════════════════════════════════════════════════════════════
     * STEP 2: Initialize WiFi and Connect
     * ═══════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "[2/4] Initializing WiFi...");
    
    ret = init_wifi();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Wait for WiFi connection (with timeout) */
    int retry_count = 0;
    const int max_retries = 20;  // 10 seconds timeout
    
    while (!g_wifi_connected && retry_count < max_retries) 
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry_count++;
    }
    
    if (!g_wifi_connected) 
    {
        ESP_LOGE(TAG, "Failed to connect to WiFi after %d seconds", max_retries / 2);
        return;
    }
    
    ESP_LOGI(TAG, "✓ WiFi connected");

    /* ═══════════════════════════════════════════════════════════════════
     * STEP 3: Initialize OTA Manager
     * ═══════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "[3/4] Initializing OTA Manager...");
    
    /* Configure OTA manager
     * 
     * OPTION A: Minimal configuration (recommended for most use cases)
     * The component provides comprehensive logging internally, so you
     * don't need a callback unless you want custom behavior.
     */
    ota_manager_config_t ota_config = OTA_MANAGER_CONFIG_DEFAULT();
    ota_config.server_port = OTA_SERVER_PORT;
    ota_config.current_version = FIRMWARE_VERSION;
    ota_config.verify_signature = true;  // Enable ECDSA signature verification
    ota_config.status_callback = NULL;   // Component handles logging internally
    
    /* OPTION B: With custom callback (uncomment if needed)
     * Only use this if you need custom behavior like LED updates,
     * remote notifications, etc. The component already logs everything.
     */
    // ota_config.status_callback = ota_status_callback;
    
    ret = ota_manager_init(&ota_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize OTA manager: %s", esp_err_to_name(ret));
        /* Continue execution - OTA is non-critical for basic operation */
    }
    else
    {
        ESP_LOGI(TAG, "✓ OTA Manager initialized");
    }

    /* ═══════════════════════════════════════════════════════════════════
     * STEP 4: Verify System Health and Mark Firmware Valid
     * ═══════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "[4/4] Verifying system health...");
    
    /* CRITICAL: After OTA update, we must validate the new firmware
     * and call ota_manager_mark_valid() to prevent rollback.
     * 
     * Add your own checks here:
     * - WiFi working? ✓
     * - Sensors responding?
     * - Motors controllable?
     * - Critical resources available?
     * 
     * If any check fails, DON'T call mark_valid() and the system will
     * automatically roll back to previous firmware on next boot.
     */
    if (verify_system_health()) 
    {
        ESP_LOGI(TAG, "✓ All systems operational");
        
        /* Mark this firmware as valid (prevents rollback) */
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

    /* ═══════════════════════════════════════════════════════════════════
     * Main Application Loop
     * ═══════════════════════════════════════════════════════════════════ */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "   Application Ready!");
    ESP_LOGI(TAG, "   OTA Web Interface: http://<device-ip>:%d", OTA_SERVER_PORT);
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    /* Your main application code here */
    while (1) 
    {
        /* Example: Blink LED, read sensors, control motors, etc. */
        
        ESP_LOGI(TAG, "Application running... (uptime: %llu seconds)", 
                 esp_timer_get_time() / 1000000);
        
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 second loop
    }
}

/*******************************************************************************/
/*                     STATIC FUNCTION DEFINITIONS                             */
/*******************************************************************************/

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    
    /* If NVS partition is full or version mismatch, erase and retry */
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    return ret;
}

static esp_err_t init_wifi(void)
{
    /* Initialize network interface */
    ESP_ERROR_CHECK(esp_netif_init());
    
    /* Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Create default WiFi station */
    esp_netif_create_default_wifi_sta();

    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register WiFi event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, 
                                                 ESP_EVENT_ANY_ID, 
                                                 &wifi_event_handler, 
                                                 NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, 
                                                 IP_EVENT_STA_GOT_IP, 
                                                 &wifi_event_handler, 
                                                 NULL));

    /* Configure WiFi */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);

    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        g_wifi_connected = false;
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;
    }
}

static void ota_status_callback(ota_status_t status, int progress)
{
    /* OPTIONAL: This callback demonstrates custom handling beyond the 
     * component's internal logging. Use this ONLY if you need to:
     * - Update LEDs or other visual indicators
     * - Send notifications to a remote server
     * - Trigger custom actions on specific events
     * 
     * The component already logs all status changes with comprehensive
     * information, so this is purely for additional custom behavior.
     * 
     * NOTE: This function is NOT called by default (status_callback = NULL).
     *       Uncomment the callback assignment in app_main() to enable it.
     */
    
    switch (status) 
    {
        case OTA_STATUS_RECEIVING:
            /* Example: Update LED color based on progress */
            // set_led_color(LED_BLUE, progress);
            break;
            
        case OTA_STATUS_VERIFYING_SIGNATURE:
            /* Example: Flash LED while verifying */
            // set_led_pattern(LED_PATTERN_FLASH);
            break;
            
        case OTA_STATUS_SUCCESS:
            /* Example: Green LED for success */
            // set_led_color(LED_GREEN, 100);
            break;
            
        case OTA_STATUS_ERROR_SIGNATURE:
        case OTA_STATUS_ERROR_VERSION:
        case OTA_STATUS_ERROR_PARTITION:
        case OTA_STATUS_ERROR_SIZE:
        case OTA_STATUS_ERROR_NETWORK:
            /* Example: Red LED for any error */
            // set_led_color(LED_RED, 100);
            break;
            
        default:
            break;
    }
}

static bool verify_system_health(void)
{
    /* Add your own system health checks here
     * 
     * Examples:
     * - Check if WiFi is connected
     * - Verify motor controllers are responding
     * - Test critical peripherals
     * - Validate configuration data
     * - Check available heap memory
     * 
     * Return false if ANY critical check fails
     */
    
    /* Example check: WiFi connected? */
    if (!g_wifi_connected) 
    {
        ESP_LOGE(TAG, "Health check failed: WiFi not connected");
        return false;
    }
    
    /* Example check: Enough free heap? */
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < 50000) 
    {
        ESP_LOGE(TAG, "Health check failed: Low memory (%zu bytes)", free_heap);
        return false;
    }
    
    /* Example check: Verify WiFi mode is correct */
    wifi_mode_t wifi_mode;
    if (esp_wifi_get_mode(&wifi_mode) != ESP_OK)
    {
        ESP_LOGE(TAG, "Health check failed: WiFi not initialized properly");
        return false;
    }
    
    /* Add more checks as needed for your specific application... */
    
    ESP_LOGI(TAG, "System health checks passed (Free heap: %zu bytes)", free_heap);
    
    return true;  // All checks passed
}