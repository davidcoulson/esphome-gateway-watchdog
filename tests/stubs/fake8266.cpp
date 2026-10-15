#include "fake8266.h"

#include <cstring>
#include <vector>

#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/prot/ip4.h"
#include "lwip/raw.h"

static struct netif the_netif{};
struct netif *netif_default = &the_netif;

struct raw_pcb { raw_recv_fn recv; void *arg; };
static raw_pcb *open_pcb = nullptr;

namespace fake8266 {
int pcbs_created = 0, pcbs_removed = 0;
bool pcb_open = false;
int pbufs_live = 0;
int8_t send_err = ERR_OK;
bool fail_alloc = false;
int sends = 0;
Echo last{};

static uint32_t v4(const ip_addr_t &a) {
#if LWIP_IPV6
  return a.u_addr.ip4.addr;
#else
  return a.addr;
#endif
}

int deliver(uint8_t type, uint16_t id, uint16_t seq, uint32_t from) {
  if (open_pcb == nullptr) return -1;
  const uint16_t len = IP_HLEN + sizeof(icmp_echo_hdr) + 32;
  pbuf *p = pbuf_alloc(PBUF_IP, len, PBUF_RAM);
  auto *iph = static_cast<ip_hdr *>(p->payload);
  iph->_v_hl = 0x45;
  iph->_proto = IP_PROTO_ICMP;
  iph->src = from;
  auto *echo = reinterpret_cast<icmp_echo_hdr *>(static_cast<uint8_t *>(p->payload) + IP_HLEN);
  echo->type = type;
  echo->code = 0;
  echo->id = htons(id);
  echo->seqno = htons(seq);
  ip_addr_t addr{};
  ip_addr_set_ip4_u32_val(addr, from);
  const uint8_t eaten = open_pcb->recv(open_pcb->arg, open_pcb, p, &addr);
  // Not consumed: lwIP still owns it and frees it after its own ICMP code.
  if (!eaten) pbuf_free(p);
  return eaten;
}

int reply_to_last() { return deliver(ICMP_ER, last.id, last.seq, v4(last.to)); }

void set_gateway(uint32_t gw) {
  netif_default = &the_netif;
  ip_addr_set_ip4_u32_val(the_netif.gw, gw);
}
void set_no_netif() { netif_default = nullptr; }

void reset() {
  if (open_pcb != nullptr) { delete open_pcb; open_pcb = nullptr; }
  pcbs_created = pcbs_removed = 0;
  pcb_open = false;
  pbufs_live = 0;
  send_err = ERR_OK;
  fail_alloc = false;
  sends = 0;
  last = Echo{};
  set_gateway(0);
}
}  // namespace fake8266

pbuf *pbuf_alloc(pbuf_layer, uint16_t length, pbuf_type) {
  if (fake8266::fail_alloc) return nullptr;
  fake8266::pbufs_live++;
  auto *p = new pbuf{new uint8_t[length](), length, length};
  return p;
}
uint8_t pbuf_free(pbuf *p) {
  fake8266::pbufs_live--;
  delete[] static_cast<uint8_t *>(p->payload);
  delete p;
  return 1;
}
uint16_t pbuf_copy_partial(const pbuf *p, void *dataptr, uint16_t len, uint16_t offset) {
  if (offset >= p->tot_len) return 0;
  const uint16_t n = (p->tot_len - offset) < len ? p->tot_len - offset : len;
  std::memcpy(dataptr, static_cast<const uint8_t *>(p->payload) + offset, n);
  return n;
}

// RFC 1071, as lwIP computes it: the ones' complement of the ones' complement
// sum, returned in network byte order like the header field it fills.
uint16_t inet_chksum(const void *dataptr, uint16_t len) {
  const auto *b = static_cast<const uint8_t *>(dataptr);
  uint32_t sum = 0;
  for (uint16_t i = 0; i + 1 < len; i += 2) sum += (b[i] << 8) | b[i + 1];
  if (len & 1) sum += b[len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return htons(static_cast<uint16_t>(~sum));
}

raw_pcb *raw_new(uint8_t) {
  fake8266::pcbs_created++;
  open_pcb = new raw_pcb{nullptr, nullptr};
  return open_pcb;
}
void raw_remove(raw_pcb *pcb) {
  fake8266::pcbs_removed++;
  if (pcb == open_pcb) { open_pcb = nullptr; fake8266::pcb_open = false; }
  delete pcb;
}
void raw_recv(raw_pcb *pcb, raw_recv_fn recv, void *arg) {
  pcb->recv = recv;
  pcb->arg = arg;
  fake8266::pcb_open = true;
}
err_t raw_sendto(raw_pcb *, pbuf *p, const ip_addr_t *ipaddr) {
  fake8266::sends++;
  auto *echo = static_cast<icmp_echo_hdr *>(p->payload);
  // A correct checksum sums, over the whole message, to 0xffff.
  fake8266::last = {ntohs(echo->id), ntohs(echo->seqno), inet_chksum(p->payload, p->tot_len) == 0,
                    p->tot_len, *ipaddr};
  return fake8266::send_err;
}
