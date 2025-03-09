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
static otSrpClientBuffersServiceEntry short_name_coap_service;
#define SRP_SHORTNAME_SERVICE  "_ble._loki_coap._udp"
// e.g. "Keihan Otsu Line Type 700 [Sound! Euphonium] Wrapping Train 2023"._name._loki._coap._udp.local
static otSrpClientBuffersServiceEntry long_name_coap_service;
#define SRP_LONGNAME_SERVICE  "_name._loki_coap._udp"
// e.g. 53._dcc._loki._coap._udp.local
static otSrpClientBuffersServiceEntry dcc_name_coap_service;
#define SRP_DCC_SERVICE  "_dcc._loki_dcc._udp"
static otSrpClientBuffersServiceEntry loconet_udp_service;
#define SRP_LCN_SERVICE  "_loconet._loki_loconet._udp"
#define SRP_LCN_PORT  1234

otUdpSocket loconet_udp_socket;

otSrpClientBuffersServiceEntry *register_service( otInstance *p_instance ,  char *instance_name, char *service_name, int port);
otSrpClientBuffersServiceEntry *register_coap_service( otInstance *p_instance ,  char *instance_name, char *service_name);
int re_register_coap_service( otInstance *p_instance ,  otSrpClientBuffersServiceEntry *entry, char *instance_name, char *service_name) ;
int re_register_service( otInstance *p_instance ,  otSrpClientBuffersServiceEntry *entry, char *instance_name, char *service_name, int port);
int bindUdpHandler(otInstance *aInstance, otUdpSocket *aSocket, uint16_t port, otUdpReceive aHandler);



#ifdef __cplusplus
}
#endif

#endif // MAIN_OT_UTILS_H