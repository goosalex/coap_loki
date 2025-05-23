#include "main_display.h"

#ifndef CONFIG_LVGL
#error "LVGL is not enabled in the configuration"
#endif
// Start Display related includes
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h> // KWORK....
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(lvgl_1306, CONFIG_DISPLAY_LVGL_LOG_LEVEL);


// Define global labels for each line of the display
static lv_obj_t* connection_status_label;
static lv_obj_t* direction_speed_label;
static lv_obj_t* name_label;
static lv_obj_t* ipv6_address_label;

// Define a flag to check if the display is initialized
// this flag is used to prevent accessing the display before it is initialized or if the display is not ready/connected
static bool display_initialized = false;

// Initialize the display (to be called once before using the update functions)
bool display_initDisplay() {
    // Initialize the LVGL library
    lv_init();

    // Initialize the display driver (assuming a Zephyr-based display)
    const struct device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display device not ready\n");
        return false;
    } else  {
        display_initialized = true;
    }

    // Initialize the display
    display_blanking_off(display_dev);
    lv_task_handler(); // This call is executed once, by virtue of main.c, so no wrapping as WORK is needed.

    lv_obj_t* scr = lv_scr_act();
    const u_int8_t line_height = 8; // Height of each line in pixels
    const u_int8_t line_count = 4; // Number of lines to display
    const u_int8_t screen_width = 128; // Width of the screen in pixels
    const u_int8_t screen_height = 64; // Height of the screen in pixels

    // Create and position labels for each line
    connection_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(connection_status_label, &lv_font_unscii_8, 0);
    lv_obj_align(connection_status_label, LV_ALIGN_TOP_LEFT, 0, 0*line_height); // Top line

    direction_speed_label = lv_label_create(scr);
    lv_obj_set_style_text_font(direction_speed_label, &lv_font_unscii_8, 0);
    lv_obj_align(direction_speed_label, LV_ALIGN_TOP_LEFT, 0, 1*line_height); // Second line

    name_label = lv_label_create(scr);
    lv_obj_set_style_text_font(name_label, &lv_font_unscii_8, 0);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 2*line_height); // Third line

    ipv6_address_label = lv_label_create(scr);
    lv_obj_set_style_text_font(ipv6_address_label, &lv_font_unscii_8, 0);
    lv_obj_align(ipv6_address_label, LV_ALIGN_TOP_LEFT, 0, 3*line_height); 
    lv_obj_set_width(ipv6_address_label, screen_width); // Set the width of the label to the screen width
    // Fourth line
    lv_label_set_long_mode(ipv6_address_label, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    return true;
}


void updateDisplay(struct k_work_delayable *work) {
    // Call the LVGL task handler to refresh the display
    if (!display_initialized) {
        return; // Display not initialized, skip the update
    }
    lv_task_handler();
}

// Function to update the display (to be called periodically)
K_WORK_DELAYABLE_DEFINE(display_update_work, updateDisplay);

// Updates the display with the current connection status
void display_updateConnectionStatus(const char* status) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Status: %s", status);
    lv_label_set_text(connection_status_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    
}

char bt_conn_status[9] = {0};
char ot_conn_status[9] = {0};

// Updates the display with the current Bluetooth connection status
void display_updateBTConnectionStatus(const char* status) {
    static char buffer[256];
    snprintf(bt_conn_status, sizeof(bt_conn_status), "%s", status);

    sprintf(buffer,"BT %s OT %s", bt_conn_status, ot_conn_status);
    lv_label_set_text(connection_status_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    
}
// Updates the display with the current OpenThread connection status
void display_updateOTConnectionStatus(const char* status) {
    static char buffer[256];
    snprintf(ot_conn_status, sizeof(ot_conn_status), "%s", status);
    
    sprintf(buffer,"BT %s OT %s", bt_conn_status, ot_conn_status);
    lv_label_set_text(connection_status_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    
}

// Updates the display with the current direction and speed
void display_updateDirectionAndSpeed(u_int8_t direction_pattern, u_int8_t speed) {
    char d_buffer[3]; 
		if ((direction_pattern & 1)==1) {
			sprintf(d_buffer, "%s", " >");
		} else if ((direction_pattern & 2)==2)
		{
			sprintf(d_buffer, "%s", "< ");
		} else
		 {
			sprintf(d_buffer, "%s", "--");
		}
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), " %s Speed: %.2f", d_buffer, (float)speed);
    // Update the label with the new text
    lv_label_set_text(direction_speed_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    

}

// Updates the display with the current name
void display_updateName(const char* name) {
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Name: %s", name);
    lv_label_set_text(name_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    
}

// Updates the display with the current IPv6 address
void display_updateIPv6Address(const char* ipv6Address) {
    static char buffer[256];
    if (ipv6Address == NULL) {
        snprintf(buffer, sizeof(buffer), "IPv6 Address: N/A");
        lv_label_set_text(ipv6_address_label, buffer);
        return;
    }
    snprintf(buffer, sizeof(buffer), "IPv6 Address: %s", ipv6Address);
    lv_label_set_text(ipv6_address_label, buffer);
    k_work_submit(&display_update_work); // Schedule the display update    
}