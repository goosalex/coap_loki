#ifndef MAIN_OT_UTILS_H
#define MAIN_OT_UTILS_H

#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

void HandleJoinerCallback(otError aError, void *aContext);
int disable_thread(otInstance *p_instance);
int start_thread_joiner(char *secret);
int enable_thread(void);

#ifdef __cplusplus
}
#endif

#endif // MAIN_OT_UTILS_H