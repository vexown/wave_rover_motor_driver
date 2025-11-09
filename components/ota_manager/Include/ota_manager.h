/******************************************************************************
 * @file ota_manager.h
 * @brief Header file for OTA (Over-The-Air) firmware update manager component
 *
 * @details This component provides a secure, user-friendly web-based OTA update
 *          system for ESP32 devices. Features include:
 *          - Simple HTTP web server with drag-and-drop firmware upload interface
 *          - ECDSA P-256 signature verification for firmware authentication
 *          - Automatic rollback protection (boots previous firmware if new one fails)
 *          - Version checking to prevent downgrade attacks
 *          - Real-time upload progress via WebSocket
 *          - Safe partition management with atomic updates
 *
 *          The component is designed to be dropped into any ESP-IDF project with
 *          minimal integration effort. Prerequisites:
 *          - NVS must be initialized (nvs_flash_init)
 *          - WiFi must be initialized and connected
 *          - Partition table must include two OTA partitions (ota_0, ota_1)
 *
 ******************************************************************************/

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/

/* Default HTTP server port for OTA web interface */
#define OTA_MANAGER_DEFAULT_PORT 8080

/* Maximum length of firmware version string (e.g., "1.2.3") */
#define OTA_MANAGER_VERSION_MAX_LEN 32

/* Maximum number of simultaneous HTTP connections */
#define OTA_MANAGER_MAX_CONNECTIONS 4

/* Size of buffer for streaming firmware data during upload (4KB chunks) */
#define OTA_MANAGER_BUFFER_SIZE 4096

/* ECDSA P-256 signature size in bytes (64 bytes for secp256r1) */
#define OTA_SIGNATURE_SIZE 64

/* ECDSA P-256 public key size in uncompressed form (65 bytes: 0x04 + 32-byte X + 32-byte Y) */
#define OTA_PUBLIC_KEY_SIZE 65

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/**
 * @brief OTA update status codes
 * 
 * @details Represents the current state of an OTA operation. Used for progress
 *          reporting and error handling.
 */
typedef enum 
{
    OTA_STATUS_IDLE = 0,            /**< No OTA operation in progress */
    OTA_STATUS_RECEIVING,           /**< Receiving firmware data from client */
    OTA_STATUS_VERIFYING_SIGNATURE, /**< Validating ECDSA signature */
    OTA_STATUS_VERIFYING_VERSION,   /**< Checking version to prevent downgrades */
    OTA_STATUS_FLASHING,            /**< Writing firmware to OTA partition */
    OTA_STATUS_SUCCESS,             /**< OTA update completed successfully */
    OTA_STATUS_ERROR_SIGNATURE,     /**< Signature verification failed */
    OTA_STATUS_ERROR_VERSION,       /**< Version check failed (downgrade attempt) */
    OTA_STATUS_ERROR_PARTITION,     /**< Partition operation failed */
    OTA_STATUS_ERROR_SIZE,          /**< Firmware size exceeds partition capacity */
    OTA_STATUS_ERROR_NETWORK,       /**< Network/upload error */
    OTA_STATUS_ERROR_GENERIC        /**< Unspecified error */
} ota_status_t;

/**
 * @brief Callback function type for OTA status updates
 * 
 * @details Invoked during OTA operations to report progress and errors.
 *          Can be used to update LEDs, display status, log events, etc.
 * 
 * @param[in] status Current OTA status
 * @param[in] progress_percent Upload progress (0-100), valid only during RECEIVING/FLASHING
 */
typedef void (*ota_status_callback_t)(ota_status_t status, int progress_percent);

/**
 * @brief Configuration structure for OTA manager
 * 
 * @details All settings for OTA behavior, security, and callbacks.
 */
typedef struct 
{
    /* --- Network Configuration --- */
    
    /** Port number for HTTP web server (default: 8080) */
    uint16_t server_port;
    
    /** Maximum time in seconds that OTA web server stays active
     *  Set to 0 for always-on, or specify timeout for auto-disable
     *  Example: 600 = OTA disabled after 10 minutes of inactivity */
    uint32_t server_timeout_sec;
    
    /* --- Security Configuration --- */
    
    /** Current firmware version string (e.g., "1.2.3")
     *  Used for anti-rollback: rejects firmware with version <= current_version
     *  Set to NULL to disable version checking */
    const char *current_version;
    
    /** Enable ECDSA signature verification
     *  If true, firmware must be signed with matching private key
     *  If false, any firmware binary will be accepted (insecure!) */
    bool verify_signature;
    
    /* --- Callbacks --- */
    
    /** Optional callback for status updates during OTA process
     *  Set to NULL if not needed */
    ota_status_callback_t status_callback;
    
} ota_manager_config_t;

/**
 * @brief Default configuration initializer macro
 * 
 * @details Use this to get a sensible default configuration, then customize
 *          fields as needed.
 * 
 * Example usage:
 * @code
 *   ota_manager_config_t config = OTA_MANAGER_CONFIG_DEFAULT();
 *   config.current_version = "1.0.0";
 *   config.verify_signature = true;
 *   ota_manager_init(&config);
 * @endcode
 */
#define OTA_MANAGER_CONFIG_DEFAULT() { \
    .server_port = OTA_MANAGER_DEFAULT_PORT, \
    .server_timeout_sec = 0, \
    .current_version = NULL, \
    .verify_signature = true, \
    .status_callback = NULL \
}

/*******************************************************************************/
/*                     GLOBAL VARIABLES DECLARATIONS                           */
/*******************************************************************************/

/*******************************************************************************/
/*                     GLOBAL FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/**
 * @brief Initialize OTA manager and start web server
 *
 * @details This function sets up the OTA update infrastructure and launches
 *          an HTTP web server that provides a user-friendly interface for
 *          firmware uploads. 
 * 
 *          IMPORTANT PREREQUISITES:
 *          Before calling this function, the following MUST be initialized:
 * 
 *          1. NVS Flash:
 *             esp_err_t ret = nvs_flash_init();
 *             if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
 *                 nvs_flash_erase();
 *                 nvs_flash_init();
 *             }
 * 
 *          2. Network Interface & WiFi:
 *             esp_netif_init();
 *             esp_event_loop_create_default();
 *             esp_wifi_init(&wifi_init_config);
 *             esp_wifi_set_mode(WIFI_MODE_STA);
 *             esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
 *             esp_wifi_start();
 *             // Wait for WiFi connection (IP assigned)
 * 
 *          3. Partition Table Configuration:
 *             Your partition table (partition.csv or menuconfig) must include:
 *             - Two OTA partitions (ota_0, ota_1) of equal size
 *             - OTA data partition for tracking active/bootable partition
 *             Example:
 *               # Name,   Type, SubType,  Offset,  Size
 *               nvs,      data, nvs,      0x9000,  0x4000
 *               otadata,  data, ota,      0xd000,  0x2000
 *               phy_init, data, phy,      0xf000,  0x1000
 *               ota_0,    app,  ota_0,    0x10000, 0x180000
 *               ota_1,    app,  ota_1,    0x190000,0x180000
 * 
 *          OPERATIONAL FLOW:
 *          After successful initialization:
 *          1. Web server listens on http://<device-ip>:<server_port>
 *          2. User navigates to the web interface in a browser
 *          3. User uploads signed firmware binary via drag-and-drop or file picker
 *          4. Component validates signature (if enabled), checks version, flashes firmware
 *          5. On success, device reboots into new firmware
 *          6. New firmware MUST call esp_ota_mark_app_valid_cancel_rollback() within
 *             a reasonable timeout (e.g., 30 seconds) to confirm successful boot
 *          7. If new firmware crashes or doesn't confirm, ESP32 automatically rolls
 *             back to previous working firmware on next boot
 * 
 *          SIGNATURE VERIFICATION:
 *          If config->verify_signature is true, uploaded firmware must include
 *          a 64-byte ECDSA signature appended to the end of the .bin file.
 *          Use scripts/sign_firmware.sh to sign your firmware before upload.
 * 
 *          The public key is embedded at compile time (see ota_public_key.h).
 *          To generate keys, run: ./scripts/setup_ota_signing.sh
 *
 * @param[in] config Pointer to configuration structure. Cannot be NULL.
 *
 * @return
 *      - ESP_OK on success (web server running, ready for OTA)
 *      - ESP_ERR_INVALID_ARG if config is NULL or invalid
 *      - ESP_ERR_INVALID_STATE if prerequisites not met (WiFi/NVS not initialized)
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_FAIL if OTA partitions are not properly configured
 *      - Other esp_err_t codes on failure
 */
esp_err_t ota_manager_init(const ota_manager_config_t *config);

/**
 * @brief Stop OTA web server and release resources
 *
 * @details Shuts down the HTTP server and cleans up OTA manager state.
 *          After calling this, OTA updates are no longer possible until
 *          ota_manager_init() is called again.
 * 
 *          This function is useful for:
 *          - Disabling OTA after a timeout period for security
 *          - Freeing resources when OTA is not needed
 *          - Clean shutdown before device sleep/restart
 * 
 *          Note: Does NOT affect WiFi state (WiFi remains active)
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if OTA manager was not initialized
 */
esp_err_t ota_manager_deinit(void);

/**
 * @brief Get current OTA update status
 *
 * @details Query the current state of any ongoing OTA operation.
 *          Useful for polling status from application code.
 *
 * @return Current OTA status (see ota_status_t)
 */
ota_status_t ota_manager_get_status(void);

/**
 * @brief Mark current firmware as valid (confirm successful boot)
 *
 * @details CRITICAL for rollback protection! After flashing new firmware,
 *          the ESP32 boots into it in a "pending verification" state.
 *          The new firmware MUST call this function to confirm it's working.
 * 
 *          If this function is NOT called within the rollback timeout:
 *          - ESP32 assumes new firmware is faulty
 *          - On next reboot, automatically boots previous firmware
 * 
 *          WHEN TO CALL:
 *          Call this function after verifying that critical systems are operational:
 *          - WiFi connected successfully
 *          - Sensors/peripherals responding
 *          - No critical errors detected
 * 
 *          Example integration in main.c:
 *          @code
 *            void app_main(void) {
 *                // Initialize all systems
 *                init_nvs();
 *                init_wifi();
 *                init_sensors();
 *                
 *                // Verify everything works
 *                if (all_systems_ok()) {
 *                    ota_manager_mark_valid();  // Commit to this firmware
 *                }
 *                
 *                // Continue normal operation...
 *            }
 *          @endcode
 * 
 *          This is a thin wrapper around esp_ota_mark_app_valid_cancel_rollback().
 *
 * @return
 *      - ESP_OK if firmware marked as valid
 *      - ESP_ERR_INVALID_STATE if not in pending verification state
 *      - ESP_ERR_NOT_FOUND if OTA data partition not found
 */
esp_err_t ota_manager_mark_valid(void);

/**
 * @brief Get version string of currently running firmware
 *
 * @details Retrieves the version from the app descriptor of the running firmware.
 *          This version is embedded at compile time via esp_app_desc_t.
 * 
 *          To set your firmware version, define it in your main CMakeLists.txt:
 *          @code
 *            idf_component_register(...)
 *            target_compile_definitions(${COMPONENT_TARGET} PUBLIC
 *                APP_VERSION="1.2.3"
 *            )
 *          @endcode
 * 
 *          And in your code:
 *          @code
 *            const esp_app_desc_t *app_desc = esp_app_get_description();
 *            ESP_LOGI(TAG, "Firmware version: %s", app_desc->version);
 *          @endcode
 *
 * @param[out] version_buf Buffer to store version string
 * @param[in] buf_size Size of version_buf in bytes
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if version_buf is NULL or buf_size too small
 */
esp_err_t ota_manager_get_version(char *version_buf, size_t buf_size);

#endif /* OTA_MANAGER_H */
