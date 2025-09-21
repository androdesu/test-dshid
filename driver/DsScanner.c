#include "Driver.h"
#include "DsScanner.tmh"

//
// Scanner enable command as per Datalogic USB-OEM protocol
// 
static const UCHAR SCANNER_ENABLE_COMMAND[11] = {
    17, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

//
// Initialize scanner device
//
NTSTATUS
DsScanner_Initialize(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    DS_SCANNER_CONFIGURATION config = {0};
    
    FuncEntry(TRACE_DSSCANNER);
    
    // Get scanner configuration
    config.IsEnabled = TRUE;
    config.ScannerType = 1; // Datalogic
    config.ReportMode = 1;  // Standard mode
    config.UseComMode = FALSE;  // Set to TRUE to use COM mode instead of HID (COM mode disabled for now)
    config.ComPortNumber = 1;   // COM1 (modify as needed)

    if (config.UseComMode)
    {
        // Initialize COM scanner
        #ifdef INCLUDE_COM_SCANNER
        status = DsScannerCom_Initialize(DeviceContext, config.ComPortNumber);
        if (!NT_SUCCESS(status))
        {
            TraceError(TRACE_DSSCANNER, "DsScannerCom_Initialize failed with status %!STATUS!", status);
            goto exit;
        }
        
        status = DsScannerCom_Start(DeviceContext);
        if (!NT_SUCCESS(status))
        {
            TraceError(TRACE_DSSCANNER, "DsScannerCom_Start failed with status %!STATUS!", status);
            goto exit;
        }
        
        TraceInformation(TRACE_DSSCANNER, "COM Scanner initialized on COM%d", config.ComPortNumber);
        #else
        TraceWarning(TRACE_DSSCANNER, "COM scanner support not compiled in");
        status = STATUS_NOT_SUPPORTED;
        goto exit;
        #endif
    }
    else
    {
        // Initialize HID scanner (original implementation)
        status = DsScanner_SendCommand(DeviceContext, (PUCHAR)SCANNER_ENABLE_COMMAND, sizeof(SCANNER_ENABLE_COMMAND));
        if (!NT_SUCCESS(status))
        {
            TraceError(TRACE_DSSCANNER, "DsScanner_SendCommand failed with status %!STATUS!", status);
            goto exit;
        }

        TraceInformation(TRACE_DSSCANNER, "HID Scanner initialized successfully");
    }

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Process incoming scanner data
//
NTSTATUS
DsScanner_ProcessInputData(
    _In_ PDEVICE_CONTEXT Context,
    _In_ PUCHAR Buffer,
    _In_ ULONG BufferLength
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PSCANNER_DATA_PACKET packet;
    
    FuncEntry(TRACE_DSSCANNER);
    
    if (BufferLength < sizeof(SCANNER_DATA_PACKET))
    {
        TraceError(
            TRACE_DSSCANNER,
            "Invalid buffer length %lu, expected %lu",
            BufferLength,
            sizeof(SCANNER_DATA_PACKET)
        );
        return STATUS_INVALID_BUFFER_SIZE;
    }
    
    packet = (PSCANNER_DATA_PACKET)Buffer;
    
    TraceInformation(
        TRACE_DSSCANNER,
        "Received scanner data: PayloadLength=%d, Status=[%02X,%02X,%02X]",
        packet->PayloadLength,
        packet->StatusByte0,
        packet->StatusByte1,
        packet->StatusByte2
    );
    
    // Process barcode data if payload length is valid
    if (packet->PayloadLength > 4 && packet->PayloadLength <= SCANNER_PACKET_SIZE)
    {
        ULONG barcodeDataLength = packet->PayloadLength - 4; // Subtract status bytes
        
        TraceInformation(
            TRACE_DSSCANNER,
            "Barcode data received, length=%lu",
            barcodeDataLength
        );
        
        // Log first few bytes of barcode data for debugging
        if (barcodeDataLength > 0)
        {
            TraceInformation(
                TRACE_DSSCANNER,
                "Barcode data preview: %02X %02X %02X %02X...",
                packet->BarcodeData[0],
                barcodeDataLength > 1 ? packet->BarcodeData[1] : 0,
                barcodeDataLength > 2 ? packet->BarcodeData[2] : 0,
                barcodeDataLength > 3 ? packet->BarcodeData[3] : 0
            );
        }
    }
    
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    
    return status;
}

//
// Send command to scanner
//
NTSTATUS
DsScanner_SendCommand(
    _In_ PDEVICE_CONTEXT Context,
    _In_ PUCHAR Command,
    _In_ ULONG CommandLength
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_MEMORY_DESCRIPTOR memDesc;
    
    FuncEntry(TRACE_DSSCANNER);
    
    if (CommandLength > SCANNER_COMMAND_SIZE)
    {
        TraceError(
            TRACE_DSSCANNER,
            "Command length %lu exceeds maximum %d",
            CommandLength,
            SCANNER_COMMAND_SIZE
        );
        return STATUS_INVALID_PARAMETER;
    }
    
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &memDesc,
        Command,
        CommandLength
    );
    
    status = USB_WriteInterruptOutSync(Context, &memDesc);
    
    if (!NT_SUCCESS(status))
    {
        TraceError(
            TRACE_DSSCANNER,
            "Failed to send scanner command with status %!STATUS!",
            status
        );
    }
    
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    
    return status;
}
