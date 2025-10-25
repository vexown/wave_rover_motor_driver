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
#include "esp_now_comm.h"

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

/**
 * @brief Callback for ESP-NOW send completion
 *
 * @details Called by ESP-NOW stack after each transmission attempt to report
 *          success or failure.
 *
 * @param[in] mac_addr 6-byte MAC address of the destination peer
 * @param[in] status ESP_NOW_SEND_SUCCESS if transmission succeeded,
 *                   ESP_NOW_SEND_FAIL if failed
 *
 * @return None
 */
static void on_data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status);

/**
 * @brief Callback for ESP-NOW data reception
 *
 * @details Called by ESP-NOW stack asynchronously whenever a valid packet
 *          is received from a registered peer device. Can be used to parse
 *          protocol messages and update device state.
 *
 * @param[in] mac_addr 6-byte MAC address of the peer that sent the data
 * @param[in] data Pointer to the received data payload
 * @param[in] len Length of received data in bytes
 *
 * @return None
 */
static void on_data_recv_callback(const uint8_t *mac_addr, const uint8_t *data, int len);

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
    /************************ Component #01 - ESP-NOW Communication ***********************/
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

    /************************ Component #02 - Placeholder ***********************/
    
    ESP_LOGI(TAG, "All components initialized successfully");
    return ESP_OK;
}

static void on_data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    /* Determine if send succeeded or failed and log the result */
    const char *status_str = (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL";
    ESP_LOGI(TAG, "Send to %02x:%02x:%02x:%02x:%02x:%02x: %s", 
             mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5], status_str);
    
    /* Here you could implement retry logic, update statistics, etc.
     * For example: increment failure counter if status is FAIL
     */
}

static void on_data_recv_callback(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    /* Log the reception event with peer MAC address and data length */
    ESP_LOGI(TAG, "Received %d bytes from %02x:%02x:%02x:%02x:%02x:%02x", 
             len, mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5]);
    
    /* Here you could parse the received data and take action
     * For example: deserialize protocol messages, update device state, etc.
     */
}