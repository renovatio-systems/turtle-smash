#ifndef CONFIG_H
#define CONFIG_H

// Wiring for the "RP2040 USB" stick board: SPI0 on the GP0-GP8 edge.
// See README.md for the full table. CS/DC/RST can be any GPIO; SCK/MOSI/MISO
// must be a valid SPI0 group (SCK 2/6/18/22, TX 3/7/19/23, RX 0/4/16/20).
#define TFT_SPI  spi0
#define PIN_SCK  2  // display SCK      (pin 7)
#define PIN_MOSI 3  // display SDI/MOSI (pin 6)
#define PIN_MISO 4  // display SDO/MISO (pin 9)
#define PIN_CS   5  // display CS       (pin 3)
#define PIN_DC   6  // display DC/RS    (pin 5)
#define PIN_RST  7  // display RESET    (pin 4)
#define PIN_BL   -1 // display LED (pin 8) tied to 3V3; use 8 to switch it from GP8

#define SPI_BAUD (40 * 1000 * 1000) // gives 31.25 MHz with the 125 MHz default clock

#endif
