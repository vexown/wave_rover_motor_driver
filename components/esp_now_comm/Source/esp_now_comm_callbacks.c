#include "esp_now_comm_callbacks.h"
#include "esp_log.h"
#include <string.h>

extern void motor_control_handle_command(const uint8_t *data, size_t len); // from motor_control component
extern const uint8_t wave_rover_driver_mac[6]; // from main.c

#define TAG "ESP_NOW_COMM_CALLBACK"

void on_data_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status)
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

void on_data_recv_callback(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    /* Log the reception event with peer MAC address and data length */
    ESP_LOGI(TAG, "Received %d bytes from %02x:%02x:%02x:%02x:%02x:%02x", 
             len, mac_addr[0], mac_addr[1], mac_addr[2], 
             mac_addr[3], mac_addr[4], mac_addr[5]);
    
    /* Print the received message as an ASCII string */
    ESP_LOGI(TAG, "Data: %.*s", len, (char*)data);

    /* Check if the message came from the wave_rover_driver controller */
    if (memcmp(mac_addr, wave_rover_driver_mac, 6) == 0) 
    {
        /* Handle motor control command from authorized peer */
        ESP_LOGI(TAG, "Motor command from authorized peer, forwarding to motor control");
        motor_control_handle_command(data, len);
    }
    else
    {
        /* Log message from unauthorized/unknown peer */
        ESP_LOGW(TAG, "Received data from unknown peer - ignoring");
    }
}
