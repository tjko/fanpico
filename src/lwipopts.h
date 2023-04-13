#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include "fanpico-compile.h"

// Settings for FanPico when using Pico W...
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

// allow override in some examples
#ifndef NO_SYS
#define NO_SYS                      1
#endif
// allow override in some examples
#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif
#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC             1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC             0
#endif
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    32768
#define MEM_SANITY_CHECK            1
#define MEM_OVERFLOW_CHECK          1
#define MEMP_NUM_TCP_SEG            48
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_UDP_PCB            8
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (12 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define MEM_STATS                   1
#define SYS_STATS                   1
#define MEMP_STATS                  1
#define LINK_STATS                  0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_DHCP_GET_NTP_SRV       1

#define SNTP_GET_SERVERS_FROM_DHCP      1
#define SNTP_STARTUP_DELAY              1
#define SNTP_STARTUP_DELAY_FUNC         (5000 + LWIP_RAND() % 4000)
//#define SNTP_MAX_SERVERS              2
void pico_set_system_time(long int sec);
#define SNTP_SET_SYSTEM_TIME(sec)       pico_set_system_time(sec)

#define LWIP_HOOK_FILENAME              "lwip_hooks.h"
#define LWIP_HOOK_DHCP_APPEND_OPTIONS   pico_dhcp_option_add_hook
#define LWIP_HOOK_DHCP_PARSE_OPTION     pico_dhcp_option_parse_hook

#define HTTPD_FSDATA_FILE               "fanpico_fsdata.c"
#define HTTPD_USE_MEM_POOL              0
#define LWIP_HTTPD_SSI                  1
#define LWIP_HTTPD_SSI_RAW              1
#define LWIP_HTTPD_SSI_MULTIPART        1
#define LWIP_HTTPD_SSI_INCLUDE_TAG      0
#define LWIP_HTTPD_SSI_EXTENSIONS       ".shtml", ".xml", ".json", ".csv"

#if TLS_SUPPORT
#define HTTPD_ENABLE_HTTPS              1
#define LWIP_ALTCP                      1
#define LWIP_ALTCP_TLS                  1
#define LWIP_ALTCP_TLS_MBEDTLS          1
#endif

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#define LWIP_DEBUG_TIMERNAMES       1
#endif

#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_ON
#define MEMP_DEBUG                  LWIP_DBG_ON
#define SYS_DEBUG                   LWIP_DBG_ON
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF
#define SNTP_DEBUG                  LWIP_DBG_OFF
#define HTTPD_DEBUG                 LWIP_DBG_OFF
#define ALTCP_MBEDTLS_DEBUG         LWIP_DBG_ON
#define ALTCP_MBEDTLS_MEM_DEBUG     LWIP_DBG_ON
#define ALTCP_MBEDTLS_LIB_DEBUG     LWIP_DBG_ON
#define TIMERS_DEBUG                LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
