#pragma once
#include <cstdint>
// A single-buffer pbuf: enough for one echo request or reply.
typedef enum { PBUF_IP } pbuf_layer;
typedef enum { PBUF_RAM } pbuf_type;
struct pbuf { void *payload; uint16_t tot_len; uint16_t len; };
struct pbuf *pbuf_alloc(pbuf_layer l, uint16_t length, pbuf_type type);
uint8_t pbuf_free(struct pbuf *p);
uint16_t pbuf_copy_partial(const struct pbuf *p, void *dataptr, uint16_t len, uint16_t offset);
