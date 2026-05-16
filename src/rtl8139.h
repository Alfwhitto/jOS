#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

int rtl8139_init(void);
void rtl8139_poll(void);
int rtl8139_send(const void *data, uint32_t length);
const uint8_t* rtl8139_get_mac(void);
int rtl8139_is_ready(void);

#endif
