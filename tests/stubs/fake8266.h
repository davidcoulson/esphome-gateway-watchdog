// The ESP8266 side of the fakes: a raw lwIP socket and the default netif.
// Tests read what the component sent and hand it ICMP packets to receive.
#pragma once
#include <cstdint>

#include "lwip/ip_addr.h"

namespace fake8266 {
struct Echo { uint16_t id, seq; bool checksum_ok; uint16_t len; ip_addr_t to; };

extern int pcbs_created, pcbs_removed;
extern bool pcb_open;                     // a raw pcb exists and has a receive callback
extern int pbufs_live;                    // allocated and not yet freed
extern int8_t send_err;                   // what raw_sendto() returns
extern bool fail_alloc;                   // pbuf_alloc() returns nullptr
extern int sends;
extern Echo last;                         // the most recent echo request sent

// Hand the open pcb an ICMP packet from `from` (IPv4, lwIP byte order).
// Returns what the receive callback did: 1 consumed, 0 passed on, -1 no pcb.
int deliver(uint8_t type, uint16_t id, uint16_t seq, uint32_t from);
// A reply to the last request sent, from where it was sent.
int reply_to_last();

void set_gateway(uint32_t gw);            // netif_default's gateway; 0 = none
void set_no_netif();
void reset();
}  // namespace fake8266
