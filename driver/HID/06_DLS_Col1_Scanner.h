// HID Report Descriptor for Datalogic Scanner (HID POS)
// Based on USB HID Point of Sale Usage Tables 1.02

0x05, 0x8C,        // Usage Page (Bar Code Scanner)
0x09, 0x02,        // Usage (POS Scanner)
0xA1, 0x01,        // Collection (Application)
0x85, 0x01,        //   Report ID (1)

// Scanner data input report
0x09, 0x30,        //   Usage (Data Event)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x95, 0x40,        //   Report Count (64) - 64 byte packets
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

// Scanner control output report
0x09, 0x31,        //   Usage (Scanner Control)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x95, 0x0B,        //   Report Count (11) - 11 byte command packets
0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

// Scanner status feature report
0x09, 0x32,        //   Usage (Scanner Status)
0x15, 0x00,        //   Logical Minimum (0)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x75, 0x08,        //   Report Size (8)
0x95, 0x08,        //   Report Count (8) - 8 byte status
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)

0xC0,              // End Collection
