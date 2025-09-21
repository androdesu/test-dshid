#pragma once

//
// Datalogic Scanner specific functions and definitions
//

//
// Scanner device initialization
//
NTSTATUS
DsScanner_Initialize(
    _In_ PDEVICE_CONTEXT Context
);

//
// Process incoming scanner data
//
NTSTATUS
DsScanner_ProcessInputData(
    _In_ PDEVICE_CONTEXT Context,
    _In_ PUCHAR Buffer,
    _In_ ULONG BufferLength
);

//
// Send command to scanner
//
NTSTATUS
DsScanner_SendCommand(
    _In_ PDEVICE_CONTEXT Context,
    _In_ PUCHAR Command,
    _In_ ULONG CommandLength
);

//
// Scanner-specific constants
//
#define SCANNER_PACKET_SIZE         64
#define SCANNER_COMMAND_SIZE        11
#define SCANNER_STATUS_SIZE         8

//
// Barcode type codes (examples)
//
#define BARCODE_TYPE_QR_CODE        { 0x00, 0x33, 0x0B }
#define BARCODE_TYPE_CODE128        { 0x00, 0x18, 0x0B }
#define BARCODE_TYPE_CODE39         { 0x00, 0x01, 0x0B }
#define BARCODE_TYPE_EAN13          { 0x00, 0x0C, 0x0B }

//
// Scanner Configuration
//
typedef struct _DS_SCANNER_CONFIGURATION
{
    BOOLEAN IsEnabled;
    ULONG ScannerType;
    ULONG ReportMode;
    BOOLEAN UseComMode;      // TRUE for COM mode, FALSE for HID mode
    ULONG ComPortNumber;     // COM port number (1-255) when UseComMode is TRUE
} DS_SCANNER_CONFIGURATION, *PDS_SCANNER_CONFIGURATION;

//
// Scanner data packet structure
//
typedef struct _SCANNER_DATA_PACKET
{
    UCHAR PayloadLength;        // Byte 0: length of payload
    UCHAR StatusByte0;          // Byte 1: status byte 0
    UCHAR StatusByte1;          // Byte 2: status byte 1
    UCHAR StatusByte2;          // Byte 3: status byte 2
    UCHAR BarcodeData[60];      // Byte 4-63: barcode data + type code + padding
} SCANNER_DATA_PACKET, *PSCANNER_DATA_PACKET;
