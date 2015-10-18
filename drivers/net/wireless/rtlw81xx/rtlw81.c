/*++

Copyright (c) 2015 Minoca Corp. All Rights Reserved

Module Name:

    rtlw81.c

Abstract:

    This module implements support for the driver portion of the RTL81xx
    family of USB WIFI Controllers.

Author:

    Chris Stevens 7-Oct-2015

Environment:

    Kernel

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <minoca/driver.h>
#include <minoca/net/netdrv.h>
#include <minoca/net/net80211.h>
#include <usb.h>
#include "rtlw81.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// Known vendors and products.
//

#define RTLW81_USB_DEVICE_ID_FORMAT "VID_%x&PID_%x"

#define RTLW81_VENDOR_DLINK 0x2001
#define RTLW81_DLINK_DWA125D1 0x330F
#define RTLW81_DLINK_DWA123D1 0x3310

#define RTLW81_VENDOR_ELECOM 0x056E
#define RTLW81_ELECOM_WDC150SU2M 0x4008

#define RTLW81_VENDOR_REALTEK 0x0BDA
#define RTLW81_REALTEK_RTL8188ETV 0x0179
#define RTLW81_REALTEK_RTL8188EU 0x8179

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

KSTATUS
Rtlw81AddDevice (
    PVOID Driver,
    PSTR DeviceId,
    PSTR ClassId,
    PSTR CompatibleIds,
    PVOID DeviceToken
    );

VOID
Rtlw81DispatchStateChange (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
Rtlw81DispatchOpen (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
Rtlw81DispatchClose (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
Rtlw81DispatchIo (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

VOID
Rtlw81DispatchSystemControl (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    );

KSTATUS
Rtlw81pInitializeDeviceStructures (
    PVOID OsDevice,
    PRTLW81_DEVICE *NewDevice
    );

VOID
Rtlw81pDestroyDeviceStructures (
    PRTLW81_DEVICE Device
    );

KSTATUS
Rtlw81pSetUpUsbDevice (
    PRTLW81_DEVICE Device
    );

KSTATUS
Rtlw81pStartDevice (
    PIRP Irp,
    PRTLW81_DEVICE Device
    );

KSTATUS
Rtlw81pStopDevice (
    PIRP Irp,
    PRTLW81_DEVICE Device
    );

//
// -------------------------------------------------------------------- Globals
//

PDRIVER Rtlw81Driver = NULL;
UUID Rtlw81NetworkDeviceInformationUuid = NETWORK_DEVICE_INFORMATION_UUID;

//
// ------------------------------------------------------------------ Functions
//

KSTATUS
DriverEntry (
    PDRIVER Driver
    )

/*++

Routine Description:

    This routine is the entry point for the SMSC95xx driver. It registers its
    other dispatch functions, and performs driver-wide initialization.

Arguments:

    Driver - Supplies a pointer to the driver object.

Return Value:

    STATUS_SUCCESS on success.

    Failure code on error.

--*/

{

    DRIVER_FUNCTION_TABLE FunctionTable;
    KSTATUS Status;

    Rtlw81Driver = Driver;
    RtlZeroMemory(&FunctionTable, sizeof(DRIVER_FUNCTION_TABLE));
    FunctionTable.Version = DRIVER_FUNCTION_TABLE_VERSION;
    FunctionTable.AddDevice = Rtlw81AddDevice;
    FunctionTable.DispatchStateChange = Rtlw81DispatchStateChange;
    FunctionTable.DispatchOpen = Rtlw81DispatchOpen;
    FunctionTable.DispatchClose = Rtlw81DispatchClose;
    FunctionTable.DispatchIo = Rtlw81DispatchIo;
    FunctionTable.DispatchSystemControl = Rtlw81DispatchSystemControl;
    Status = IoRegisterDriverFunctions(Driver, &FunctionTable);
    return Status;
}

KSTATUS
Rtlw81AddDevice (
    PVOID Driver,
    PSTR DeviceId,
    PSTR ClassId,
    PSTR CompatibleIds,
    PVOID DeviceToken
    )

/*++

Routine Description:

    This routine is called when a device is detected for which the SMSC95xx
    driver acts as the function driver. The driver will attach itself to the
    stack.

Arguments:

    Driver - Supplies a pointer to the driver being called.

    DeviceId - Supplies a pointer to a string with the device ID.

    ClassId - Supplies a pointer to a string containing the device's class ID.

    CompatibleIds - Supplies a pointer to a string containing device IDs
        that would be compatible with this device.

    DeviceToken - Supplies an opaque token that the driver can use to identify
        the device in the system. This token should be used when attaching to
        the stack.

Return Value:

    STATUS_SUCCESS on success.

    Failure code if the driver was unsuccessful in attaching itself.

--*/

{

    PRTLW81_DEVICE Device;
    ULONG ItemsScanned;
    KSTATUS Status;
    USHORT UsbProductId;
    USHORT UsbVendorId;

    Status = Rtlw81pInitializeDeviceStructures(DeviceToken, &Device);
    if (!KSUCCESS(Status)) {
        goto AddDeviceEnd;
    }

    //
    // Detect variants by USB vendor and product ID.
    //

    Status = RtlStringScan(DeviceId,
                           RtlStringLength(DeviceId) + 1,
                           RTLW81_USB_DEVICE_ID_FORMAT,
                           sizeof(RTLW81_USB_DEVICE_ID_FORMAT),
                           CharacterEncodingDefault,
                           &ItemsScanned,
                           &UsbVendorId,
                           &UsbProductId);

    if (!KSUCCESS(Status)) {
        goto AddDeviceEnd;
    }

    if (ItemsScanned != 2) {
        Status = STATUS_INVALID_CONFIGURATION;
        goto AddDeviceEnd;
    }

    switch (UsbVendorId) {
    case RTLW81_VENDOR_DLINK:
        if ((UsbProductId == RTLW81_DLINK_DWA123D1) ||
            (UsbProductId == RTLW81_DLINK_DWA123D1)) {

            Device->Flags |= RTLW81_FLAG_8188E;
        }

        break;

    case RTLW81_VENDOR_ELECOM:
        if (UsbProductId == RTLW81_ELECOM_WDC150SU2M) {
            Device->Flags |= RTLW81_FLAG_8188E;
        }

        break;

    case RTLW81_VENDOR_REALTEK:
        if ((UsbProductId == RTLW81_REALTEK_RTL8188ETV) ||
            (UsbProductId == RTLW81_REALTEK_RTL8188EU)) {

            Device->Flags |= RTLW81_FLAG_8188E;
        }

        break;

    default:
        break;
    }

    Status = IoAttachDriverToDevice(Driver, DeviceToken, Device);
    if (!KSUCCESS(Status)) {
        goto AddDeviceEnd;
    }

AddDeviceEnd:
    if (!KSUCCESS(Status)) {
        if (Device != NULL) {
            Rtlw81pDestroyDeviceStructures(Device);
        }
    }

    return Status;
}

VOID
Rtlw81DispatchStateChange (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles State Change IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    KSTATUS Status;

    ASSERT(Irp->MajorCode == IrpMajorStateChange);

    if (Irp->Direction == IrpUp) {
        switch (Irp->MinorCode) {
        case IrpMinorQueryResources:
        case IrpMinorQueryChildren:
            IoCompleteIrp(Rtlw81Driver, Irp, STATUS_SUCCESS);
            break;

        case IrpMinorStartDevice:
            Status = Rtlw81pStartDevice(Irp, DeviceContext);
            if (!KSUCCESS(Status)) {
                IoCompleteIrp(Rtlw81Driver, Irp, Status);
            }

            break;

        case IrpMinorRemoveDevice:
            Status = Rtlw81pStopDevice(Irp, DeviceContext);
            if (!KSUCCESS(Status)) {
                IoCompleteIrp(Rtlw81Driver, Irp, Status);
                break;
            }

            Rtlw81pDestroyDeviceStructures(DeviceContext);
            break;

        default:
            break;
        }
    }

    return;
}

VOID
Rtlw81DispatchOpen (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles Open IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    return;
}

VOID
Rtlw81DispatchClose (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles Close IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    return;
}

VOID
Rtlw81DispatchIo (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles I/O IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    return;
}

VOID
Rtlw81DispatchSystemControl (
    PIRP Irp,
    PVOID DeviceContext,
    PVOID IrpContext
    )

/*++

Routine Description:

    This routine handles System Control IRPs.

Arguments:

    Irp - Supplies a pointer to the I/O request packet.

    DeviceContext - Supplies the context pointer supplied by the driver when it
        attached itself to the driver stack. Presumably this pointer contains
        driver-specific device context.

    IrpContext - Supplies the context pointer supplied by the driver when
        the IRP was created.

Return Value:

    None.

--*/

{

    PRTLW81_DEVICE Device;
    PSYSTEM_CONTROL_DEVICE_INFORMATION DeviceInformationRequest;
    KSTATUS Status;

    ASSERT(Irp->MajorCode == IrpMajorSystemControl);

    Device = DeviceContext;
    if (Irp->Direction == IrpDown) {
        switch (Irp->MinorCode) {
        case IrpMinorSystemControlDeviceInformation:
            DeviceInformationRequest = Irp->U.SystemControl.SystemContext;
            Status = NetGetSetLinkDeviceInformation(
                                         Device->NetworkLink,
                                         &(DeviceInformationRequest->Uuid),
                                         DeviceInformationRequest->Data,
                                         &(DeviceInformationRequest->DataSize),
                                         DeviceInformationRequest->Set);

            IoCompleteIrp(Rtlw81Driver, Irp, Status);
            break;

        default:
            break;
        }
    }

    return;
}

KSTATUS
Rtlw81pCreateNetworkDevice (
    PRTLW81_DEVICE Device
    )

/*++

Routine Description:

    This routine creates a core networking device object.

Arguments:

    Device - Supplies a pointer to the device to create an object for.

Return Value:

    Status code.

--*/

{

    KSTATUS Status;

    if (Device->NetworkLink != NULL) {
        Status = STATUS_SUCCESS;
        goto CreateNetworkDeviceEnd;
    }

    //
    // TODO: Call into the 802.11 networking core to create a new device.
    //

    //
    // Register for network device information requests.
    //

    Status = IoRegisterDeviceInformation(Device->OsDevice,
                                         &Rtlw81NetworkDeviceInformationUuid,
                                         TRUE);

    if (!KSUCCESS(Status)) {
        goto CreateNetworkDeviceEnd;
    }

CreateNetworkDeviceEnd:
    return Status;
}

//
// --------------------------------------------------------- Internal Functions
//

KSTATUS
Rtlw81pInitializeDeviceStructures (
    PVOID OsDevice,
    PRTLW81_DEVICE *NewDevice
    )

/*++

Routine Description:

    This routine initializes an SMSC95xx device.

Arguments:

    OsDevice - Supplies a pointer to the system token that represents this
        device.

    NewDevice - Supplies a pointer where the new structure will be returned.

Return Value:

    Status code.

--*/

{

    ULONG BufferAlignment;
    PRTLW81_DEVICE Device;
    ULONG Index;
    ULONG IoBufferFlags;
    UINTN IoBufferSize;
    ULONG MaxBulkInTransferSize;
    ULONG MaxControlSize;
    PHYSICAL_ADDRESS PhysicalAddress;
    KSTATUS Status;
    PUSB_TRANSFER UsbTransfer;
    PVOID VirtualAddress;

    Device = MmAllocatePagedPool(sizeof(RTLW81_DEVICE), RTLW81_ALLOCATION_TAG);
    if (Device == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto InitializeDeviceStructuresEnd;
    }

    RtlZeroMemory(Device, sizeof(RTLW81_DEVICE));
    Device->OsDevice = OsDevice;
    Device->UsbCoreHandle = INVALID_HANDLE;
    for (Index = 0; Index < Rtlw81BulkOutTypeCount; Index += 1) {
        INITIALIZE_LIST_HEAD(&(Device->BulkOutFreeTransferList[Index]));
    }

    Device->BulkOutListLock = KeCreateQueuedLock();
    if (Device->BulkOutListLock == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto InitializeDeviceStructuresEnd;
    }

    //
    // Attempt to attach to the USB core.
    //

    Status = UsbDriverAttach(OsDevice, Rtlw81Driver, &(Device->UsbCoreHandle));
    if (!KSUCCESS(Status)) {
        goto InitializeDeviceStructuresEnd;
    }

    Status = Rtlw81pSetUpUsbDevice(Device);
    if (!KSUCCESS(Status)) {
        goto InitializeDeviceStructuresEnd;
    }

    //
    // Create an I/O buffer for the control and receive transfers.
    //

    BufferAlignment = MmGetIoBufferAlignment();
    MaxBulkInTransferSize = ALIGN_RANGE_UP(RTLW81_BULK_IN_TRANSFER_SIZE,
                                           BufferAlignment);

    MaxControlSize = ALIGN_RANGE_UP(RTLW81_MAX_CONTROL_TRANSFER_SIZE,
                                    BufferAlignment);

    IoBufferSize = (MaxBulkInTransferSize * RTLW81_BULK_IN_TRANSFER_COUNT) +
                   MaxControlSize;

    IoBufferFlags = IO_BUFFER_FLAG_PHYSICALLY_CONTIGUOUS;
    Device->IoBuffer = MmAllocateNonPagedIoBuffer(0,
                                                  MAX_ULONG,
                                                  BufferAlignment,
                                                  IoBufferSize,
                                                  IoBufferFlags);

    if (Device->IoBuffer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto InitializeDeviceStructuresEnd;
    }

    ASSERT(Device->IoBuffer->FragmentCount == 1);
    ASSERT(Device->IoBuffer->Fragment[0].VirtualAddress != NULL);

    PhysicalAddress = Device->IoBuffer->Fragment[0].PhysicalAddress;
    VirtualAddress = Device->IoBuffer->Fragment[0].VirtualAddress;

    //
    // Set up the bulk in transfers that are used to receive packets.
    //

    for (Index = 0; Index < RTLW81_BULK_IN_TRANSFER_COUNT; Index += 1) {
        UsbTransfer = UsbAllocateTransfer(Device->UsbCoreHandle,
                                          Device->BulkInEndpoint,
                                          RTLW81_BULK_IN_TRANSFER_SIZE);

        if (UsbTransfer == NULL) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto InitializeDeviceStructuresEnd;
        }

        UsbTransfer->Buffer = VirtualAddress;
        UsbTransfer->BufferPhysicalAddress = PhysicalAddress;
        UsbTransfer->Direction = UsbTransferDirectionIn;
        UsbTransfer->Length = RTLW81_BULK_IN_TRANSFER_SIZE;
        UsbTransfer->BufferActualLength = MaxBulkInTransferSize;
        UsbTransfer->UserData = Device;
        UsbTransfer->CallbackRoutine = Rtlw81BulkInTransferCompletion;
        Device->BulkInTransfer[Index] = UsbTransfer;
        PhysicalAddress += MaxBulkInTransferSize;
        VirtualAddress += MaxBulkInTransferSize;
    }

    //
    // Set up the control transfer that's used for register reads and writes.
    //

    Device->ControlTransfer = UsbAllocateTransfer(
                                             Device->UsbCoreHandle,
                                             0,
                                             RTLW81_MAX_CONTROL_TRANSFER_SIZE);

    if (Device->ControlTransfer == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto InitializeDeviceStructuresEnd;
    }

    Device->ControlTransfer->Buffer = VirtualAddress;
    Device->ControlTransfer->BufferPhysicalAddress = PhysicalAddress;
    Device->ControlTransfer->BufferActualLength = MaxControlSize;
    VirtualAddress += MaxControlSize;
    PhysicalAddress += MaxControlSize;
    Status = STATUS_SUCCESS;

InitializeDeviceStructuresEnd:
    if (!KSUCCESS(Status)) {
        if (Device != NULL) {
            Rtlw81pDestroyDeviceStructures(Device);
            Device = NULL;
        }
    }

    *NewDevice = Device;
    return Status;
}

VOID
Rtlw81pDestroyDeviceStructures (
    PRTLW81_DEVICE Device
    )

/*++

Routine Description:

    This routine destroys an SMSC95xx device structure.

Arguments:

    Device - Supplies a pointer to the device.

Return Value:

    None.

--*/

{

    ULONG Index;

    //
    // Detach the device. This will cancel all transfer attached to the device,
    // including the in-flight bulk out transfers that this driver does not
    // track.
    //

    if (Device->UsbCoreHandle != INVALID_HANDLE) {
        UsbDetachDevice(Device->UsbCoreHandle);
    }

    //
    // Destroy all the allocated transfers. For good measure, make sure they
    // are cancelled.
    //

    for (Index = 0; Index < RTLW81_BULK_IN_TRANSFER_COUNT; Index += 1) {
        if (Device->BulkInTransfer[Index] != NULL) {
            UsbCancelTransfer(Device->BulkInTransfer[Index], TRUE);
            UsbDestroyTransfer(Device->BulkInTransfer[Index]);
        }
    }

    if (Device->ControlTransfer != NULL) {
        UsbCancelTransfer(Device->ControlTransfer, TRUE);
        UsbDestroyTransfer(Device->ControlTransfer);
    }

    if (Device->InterfaceClaimed != FALSE) {
        UsbReleaseInterface(Device->UsbCoreHandle, Device->InterfaceNumber);
    }

    if (Device->UsbCoreHandle != INVALID_HANDLE) {
        UsbDeviceClose(Device->UsbCoreHandle);
    }

    if (Device->IoBuffer != NULL) {
        MmFreeIoBuffer(Device->IoBuffer);
    }

    IoRegisterDeviceInformation(Device->OsDevice,
                                &Rtlw81NetworkDeviceInformationUuid,
                                FALSE);

    if (Device->NetworkLink != NULL) {
        NetDestroyLink(Device->NetworkLink);
        Device->NetworkLink = NULL;
    }

    //
    // There should be no active bulk out transfers, so destroy all the free
    // transfers.
    //

    Rtlw81pDestroyBulkOutTransfers(Device);
    if (Device->BulkOutListLock != NULL) {
        KeDestroyQueuedLock(Device->BulkOutListLock);
    }

    MmFreePagedPool(Device);
    return;
}

KSTATUS
Rtlw81pSetUpUsbDevice (
    PRTLW81_DEVICE Device
    )

/*++

Routine Description:

    This routine claims the proper interface for the device and finds the
    bulk in, bulk out, and interrupt endpoints.

Arguments:

    Device - Supplies a pointer to the device.

Return Value:

    Status code.

--*/

{

    ULONG BulkOutEndpointCount;
    PUSB_CONFIGURATION_DESCRIPTION Configuration;
    PLIST_ENTRY CurrentEntry;
    USB_TRANSFER_DIRECTION Direction;
    PUSB_ENDPOINT_DESCRIPTION Endpoint;
    UCHAR EndpointType;
    ULONG Index;
    PUSB_INTERFACE_DESCRIPTION Interface;
    KSTATUS Status;

    if (Device->InterfaceClaimed != FALSE) {

        ASSERT((Device->BulkInEndpoint != 0) &&
               (Device->BulkOutEndpointCount != 0));

        Status = STATUS_SUCCESS;
        goto SetUpUsbDeviceEnd;
    }

    //
    // If the configuration isn't yet set, set the first one.
    //

    Configuration = UsbGetActiveConfiguration(Device->UsbCoreHandle);
    if (Configuration == NULL) {
        Status = UsbSetConfiguration(Device->UsbCoreHandle, 0, TRUE);
        if (!KSUCCESS(Status)) {
            goto SetUpUsbDeviceEnd;
        }

        Configuration = UsbGetActiveConfiguration(Device->UsbCoreHandle);

        ASSERT(Configuration != NULL);

    }

    //
    // Get and verify the interface.
    //

    Interface = UsbGetDesignatedInterface(Device->OsDevice,
                                          Device->UsbCoreHandle);

    if (Interface == NULL) {
        Status = STATUS_NO_INTERFACE;
        goto SetUpUsbDeviceEnd;
    }

    if (Interface->Descriptor.Class != UsbInterfaceClassVendor) {
        Status = STATUS_NO_INTERFACE;
        goto SetUpUsbDeviceEnd;
    }

    //
    // Locate the IN and OUT bulk endpoints, and the interrupt endpoint.
    //

    BulkOutEndpointCount = 0;
    CurrentEntry = Interface->EndpointListHead.Next;
    while (CurrentEntry != &(Interface->EndpointListHead)) {
        if ((Device->BulkInEndpoint != 0) &&
            (BulkOutEndpointCount == RTLW81_MAX_BULK_OUT_ENDPOINT_COUNT)) {

            break;
        }

        Endpoint = LIST_VALUE(CurrentEntry,
                              USB_ENDPOINT_DESCRIPTION,
                              ListEntry);

        CurrentEntry = CurrentEntry->Next;

        //
        // Deconstruct the components of the endpoint descriptor.
        //

        EndpointType = Endpoint->Descriptor.Attributes &
                       USB_ENDPOINT_ATTRIBUTES_TYPE_MASK;

        if ((Endpoint->Descriptor.EndpointAddress &
             USB_ENDPOINT_ADDRESS_DIRECTION_IN) != 0) {

            Direction = UsbTransferDirectionIn;

        } else {
            Direction = UsbTransferDirectionOut;
        }

        //
        // Look to match the endpoint up to one of the required ones.
        //

        if (EndpointType == USB_ENDPOINT_ATTRIBUTES_TYPE_BULK) {
            if ((Device->BulkInEndpoint == 0) &&
                (Direction == UsbTransferDirectionIn)) {

                Device->BulkInEndpoint = Endpoint->Descriptor.EndpointAddress;

            } else if ((BulkOutEndpointCount <
                        RTLW81_MAX_BULK_OUT_ENDPOINT_COUNT) &&
                       (Direction == UsbTransferDirectionOut)) {

                Device->BulkOutEndpoint[BulkOutEndpointCount] =
                                          Endpoint->Descriptor.EndpointAddress;

                BulkOutEndpointCount += 1;
            }
        }
    }

    if ((Device->BulkInEndpoint == 0) ||
        (BulkOutEndpointCount == 0)) {

        Status = STATUS_INVALID_CONFIGURATION;
        goto SetUpUsbDeviceEnd;
    }

    //
    // Assign the bulk out endpoints based on how many there are.
    //

    if (BulkOutEndpointCount == 1) {
        for (Index = 0; Index < Rtlw81BulkOutTypeCount; Index += 1) {
            Device->BulkOutTypeEndpointIndex[Index] = 0;
        }

    } else if (BulkOutEndpointCount == 2) {
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutBe] = 1;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutBk] = 1;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutVi] = 0;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutVo] = 0;

    } else {

        ASSERT(BulkOutEndpointCount == RTLW81_MAX_BULK_OUT_ENDPOINT_COUNT);

        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutBe] = 2;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutBk] = 2;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutVi] = 1;
        Device->BulkOutTypeEndpointIndex[Rtlw81BulkOutVo] = 0;
    }

    //
    // Everything's all ready, claim the interface.
    //

    Status = UsbClaimInterface(Device->UsbCoreHandle,
                               Interface->Descriptor.InterfaceNumber);

    if (!KSUCCESS(Status)) {
        goto SetUpUsbDeviceEnd;
    }

    Device->InterfaceNumber = Interface->Descriptor.InterfaceNumber;
    Device->InterfaceClaimed = TRUE;
    Status = STATUS_SUCCESS;

SetUpUsbDeviceEnd:
    return Status;
}

KSTATUS
Rtlw81pStartDevice (
    PIRP Irp,
    PRTLW81_DEVICE Device
    )

/*++

Routine Description:

    This routine starts the SMSC95xx LAN device.

Arguments:

    Irp - Supplies a pointer to the start IRP.

    Device - Supplies a pointer to the device information.

Return Value:

    Status code.

--*/

{

    KSTATUS Status;

    //
    // Start up the controller.
    //

    Status = Rtlw81pInitialize(Device, Irp);
    if (!KSUCCESS(Status)) {
        goto StartDeviceEnd;
    }

StartDeviceEnd:
    return Status;
}

KSTATUS
Rtlw81pStopDevice (
    PIRP Irp,
    PRTLW81_DEVICE Device
    )

/*++

Routine Description:

    This routine stops the SMSC95xx LAN device.

Arguments:

    Irp - Supplies a pointer to the removal IRP.

    Device - Supplies a pointer to the device information.

Return Value:

    Status code.

--*/

{

    //
    // Reset the initialization phase so that the device starts over it gets
    // restarted.
    //

    Device->InitializationPhase = 0;

    //
    // TODO: Announce to 802.11 that the device has stopped.
    //

    return STATUS_SUCCESS;
}

