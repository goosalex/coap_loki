#ifndef MAIN_DISPLAY_H
#define MAIN_DISPLAY_H

// Updates the display with the current connection status
void updateConnectionStatus(const char* status);

// Updates the display with the current direction and speed
void updateDirectionAndSpeed(const char* direction, float speed);

// Updates the display with the current name
void updateName(const char* name);

// Updates the display with the current IPv6 address
void updateIPv6Address(const char* ipv6Address);

#endif // MAIN_DISPLAY_H