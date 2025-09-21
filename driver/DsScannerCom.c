#include "DsScannerCom.h"

#ifndef WIN32_NO_STATUS
#define WIN32_NO_STATUS
#pragma warning(push)
#pragma warning(disable: 4005)  // Disable macro redefinition warnings
#include <ntddser.h>  // For serial port definitions
#pragma warning(pop)
#undef WIN32_NO_STATUS
#else
#pragma warning(push)
#pragma warning(disable: 4005)  // Disable macro redefinition warnings
#include <ntddser.h>  // For serial port definitions
#pragma warning(pop)
#endif

#include "DsScannerCom.tmh"

//
// Scanner HID Input Report Structure (for COM mode)
//
typedef struct _DS_SCANNER_HID_INPUT_REPORT
{
    UCHAR ReportId;
    UCHAR PayloadLength;
    UCHAR BarcodeData[58];
    UCHAR BarcodeType[3];
    UCHAR Reserved;
} DS_SCANNER_HID_INPUT_REPORT, *PDS_SCANNER_HID_INPUT_REPORT;

//
// Initialize COM Scanner
//
NTSTATUS
DsScannerCom_Initialize(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG ComPortNumber
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES attributes;
    PDS_SCANNER_COM_CONTEXT comContext;

    FuncEntry(TRACE_DSSCANNER);

    // Get WDF device object
    WDFDEVICE device = WdfObjectContextGetObject(DeviceContext);
    
    // Allocate COM context
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DS_SCANNER_COM_CONTEXT);
    status = WdfObjectAllocateContext(device, &attributes, (PVOID*)&comContext);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfObjectAllocateContext failed with status %!STATUS!", status);
        goto exit;
    }

    // Initialize COM context
    comContext->Device = device;
    comContext->ComPortNumber = ComPortNumber;
    comContext->IsEnabled = FALSE;
    comContext->IsConnected = FALSE;
    comContext->TotalScansReceived = 0;
    comContext->TotalErrors = 0;

    // Create spinlock for synchronization
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &comContext->Lock);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfSpinLockCreate failed with status %!STATUS!", status);
        goto exit;
    }

    // Create polling timer
    WDF_TIMER_CONFIG_INIT(&timerConfig, DsScannerCom_PollingTimer);
    timerConfig.Period = 100; // Poll every 100ms
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfTimerCreate(&timerConfig, &attributes, &comContext->PollingTimer);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfTimerCreate failed with status %!STATUS!", status);
        goto exit;
    }

    // Open COM port
    status = DsScannerCom_OpenComPort(DeviceContext, ComPortNumber);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "DsScannerCom_OpenComPort failed with status %!STATUS!", status);
        goto exit;
    }

    TraceInformation(TRACE_DSSCANNER, "COM Scanner initialized on COM%d", ComPortNumber);

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Start COM Scanner
//
NTSTATUS
DsScannerCom_Start(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDS_SCANNER_COM_CONTEXT comContext;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext == NULL)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    // Send enable command
    status = DsScannerCom_SendCommand(DeviceContext, DS_SCANNER_COM_ENABLE_CMD, NULL, 0);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "Failed to enable COM scanner with status %!STATUS!", status);
        goto exit;
    }

    // Start polling timer
    WdfTimerStart(comContext->PollingTimer, WDF_REL_TIMEOUT_IN_MS(100));
    
    comContext->IsEnabled = TRUE;
    TraceInformation(TRACE_DSSCANNER, "COM Scanner started");

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Stop COM Scanner
//
NTSTATUS
DsScannerCom_Stop(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDS_SCANNER_COM_CONTEXT comContext;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext == NULL)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    // Stop polling timer
    WdfTimerStop(comContext->PollingTimer, TRUE);

    // Send disable command
    status = DsScannerCom_SendCommand(DeviceContext, DS_SCANNER_COM_DISABLE_CMD, NULL, 0);
    if (!NT_SUCCESS(status))
    {
        TraceWarning(TRACE_DSSCANNER, "Failed to disable COM scanner with status %!STATUS!", status);
    }

    comContext->IsEnabled = FALSE;
    TraceInformation(TRACE_DSSCANNER, "COM Scanner stopped");

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Send Command to COM Scanner
//
NTSTATUS
DsScannerCom_SendCommand(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PCSTR Command,
    _Out_opt_ PSTR Response,
    _In_ ULONG ResponseSize
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDS_SCANNER_COM_CONTEXT comContext;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    WDFREQUEST request;
    WDFMEMORY memory;
    WDF_OBJECT_ATTRIBUTES attributes;
    size_t commandLength;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext == NULL || comContext->ComPortTarget == NULL)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    commandLength = strlen(Command);
    
    // Create request
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfRequestCreate(&attributes, comContext->ComPortTarget, &request);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfRequestCreate failed with status %!STATUS!", status);
        goto exit;
    }

    // Create memory for command
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = request;
    status = WdfMemoryCreate(&attributes, NonPagedPool, 0, commandLength, &memory, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfMemoryCreate failed with status %!STATUS!", status);
        goto cleanup;
    }

    // Copy command to memory
    RtlCopyMemory(WdfMemoryGetBuffer(memory, NULL), Command, commandLength);

    // Format request for write
    status = WdfIoTargetFormatRequestForWrite(comContext->ComPortTarget, request, memory, NULL, NULL);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfIoTargetFormatRequestForWrite failed with status %!STATUS!", status);
        goto cleanup;
    }

    // Send request synchronously
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_MS(DS_SCANNER_COM_TIMEOUT_MS));
    
    if (!WdfRequestSend(request, comContext->ComPortTarget, &sendOptions))
    {
        status = WdfRequestGetStatus(request);
        TraceError(TRACE_DSSCANNER, "WdfRequestSend failed with status %!STATUS!", status);
        goto cleanup;
    }

    status = WdfRequestGetStatus(request);
    TraceVerbose(TRACE_DSSCANNER, "Command sent: %s, status: %!STATUS!", Command, status);

cleanup:
    WdfObjectDelete(request);

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Polling Timer Callback
//
VOID
DsScannerCom_PollingTimer(
    _In_ WDFTIMER Timer
)
{
    WDFDEVICE device = WdfTimerGetParentObject(Timer);
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);
    PDS_SCANNER_COM_CONTEXT comContext = DsScannerComGetContext(device);
    NTSTATUS status;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    WDFREQUEST request;
    WDFMEMORY memory;
    WDF_OBJECT_ATTRIBUTES attributes;

    if (!comContext->IsEnabled || comContext->ComPortTarget == NULL)
    {
        return;
    }

    // Create read request
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfRequestCreate(&attributes, comContext->ComPortTarget, &request);
    if (!NT_SUCCESS(status))
    {
        return;
    }

    // Create memory for read buffer
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = request;
    status = WdfMemoryCreate(&attributes, NonPagedPool, 0, DS_SCANNER_COM_BUFFER_SIZE, &memory, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(request);
        return;
    }

    // Format request for read
    status = WdfIoTargetFormatRequestForRead(comContext->ComPortTarget, request, memory, NULL, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(request);
        return;
    }

    // Set completion routine
    WdfRequestSetCompletionRoutine(request, DsScannerCom_ReadComplete, deviceContext);

    // Send request asynchronously
    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_MS(100));
    
    if (!WdfRequestSend(request, comContext->ComPortTarget, &sendOptions))
    {
        WdfObjectDelete(request);
    }
}

//
// Read Completion Routine
//
VOID
DsScannerCom_ReadComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
)
{
    PDEVICE_CONTEXT deviceContext = (PDEVICE_CONTEXT)Context;
    PDS_SCANNER_COM_CONTEXT comContext = DsScannerComGetContext(WdfObjectContextGetObject(deviceContext));
    NTSTATUS status = Params->IoStatus.Status;
    ULONG bytesRead = (ULONG)Params->IoStatus.Information;
    PUCHAR buffer;
    WDFMEMORY memory;

    UNREFERENCED_PARAMETER(Target);

    if (NT_SUCCESS(status) && bytesRead > 0)
    {
        // Get the read buffer
        memory = Params->Parameters.Read.Buffer;
        buffer = (PUCHAR)WdfMemoryGetBuffer(memory, NULL);

        // Process the received data
        DsScannerCom_ProcessData(deviceContext, buffer, bytesRead);
        
        comContext->TotalScansReceived++;
    }
    else if (status != STATUS_TIMEOUT)
    {
        comContext->TotalErrors++;
        TraceWarning(TRACE_DSSCANNER, "COM read failed with status %!STATUS!", status);
    }

    // Clean up request
    WdfObjectDelete(Request);
}

//
// Process COM Data
//
NTSTATUS
DsScannerCom_ProcessData(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ PUCHAR Data,
    _In_ ULONG DataLength
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UCHAR barcodeData[DS_SCANNER_COM_BUFFER_SIZE];
    ULONG barcodeLength = 0;
    ULONG barcodeType = 0;
    DS_SCANNER_HID_INPUT_REPORT hidReport;

    FuncEntry(TRACE_DSSCANNER);

    // Parse barcode data from COM input
    if (!DsScannerCom_ParseBarcodeData(Data, DataLength, barcodeData, &barcodeLength, &barcodeType))
    {
        TraceWarning(TRACE_DSSCANNER, "Failed to parse barcode data");
        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    // Convert to HID format
    RtlZeroMemory(&hidReport, sizeof(hidReport));
    hidReport.ReportId = 1;
    hidReport.PayloadLength = (UCHAR)min(barcodeLength, sizeof(hidReport.BarcodeData));
    RtlCopyMemory(hidReport.BarcodeData, barcodeData, hidReport.PayloadLength);
    
    // Set barcode type
    hidReport.BarcodeType[0] = (UCHAR)(barcodeType & 0xFF);
    hidReport.BarcodeType[1] = (UCHAR)((barcodeType >> 8) & 0xFF);
    hidReport.BarcodeType[2] = (UCHAR)((barcodeType >> 16) & 0xFF);

    // Send to HID processing
    status = DsScanner_ProcessInputData(DeviceContext, (PUCHAR)&hidReport, sizeof(hidReport));
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "DsScanner_ProcessInputData failed with status %!STATUS!", status);
        goto exit;
    }

    TraceInformation(TRACE_DSSCANNER, "Processed COM barcode data: %d bytes, type: 0x%X", barcodeLength, barcodeType);

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Open COM Port
//
NTSTATUS
DsScannerCom_OpenComPort(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG ComPortNumber
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDS_SCANNER_COM_CONTEXT comContext;
    UNICODE_STRING comPortName;
    WCHAR comPortNameBuffer[32];
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    WDF_OBJECT_ATTRIBUTES attributes;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext == NULL)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    // Format COM port name (e.g., "\Device\Serial0")
    // Use swprintf_s which is available in kernel mode
    if (swprintf_s(comPortNameBuffer, ARRAYSIZE(comPortNameBuffer), L"\\Device\\Serial%d", ComPortNumber - 1) < 0)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        TraceError(TRACE_DSSCANNER, "swprintf_s failed for COM port %d", ComPortNumber);
        goto exit;
    }

    RtlInitUnicodeString(&comPortName, comPortNameBuffer);

    // Create IO target for COM port
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = WdfObjectContextGetObject(DeviceContext);
    status = WdfIoTargetCreate(WdfObjectContextGetObject(DeviceContext), &attributes, &comContext->ComPortTarget);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfIoTargetCreate failed with status %!STATUS!", status);
        goto exit;
    }

    // Open COM port
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &comPortName, GENERIC_READ | GENERIC_WRITE);
    openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
    
    status = WdfIoTargetOpen(comContext->ComPortTarget, &openParams);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "WdfIoTargetOpen failed for %wZ with status %!STATUS!", &comPortName, status);
        goto cleanup;
    }

    // Configure COM port settings
    status = DsScannerCom_ConfigurePort(DeviceContext);
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "DsScannerCom_ConfigurePort failed with status %!STATUS!", status);
        WdfIoTargetClose(comContext->ComPortTarget);
        goto cleanup;
    }

    comContext->IsConnected = TRUE;
    TraceInformation(TRACE_DSSCANNER, "COM port %wZ opened successfully", &comPortName);
    goto exit;

cleanup:
    if (comContext->ComPortTarget != NULL)
    {
        WdfObjectDelete(comContext->ComPortTarget);
        comContext->ComPortTarget = NULL;
    }

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Close COM Port
//
VOID
DsScannerCom_CloseComPort(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    PDS_SCANNER_COM_CONTEXT comContext;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext != NULL && comContext->ComPortTarget != NULL)
    {
        WdfIoTargetClose(comContext->ComPortTarget);
        WdfObjectDelete(comContext->ComPortTarget);
        comContext->ComPortTarget = NULL;
        comContext->IsConnected = FALSE;
        
        TraceInformation(TRACE_DSSCANNER, "COM port closed");
    }

    FuncExitVoid(TRACE_DSSCANNER);
}

//
// Configure COM Port Settings
//
NTSTATUS
DsScannerCom_ConfigurePort(
    _In_ PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDS_SCANNER_COM_CONTEXT comContext;
    SERIAL_BAUD_RATE baudRate;
    SERIAL_LINE_CONTROL lineControl;
    SERIAL_TIMEOUTS timeouts;
    WDF_REQUEST_SEND_OPTIONS sendOptions;
    WDFREQUEST request;
    WDFMEMORY memory;
    WDF_OBJECT_ATTRIBUTES attributes;

    FuncEntry(TRACE_DSSCANNER);

    comContext = DsScannerComGetContext(WdfObjectContextGetObject(DeviceContext));
    if (comContext == NULL || comContext->ComPortTarget == NULL)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    // Set baud rate
    baudRate.BaudRate = DS_SCANNER_COM_BAUD_RATE;
    
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfRequestCreate(&attributes, comContext->ComPortTarget, &request);
    if (!NT_SUCCESS(status))
    {
        goto exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = request;
    status = WdfMemoryCreate(&attributes, NonPagedPool, 0, sizeof(baudRate), &memory, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(request);
        goto exit;
    }

    RtlCopyMemory(WdfMemoryGetBuffer(memory, NULL), &baudRate, sizeof(baudRate));
    
    status = WdfIoTargetFormatRequestForIoctl(comContext->ComPortTarget, request, IOCTL_SERIAL_SET_BAUD_RATE, memory, NULL, NULL, NULL);
    if (!NT_SUCCESS(status))
    {
        WdfObjectDelete(request);
        goto exit;
    }

    WDF_REQUEST_SEND_OPTIONS_INIT(&sendOptions, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&sendOptions, WDF_REL_TIMEOUT_IN_MS(1000));
    
    if (!WdfRequestSend(request, comContext->ComPortTarget, &sendOptions))
    {
        status = WdfRequestGetStatus(request);
    }
    
    WdfObjectDelete(request);
    
    if (!NT_SUCCESS(status))
    {
        TraceError(TRACE_DSSCANNER, "Failed to set baud rate with status %!STATUS!", status);
        goto exit;
    }

    // Set line control (8N1)
    lineControl.StopBits = STOP_BIT_1;
    lineControl.Parity = NO_PARITY;
    lineControl.WordLength = 8;
    
    // Similar IOCTL calls for line control and timeouts...
    // (Implementation continues with IOCTL_SERIAL_SET_LINE_CONTROL and IOCTL_SERIAL_SET_TIMEOUTS)

    TraceInformation(TRACE_DSSCANNER, "COM port configured: %d baud, 8N1", DS_SCANNER_COM_BAUD_RATE);

exit:
    FuncExit(TRACE_DSSCANNER, "status=%!STATUS!", status);
    return status;
}

//
// Parse Barcode Data from COM Input
//
BOOLEAN
DsScannerCom_ParseBarcodeData(
    _In_ PUCHAR RawData,
    _In_ ULONG DataLength,
    _Out_ PUCHAR BarcodeData,
    _Out_ PULONG BarcodeLength,
    _Out_ PULONG BarcodeType
)
{
    ULONG i;
    BOOLEAN foundStart = FALSE;
    ULONG startIndex = 0;
    ULONG endIndex = 0;

    // Simple parsing: look for printable ASCII data terminated by CR/LF
    for (i = 0; i < DataLength; i++)
    {
        if (!foundStart && RawData[i] >= 0x20 && RawData[i] <= 0x7E)
        {
            // Found start of barcode data
            foundStart = TRUE;
            startIndex = i;
        }
        else if (foundStart && (RawData[i] == '\r' || RawData[i] == '\n' || RawData[i] == 0))
        {
            // Found end of barcode data
            endIndex = i;
            break;
        }
    }

    if (!foundStart || endIndex <= startIndex)
    {
        return FALSE;
    }

    // Copy barcode data
    *BarcodeLength = endIndex - startIndex;
    if (*BarcodeLength > DS_SCANNER_COM_BUFFER_SIZE - 1)
    {
        *BarcodeLength = DS_SCANNER_COM_BUFFER_SIZE - 1;
    }
    
    RtlCopyMemory(BarcodeData, &RawData[startIndex], *BarcodeLength);
    BarcodeData[*BarcodeLength] = 0; // Null terminate

    // Simple barcode type detection based on length and content
    if (*BarcodeLength == 12 || *BarcodeLength == 13)
    {
        *BarcodeType = 0x0B1800; // EAN13/UPC-A
    }
    else if (*BarcodeLength >= 4 && *BarcodeLength <= 20)
    {
        *BarcodeType = 0x0B1800; // Code 128
    }
    else
    {
        *BarcodeType = 0x0B3300; // Unknown/Generic
    }

    return TRUE;
}