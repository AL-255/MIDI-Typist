#ifndef MIDI_TYPIST_AT32_CONF_H
#define MIDI_TYPIST_AT32_CONF_H
#include "defaults.h"
#define HICK_VALUE 8000000u
#define HEXT_STARTUP_TIMEOUT AT32_CLOCK_WAIT_LOOPS
/* The official SDK compiles only these peripheral modules. */
#define ADC_MODULE_ENABLED
#define CRM_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define ERTC_MODULE_ENABLED
#define EXINT_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define SPI_MODULE_ENABLED
#define TMR_MODULE_ENABLED
#define USB_MODULE_ENABLED
#include "at32f402_405_adc.h"
#include "at32f402_405_crm.h"
#include "at32f402_405_flash.h"
#include "at32f402_405_dma.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_pwc.h"
#include "at32f402_405_ertc.h"
#include "at32f402_405_exint.h"
#include "at32f402_405_spi.h"
#include "at32f402_405_tmr.h"
#include "at32f402_405_usb.h"
#endif
