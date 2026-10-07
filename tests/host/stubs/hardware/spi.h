// Host stub of hardware/spi.h. Implemented in emulator.c.
#ifndef HARDWARE_SPI_H
#define HARDWARE_SPI_H

#include "pico/stdlib.h"

typedef struct spi_inst spi_inst_t;
extern spi_inst_t *const spi0;

uint spi_init(spi_inst_t *spi, uint baudrate);
uint spi_set_baudrate(spi_inst_t *spi, uint baudrate);
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);
int spi_read_blocking(spi_inst_t *spi, uint8_t repeated_tx, uint8_t *dst, size_t len);

#endif
