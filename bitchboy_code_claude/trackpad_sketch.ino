#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>

#define HOST_PIN_DP 1

Adafruit_USBH_Host USBHost;
Adafruit_USBD_HID usb_hid;

// Mouse descriptor with scroll wheel
uint8_t const desc_hid_report[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
  0x09, 0x01,        //   Usage (Pointer)
  0xA1, 0x00,        //   Collection (Physical)
  0x05, 0x09,        //     Usage Page (Button)
  0x19, 0x01,        //     Usage Minimum (1)
  0x29, 0x05,        //     Usage Maximum (5)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x95, 0x05,        //     Report Count (5)
  0x75, 0x01,        //     Report Size (1)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0x95, 0x01,        //     Report Count (1)
  0x75, 0x03,        //     Report Size (3)
  0x81, 0x01,        //     Input (Const)
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x30,        //     Usage (X)
  0x09, 0x31,        //     Usage (Y)
  0x09, 0x38,        //     Usage (Wheel)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x06,        //     Input (Data,Var,Rel)
  0x05, 0x0C,        //     Usage Page (Consumer)
  0x0A, 0x38, 0x02,  //     Usage (AC Pan)
  0x15, 0x81,        //     Logical Minimum (-127)
  0x25, 0x7F,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x06,        //     Input (Data,Var,Rel)
  0xC0,              //   End Collection
  0xC0               // End Collection
};

void setup() {
  Serial.begin(115200);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
  while (!TinyUSBDevice.mounted()) delay(1);
  Serial.println("USB Device ready");
}

void loop() {}

void setup1() {
  delay(1000);
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP;
  pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
  Serial.println("USB Host ready");
}

void loop1() {
  USBHost.task();
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  Serial.println("Trackpad connected");
  tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
  tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  Serial.println("Trackpad disconnected");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  if (!usb_hid.ready()) {
    tuh_hid_receive_report(dev_addr, instance);
    return;
  }
  
  uint8_t report_out[5] = {0, 0, 0, 0, 0};  // buttons, x, y, wheel, pan
  
  if (len == 6 && report[0] == 0x06) {
    // Normal movement + scroll
    // Format: [ID=06] [buttons] [X] [Y] [V-scroll] [H-scroll]
    report_out[0] = report[1];           // buttons
    report_out[1] = report[2];           // X
    report_out[2] = report[3];           // Y
    report_out[3] = (int8_t)report[4];   // vertical scroll
    report_out[4] = (int8_t)report[5];   // horizontal scroll (pan)
    
    usb_hid.sendReport(0, report_out, 5);
  }
  else if (len == 4 && report[0] == 0x08) {
    // Pinch gesture - map to Ctrl+Scroll for zoom
    // For now just forward as button press
    report_out[0] = report[1];
    usb_hid.sendReport(0, report_out, 5);
  }
  
  tuh_hid_receive_report(dev_addr, instance);
}