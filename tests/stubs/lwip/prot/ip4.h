#pragma once
#include <cstdint>
#define IP_HLEN 20
struct ip_hdr { uint8_t _v_hl; uint8_t _tos; uint16_t _len, _id, _offset; uint8_t _ttl, _proto; uint16_t _chksum; uint32_t src, dest; };
#define IPH_HL(hdr) ((hdr)->_v_hl & 0x0f)
#define IPH_HL_BYTES(hdr) ((uint8_t) (IPH_HL(hdr) * 4))
