#ifndef MAIN_DISPLAY_H
#define MAIN_DISPLAY_H
#include <stdbool.h>
#include <sys/types.h>

// Updates the display with the current connection status
void display_updateConnectionStatus(const char* status);
void display_updateBTConnectionStatus(const char* status);
void display_updateOTConnectionStatus(const char* status);

// Updates the display with the current direction and speed
void display_updateDirectionAndSpeed(u_int8_t direction, u_int8_t speed);

// Updates the display with the current name
void display_updateName(const char* name);

// Updates the display with the current IPv6 address
void display_updateIPv6Address(const char* ipv6Address);

bool display_initDisplay();

#endif // MAIN_DISPLAY_H