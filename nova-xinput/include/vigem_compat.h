/*
 * vigem_compat.h -- C-friendly forward declarations for the ViGEmClient API.
 *
 * ViGEm/Client.h v1.21.222.0 uses C++-only `using` type aliases for ~9 types
 * which fail C compilation. This header pulls in Common.h (the C-clean part)
 * and hand-declares the API symbols we link against.
 */

#ifndef VIGEM_COMPAT_H
#define VIGEM_COMPAT_H

#include <ViGEm/Common.h>

/* VIGEM_ERROR is defined in Client.h (not Common.h). Replicate inline. */
typedef enum _VIGEM_ERRORS {
    VIGEM_ERROR_NONE = 0x20000000,
    VIGEM_ERROR_BUS_NOT_FOUND = 0xE0000001,
    VIGEM_ERROR_NO_FREE_SLOT = 0xE0000002,
    VIGEM_ERROR_INVALID_TARGET = 0xE0000003,
    VIGEM_ERROR_REMOVAL_FAILED = 0xE0000004,
    VIGEM_ERROR_ALREADY_CONNECTED = 0xE0000005,
    VIGEM_ERROR_TARGET_UNINITIALIZED = 0xE0000006,
    VIGEM_ERROR_TARGET_NOT_PLUGGED_IN = 0xE0000007,
    VIGEM_ERROR_BUS_VERSION_MISMATCH = 0xE0000008,
    VIGEM_ERROR_BUS_ACCESS_FAILED = 0xE0000009,
    VIGEM_ERROR_CALLBACK_ALREADY_REGISTERED = 0xE0000010,
    VIGEM_ERROR_CALLBACK_NOT_FOUND = 0xE0000011,
    VIGEM_ERROR_BUS_ALREADY_CONNECTED = 0xE0000012,
    VIGEM_ERROR_BUS_INVALID_HANDLE = 0xE0000013,
    VIGEM_ERROR_XUSB_USERINDEX_OUT_OF_RANGE = 0xE0000014,
    VIGEM_ERROR_INVALID_PARAMETER = 0xE0000015,
    VIGEM_ERROR_NOT_SUPPORTED = 0xE0000016,
    VIGEM_ERROR_WINAPI = 0xE0000017,
    VIGEM_ERROR_TIMED_OUT = 0xE0000018,
    VIGEM_ERROR_IS_DISPOSING = 0xE0000019
} VIGEM_ERROR;

typedef struct _VIGEM_CLIENT_T *PVIGEM_CLIENT;
typedef struct _VIGEM_TARGET_T *PVIGEM_TARGET;

PVIGEM_CLIENT vigem_alloc(void);
void          vigem_free(PVIGEM_CLIENT vigem);
VIGEM_ERROR   vigem_connect(PVIGEM_CLIENT vigem);
void          vigem_disconnect(PVIGEM_CLIENT vigem);

PVIGEM_TARGET vigem_target_x360_alloc(void);
void          vigem_target_free(PVIGEM_TARGET target);
VIGEM_ERROR   vigem_target_add(PVIGEM_CLIENT vigem, PVIGEM_TARGET target);
VIGEM_ERROR   vigem_target_remove(PVIGEM_CLIENT vigem, PVIGEM_TARGET target);
VIGEM_ERROR   vigem_target_x360_update(PVIGEM_CLIENT vigem, PVIGEM_TARGET target, XUSB_REPORT report);

/* X360 rumble notification callback.
 * LargeMotor and SmallMotor are 0..255. LedNumber is the player-LED index. */
typedef VOID (CALLBACK *PFN_VIGEM_X360_NOTIFICATION)(
    PVIGEM_CLIENT Client,
    PVIGEM_TARGET Target,
    UCHAR         LargeMotor,
    UCHAR         SmallMotor,
    UCHAR         LedNumber,
    LPVOID        UserData);

VIGEM_ERROR vigem_target_x360_register_notification(
    PVIGEM_CLIENT              vigem,
    PVIGEM_TARGET              target,
    PFN_VIGEM_X360_NOTIFICATION notification,
    LPVOID                     userData);

VIGEM_ERROR vigem_target_x360_unregister_notification(PVIGEM_TARGET target);

#endif /* VIGEM_COMPAT_H */
