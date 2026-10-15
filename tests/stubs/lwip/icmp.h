#pragma once
#include <arpa/inet.h>
#include <cstdint>
#define ICMP_ER 0
#define ICMP_ECHO 8
#define IP_PROTO_ICMP 1
struct icmp_echo_hdr { uint8_t type; uint8_t code; uint16_t chksum; uint16_t id; uint16_t seqno; };
#define ICMPH_TYPE(h) ((h)->type)
#define ICMPH_TYPE_SET(h, t) ((h)->type = (t))
#define ICMPH_CODE_SET(h, c) ((h)->code = (c))
#define lwip_htons(x) htons(x)
#define lwip_ntohs(x) ntohs(x)
