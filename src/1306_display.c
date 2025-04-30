#include "main_display.h"

#ifndef CONFIG_LVGL
#error "LVGL is not enabled in the configuration"
#endif
// Start Display related includes
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>


// Define global labels for each line of the display
static lv_obj_t* connection_status_label;
static lv_obj_t* direction_speed_label;
static lv_obj_t* name_label;
static lv_obj_t* ipv6_address_label;

// Initialize the display (to be called once before using the update functions)
void initDisplay() {
    // Initialize the LVGL library
    lv_init();
    lvgl_input_device_init(); // Initialize the input device for LVGL

    // Initialize the display driver (assuming a Zephyr-based display)
    const struct device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display device not ready\n");
        return;
    }

    // Initialize the display
    display_blanking_off(display_dev);
    lv_task_handler();

    lv_obj_t* scr = lv_scr_act();

    // Create and position labels for each line
    connection_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(connection_status_label, &lv_font_unscii_8, 0);
    lv_obj_align(connection_status_label, LV_ALIGN_TOP_LEFT, 0, 0); // Top line

    direction_speed_label = lv_label_create(scr);
    lv_obj_set_style_text_font(direction_speed_label, &lv_font_unscii_8, 0);
    lv_obj_align(direction_speed_label, LV_ALIGN_TOP_LEFT, 0, 10); // Second line

    name_label = lv_label_create(scr);
    lv_obj_set_style_text_font(name_label, &lv_font_unscii_8, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 20); // Third line

    ipv6_address_label = lv_label_create(scr);
    lv_obj_set_style_text_font(ipv6_address_label, &lv_font_unscii_8, 0);
    lv_obj_align(ipv6_address_label, LV_ALIGN_TOP_LEFT, 0, 30); // Fourth line
}

// Updates the display with the current connection status
void updateConnectionStatus(const char* status) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Connection Status: %s", status);
    lv_label_set_text(connection_status_label, buffer);
    lv_task_handler(); // Refresh the display
}

// Updates the display with the current direction and speed
void updateDirectionAndSpeed(const char* direction, float speed) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Direction: %s, Speed: %.2f", direction, speed);
    lv_label_set_text(direction_speed_label, buffer);
    lv_task_handler(); // Refresh the display
}

// Updates the display with the current name
void updateName(const char* name) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Name: %s", name);
    lv_label_set_text(name_label, buffer);
    lv_task_handler(); // Refresh the display
}

// Updates the display with the current IPv6 address
void updateIPv6Address(const char* ipv6Address) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "IPv6 Address: %s", ipv6Address);
    lv_label_set_text(ipv6_address_label, buffer);
    lv_task_handler(); // Refresh the display
}