/******************************************************************************
 * @file ota_manager.c
 * @brief OTA (Over-The-Air) firmware update manager implementation
 *
 ******************************************************************************/

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include "ota_manager.h"
#include "ota_signature.h"
#include "ota_web_ui.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <sys/param.h>

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
#define TAG "OTA_MANAGER"

/* HTTP response codes */
#define HTTP_OK 200
#define HTTP_BAD_REQUEST 400
#define HTTP_INTERNAL_ERROR 500

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/**
 * @brief Internal state structure for OTA operations
 * 
 * @details Tracks the current OTA process state for streaming firmware updates
 */
typedef struct 
{
    esp_ota_handle_t ota_handle;           /**< Handle for OTA write operations */
    const esp_partition_t *update_partition; /**< Target partition for new firmware */
    ota_status_t status;                    /**< Current OTA operation status */
} ota_state_t;

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
 * @brief HTTP handler for serving the OTA web UI page
 * 
 * @param[in] req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t http_get_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for device info API endpoint
 * 
 * @param[in] req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t http_info_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for firmware upload POST requests
 * 
 * @param[in] req HTTP request handle
 * @return ESP_OK on success
 */
static esp_err_t http_update_handler(httpd_req_t *req);

/**
 * @brief Verify signature of firmware already flashed to partition
 * 
 * @param[in] partition Partition containing firmware to verify
 * @param[in] total_size Total size of flashed data (firmware + signature)
 * @return ESP_OK if signature is valid, error code otherwise
 */
static esp_err_t verify_flashed_firmware(const esp_partition_t *partition, size_t total_size);

/**
 * @brief Compare two semantic version strings
 * 
 * @param[in] version1 First version string (e.g., "1.2.3")
 * @param[in] version2 Second version string
 * @return >0 if version1 > version2, <0 if version1 < version2, 0 if equal
 */
static int compare_versions(const char *version1, const char *version2);

/**
 * @brief Update OTA status and invoke callback if registered
 * 
 * @param[in] new_status New status to set
 * @param[in] progress Upload/flash progress (0-100)
 */
static void update_status(ota_status_t new_status, int progress);

/*******************************************************************************/
/*                             STATIC VARIABLES                                */
/*******************************************************************************/

/** Global configuration (copied from user config during init) */
static ota_manager_config_t g_config = {0};

/** HTTP server handle */
static httpd_handle_t g_server = NULL;

/** Current OTA operation state */
static ota_state_t g_ota_state = {0};

/** Flag indicating if OTA manager is initialized */
static bool g_initialized = false;

/*******************************************************************************/
/*                     GLOBAL FUNCTION DEFINITIONS                             */
/*******************************************************************************/

esp_err_t ota_manager_init(const ota_manager_config_t *config)
{
    if (!config) 
    {
        ESP_LOGE(TAG, "Configuration cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (g_initialized) 
    {
        ESP_LOGW(TAG, "OTA manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    /* #01 - Copy user configuration to global state */
    memcpy(&g_config, config, sizeof(ota_manager_config_t));
    
    /* Apply defaults for unset values */
    if (g_config.server_port == 0) 
    {
        g_config.server_port = OTA_MANAGER_DEFAULT_PORT;
    }

    /* #02 - Verify OTA partitions are configured
     * 
     * The partition table MUST include two OTA app partitions (ota_0, ota_1).
     * This is required for rollback protection:
     * - One partition holds current running firmware
     * - Other partition receives new firmware
     * - If new firmware fails, ESP32 boots from old partition
     */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    
    if (!running || !update) 
    {
        ESP_LOGE(TAG, "OTA partitions not found! Check partition table configuration.");
        ESP_LOGE(TAG, "Required: Two app partitions with subtype ota_0 and ota_1");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Running partition: %s (offset 0x%lx, size %lu KB)",
             running->label, running->address, running->size / 1024);
    ESP_LOGI(TAG, "Update partition: %s (offset 0x%lx, size %lu KB)",
             update->label, update->address, update->size / 1024);

    /* #03 - Initialize OTA state */
    memset(&g_ota_state, 0, sizeof(ota_state_t));
    g_ota_state.status = OTA_STATUS_IDLE;

    /* #04 - Configure and start HTTP server
     * 
     * The HTTP server provides two endpoints:
     * - GET / : Serves the web UI HTML page
     * - POST /api/update : Receives firmware uploads
     * - GET /api/info : Provides device info (version, IP)
     */
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = g_config.server_port;
    http_config.max_open_sockets = OTA_MANAGER_MAX_CONNECTIONS;
    http_config.lru_purge_enable = true;
    
    /* Increase URI match length for longer API paths */
    http_config.max_uri_handlers = 8;
    
    /* Allocate larger receive buffer for firmware uploads */
    http_config.stack_size = 8192;

    ret = httpd_start(&g_server, &http_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* #05 - Register HTTP handlers */
    
    /* Main page handler - serves web UI */
    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = http_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_server, &uri_get);

    /* Device info API endpoint */
    httpd_uri_t uri_info = {
        .uri = "/api/info",
        .method = HTTP_GET,
        .handler = http_info_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_server, &uri_info);

    /* Firmware upload handler */
    httpd_uri_t uri_update = {
        .uri = "/api/update",
        .method = HTTP_POST,
        .handler = http_update_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(g_server, &uri_update);

    /* #06 - Get device IP address for user information */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) 
    {
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");
        ESP_LOGI(TAG, "   OTA Update Manager Ready!");
        ESP_LOGI(TAG, "   Web Interface: http://" IPSTR ":%d",
                 IP2STR(&ip_info.ip), g_config.server_port);
        ESP_LOGI(TAG, "═══════════════════════════════════════════════");
    } 
    else 
    {
        ESP_LOGI(TAG, "OTA server started on port %d", g_config.server_port);
    }

    if (g_config.current_version) 
    {
        ESP_LOGI(TAG, "Current firmware version: %s", g_config.current_version);
    }
    
    ESP_LOGI(TAG, "Signature verification: %s", 
             g_config.verify_signature ? "ENABLED (verified after flashing)" : "DISABLED");

    g_initialized = true;
    update_status(OTA_STATUS_IDLE, 0);

    return ESP_OK;
}

esp_err_t ota_manager_deinit(void)
{
    if (!g_initialized) 
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop HTTP server if running */
    if (g_server) 
    {
        httpd_stop(g_server);
        g_server = NULL;
    }

    g_initialized = false;
    ESP_LOGI(TAG, "OTA manager stopped");

    return ESP_OK;
}

ota_status_t ota_manager_get_status(void)
{
    return g_ota_state.status;
}

esp_err_t ota_manager_mark_valid(void)
{
    /* Mark current firmware as valid to cancel rollback
     * 
     * This tells the ESP32 bootloader that the current firmware is working
     * correctly and should be kept as the active partition. Without this call,
     * if the device reboots, it will assume the firmware is faulty and roll
     * back to the previous version.
     * 
     * Call this function after verifying that all critical systems are operational.
     */
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    
    if (ret == ESP_OK) 
    {
        ESP_LOGI(TAG, "✓ Firmware marked as valid (rollback cancelled)");
    } 
    else if (ret == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) 
    {
        ESP_LOGD(TAG, "Not in pending verification state (already committed)");
    } 
    else 
    {
        ESP_LOGW(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t ota_manager_get_version(char *version_buf, size_t buf_size)
{
    if (!version_buf || buf_size == 0) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Get app descriptor from currently running partition */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    if (!app_desc) 
    {
        return ESP_FAIL;
    }

    /* Copy version string to output buffer */
    snprintf(version_buf, buf_size, "%s", app_desc->version);

    return ESP_OK;
}

/*******************************************************************************/
/*                     STATIC FUNCTION DEFINITIONS                             */
/*******************************************************************************/

static esp_err_t http_get_handler(httpd_req_t *req)
{
    /* Serve the embedded HTML page */
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, OTA_HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_info_handler(httpd_req_t *req)
{
    /* Build JSON response with device information */
    char json_response[256];
    char version_str[OTA_MANAGER_VERSION_MAX_LEN] = "Unknown";
    
    ota_manager_get_version(version_str, sizeof(version_str));
    
    /* Get IP address */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) 
    {
        snprintf(json_response, sizeof(json_response),
                 "{\"version\":\"%s\",\"ip\":\"" IPSTR "\"}",
                 version_str, IP2STR(&ip_info.ip));
    } 
    else 
    {
        snprintf(json_response, sizeof(json_response),
                 "{\"version\":\"%s\"}", version_str);
    }
    
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_update_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    char error_msg[128] = {0};
    
    ESP_LOGI(TAG, "Firmware upload started (content length: %d bytes)", req->content_len);
    
    /* #01 - Validate request size */
    if (req->content_len <= 0) 
    {
        snprintf(error_msg, sizeof(error_msg), "No data received");
        goto error;
    }

    /* Check if firmware fits in partition */
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) 
    {
        snprintf(error_msg, sizeof(error_msg), "Update partition not found");
        goto error;
    }

    if (req->content_len > update_partition->size) 
    {
        snprintf(error_msg, sizeof(error_msg), 
                 "Firmware too large (%d bytes, partition is %lu bytes)",
                 req->content_len, update_partition->size);
        goto error;
    }

    /* #02 - Allocate small streaming buffer (1KB, reused for each chunk)
     * 
     * We stream firmware directly to flash instead of buffering in RAM.
     * This is memory-efficient and works even with large firmwares.
     * 
     * NOTE: Signature verification is disabled when streaming because
     * the signature is at the end of the file. To enable signatures,
     * we would need to buffer the entire firmware (requires ~1MB RAM).
     */
    #define STREAM_BUFFER_SIZE 1024
    uint8_t *stream_buffer = malloc(STREAM_BUFFER_SIZE);
    if (!stream_buffer) 
    {
        snprintf(error_msg, sizeof(error_msg), "Failed to allocate stream buffer");
        ESP_LOGE(TAG, "%s", error_msg);
        goto error;
    }

    /* #03 - Begin OTA operation */
    update_status(OTA_STATUS_RECEIVING, 0);
    
    ret = esp_ota_begin(update_partition, req->content_len, &g_ota_state.ota_handle);
    if (ret != ESP_OK) 
    {
        snprintf(error_msg, sizeof(error_msg), 
                 "OTA begin failed: %s", esp_err_to_name(ret));
        update_status(OTA_STATUS_ERROR_PARTITION, 0);
        free(stream_buffer);
        goto error;
    }

    /* #04 - Stream firmware data directly to flash */
    int remaining = req->content_len;
    int total_received = 0;
    
    while (remaining > 0) 
    {
        int to_read = MIN(remaining, STREAM_BUFFER_SIZE);
        int chunk_size = httpd_req_recv(req, (char *)stream_buffer, to_read);
        
        if (chunk_size < 0) 
        {
            if (chunk_size == HTTPD_SOCK_ERR_TIMEOUT) 
            {
                /* Retry on timeout */
                continue;
            }
            snprintf(error_msg, sizeof(error_msg), "Network error during receive");
            esp_ota_abort(g_ota_state.ota_handle);
            free(stream_buffer);
            goto error;
        }
        
        if (chunk_size == 0) 
        {
            /* Connection closed prematurely */
            snprintf(error_msg, sizeof(error_msg), "Connection closed unexpectedly");
            esp_ota_abort(g_ota_state.ota_handle);
            free(stream_buffer);
            goto error;
        }
        
        /* Write chunk directly to flash */
        ret = esp_ota_write(g_ota_state.ota_handle, stream_buffer, chunk_size);
        if (ret != ESP_OK) 
        {
            snprintf(error_msg, sizeof(error_msg), 
                     "OTA write failed: %s", esp_err_to_name(ret));
            update_status(OTA_STATUS_ERROR_PARTITION, 0);
            esp_ota_abort(g_ota_state.ota_handle);
            free(stream_buffer);
            goto error;
        }
        
        total_received += chunk_size;
        remaining -= chunk_size;
        
        /* Report progress every 10% */
        int progress = (total_received * 100) / req->content_len;
        static int last_progress = 0;
        if (progress >= last_progress + 10) 
        {
            update_status(OTA_STATUS_FLASHING, progress);
            last_progress = progress;
        }
    }

    ESP_LOGI(TAG, "Received and flashed %d bytes", total_received);
    
    /* Free streaming buffer */
    free(stream_buffer);
    stream_buffer = NULL;

    update_status(OTA_STATUS_FLASHING, 90);

    /* #05 - Finalize OTA write */
    ret = esp_ota_end(g_ota_state.ota_handle);
    if (ret != ESP_OK) 
    {
        snprintf(error_msg, sizeof(error_msg), 
                 "OTA end failed: %s", esp_err_to_name(ret));
        update_status(OTA_STATUS_ERROR_PARTITION, 0);
        goto error;
    }

    /* #05.5 - Verify new firmware version (anti-downgrade protection)
     * 
     * Read app descriptor from the newly flashed partition to check version.
     * This prevents accidental downgrades to older firmware versions.
     */
    if (g_config.current_version && strlen(g_config.current_version) > 0) 
    {
        update_status(OTA_STATUS_VERIFYING_VERSION, 0);
        
        /* Read app descriptor from update partition */
        esp_app_desc_t new_app_desc;
        ret = esp_ota_get_partition_description(update_partition, &new_app_desc);
        
        if (ret == ESP_OK) 
        {
            ESP_LOGI(TAG, "New firmware version: %s", new_app_desc.version);
            ESP_LOGI(TAG, "Current firmware version: %s", g_config.current_version);
            
            /* Compare versions */
            int version_cmp = compare_versions(new_app_desc.version, g_config.current_version);
            
            if (version_cmp <= 0) 
            {
                ESP_LOGE(TAG, "Version check failed: new (%s) is not newer than current (%s)",
                         new_app_desc.version, g_config.current_version);
                snprintf(error_msg, sizeof(error_msg), 
                         "Downgrade not allowed (current: %s, new: %s)",
                         g_config.current_version, new_app_desc.version);
                update_status(OTA_STATUS_ERROR_VERSION, 0);
                goto error;
            }
            
            ESP_LOGI(TAG, "✓ Version check passed (%s > %s)", 
                     new_app_desc.version, g_config.current_version);
        }
        else
        {
            ESP_LOGW(TAG, "Could not read new firmware app descriptor: %s", 
                     esp_err_to_name(ret));
            /* Continue anyway - version check is optional */
        }
    }

    /* #06 - Verify signature if enabled (read back from flash)
     * 
     * Now that firmware is in flash, we can read it back and verify the signature.
     * This is done BEFORE setting the boot partition, so if verification fails,
     * we never switch to the bad firmware.
     */
    if (g_config.verify_signature) 
    {
        update_status(OTA_STATUS_VERIFYING_SIGNATURE, 0);
        ESP_LOGI(TAG, "Verifying signature from flash...");
        
        ret = verify_flashed_firmware(update_partition, req->content_len);
        if (ret != ESP_OK) 
        {
            snprintf(error_msg, sizeof(error_msg), "Signature verification failed");
            update_status(OTA_STATUS_ERROR_SIGNATURE, 0);
            goto error;
        }
        
        ESP_LOGI(TAG, "✓ Signature verified successfully");
    }
    else
    {
        ESP_LOGW(TAG, "⚠ Signature verification DISABLED - skipping verification");
    }

    /* #07 - Set new partition as boot partition
     * 
     * This marks the new firmware partition as the one to boot from on next reset.
     * IMPORTANT: The partition is in "pending verification" state.
     * The new firmware MUST call esp_ota_mark_app_valid_cancel_rollback()
     * within a timeout period, or the bootloader will automatically revert
     * to the old firmware on subsequent boots.
     */
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) 
    {
        snprintf(error_msg, sizeof(error_msg), 
                 "Failed to set boot partition: %s", esp_err_to_name(ret));
        update_status(OTA_STATUS_ERROR_PARTITION, 0);
        goto error;
    }

    update_status(OTA_STATUS_SUCCESS, 100);
    ESP_LOGI(TAG, "✓ OTA update successful!");
    ESP_LOGI(TAG, "✓ New firmware will boot on next reset");
    ESP_LOGI(TAG, "⚠ New firmware MUST call ota_manager_mark_valid() to confirm!");

    /* Send success response */
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);

    /* Schedule reboot after a short delay (allow response to send) */
    ESP_LOGI(TAG, "Rebooting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;

error:
    ESP_LOGE(TAG, "OTA update failed: %s", error_msg);

    /* Send error response */
    char json_error[256];
    snprintf(json_error, sizeof(json_error), "{\"error\":\"%s\"}", error_msg);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_error, HTTPD_RESP_USE_STRLEN);

    return ESP_FAIL;
}

static esp_err_t verify_flashed_firmware(const esp_partition_t *partition, size_t total_size)
{
    /* Signature is at the end of the uploaded file:
     * [firmware binary][64-byte ECDSA signature]
     * 
     * Read strategy:
     * 1. Read the last 64 bytes to get signature
     * 2. Stream firmware data in chunks, computing SHA256 hash incrementally
     * 3. Verify hash against signature using public key
     * 
     * This uses minimal RAM - only one chunk buffer at a time!
     */
    
    #define VERIFY_CHUNK_SIZE 4096
    const size_t signature_size = 64;  /* ECDSA P-256 signature */
    
    if (total_size < signature_size) 
    {
        ESP_LOGE(TAG, "Firmware too small to contain signature");
        return ESP_FAIL;
    }
    
    size_t firmware_size = total_size - signature_size;
    
    /* Allocate buffers - only ~4KB needed! */
    uint8_t *chunk_buffer = malloc(VERIFY_CHUNK_SIZE);
    uint8_t *signature = malloc(signature_size);
    uint8_t firmware_hash[32];  /* SHA256 hash output */
    
    if (!chunk_buffer || !signature) 
    {
        ESP_LOGE(TAG, "Failed to allocate verification buffers");
        free(chunk_buffer);
        free(signature);
        return ESP_ERR_NO_MEM;
    }
    
    /* Read signature from end of partition */
    esp_err_t ret = esp_partition_read(partition, firmware_size, signature, signature_size);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to read signature from flash: %s", esp_err_to_name(ret));
        free(chunk_buffer);
        free(signature);
        return ret;
    }
    
    /* Initialize SHA256 context for incremental hashing */
    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);
    mbedtls_sha256_starts(&sha256_ctx, 0);  /* 0 = SHA256 (not SHA224) */
    
    /* Stream firmware in chunks and hash incrementally */
    size_t offset = 0;
    size_t remaining = firmware_size;
    
    while (remaining > 0) 
    {
        size_t chunk_size = (remaining < VERIFY_CHUNK_SIZE) ? remaining : VERIFY_CHUNK_SIZE;
        
        /* Read chunk from flash */
        ret = esp_partition_read(partition, offset, chunk_buffer, chunk_size);
        if (ret != ESP_OK) 
        {
            ESP_LOGE(TAG, "Failed to read firmware chunk at offset %zu: %s", 
                     offset, esp_err_to_name(ret));
            mbedtls_sha256_free(&sha256_ctx);
            free(chunk_buffer);
            free(signature);
            return ret;
        }
        
        /* Update hash with this chunk */
        mbedtls_sha256_update(&sha256_ctx, chunk_buffer, chunk_size);
        
        offset += chunk_size;
        remaining -= chunk_size;
        
        /* Optional: Log progress every 100KB */
        if (offset % (100 * 1024) == 0) 
        {
            ESP_LOGD(TAG, "Verification progress: %zu / %zu bytes", offset, firmware_size);
        }
    }
    
    /* Finalize hash */
    mbedtls_sha256_finish(&sha256_ctx, firmware_hash);
    mbedtls_sha256_free(&sha256_ctx);
    
    /* Verify ECDSA signature against computed hash
     * 
     * ECDSA verification process:
     * 1. Firmware is hashed with SHA256 (we just computed this)
     * 2. Signature was created by signing this hash with private key
     * 3. Verify signature against hash using public key
     */
    bool signature_valid = ota_signature_verify_hash(firmware_hash, 32, signature);
    
    /* Clean up */
    free(chunk_buffer);
    free(signature);
    
    if (!signature_valid) 
    {
        ESP_LOGE(TAG, "✗ Signature verification FAILED - firmware is not authentic!");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✓ Firmware hash verified using incremental hashing (%zu bytes)", firmware_size);
    
    return ESP_OK;
}

static int compare_versions(const char *version1, const char *version2)
{
    /* Simple semantic version comparison (major.minor.patch)
     * 
     * Returns:
     *   > 0 if version1 > version2
     *   < 0 if version1 < version2
     *   = 0 if version1 == version2
     * 
     * Examples:
     *   compare_versions("2.0.0", "1.9.9") = +1
     *   compare_versions("1.2.3", "1.2.4") = -1
     *   compare_versions("1.0.0", "1.0.0") = 0
     */
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;

    sscanf(version1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(version2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

static void update_status(ota_status_t new_status, int progress)
{
    g_ota_state.status = new_status;
    
    /* Invoke user callback if registered */
    if (g_config.status_callback) 
    {
        g_config.status_callback(new_status, progress);
    }
}
