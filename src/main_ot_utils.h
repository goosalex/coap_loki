#ifndef MAIN_OT_UTILS_H
#define MAIN_OT_UTILS_H

#include <openthread/instance.h>
#include <openthread/srp_client.h>
#include <openthread/srp_client_buffers.h>
#include <openthread/udp.h>



void HandleJoinerCallback(otError aError, void *aContext);
int disable_thread(otInstance *p_instance);
int start_thread_joiner(char *secret);
int enable_thread(void);
void init_srp(void);

/* SRP buffer entries are POINTERS into the OpenThread SRP client buffer pool,
 * defined once in main_ot_utils.c. Pointers (not value-copies) are required so
 * that otSrpClientBuffersFreeService() receives the original pool address —
 * see TODO/01 §1.6 (per-TU duplicates) and §1.7 (snapshot vs handle). NULL
 * means "no current registration"; check that, not the inner mInstanceName. */
// e.g. LOKI0815._ble._loki._coap._udp.local or BR212._ble._loki._coap._udp.local
extern otSrpClientBuffersServiceEntry *short_name_coap_service;
#define SRP_SHORTNAME_SERVICE  "_ble._loki_coap._udp"
// e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023"._name._loki._coap._udp.local
extern otSrpClientBuffersServiceEntry *long_name_coap_service;
#define SRP_LONGNAME_SERVICE  "_name._loki_coap._udp"
// e.g. 53._dcc._loki._coap._udp.local
extern otSrpClientBuffersServiceEntry *dcc_name_coap_service;
#define SRP_DCC_SERVICE  "_dcc._loki_dcc._udp"
extern otSrpClientBuffersServiceEntry *loconet_udp_service;
#define SRP_LCN_SERVICE  "_loconet._loki_loconet._udp"
#define SRP_LCN_PORT  1234

extern otUdpSocket loconet_udp_socket;

otSrpClientBuffersServiceEntry *register_service( otInstance *p_instance ,  char *instance_name, char *service_name, int port);
otSrpClientBuffersServiceEntry *register_coap_service( otInstance *p_instance ,  char *instance_name, char *service_name);
/* re_register_*: takes a pointer to the caller's entry-pointer so it can free
 * the old pool entry, allocate a new one, and rebind the caller's variable to
 * the new pointer. Callers continue to pass `&long_name_coap_service` etc. */
int re_register_coap_service( otInstance *p_instance ,  otSrpClientBuffersServiceEntry **entry, char *instance_name, char *service_name) ;
int re_register_service( otInstance *p_instance ,  otSrpClientBuffersServiceEntry **entry, char *instance_name, char *service_name, int port);
int bindUdpHandler(otInstance *aInstance, otUdpSocket *aSocket, uint16_t port, otUdpReceive aHandler);


#endif /* MAIN_OT_UTILS_H */
