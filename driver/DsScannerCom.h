#pragma once

#include "Driver.h"

//
// COM Scanner Protocol Definitions
//
#define DS_SCANNER_COM_BUFFER_SIZE      256
#define DS_SCANNER_COM_TIMEOUT_MS       1000
#define DS_SCANNER_COM_BAUD_RATE        9600

//
// COM Scanner Commands
//
#define DS_SCANNER_COM_ENABLE_CMD       "ENABLE\r\n"
#define DS_SCANNER_COM_DISABLE_CMD      "DISABLE\r\n"
#define DS_SCANNER_COM_STATUS_CMD       "STATUS\r\n"

//
// COM Scanner Response Patterns
//
#define DS_SCANNER_COM_ACK              "ACK"
#define DS_SCANNER_COM_NAK              "NAK"
#define DS_SCANNER_COM_DATA_PREFIX      "DATA:"

//
// COM Scanner Context
//
typedef struct _DS_SCANNER_COM_CONTEXT
{
    WDFDEVICE Device;
    WDFIOTARGET ComPortTarget;
    WDFTIMER PollingTimer;
    WDFSPINLOCK Lock;
    
    BOOLEAN IsEnabled;
    BOOLEAN IsConnected;
    ULONG ComPortNumber;
    
    UCHAR InputBuffer[DS_SCANNER_COM_BUFFER_SIZE];
    UCHAR OutputBuffer[DS_SCANNER_COM_BUFFER_SIZE];
    
    // Statistics
    ULONG TotalScansReceived;
    ULONG TotalErrors;
    
} DS_SCANNER_COM_CONTEXT, *PDS_SCANNER_COM_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DS_SCANNER_COM_CONTEXT, DsScannerComGetContext)

//
// Function Declarations
//

NTSTATUS
DsScannerCom_Initialize(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG ComPortNumber
);

NTSTATUS
DsScannerCom_Start(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
DsScannerCom_Stop(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
DsScannerCom_SendCommand(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PCSTR Command,
    _Out_opt_ PSTR Response,
    _In_ ULONG ResponseSize
);

NTSTATUS
DsScannerCom_ProcessData(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PUCHAR Data,
    _In_ ULONG DataLength
);

EVT_WDF_TIMER DsScannerCom_PollingTimer;
EVT_WDF_REQUEST_COMPLETION_ROUTINE DsScannerCom_ReadComplete;
EVT_WDF_REQUEST_COMPLETION_ROUTINE DsScannerCom_WriteComplete;

//
// Helper Functions
//

NTSTATUS
DsScannerCom_OpenComPort(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG ComPortNumber
);

VOID
DsScannerCom_CloseComPort(
    _In_ PDEVICE_CONTEXT DeviceContext
);

NTSTATUS
DsScannerCom_ConfigurePort(
    _In_ PDEVICE_CONTEXT DeviceContext
);

BOOLEAN
DsScannerCom_ParseBarcodeData(
    _In_ PUCHAR RawData,
    _In_ ULONG DataLength,
    _Out_ PUCHAR BarcodeData,
    _Out_ PULONG BarcodeLength,
    _Out_ PULONG BarcodeType
);
