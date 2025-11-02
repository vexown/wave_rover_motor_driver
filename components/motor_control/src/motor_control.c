/******************************************************************************
 * @file motor_control.c
 * @brief Motor control implementation for BTS7960B dual motor drivers
 * 
 ******************************************************************************/

/*******************************************************************************/
/*                                 INCLUDES                                    */
/*******************************************************************************/
#include "motor_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

/*******************************************************************************/
/*                                  MACROS                                     */
/*******************************************************************************/
#define TAG "MOTOR_CONTROL"

/* LEDC Timer Configuration */
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE

/* LEDC Channel Assignments */
#define LEDC_CHANNEL_LEFT_LPWM  LEDC_CHANNEL_0
#define LEDC_CHANNEL_LEFT_RPWM  LEDC_CHANNEL_1
#define LEDC_CHANNEL_RIGHT_LPWM LEDC_CHANNEL_2
#define LEDC_CHANNEL_RIGHT_RPWM LEDC_CHANNEL_3

/*******************************************************************************/
/*                                DATA TYPES                                   */
/*******************************************************************************/

/*******************************************************************************/
/*                     STATIC FUNCTION DECLARATIONS                            */
/*******************************************************************************/

/**
 * @brief Initialize a single motor driver
 * 
 * @param[in] motor Pointer to motor driver configuration
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t motor_driver_init(const motor_driver_t *motor);

/**
 * @brief Set motor speed and direction
 * 
 * @param[in] motor Pointer to motor driver configuration
 * @param[in] speed Speed value from -255 to 255 (negative = reverse)
 */
static void motor_set_speed(const motor_driver_t *motor, int16_t speed);

/**
 * @brief Watchdog timer callback
 * 
 * @details Called when watchdog timeout expires, stops all motors
 * 
 * @param[in] arg Unused timer argument
 */
static void motor_watchdog_callback(void *arg);

/**
 * @brief Parse motor command string
 * 
 * @param[in] data Command string in format "L:%d|R:%d"
 * @param[in] len Length of command string
 * @param[out] left_speed Parsed left motor speed
 * @param[out] right_speed Parsed right motor speed
 * @return true if parsing successful, false otherwise
 */
static bool parse_motor_command(const uint8_t *data, size_t len, 
                                int16_t *left_speed, int16_t *right_speed);

/*******************************************************************************/
/*                             STATIC VARIABLES                                */
/*******************************************************************************/

/* Motor driver configurations */
static motor_driver_t left_motor = {
    .l_en_pin = MOTOR_LEFT_L_EN_PIN,
    .r_en_pin = MOTOR_LEFT_R_EN_PIN,
    .lpwm_pin = MOTOR_LEFT_LPWM_PIN,
    .rpwm_pin = MOTOR_LEFT_RPWM_PIN,
    .ledc_channel_lpwm = LEDC_CHANNEL_LEFT_LPWM,
    .ledc_channel_rpwm = LEDC_CHANNEL_LEFT_RPWM
};

static motor_driver_t right_motor = {
    .l_en_pin = MOTOR_RIGHT_L_EN_PIN,
    .r_en_pin = MOTOR_RIGHT_R_EN_PIN,
    .lpwm_pin = MOTOR_RIGHT_LPWM_PIN,
    .rpwm_pin = MOTOR_RIGHT_RPWM_PIN,
    .ledc_channel_lpwm = LEDC_CHANNEL_RIGHT_LPWM,
    .ledc_channel_rpwm = LEDC_CHANNEL_RIGHT_RPWM
};

/* Watchdog timer handle */
static esp_timer_handle_t watchdog_timer = NULL;

/* Flag to track initialization state */
static bool is_initialized = false;

/*******************************************************************************/
/*                     GLOBAL FUNCTION DEFINITIONS                             */
/*******************************************************************************/

esp_err_t motor_control_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Motor control already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing motor control...");

    /* Configure LEDC timer for PWM generation */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = MOTOR_PWM_RESOLUTION,
        .freq_hz          = MOTOR_PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize left motor driver */
    ret = motor_driver_init(&left_motor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize left motor driver");
        return ret;
    }

    /* Initialize right motor driver */
    ret = motor_driver_init(&right_motor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize right motor driver");
        return ret;
    }

    /* Create watchdog timer */
    const esp_timer_create_args_t watchdog_timer_args = {
        .callback = &motor_watchdog_callback,
        .name = "motor_watchdog"
    };
    ret = esp_timer_create(&watchdog_timer_args, &watchdog_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create watchdog timer: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start watchdog timer (one-shot mode) */
    ret = esp_timer_start_once(watchdog_timer, MOTOR_WATCHDOG_TIMEOUT_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start watchdog timer: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize motors to stopped state */
    motor_control_stop_all();

    is_initialized = true;
    ESP_LOGI(TAG, "Motor control initialized successfully");
    ESP_LOGI(TAG, "PWM Frequency: %d Hz, Watchdog Timeout: %d ms", 
             MOTOR_PWM_FREQ_HZ, MOTOR_WATCHDOG_TIMEOUT_MS);
    
    return ESP_OK;
}

void motor_control_handle_command(const uint8_t *data, size_t len)
{
    if (!is_initialized) {
        ESP_LOGW(TAG, "Motor control not initialized");
        return;
    }

    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "Invalid command data");
        return;
    }

    /* Parse the command */
    int16_t left_speed = 0;
    int16_t right_speed = 0;
    
    if (!parse_motor_command(data, len, &left_speed, &right_speed)) {
        ESP_LOGW(TAG, "Failed to parse motor command");
        return;
    }

    ESP_LOGI(TAG, "Motor command received - Left: %d, Right: %d", left_speed, right_speed);

    /* Set motor speeds */
    motor_set_speed(&left_motor, left_speed);
    motor_set_speed(&right_motor, right_speed);

    /* Reset watchdog timer */
    esp_timer_stop(watchdog_timer);
    esp_timer_start_once(watchdog_timer, MOTOR_WATCHDOG_TIMEOUT_MS * 1000);
}

void motor_control_stop_all(void)
{
    ESP_LOGI(TAG, "Stopping all motors");
    motor_set_speed(&left_motor, 0);
    motor_set_speed(&right_motor, 0);
}

/*******************************************************************************/
/*                     STATIC FUNCTION DEFINITIONS                             */
/*******************************************************************************/

static esp_err_t motor_driver_init(const motor_driver_t *motor)
{
    if (motor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Configure enable pins as outputs */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << motor->l_en_pin) | (1ULL << motor->r_en_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure enable pins: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set enable pins HIGH (enable the driver) */
    gpio_set_level(motor->l_en_pin, 1);
    gpio_set_level(motor->r_en_pin, 1);

    /* Configure LPWM channel */
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = motor->ledc_channel_lpwm,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = motor->lpwm_pin,
        .duty           = 0,
        .hpoint         = 0
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LPWM channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure RPWM channel */
    ledc_channel.channel = motor->ledc_channel_rpwm;
    ledc_channel.gpio_num = motor->rpwm_pin;
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RPWM channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Motor driver initialized - L_EN: GPIO%d, R_EN: GPIO%d, LPWM: GPIO%d, RPWM: GPIO%d",
             motor->l_en_pin, motor->r_en_pin, motor->lpwm_pin, motor->rpwm_pin);

    return ESP_OK;
}

static void motor_set_speed(const motor_driver_t *motor, int16_t speed)
{
    if (motor == NULL) {
        return;
    }

    /* Clamp speed to valid range */
    if (speed > MOTOR_PWM_MAX_DUTY) {
        speed = MOTOR_PWM_MAX_DUTY;
    } else if (speed < -MOTOR_PWM_MAX_DUTY) {
        speed = -MOTOR_PWM_MAX_DUTY;
    }

    uint32_t duty = 0;
    int lpwm_duty = 0;
    int rpwm_duty = 0;

    if (speed > 0) {
        /* Forward direction - use LPWM */
        duty = (uint32_t)speed;
        lpwm_duty = duty;
        rpwm_duty = 0;
    } else if (speed < 0) {
        /* Reverse direction - use RPWM */
        duty = (uint32_t)(-speed);
        lpwm_duty = 0;
        rpwm_duty = duty;
    } else {
        /* Stop - both PWM to 0 */
        lpwm_duty = 0;
        rpwm_duty = 0;
    }

    /* Set PWM duty cycles */
    ledc_set_duty(LEDC_MODE, motor->ledc_channel_lpwm, lpwm_duty);
    ledc_update_duty(LEDC_MODE, motor->ledc_channel_lpwm);
    
    ledc_set_duty(LEDC_MODE, motor->ledc_channel_rpwm, rpwm_duty);
    ledc_update_duty(LEDC_MODE, motor->ledc_channel_rpwm);
}

static void motor_watchdog_callback(void *arg)
{
    ESP_LOGW(TAG, "Watchdog timeout! No command received - stopping motors for safety");
    motor_control_stop_all();
}

static bool parse_motor_command(const uint8_t *data, size_t len, 
                                int16_t *left_speed, int16_t *right_speed)
{
    if (data == NULL || left_speed == NULL || right_speed == NULL) {
        return false;
    }

    /* Create null-terminated string */
    char cmd_str[64] = {0};
    size_t copy_len = (len < sizeof(cmd_str) - 1) ? len : sizeof(cmd_str) - 1;
    memcpy(cmd_str, data, copy_len);
    cmd_str[copy_len] = '\0';

    /* Parse format: "L:%d|R:%d" */
    int left_val = 0;
    int right_val = 0;
    int matched = sscanf(cmd_str, "L:%d|R:%d", &left_val, &right_val);
    
    if (matched != 2) {
        ESP_LOGW(TAG, "Command parse failed. Expected 'L:%%d|R:%%d', got: '%s'", cmd_str);
        return false;
    }

    /* Validate and clamp values */
    if (left_val < -255 || left_val > 255 || right_val < -255 || right_val > 255) {
        ESP_LOGW(TAG, "Speed values out of range (-255 to 255): L=%d, R=%d", left_val, right_val);
        /* Clamp to valid range */
        if (left_val < -255) left_val = -255;
        if (left_val > 255) left_val = 255;
        if (right_val < -255) right_val = -255;
        if (right_val > 255) right_val = 255;
    }

    *left_speed = (int16_t)left_val;
    *right_speed = (int16_t)right_val;

    return true;
}
