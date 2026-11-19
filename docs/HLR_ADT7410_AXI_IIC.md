# High-Level Requirements (HLR) — ADT7410 & AXI IIC [DRAFT PLACEHOLDER]

| Field | Value |
| :--- | :--- |
| Document ID | HLR-ADT-IIC (Placeholder) |
| Status | **DRAFT PLACEHOLDER — Pending System Engineering & Safety Approval (`OI-01`)** |
| Child LLR | `docs/LLR_ADT7410_AXI_IIC.md` (`LLR-ADT-IIC`) |

> **Important (`OI-01`):** This placeholder establishes the parent HLR slot
> structure derived from `LLR_ADT7410_AXI_IIC.md` and `SDD_ADT7410_AXI_IIC.md`.
> `LLR_ADT7410_AXI_IIC.md` (Draft B, §1.3) already links every LLR group to the
> IDs below. The IDs and wording must be reviewed/replaced by approved system
> requirements before baseline freeze.

## 1. Proposed Parent HLR Mapping

| Proposed HLR ID | Capability Summary | Child LLR Groups |
| :--- | :--- | :--- |
| `HLR-IIC-001` | Deterministic, polled master-only AXI IIC communication at 100 kHz Standard Mode (7-bit addressing, bounded wait loops, explicit status codes). | `LLR-IIC-001`, `LLR-IIC-002`, `LLR-IIC-003`, `LLR-IIC-004` |
| `HLR-ADT-001` | ADT7410 sensor initialization, device identity verification (`0x19`), software reset, and lifecycle state integrity. | `LLR-ADT-001` |
| `HLR-ADT-002` | Runtime sensor configuration (13/16-bit resolution, operating mode, event mode, pin polarity, fault queue, one-shot trigger). | `LLR-ADT-002` |
| `HLR-ADT-003` | Die temperature acquisition in physical units (`float` degC, `mdegC`, `Q8.7`) and status/fault flag reporting. | `LLR-ADT-003` |
| `HLR-ADT-004` | Programmable over-temperature, under-temperature, critical temperature setpoints (`-55 °C..+150 °C`), and hysteresis (`0..15 °C`). | `LLR-ADT-004`, `LLR-ADT-005` |
| `HLR-APP-001` | Reference health monitor: start-up configuration with alarm thresholds, 10 ms periodic temperature/status acquisition, INT-pin alert handling, and bounded recovery from bus or sensor faults. | `LLR-APP-001` … `LLR-APP-005` |

## 2. Derived Requirements Requiring System Safety Assessment

Per `docs/LLR_ADT7410_AXI_IIC.md` (§1.1) and `OI-07`, the following derived LLR clauses
must be submitted to the system safety assessment process:
- `LLR-IIC-001.3` (32-bit Hamming-distance-31 lifecycle state encoding)
- `LLR-IIC-004.6` (TX_ERR from the master's final NACK is not an error)
- `LLR-ADT-001.2` (Mapping all non-OK HAL errors to `ADT7410_ERR_I2C_COMM`)
- `LLR-ADT-001.10` (32-bit Hamming-distance-31 driver lifecycle state encoding)
- `LLR-ADT-003.6` (Defensive `ERR_STATE` handling for corrupted `active_res`)
- `LLR-APP-002.3` (Recovery deferred to the next 10 ms cycle)
- `LLR-APP-003.4` (Alert flag re-armed when the alert service fails)
- `LLR-APP-004` (Application timing budget, see `OI-08`)
