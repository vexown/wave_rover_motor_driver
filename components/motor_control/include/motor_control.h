/******************************************************************************
 * @file motor_control.h
 * @brief Motor control component for BTS7960B dual motor drivers
 * 
 * @details Controls two BTS7960B motor drivers (left and right wheels) via
 *          ESP-NOW commands. Includes safety watchdog to stop motors if
 *          connection is lost.
 ******************************************************************************/

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
/* PWM Configuration */
#define MOTOR_PWM_FREQ_HZ           20000      /* 20 kHz PWM frequency */
#define MOTOR_PWM_RESOLUTION        8          /* 8-bit resolution (0-255) */
#define MOTOR_PWM_MAX_DUTY          255        /* Maximum duty cycle value */

/* Safety Watchdog */
#define MOTOR_WATCHDOG_TIMEOUT_MS   500       /* Stop motors if no command for 500 ms */

/* GPIO Pin Definitions for XIAO ESP32-C6 */
/* Left Motor Driver (BTS7960B) */
#define MOTOR_LEFT_L_EN_PIN         GPIO_NUM_17    /* Left motor - Left Enable */
#define MOTOR_LEFT_R_EN_PIN         GPIO_NUM_18    /* Left motor - Right Enable */
#define MOTOR_LEFT_LPWM_PIN         GPIO_NUM_19    /* Left motor - Left PWM */
#define MOTOR_LEFT_RPWM_PIN         GPIO_NUM_20    /* Left motor - Right PWM */

/* Right Motor Driver (BTS7960B) */
#define MOTOR_RIGHT_L_EN_PIN        GPIO_NUM_21    /* Right motor - Left Enable */
#define MOTOR_RIGHT_R_EN_PIN        GPIO_NUM_22   /* Right motor - Right Enable */
#define MOTOR_RIGHT_LPWM_PIN        GPIO_NUM_23   /* Right motor - Left PWM */
#define MOTOR_RIGHT_RPWM_PIN        GPIO_NUM_16   /* Right motor - Right PWM */

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/**
 * @brief Motor control structure
 * 
 * @details Holds the configuration for a single motor driver
 */
typedef struct {
    uint8_t l_en_pin;       /* Left enable pin */
    uint8_t r_en_pin;       /* Right enable pin */
    uint8_t lpwm_pin;       /* Left PWM pin */
    uint8_t rpwm_pin;       /* Right PWM pin */
    int ledc_channel_lpwm;  /* LEDC channel for LPWM */
    int ledc_channel_rpwm;  /* LEDC channel for RPWM */
} motor_driver_t;

/*******************************************************************************/
/*                     GLOBAL FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/**
 * @brief Initialize the motor control component
 * 
 * @details Configures GPIO pins, PWM channels, and starts the safety watchdog
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 */
esp_err_t motor_control_init(void);

/**
 * @brief Handle incoming motor control command
 * 
 * @details Parses the command string in format "L:%d|R:%d" and controls motors
 *          accordingly. Updates the watchdog timer on each valid command.
 * 
 * @param[in] data Pointer to command string
 * @param[in] len Length of command string
 * 
 * @note This function should be called from the ESP-NOW receive callback
 */
void motor_control_handle_command(const uint8_t *data, size_t len);

/**
 * @brief Manually stop all motors
 * 
 * @details Sets all motor outputs to zero. Used by watchdog on timeout.
 */
void motor_control_stop_all(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_CONTROL_H */
