// Host stub of hardware/spi.h. Implemented in emulator.c.
#ifndef HARDWARE_SPI_H
#define HARDWARE_SPI_H

#include "pico/stdlib.h"

typedef struct spi_inst spi_inst_t;
extern spi_inst_t *const spi0;

#define SPI_CPOL_0 0
#define SPI_CPHA_0 0
#define SPI_MSB_FIRST 0
#define SPI_SSPICR_RORIC_BITS 2
typedef struct {
    uint32_t dr, icr;
} spi_hw_t;
static inline spi_hw_t *spi_get_hw(spi_inst_t *spi) { static spi_hw_t hw; (void)spi; return &hw; }
static inline bool spi_is_busy(spi_inst_t *spi) { (void)spi; return false; }
static inline bool spi_is_readable(spi_inst_t *spi) { (void)spi; return false; }
static inline uint spi_get_dreq(spi_inst_t *spi, bool is_tx) { (void)spi; (void)is_tx; return 0; }
void spi_set_format(spi_inst_t *spi, uint data_bits, int cpol, int cpha, int order);

uint spi_init(spi_inst_t *spi, uint baudrate);
uint spi_set_baudrate(spi_inst_t *spi, uint baudrate);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx, uint8_t *dst, size_t len);

#endif
