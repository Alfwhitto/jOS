#ifndef NET_H
#define NET_H

#include <stdint.h>

int net_init(void);
void net_poll(void);
int net_ping(const char *target_ip_string);
int net_ping6(const char *target_ip_string);
void net_handle_frame(const uint8_t *frame, uint32_t length);
const uint8_t* net_get_local_mac(void);
const uint8_t* net_get_local_ip(void);
const uint8_t* net_get_local_ipv6(void);

#endif
