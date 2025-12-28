#ifdef CONFIG_SSD1306
#ifdef CONFIG_DISPLAY
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
    // Check if the display is already initialized
    if (display_initialized) {
        LOG_WRN("Display is already initialized");
        return true; // Display is already initialized, no need to reinitialize
    }
    lv_mem_init(); // Initialize LVGL memory management
    // Initialize the LVGL library
    lv_init();

    // Initialize the display driver (assuming a Zephyr-based display)
    const struct device* display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display device not ready\n");
        return false;
    } else  {

    }

    // Initialize the display
    display_blanking_off(display_dev);
    lv_task_handler(); // This call is executed once, by virtue of main.c, so no wrapping as WORK is needed.

    lv_obj_t* scr = lv_scr_act();
    const uint8_t line_height = 8; // Height of each line in pixels
    const uint8_t line_count = 4; // Number of lines to display
    const uint8_t screen_width = 128; // Width of the screen in pixels
    const uint8_t screen_height = 64; // Height of the screen in pixels

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
    display_initialized = true;
    display_initRefresh(); // Initialize the display refresh mechanism
    return true;
}

#if (CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS > 0 )

// Mutex to protect access to LVGL functions
#include <zephyr/sys/mutex.h>
// This mutex is used to ensure that LVGL functions are not called from multiple threads simultaneously
// This is important because LVGL is not thread-safe and can cause issues if accessed concurrently
// It is used in the display_updateConnectionStatus, display_updateBTConnectionStatus, display_updateOTConnectionStatus,
// display_updateDirectionAndSpeed, display_updateName, and display_updateIPv6Address functions 
// to ensure that only one thread can access LVGL functions at a time
// This is especially important when using the display in a multi-threaded environment, such as with Zephyr's work queues

#include <zephyr/sys/mutex.h>
static struct k_mutex lvgl_mutex;



// Function to update the display periodically
void animate_ip_scroll(void *p1, void *p2, void *p3){
while (1) {    
    if (
        k_mutex_lock(&lvgl_mutex, K_MSEC(100))  == 0){
        // Call the LVGL task handler to process events and update the display
        lv_task_handler();
        k_mutex_unlock(&lvgl_mutex);
    } else {
        // If the mutex could not be locked, skip the update to avoid deadlock
        printk("LVGL mutex lock failed, skipping display update\n");
    }
    k_sleep(K_MSEC(CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS)); // Sleep for the configured period
}
}
#define ANIMATE_SCROLL_STACK_SIZE 4096
// Define a thread to handle the periodic display updates, but don't start it yet
K_THREAD_STACK_DEFINE(animate_scroll_stack_area, ANIMATE_SCROLL_STACK_SIZE);
struct k_thread animate_scroll_thread_data;

// Function to initialize the mutex
void display_initRefresh() {
    k_mutex_init(&lvgl_mutex);
    // Initialize the mutex to protect access to LVGL functions
    // Start the thread that will handle the periodic display updates
    k_thread_create(&animate_scroll_thread_data, animate_scroll_stack_area, K_THREAD_STACK_SIZEOF(animate_scroll_stack_area),
                  animate_ip_scroll, NULL, NULL, NULL,
                  K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
}
// Function to lock the mutex before accessing LVGL functions
void display_lock() {
    k_mutex_lock(&lvgl_mutex, K_FOREVER);
}
// Function to unlock the mutex after accessing LVGL functions
void display_unlock() {
    k_mutex_unlock(&lvgl_mutex);
}

#else


void display_initRefresh() {
    // No mutex initialization needed if CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS is not defined
    // This is a no-op function to maintain the same interface
    // LVGL functions can be called directly without locking in this case 
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


void display_lock() {
    // No mutex locking needed if CONFIG_LVGL_DISPLAY_UPDATE_PERIOD_MS is not defined
    // This is a no-op function to maintain the same interface
    // LVGL functions can be called directly without locking in this case
}
// Function to unlock the mutex after accessing LVGL functions
void display_unlock() {
    k_work_submit(&display_update_work); // Schedule the display update  
}

#endif



// Updates the display with the current connection status
void display_updateConnectionStatus(const char* status) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }
    if (status == NULL) {
        status = "N/A"; // Default status if NULL is provided
    }
    // Create a buffer to hold the formatted status message
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Status: %s", status);
    display_lock(); // Lock the mutex to ensure thread safety
        lv_label_set_text(connection_status_label, buffer);
    display_unlock();
}

char bt_conn_status[9] = {0};
char ot_conn_status[9] = {0};

// Updates the display with the current Bluetooth connection status
void display_updateBTConnectionStatus(const char* status) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }
    if (status == NULL) {
        status = "N/A"; // Default status if NULL is provided
    }
    // Create a buffer to hold the formatted status message

    static char buffer[256];
    snprintf(bt_conn_status, sizeof(bt_conn_status), "%s", status);
    sprintf(buffer,"BT %s OT %s", bt_conn_status, ot_conn_status);
    display_lock(); // Lock the mutex to ensure thread safety
        lv_label_set_text(connection_status_label, buffer);
    display_unlock();
}
// Updates the display with the current OpenThread connection status
void display_updateOTConnectionStatus(const char* status) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }
    if (status == NULL) {
        status = "N/A"; // Default status if NULL is provided
    }
    static char buffer[256];
    snprintf(ot_conn_status, sizeof(ot_conn_status), "%s", status);
    
    sprintf(buffer,"BT %s OT %s", bt_conn_status, ot_conn_status);
    display_lock(); // Lock the mutex to ensure thread safety
    lv_label_set_text(connection_status_label, buffer);
    display_unlock(); // Unlock the mutex after updating the display
}

// Updates the display with the current direction and speed
void display_updateDirectionAndSpeed(uint8_t direction_pattern, uint8_t speed) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }
    // Create a buffer to hold the formatted direction  message
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
    char buffer[256];
    snprintf(buffer, sizeof(buffer), " %s Speed: %.2f", d_buffer, (float)speed);
    // Update the label with the new text
    display_lock(); // Lock the mutex to ensure thread safety
    lv_label_set_text(direction_speed_label, buffer);
    display_unlock(); // Unlock the mutex after updating the display

}

// Updates the display with the current name
void display_updateName(const char* name) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }

    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Name: %s", name);
    display_lock(); // Lock the mutex to ensure thread safety
    lv_label_set_text(name_label, buffer);
    display_unlock(); // Unlock the mutex after updating the display
}

// Updates the display with the current IPv6 address
void display_updateIPv6Address(const char* ipv6Address) {
    if (!display_initialized) {
        printk("Display not initialized, skipping update\n");
        return; // Display not initialized, skip the update
    }

    static char buffer[256];
    display_lock(); // Lock the mutex to ensure thread safety
    if (ipv6Address == NULL) {
        snprintf(buffer, sizeof(buffer), "IPv6 Address: N/A");
        lv_label_set_text(ipv6_address_label, buffer);
        display_unlock(); // Unlock the mutex after updating the display
        return;
    }
    snprintf(buffer, sizeof(buffer), "IPv6 Address: %s", ipv6Address);
    lv_label_set_text(ipv6_address_label, buffer);
    display_unlock(); // Unlock the mutex after updating the display
}

#endif // CONFIG_SSD1306
#endif // CONFIG_DISPLAY