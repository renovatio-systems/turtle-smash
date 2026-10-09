// Host stub of hardware/dma.h. The "transfer" runs at once in emulator.c.
#ifndef HARDWARE_DMA_H
#define HARDWARE_DMA_H

#include "pico/stdlib.h"

#define DMA_SIZE_16 1

typedef struct {
    int size;
} dma_channel_config;

static inline int dma_claim_unused_channel(bool required) { (void)required; return 0; }
static inline dma_channel_config dma_channel_get_default_config(int ch) { (void)ch; return (dma_channel_config){0}; }
static inline void channel_config_set_transfer_data_size(dma_channel_config *c, int size) { c->size = size; }
static inline void channel_config_set_dreq(dma_channel_config *c, uint dreq) { (void)c; (void)dreq; }
void emu_dma_transfer(const uint16_t *src, uint count);
static inline void dma_channel_configure(int ch, const dma_channel_config *c, volatile void *write_addr,
                                         const void *read_addr, uint count, bool trigger) {
    (void)ch; (void)write_addr;
    if (trigger && c->size == DMA_SIZE_16) emu_dma_transfer(read_addr, count);
}
static inline void dma_channel_wait_for_finish_blocking(int ch) { (void)ch; }

#endif
