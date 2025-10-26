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
#include "esp_now_comm_callbacks.h"

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

