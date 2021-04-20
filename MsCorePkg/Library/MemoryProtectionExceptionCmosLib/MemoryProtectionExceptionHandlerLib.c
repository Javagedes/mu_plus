/**@file

Library registers an interrupt handler which catches exceptions related to memory
protections and turns them off for the next boot.

Copyright (c) Microsoft Corporation.
SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/CpuExceptionHandlerLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryProtectionLib.h>

#include "MemoryProtectionExceptionCommon.h"

EFI_CPU_ARCH_PROTOCOL           *mCpu = NULL;

/**
  Page Fault handler which turns off memory protections and does a warm reset.

  @param  InterruptType    Defines the type of interrupt or exception that
                           occurred on the processor.This parameter is processor architecture specific.
  @param  SystemContext    A pointer to the processor context when
                           the interrupt occurred on the processor.

**/
VOID
EFIAPI
MemoryProtectionExceptionHandlerCmos (
  IN EFI_EXCEPTION_TYPE   InterruptType,
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  UINT8 val = CMOS_MEM_PROT_VALID_BIT_MASK;

  DEBUG((DEBUG_ERROR, "%a - ExceptionData: 0x%x - InterruptType: 0x%x\n", __FUNCTION__, SystemContext.SystemContextX64->ExceptionData, InterruptType));

  DumpCpuContext(InterruptType, SystemContext);

  CmosWriteMemoryProtectionByte(val);

  DEBUG((DEBUG_INFO, "%a - Resetting...\n", __FUNCTION__));

  gRT->ResetSystem(EfiResetWarm, EFI_SUCCESS, 0, NULL);
}

/**
  Registers MemoryProtectionExceptionHandlerCmos using the EFI_CPU_ARCH_PROTOCOL.

  @param  Event          The Event that is being processed, not used.
  @param  Context        Event Context, not used.

**/
VOID
EFIAPI
CpuArchRegisterMemoryProtectionExceptionHandlerCmos (
    IN  EFI_EVENT   Event,
    IN  VOID       *Context
    )
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **) &mCpu);

  if(EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "%a: - Failed to Locate gEfiCpuArchProtocolGuid. \
      Memory protections cannot be turned off via Page Fault handler\n", __FUNCTION__));
    return;
  }

  Status = mCpu->RegisterInterruptHandler (mCpu, EXCEPT_IA32_PAGE_FAULT, MemoryProtectionExceptionHandlerCmos);

  if(EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "%a: - Failed to Register Exception Handler. \
      Memory protections cannot be turned off via Page Fault handler\n", __FUNCTION__));
  }

  return;
}

/**
  Main entry for this library.

  @param ImageHandle     Image handle this library.
  @param SystemTable     Pointer to SystemTable.

  @retval EFI_SUCCESS

**/
EFI_STATUS
EFIAPI
MemoryProtectionExceptionHandlerCmosConstructor (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_EVENT  CpuArchExHandlerCallBackEvent;
  VOID      *mCpuArchExHandlerRegistration = NULL;

  // Only install the exception handler if the global toggle is currently on
  if (!IsMemoryProtectionGlobalToggleEnabled()) {
    return EFI_SUCCESS;
  }

  Status = SystemTable->BootServices->CreateEvent(EVT_NOTIFY_SIGNAL,
                                                  TPL_CALLBACK,
                                                  CpuArchRegisterMemoryProtectionExceptionHandlerCmos,
                                                  NULL,
                                                  &CpuArchExHandlerCallBackEvent);

  if(EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "%a: - Failed to create CpuArch Notify Event. \
      Memory protections cannot be turned off via Page Fault handler\n", __FUNCTION__));
  }

  // NOTE: Installing an exception handler before gEfiCpuArchProtocolGuid has been produced causes
  //       the default handler to be overwritten by the default handlers. Registering a protocol notify
  //       ensures the handler will be registered as soon as possible.
  SystemTable->BootServices->RegisterProtocolNotify(&gEfiCpuArchProtocolGuid,
                                                    CpuArchExHandlerCallBackEvent,
                                                    &mCpuArchExHandlerRegistration);

  return EFI_SUCCESS;
}