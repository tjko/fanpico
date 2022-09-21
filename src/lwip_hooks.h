#ifndef _LWIP_HOOKS_H
#define _LWIP_HOOKS_H

#include "lwip/arch.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"


void pico_dhcp_option_add_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
			u8_t msg_type, u16_t *options_len_ptr);

void pico_dhcp_option_parse_hook(struct netif *netif, struct dhcp *dhcp, u8_t state, struct dhcp_msg *msg,
				u8_t msg_type, u8_t option, u8_t option_len, struct pbuf *pbuf, u16_t option_value_offset);

#endif /* _LWIP_HOOKS_H */
