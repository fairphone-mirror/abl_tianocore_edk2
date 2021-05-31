/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "AutoGen.h"
#include "BootLinux.h"
#include "BootStats.h"
#include "KeyPad.h"
#include "LinuxLoaderLib.h"
#include <Protocol/DiskIo.h>
#include <Protocol/EFIDisplayUtils.h>
#include <FastbootLib/FastbootMain.h>
#include <Library/DeviceInfo.h>
#include <Library/DrawUI.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/ShutdownServices.h>
#include <Library/StackCanary.h>
#include "Library/ThreadStack.h"
#include <Library/HypervisorMvCalls.h>
#include <Library/UpdateCmdLine.h>
#include <Library/fpconfig_persist.h>

// FP4-263, innproduct flag, liquan.zhou.t2m, 20210509.
// Task: 9949950
inproductflag_info_t OemInproductFlagInternal = {0};
const inproductflag_info_t * const OemInproductFlag = &OemInproductFlagInternal;

//FP4-272, read Custom ID from fpconfig, liquan.zhou.t2m, 20210518
FPConfig_t FPConfig = {0};

//FP4-589, read wifi mac from traceability, liquan.zhou.t2m, 20210531
CHAR8 TraceabilityInfo[512] = {0};

#define MAX_APP_STR_LEN 64
#define MAX_NUM_FS 10
#define DEFAULT_STACK_CHK_GUARD 0xc0c0c0c0

STATIC BOOLEAN BootReasonAlarm = FALSE;
STATIC BOOLEAN BootIntoFastboot = FALSE;
STATIC BOOLEAN BootIntoRecovery = FALSE;

// This function is used to Deactivate MDTP by entering recovery UI
STATIC EFI_STATUS MdtpDisable (VOID)
{
  BOOLEAN MdtpActive = FALSE;
  EFI_STATUS Status = EFI_SUCCESS;
  QCOM_MDTP_PROTOCOL *MdtpProtocol;

  if (FixedPcdGetBool (EnableMdtpSupport)) {
    Status = IsMdtpActive (&MdtpActive);

    if (EFI_ERROR (Status))
      return Status;

    if (MdtpActive) {
      Status = gBS->LocateProtocol (&gQcomMdtpProtocolGuid, NULL,
                                    (VOID **)&MdtpProtocol);
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n",
                Status));
        return Status;
      }
      /* Perform Local Deactivation of MDTP */
      Status = MdtpProtocol->MdtpDeactivate (MdtpProtocol, FALSE);
    }
  }

  return Status;
}

STATIC UINT8
GetRebootReason (UINT32 *ResetReason)
{
  EFI_RESETREASON_PROTOCOL *RstReasonIf;
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiResetReasonProtocolGuid, NULL,
                                (VOID **)&RstReasonIf);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error locating the reset reason protocol\n"));
    return Status;
  }

  RstReasonIf->GetResetReason (RstReasonIf, ResetReason, NULL, NULL);
  if (RstReasonIf->Revision >= EFI_RESETREASON_PROTOCOL_REVISION)
    RstReasonIf->ClearResetReason (RstReasonIf);
  return Status;
}

BOOLEAN IsABRetryCountUpdateRequired (VOID)
{
  BOOLEAN BatteryStatus;

  /* Check power off charging */
  TargetPauseForBatteryCharge (&BatteryStatus);

  /* Do not decrement bootable retry count in below states:
     * fastboot, fastbootd, charger, recovery
     */
  if ((BatteryStatus &&
       IsChargingScreenEnable ()) ||
       BootIntoFastboot ||
       BootIntoRecovery) {
    return FALSE;
  }
  return TRUE;
}

#if TARGET_BOARD_TYPE_AUTO
STATIC UINT8
WaitForDisplayCompletion (VOID)
{
  EFI_STATUS Status;
  EfiQcomDisplayUtilsProtocol *pDisplayUtilsProtocol = NULL;
  CHAR8 *sLockName = "DispInit";

  Status = gBS->LocateProtocol (&gQcomDisplayUtilsProtocolGuid,
                                NULL,
                                (VOID **)&pDisplayUtilsProtocol);
  if ((EFI_ERROR (Status)) ||
      (pDisplayUtilsProtocol == NULL)) {
    DEBUG ((EFI_D_ERROR, "Failed to locate DisplayUtils protocol, Status=%r\n",
                Status));
    return Status;
  } else {
    Status = pDisplayUtilsProtocol->DisplayUtilsSetProperty (
                                     EFI_DISPLAY_UTILS_WAIT_FOR_EVENT,
                                     sLockName, strlen (sLockName));
  }

  return Status;
}
#else
STATIC UINT8
WaitForDisplayCompletion (VOID)
{
  return EFI_SUCCESS;
}
#endif

//+ FP4-263, innproduct flag, liquan.zhou.t2m, 20210509.


// Task: 9820238
STATIC
EFI_STATUS
PartitionGetInfo (IN CONST CHAR16 *PartitionName,
                  OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
                  OUT EFI_HANDLE **Handle)
{
  EFI_STATUS Status;
  EFI_PARTITION_ENTRY *PartEntry;
  UINT16 i;
  UINT32 j;
  /* By default the LunStart and LunEnd would point to '0' and max value */
  UINT32 LunStart = 0;
  UINT32 LunEnd = GetMaxLuns ();

  for (i = LunStart; i < LunEnd; i++) {
    for (j = 0; j < Ptable[i].MaxHandles; j++) {
      Status =
          gBS->HandleProtocol (Ptable[i].HandleInfoList[j].Handle,
                               &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
      if (EFI_ERROR (Status)) {
        continue;
      }
      if (!(StrCmp (PartitionName, PartEntry->PartitionName))) {
        *BlockIo = Ptable[i].HandleInfoList[j].BlkIo;
        *Handle = Ptable[i].HandleInfoList[j].Handle;
        return Status;
      }
    }
  }

  DEBUG ((EFI_D_ERROR, "Partition not found : %s\n", PartitionName));
  return EFI_NOT_FOUND;
}


STATIC
UINT16
CalculateCrc16 (
  IN UINT8   *Data,
  IN UINTN   DataSize,
  IN UINT16  Crc
  )
{
  UINTN  Index;
  UINTN  BitIndex;

  for (Index = 0; Index < DataSize; Index++) {
    Crc ^= (UINT16)Data[Index];
    for (BitIndex = 0; BitIndex < 8; BitIndex++) {
      if ((Crc & 0x8000) != 0) {
        Crc <<= 1;
        Crc ^= 0x1021;
      } else {
        Crc <<= 1;
      }
    }
  }
  return Crc;
}

// Task: 9949950
STATIC
EFI_STATUS
OembinInproductFlagRead(inproductflag_info_t *Inproductflag,
        EFI_BLOCK_IO_PROTOCOL *BlockIo)
{
  EFI_STATUS Status;
  UINT32 DataOffset = 4096 / BlockIo->Media->BlockSize;
  UINT64 BuffSize = ROUND_TO_PAGE (sizeof(inproductflag_info_t), BlockIo->Media->BlockSize - 1);
  inproductflag_info_t *Buff = AllocateZeroPool (BuffSize);
  UINT16 Checksum = 0;
  if (!Buff) {
    DEBUG ((EFI_D_ERROR, "Error allocating memory for reading inproductflag\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((EFI_D_INFO, "Inproduction flag start loading.\n"));
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                  DataOffset, BuffSize, (VOID *) Buff);

  if (Status == EFI_SUCCESS) {
    memcpy(Inproductflag, Buff, sizeof(inproductflag_info_t));
    Checksum = Inproductflag->CHECKSUM;
    Inproductflag->CHECKSUM = 0;
    if (Checksum != CalculateCrc16 ((UINT8 *)Inproductflag,
                                        sizeof(inproductflag_info_t), 0)) {
      Status = EFI_LOAD_ERROR;
      DEBUG ((EFI_D_ERROR, "Inproduction flag data checksum error.\n"));
    }
  }
  FreePool (Buff);

  if (Status == EFI_SUCCESS) {
    Status = (Inproductflag->MAGIC == INPRODUCT_STRUCT_MAGIC) ?
                EFI_SUCCESS : EFI_LOAD_ERROR;
  }

  if (Status != EFI_SUCCESS) {
    memset(Inproductflag, 0, sizeof(inproductflag_info_t));
    DEBUG ((EFI_D_ERROR, "Oem inproductflag rest for error %d.\n", Status));
  }

  return Status;
}

STATIC
EFI_STATUS
GetOembinPartitionInfo ()
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  CONST CHAR16 *PartitionName = L"oembin";

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }

  // Task: 9949950
  OembinInproductFlagRead (&OemInproductFlagInternal, BlockIo);

  return Status;
}

//- FP4-263, innproduct flag, liquan.zhou.t2m, 20210509.


//+FP4-272, read Custom ID from fpconfig, liquan.zhou.t2m, 20210518
STATIC
EFI_STATUS
GetFPConfigPartitionInfo ()
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  CONST CHAR16 *PartitionName = L"fpconfig_persist";

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }

  UINT32 DataOffset = 0 / BlockIo->Media->BlockSize;
  UINT64 BuffSize = ROUND_TO_PAGE (sizeof(FPConfig_t), BlockIo->Media->BlockSize - 1);
  FPConfig_t *Buff = AllocateZeroPool (BuffSize);

  if (!Buff) {
    DEBUG ((EFI_D_ERROR, "Error allocating memory for reading inproductflag\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((EFI_D_INFO, "fpconfig start loading.\n"));
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                  DataOffset, BuffSize, (VOID *) Buff);

  if (Status == EFI_SUCCESS) {
    if(CompareMem (Buff->magic, FPCONFIG_MAGIC, FPCONFIG_MAGIC_SIZE)) {
      DEBUG ((EFI_D_ERROR, "fpconfig Magic does not match\n"));
      gBS->SetMem (Buff, sizeof (BuffSize), 0);
      gBS->CopyMem (Buff->magic, FPCONFIG_MAGIC, FPCONFIG_MAGIC_SIZE);
      gBS->CopyMem (Buff->cid, "STD", FPCONFIG_CID_SIZE);
      Status = BlockIo->WriteBlocks (BlockIo, BlockIo->Media->MediaId,
                  DataOffset, BuffSize, (VOID *) Buff);
      if (Status == EFI_SUCCESS) {
        DEBUG ((EFI_D_INFO, "fpconfig init success\n"));
      }
    } 
  } else {
    DEBUG ((EFI_D_ERROR, "fpconfig loading error\n"));
  }
  memcpy(&FPConfig, Buff, sizeof(FPConfig_t));

  FreePool (Buff);

  return Status;
}
//-FP4-272, read Custom ID from fpconfig, liquan.zhou.t2m, 20210518


//+FP4-589, read wifi mac from traceability, liquan.zhou.t2m, 20210531
STATIC
EFI_STATUS
GetTraceabilityPartitionInfo ()
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  CONST CHAR16 *PartitionName = L"traceability";

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }

  UINT32 DataOffset = 0 / BlockIo->Media->BlockSize;
  UINT64 BuffSize = ROUND_TO_PAGE (sizeof(TraceabilityInfo), BlockIo->Media->BlockSize - 1);
  FPConfig_t *Buff = AllocateZeroPool (BuffSize);

  if (!Buff) {
    DEBUG ((EFI_D_ERROR, "Error allocating memory for reading inproductflag\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((EFI_D_INFO, "traceability start loading.\n"));
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                  DataOffset, BuffSize, (VOID *) Buff);

  if (Status == EFI_SUCCESS) {
    memcpy(&TraceabilityInfo, Buff, sizeof(TraceabilityInfo));
  } else {
    DEBUG ((EFI_D_ERROR, "fpconfig loading error\n"));
  }

  FreePool (Buff);

  return Status;
}
//-FP4-589, read wifi mac from traceability, liquan.zhou.t2m, 20210531



/**
  Linux Loader Application EntryPoint

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

 **/

EFI_STATUS EFIAPI  __attribute__ ( (no_sanitize ("safe-stack")))
LinuxLoaderEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  UINT32 BootReason = NORMAL_MODE;
  UINT32 KeyPressed = SCAN_NULL;
  /* MultiSlot Boot */
  BOOLEAN MultiSlotBoot;

  DEBUG ((EFI_D_INFO, "Loader Build Info: %a %a\n", __DATE__, __TIME__));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoader Load Address to debug ABL: 0x%llx\n",
         (UINTN)LinuxLoaderEntry & (~ (0xFFF))));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoaderEntry Address: 0x%llx\n",
         (UINTN)LinuxLoaderEntry));

  Status = InitThreadUnsafeStack ();

  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Unable to Allocate memory for Unsafe Stack: %r\n",
            Status));
    goto stack_guard_update_default;
  }

  StackGuardChkSetup ();

  BootStatsSetTimeStamp (BS_BL_START);

  // Initialize verified boot & Read Device Info
  Status = DeviceInfoInit ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Initialize the device info failed: %r\n", Status));
    goto stack_guard_update_default;
  }

  Status = EnumeratePartitions ();

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LinuxLoader: Could not enumerate partitions: %r\n",
            Status));
    goto stack_guard_update_default;
  }

  UpdatePartitionEntries ();
  /*Check for multislot boot support*/
  MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  if (MultiSlotBoot) {
    DEBUG ((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
    FindPtnActiveSlot ();
  }

  //FP4-263, innproduct flag, liquan.zhou.t2m, 20210509.
  GetOembinPartitionInfo ();

  //FP4-272, read Custom ID from fpconfig, liquan.zhou.t2m, 20210518
  GetFPConfigPartitionInfo();

  //FP4-589, read wifi mac from traceability, liquan.zhou.t2m, 20210531
  GetTraceabilityPartitionInfo();

  Status = GetKeyPress (&KeyPressed);
  if (Status == EFI_SUCCESS) {
    if (KeyPressed == SCAN_DOWN)
      BootIntoFastboot = TRUE;
    if (KeyPressed == SCAN_UP)
      BootIntoRecovery = TRUE;
    if (KeyPressed == SCAN_ESC)
      RebootDevice (EMERGENCY_DLOAD);
  } else if (Status == EFI_DEVICE_ERROR) {
    DEBUG ((EFI_D_ERROR, "Error reading key status: %r\n", Status));
    goto stack_guard_update_default;
  }

  // check for reboot mode
  Status = GetRebootReason (&BootReason);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Failed to get Reboot reason: %r\n", Status));
    goto stack_guard_update_default;
  }

  switch (BootReason) {
  case FASTBOOT_MODE:
    BootIntoFastboot = TRUE;
    break;
  case RECOVERY_MODE:
    BootIntoRecovery = TRUE;
    break;
  case ALARM_BOOT:
    BootReasonAlarm = TRUE;
    break;
  case DM_VERITY_ENFORCING:
    // write to device info
    Status = EnableEnforcingMode (TRUE);
    if (Status != EFI_SUCCESS)
      goto stack_guard_update_default;
    break;
  case DM_VERITY_LOGGING:
    /* Disable MDTP if it's Enabled through Local Deactivation */
    Status = MdtpDisable ();
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) {
      DEBUG ((EFI_D_ERROR, "MdtpDisable Returned error: %r\n", Status));
      goto stack_guard_update_default;
    }
    // write to device info
    Status = EnableEnforcingMode (FALSE);
    if (Status != EFI_SUCCESS)
      goto stack_guard_update_default;

    break;
  case DM_VERITY_KEYSCLEAR:
    Status = ResetDeviceState ();
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "VB Reset Device State error: %r\n", Status));
      goto stack_guard_update_default;
    }
    break;
  default:
    if (BootReason != NORMAL_MODE) {
      DEBUG ((EFI_D_ERROR,
             "Boot reason: 0x%x not handled, defaulting to Normal Boot\n",
             BootReason));
    }
    break;
  }

  Status = RecoveryInit (&BootIntoRecovery);
  if (Status != EFI_SUCCESS)
    DEBUG ((EFI_D_VERBOSE, "RecoveryInit failed ignore: %r\n", Status));

  /* Populate board data required for fastboot, dtb selection and cmd line */
  Status = BoardInit ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error finding board information: %r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_INFO, "KeyPress:%u, BootReason:%u\n", KeyPressed, BootReason));
  DEBUG ((EFI_D_INFO, "Fastboot=%d, Recovery:%d\n",
                                          BootIntoFastboot, BootIntoRecovery));
  if (!GetVmData ()) {
    DEBUG ((EFI_D_ERROR, "VM Hyp calls not present\n"));
  }

  if (!BootIntoFastboot) {
    BootInfo Info = {0};
    Info.MultiSlotBoot = MultiSlotBoot;
    Info.BootIntoRecovery = BootIntoRecovery;
    Info.BootReasonAlarm = BootReasonAlarm;
    Status = LoadImageAndAuth (&Info);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "LoadImageAndAuth failed: %r\n", Status));
      goto fastboot;
    }

    Status = WaitForDisplayCompletion ();
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "Failed to wait for display completion: %r\n",
                  Status));
    }
    BootLinux (&Info);
  }

fastboot:
  Status = WaitForDisplayCompletion ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Failed to wait for display completion: %r\n",
                Status));
  }

  DEBUG ((EFI_D_INFO, "Launching fastboot\n"));
  Status = FastbootInitialize ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
    goto stack_guard_update_default;
  }

stack_guard_update_default:
  /*Update stack check guard with defualt value then return*/
  __stack_chk_guard = DEFAULT_STACK_CHK_GUARD;

  return Status;
}
