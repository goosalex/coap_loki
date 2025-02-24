#ifndef MAIN_OT_UTILS_H
#define MAIN_OT_UTILS_H

#include <openthread/instance.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>

#ifdef __cplusplus
extern "C" {
#endif

void HandleJoinerCallback(otError aError, void *aContext);
int disable_thread(otInstance *p_instance);
int start_thread_joiner(char *secret);
int enable_thread(void);
void init_srp(void);

// e.g. LOKI0815._ble._loki._coap._udp.local or BR212._ble._loki._coap._udp.local
static otSrpClientService short_name_coap_service;
#define SRP_SHORTNAME_SERVICE  "_ble._loki._coap._udp"
// e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023"._name._loki._coap._udp.local
static otSrpClientService long_name_coap_service;
#define SRP_LONGNAME_SERVICE  "_name._loki._coap._udp"
// e.g. 53._dcc._loki._coap._udp.local
static otSrpClientService dcc_name_coap_service;
#define SRP_DCC_SERVICE  "_dcc._loki._coap._udp"


otSrpClientBuffersServiceEntry *register_service( otInstance *p_instance ,  char *instance_name, char *service_name);

#ifdef __cplusplus
}
#endif

#endif // MAIN_OT_UTILS_H