/*
 * TinyUSB Configuration for BitchBoy
 *
 * Composite USB Device: MIDI + HID Mouse
 * USB Host via PIO: HID (Trackpad)
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// BOARD CONFIGURATION
//--------------------------------------------------------------------

// RHPort for device (native USB)
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort for host (PIO USB)
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      1
#endif

#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_FULL_SPEED

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENABLED
#define CFG_TUD_ENABLED       1
#endif

#ifndef CFG_TUD_MAX_SPEED
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// Device classes
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI          1
#endif

#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE   64
#endif

#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE   64
#endif

#ifndef CFG_TUD_HID
#define CFG_TUD_HID           1
#endif

#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE    16
#endif

#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC           1
#endif

#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE    256
#endif

#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE    256
#endif

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUH_ENABLED
#define CFG_TUH_ENABLED       1
#endif

#ifndef CFG_TUH_MAX_SPEED
#define CFG_TUH_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX    1
#endif

// Disable hub for simplicity
#ifndef CFG_TUH_HUB
#define CFG_TUH_HUB           0
#endif

// HID Host for trackpad
#ifndef CFG_TUH_HID
#define CFG_TUH_HID           4
#endif

#ifndef CFG_TUH_HID_EPIN_BUFSIZE
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#endif

#ifndef CFG_TUH_HID_EPOUT_BUFSIZE
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#endif

// Disable unused host classes
#ifndef CFG_TUH_CDC
#define CFG_TUH_CDC           0
#endif

#ifndef CFG_TUH_MSC
#define CFG_TUH_MSC           0
#endif

#ifndef CFG_TUH_VENDOR
#define CFG_TUH_VENDOR        0
#endif

//--------------------------------------------------------------------
// PIO USB CONFIGURATION
//--------------------------------------------------------------------

#ifndef CFG_TUH_RPI_PIO_USB
#define CFG_TUH_RPI_PIO_USB   1
#endif

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
