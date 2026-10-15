#pragma once
#include <cstdint>
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
typedef int8_t err_t;
#define ERR_OK 0
#define ERR_RTE -4
struct raw_pcb;
typedef uint8_t (*raw_recv_fn)(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);
struct raw_pcb *raw_new(uint8_t proto);
void raw_remove(struct raw_pcb *pcb);
void raw_recv(struct raw_pcb *pcb, raw_recv_fn recv, void *recv_arg);
err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *ipaddr);
