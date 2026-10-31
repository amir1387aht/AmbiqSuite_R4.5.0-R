#ifndef DISPLAY_BUS_CONFIG_H
#define DISPLAY_BUS_CONFIG_H

/* Experimental branch: 96 MHz is Apollo4l MSPI's hardware maximum, NOT a
 * validated limit for every Mi Band 8 panel. Use 48 for the known baseline.
 * Pixel writes remain 1-4-4 quad; panel control commands remain serial.
 * Keep the existing board pad strength/slew settings until measured on device.
 */
#ifndef WATCH_DISPLAY_QSPI_MHZ
#define WATCH_DISPLAY_QSPI_MHZ 96
#endif

#if WATCH_DISPLAY_QSPI_MHZ == 96
#define WATCH_DISPLAY_PIXEL_CLOCK AM_HAL_MSPI_CLK_96MHZ
#elif WATCH_DISPLAY_QSPI_MHZ == 48
#define WATCH_DISPLAY_PIXEL_CLOCK AM_HAL_MSPI_CLK_48MHZ
#else
#error "Display QSPI supports 48 MHz baseline or 96 MHz experimental only"
#endif

#define WATCH_DISPLAY_COMMAND_CLOCK AM_HAL_MSPI_CLK_48MHZ
#define WATCH_DISPLAY_READ_CLOCK    AM_HAL_MSPI_CLK_8MHZ

#endif
