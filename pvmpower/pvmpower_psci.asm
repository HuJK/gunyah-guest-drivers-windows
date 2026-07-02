;
; PSCI hypercall stub (ARM64) for the pvmpower driver.
;
; PsciHvcCall(fn, a1, a2, a3) issues an HVC #0 with the four arguments already
; placed in x0-x3 by the C calling convention, and returns x0. Used to invoke
; PSCI SYSTEM_OFF (0x84000008) / SYSTEM_RESET (0x84000009) at last-chance
; shutdown so the Gunyah host powers the VM off or resets it (the same path
; Linux/edk2 use; the platform PSCI conduit is "hvc").
;
; Copyright (c) 2026
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
        AREA    |.text|, CODE, READONLY
        EXPORT  PsciHvcCall
PsciHvcCall PROC
        hvc     #0
        ret
        ENDP
        END
