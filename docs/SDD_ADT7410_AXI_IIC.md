# Software Design Description (SDD) — ADT7410 Driver, AXI IIC HAL & Reference Application

| Field | Value |
|:---|:---|
| Document ID | SDD-ADT-IIC |
| Revision | Draft C |
| Software items | `include/axi_iic_hal.h`, `src/axi_iic_hal.c`, `include/adt7410_driver.h`, `src/adt7410_driver.c`, `src/main.c` |
| Requirements | LLR-ADT-IIC (`LLR_ADT7410_AXI_IIC.md`) |
| Coding standard | C Coding Rules v3 (MISRA C:2012 + BARR-C:2018 + DO-178C) |
| Status | **Draft — derived from source code, pending review** |

### Revision history

| Rev | Change |
|:---|:---|
| Draft A | Initial SDD for HAL and driver |
| Draft B | Added SDD-ADT-2.13 / 3.21 (float degC API); added §5 SDD-APP for `src/main.c` (incl. timing analysis SDD-APP-4); updated stack figures; restored Markdown formatting |
| Draft C | Master-receive completion: SDD-IIC-3.13 … 3.17 (TX_ERR from the master's final NACK is not an error, OI-03; decision independent of flag/data order); SR.TX_FULL corrected to bit 4 |

---

## 1. Introduction

### 1.1 Purpose

This SDD describes the architecture, data design, interface design and
internal design of the ADT7410 temperature sensor driver, the Xilinx AXI IIC
Hardware Abstraction Layer (HAL) and the reference application. Every
`@design` tag in the source code refers to a section ID in this document
(`SDD-IIC-x.y`, `SDD-ADT-x.y`, `SDD-APP-x.y`).

### 1.2 ID numbering

| Range | Content |
|:---|:---|
| `SDD-xxx-1.0` | Module overview (file header) |
| `SDD-xxx-1.n` | Data design: public types and file-scope objects |
| `SDD-xxx-2.n` | Interface design: public functions |
| `SDD-xxx-3.n` | Internal design: `static` functions and `static const` data |
| `SDD-APP-4` | Application timing analysis |

### 1.3 References

| Ref | Document |
|:---|:---|
| [PG090] | Xilinx AXI IIC Bus Interface LogiCORE IP Product Guide |
| [ADT7410] | Analog Devices ADT7410 Data Sheet |
| [RULES] | C Coding Rules v3 (`C_rules_v3_addDO178C.md`) |
| [LLR] | `LLR_ADT7410_AXI_IIC.md` |

---

## 2. Architecture

### 2.1 Layering

```
+------------------------------------------------------+
| Reference application (src/main.c)        SDD-APP    |
|  - 10 ms polling example, INT-pin interrupt example  |
|  - health snapshot, deferred recovery                |
+------------------------------------------------------+
            | adt7410_* API  (float degC, mdegC, Q8.7, flags)
            v
+------------------------------------------------------+
| ADT7410 Driver  (src/adt7410_driver.c)    SDD-ADT    |
|  - argument & state validation                       |
|  - register encode / decode, unit conversion         |
|  - one adt7410_dev_t per physical sensor             |
+------------------------------------------------------+
            | axi_iic_write_bytes / axi_iic_write_read_bytes
            v
+------------------------------------------------------+
| AXI IIC HAL  (src/axi_iic_hal.c)          SDD-IIC    |
|  - dynamic-mode framing (START / RSTART / STOP)      |
|  - bounded polling of SR / ISR, error recovery       |
|  - one axi_iic_dev_t per AXI IIC IP instance         |
+------------------------------------------------------+
            | 32-bit volatile MMIO (reg_read32/reg_write32)
            v
+------------------------------------------------------+
| Xilinx AXI IIC IP (PL)  --SCL/SDA 100 kHz-->  ADT7410|
+------------------------------------------------------+
```

### 2.2 Design constraints

| Constraint | Design decision |
|:---|:---|
| Determinism / WCET [RULES §6] | Polled I/O only, no interrupts in the HAL. Every wait loop is bounded by `timeout_polls ≤ AXI_IIC_MAX_POLL_LIMIT`. |
| No dynamic memory [RULES §8] | All buffers are local and ≤ 3 bytes. The instance contexts are owned by the caller. |
| No global coupling [RULES §3] | No `extern` data. State lives only in caller-owned `axi_iic_dev_t` / `adt7410_dev_t`. |
| SEU robustness [RULES §9] | Lifecycle states use 32-bit patterns with Hamming distance 31. Corrupted enums reach defensive branches. |
| Unit safety [RULES §11.1] | Raw register values never leave `adt7410_driver.c`. The API uses degC (float), mdegC, Q8.7 and degC hysteresis. |
| MC/DC [RULES §5] | ≤ 3 conditions per decision. No variable is used twice in one decision (range checks use nested `if` helpers). |
| Concurrency | Not ISR-safe. Reentrant across distinct instances. Calls that share one bus must be serialised by the caller (e.g. an RTOS mutex). |

### 2.3 Error propagation

```
HAL status                         Driver status
------------------------------     ----------------------
AXI_IIC_OK                     ->  ADT7410_OK
AXI_IIC_ERR_PARAM / ERR_STATE  \
AXI_IIC_ERR_BUS_BUSY            |
AXI_IIC_ERR_NACK_ARB            |-> ADT7410_ERR_I2C_COMM   (SDD-ADT-3.3)
AXI_IIC_ERR_TIMEOUT            /
```

The driver itself produces `ERR_PARAM` (bad argument), `ERR_STATE` (not
initialised, or `active_res` corrupted) and `ERR_DEV_ID` (wrong silicon).

---

## 3. AXI IIC HAL design (`SDD-IIC`)

### SDD-IIC-1.0 — Module overview

Polled master-only driver for the Xilinx AXI IIC IP in **dynamic mode**. Each
TX_FIFO word carries the START (bit 8) and STOP (bit 9) controls, so a full
transaction is queued as FIFO words and needs no CR.MSMS sequencing.
Configuration: 100 MHz AXI clock, 100 kHz Standard Mode, 7-bit addresses.
Traces: LLR-IIC-001 … 004.

### 3.1 Data design

#### SDD-IIC-1.1 — `axi_iic_status_t`

| Enumerator | Value | Meaning |
|:---|:---:|:---|
| `AXI_IIC_OK` | 0 | Success |
| `AXI_IIC_ERR_PARAM` | 1 | NULL pointer, address > 0x7F, length outside 1..32, bad poll budget |
| `AXI_IIC_ERR_STATE` | 2 | Instance not `READY` |
| `AXI_IIC_ERR_BUS_BUSY` | 3 | SR.BB still set after `P` polls before START |
| `AXI_IIC_ERR_NACK_ARB` | 4 | ISR.TX_ERR (NACK) or ISR.ARB_LOST detected |
| `AXI_IIC_ERR_TIMEOUT` | 5 | FIFO or completion wait exceeded `P` polls |

#### SDD-IIC-1.2 — `axi_iic_init_state_t`

| Enumerator | Value | Note |
|:---|:---|:---|
| `AXI_IIC_STATE_UNINIT` | `0x25C3691E` | Bit 31 clear: C99 §6.7.2.2p2 requires every enum constant to fit in `int` |
| `AXI_IIC_STATE_READY` | `0x5A3C96E1` | XOR with UNINIT = `0x7FFFFFFF` → Hamming distance 31 |

Any other value (zeroed RAM, bit-flip) counts as "not ready".

#### SDD-IIC-1.3 — `axi_iic_dev_t`

| Field | Type | Range / unit | Written by |
|:---|:---|:---|:---|
| `base_addr` | `uintptr_t` | Physical MMIO address, ≠ 0 | `axi_iic_init` |
| `timeout_polls` | `uint32_t` | 1..10 000 000 SR polls per wait | `axi_iic_init` |
| `init_state` | `axi_iic_init_state_t` | SDD-IIC-1.2 | `axi_iic_init` |

#### Register map used (PG090)

| Offset | Register | Use |
|:---|:---|:---|
| 0x020 | ISR | Bit 0 ARB_LOST, bit 1 TX_ERR; toggle-on-write clear |
| 0x040 | SOFTR | Write `0xA` = soft reset |
| 0x100 | CR | Bit 0 EN, bit 1 TX_FIFO_RESET |
| 0x104 | SR | Bit 2 BB, bit 3 SRW (master reading), bit 4 TX_FULL, bit 5 RX_FULL, bit 6 RX_EMPTY, bit 7 TX_EMPTY |
| 0x108 | TX_FIFO | `[9]` STOP, `[8]` START, `[7:0]` data / address / count |
| 0x10C | RX_FIFO | `[7:0]` received byte |
| 0x120 | RX_FIFO_PIRQ | `0x0F` (16-deep) |
| 0x128 … 0x144 | Timing | See SDD-IIC-2.1 |

### 3.2 Interface design

#### SDD-IIC-2.1 — `axi_iic_init(p_dev, base_addr, timeout_polls)`

| Step | Action |
|:---:|:---|
| 1 | Validate: `p_dev ≠ NULL`, `base_addr ≠ 0`, `is_timeout_valid()` (SDD-IIC-3.9). Otherwise → `ERR_PARAM`, no MMIO. |
| 2 | Store `base_addr` and `timeout_polls`. Set `init_state = UNINIT`. |
| 3 | SOFTR ← `0xA`. |
| 4 | Program the timing registers (table below). |
| 5 | RX_FIFO_PIRQ ← `0x0F`. |
| 6 | `clear_tx_fifo_and_errors()` (SDD-IIC-3.3). |
| 7 | `init_state = READY`, return `OK`. |

| Register | Ticks @ 10 ns | Time | PG090 / ADT7410 constraint |
|:---|---:|---:|:---|
| TSUSTA | 570 | 5.70 µs | ≥ 4.7 µs (ADT7410 ≥ 0.6 µs) |
| TSUSTO | 500 | 5.00 µs | ≥ 4.0 µs (ADT7410 ≥ 0.6 µs) |
| THDSTA | 430 | 4.30 µs | ≥ 4.0 µs (ADT7410 ≥ 0.6 µs) |
| TSUDAT | 55 | 0.55 µs | ≥ 250 ns |
| TBUF | 500 | 5.00 µs | ≥ 4.7 µs (ADT7410 ≥ 1.3 µs) |
| THIGH | 493 | 5.00 µs | 100 kHz half period (PG090 formula) |
| TLOW | 493 | 5.00 µs | 100 kHz half period (PG090 formula) |
| THDDAT | 30 | 0.30 µs | ≥ 300 ns (ADT7410 ≥ 0 ns) |

WCET: constant (12 MMIO accesses + `clear_tx_fifo_and_errors`).

#### SDD-IIC-2.2 — `axi_iic_write_bytes(p_dev, slave_addr_7b, p_tx_bytes, n)`

```
validate_xfer_args()            -> PARAM / STATE: return, no bus access
clear_tx_fifo_and_errors()
wait_bus_not_busy()             -> BUS_BUSY
push  START | addr<<1 | W
for i in 0..n-1:
    push  p_tx_bytes[i]  (| STOP if i == n-1)     -> NACK_ARB / TIMEOUT
wait_tx_complete()              -> NACK_ARB / TIMEOUT
```

Bus sequence: `S | ADDR+W | A | D0 | A | … | Dn-1 | A | P`.
WCET: ≤ `(n + 3) × P` poll iterations.

#### SDD-IIC-2.3 — `axi_iic_write_read_bytes(p_dev, slave_addr_7b, reg_addr, p_rx_bytes, n)`

```
validate_xfer_args()            -> PARAM / STATE
clear_tx_fifo_and_errors(); wait_bus_not_busy()
push  START | addr<<1 | W
push  reg_addr                  (no STOP -> repeated START follows)
push  START | addr<<1 | R
push  STOP  | n                 (controller receives n bytes, NACKs last, STOPs)
for i in 0..n-1: pop_rx_fifo_byte(&p_rx_bytes[i])
                                (TX_ERR is an error only while RX is empty)
wait_rx_complete()              (only ARB_LOST is an error; clears the
                                 TX_ERR latched by the master's final NACK)
```

Bus sequence: `S | ADDR+W | A | REG | A | Sr | ADDR+R | A | D0 | A | … | Dn-1 | N | P`.
WCET: ≤ `(n + 6) × P` poll iterations.

### 3.3 Internal design

| ID | Function | Purpose | Key design points |
|:---|:---|:---|:---|
| SDD-IIC-3.1 | `reg_read32()` | 32-bit MMIO read | Single `volatile` access. MISRA 11.4 deviation DEV-01 |
| SDD-IIC-3.2 | `reg_write32()` | 32-bit MMIO write | Single `volatile` access. MISRA 11.4 deviation DEV-01 |
| SDD-IIC-3.3 | `clear_tx_fifo_and_errors()` | Recovery primitive | CR ← TX_RST + EN, then CR ← EN; ISR ← (ISR & ERR_FLAGS) toggle-clear |
| SDD-IIC-3.4 | `wait_bus_not_busy()` | Pre-START idle wait | Reads SR before the first test, ≤ `P` polls |
| SDD-IIC-3.5 | `check_isr_errors()` | Error detector | ISR & (ARB_LOST or TX_ERR) ≠ 0 → recovery + `ERR_NACK_ARB` |
| SDD-IIC-3.6 | `push_tx_fifo_word()` | Flow-controlled TX | Waits on TX_FULL with ISR check each poll, ≤ `P` |
| SDD-IIC-3.7 | `pop_rx_fifo_byte()` | Flow-controlled RX | Waits on RX_EMPTY with `check_rx_wait_errors()` each poll, ≤ `P`; keeps bits [7:0] |
| SDD-IIC-3.8 | `wait_tx_complete()` | End-of-transfer wait | Loops on `is_tx_idle()`; last ISR check catches a NACK that latched as the bus went idle |
| SDD-IIC-3.9 | `is_timeout_valid()` | `1 ≤ P ≤ 10⁷` | Nested `if`: no coupled condition |
| SDD-IIC-3.10 | `is_xfer_len_valid()` | `1 ≤ n ≤ 32` | Nested `if`: no coupled condition |
| SDD-IIC-3.11 | `validate_xfer_args()` | Shared public-entry checks | PARAM checks first, then STATE |
| SDD-IIC-3.12 | `is_tx_idle()` | SR → done? | `if / else if` on TX_EMPTY then BB (keeps the decision uncoupled) |
| SDD-IIC-3.13 | `check_rx_wait_errors()` | ISR check while waiting for RX data | ARB_LOST → error. TX_ERR → re-read SR: RX has data → master's final NACK → OK; RX empty but BB = 1 → transfer still running → keep waiting; RX empty and BB = 0 → slave NACK → error |
| SDD-IIC-3.14 | `check_arb_lost()` | ISR check after all bytes are received | Only ARB_LOST is an error (with recovery) |
| SDD-IIC-3.15 | `clear_master_nack_flag()` | Clear the expected flag | Toggle-on-write of ISR.TX_ERR if latched |
| SDD-IIC-3.16 | `wait_rx_complete()` | End-of-receive wait | Same loop as `wait_tx_complete()` using `check_arb_lost()`; on success calls `clear_master_nack_flag()` |
| SDD-IIC-3.17 | `rx_timeout_status()` | Classify an RX wait without data | TX_ERR latched → recovery + `ERR_NACK_ARB`; else `ERR_TIMEOUT` |

**Why 3.13 … 3.16 exist (OI-03, confirmed on hardware):** at the end of a
read the master NACKs the last byte to tell the slave to stop. The AXI IIC
core latches ISR.TX_ERR for that NACK too. The board diagnostic gave, at
address 0x48: HAL `ERR_NACK_ARB`, ID byte `0xCB` received correctly,
SR `0xCC`, ISR `0xD0`. Treating that flag as a slave error made every read
fail after its data had arrived. A real slave NACK still fails, because it
leaves the RX FIFO empty (board result at 0x49 … 0x4B: `ERR_NACK_ARB`, no
data, SR `0xC4`).

**Flag / data order (raw probe on the board, no HAL):** `id = 0xCB`, ISR
before `0xD0`, ISR *when the data byte was first seen* `0xD2`, ISR at end
`0xD2` (TX_ERR = 1, ARB_LOST = 0), SR at end `0xC8`. TX_ERR is therefore
already visible when the last byte becomes visible; the two events are
less than one register read apart, in an order that cannot be resolved.
SDD-IIC-3.13 thus never decides from "TX_ERR set and RX still empty"
alone: while SR.BB = 1 it keeps waiting, and only a released bus without
data is a slave NACK. A behavioural model of the core reproduces the board
readout exactly and shows that a check based only on the RX FIFO fails if
the flag leads the data by one SCL bit, while 3.13 passes in every order.

---

## 4. ADT7410 driver design (`SDD-ADT`)

### SDD-ADT-1.0 — Module overview

Stateless driver except for the caller-owned `adt7410_dev_t`. Every public
function follows the same pattern: (1) validate pointers → `ERR_PARAM`,
(2) validate ranges and enums → `ERR_PARAM`, (3) check `init_state` →
`ERR_STATE`, (4) one or two register transactions, (5) decode or convert,
writing outputs only on success. Traces: LLR-ADT-001 … 005.

### 4.1 Data design

#### SDD-ADT-1.1 — `adt7410_status_t`

| Enumerator | Value | Produced by |
|:---|:---:|:---|
| `ADT7410_OK` | 0 | All functions |
| `ADT7410_ERR_PARAM` | 1 | Argument checks, `setpoint_to_reg()` |
| `ADT7410_ERR_STATE` | 2 | `init_state ≠ READY`; corrupted `active_res` (SDD-ADT-3.20) |
| `ADT7410_ERR_I2C_COMM` | 3 | `map_iic_status()` |
| `ADT7410_ERR_DEV_ID` | 4 | `read_id_fields()` |
| `ADT7410_ERR_NOT_READY` | 5 | *Not produced at present (open item `OI-02`)* |

#### SDD-ADT-1.2 — `adt7410_init_state_t`

| Enumerator | Value |
|:---|:---|
| `ADT7410_STATE_UNINIT` | `0x3C96A5E1` |
| `ADT7410_STATE_READY` | `0x43695A1E` (XOR = `0x7FFFFFFF`, Hamming distance 31) |

#### SDD-ADT-1.3 … 1.7 — Configuration enums (register 0x03, Table 11)

The numeric value of each enumerator **equals** its register field code. That
lets `config_to_reg()` shift values directly.

| ID | Type | Field | Codes |
|:---|:---|:---|:---|
| SDD-ADT-1.3 | `adt7410_resolution_t` | bit 7 | 0 = 13-bit (0.0625 °C), 1 = 16-bit (0.0078125 °C) |
| SDD-ADT-1.4 | `adt7410_op_mode_t` | bits [6:5] | 0 continuous, 1 one-shot, 2 1 SPS, 3 shutdown |
| SDD-ADT-1.5 | `adt7410_event_mode_t` | bit 4 | 0 interrupt, 1 comparator |
| SDD-ADT-1.6 | `adt7410_pin_polarity_t` | bit 3 (INT), bit 2 (CT) | 0 active low, 1 active high |
| SDD-ADT-1.7 | `adt7410_fault_queue_t` | bits [1:0] | 0..3 → 1..4 faults |

#### SDD-ADT-1.8 — `adt7410_setpoint_id_t`

| Enumerator | Register (MSB, LSB) |
|:---|:---|
| `ADT7410_SETPOINT_THIGH` | 0x04, 0x05 |
| `ADT7410_SETPOINT_TLOW` | 0x06, 0x07 |
| `ADT7410_SETPOINT_TCRIT` | 0x08, 0x09 |

#### SDD-ADT-1.9 — `adt7410_config_t`

Holds one field of each SDD-ADT-1.3 … 1.7 type (`resolution`, `op_mode`,
`event_mode`, `int_polarity`, `ct_polarity`, `fault_queue`).

#### SDD-ADT-1.10 — `adt7410_status_flags_t` (register 0x02)

| Field | Bit | Meaning |
|:---|:---:|:---|
| `is_not_ready` | 7 | RDY (active low): 1 = conversion result not yet written |
| `has_tcrit_flt` | 6 | T ≥ TCRIT |
| `has_thigh_flt` | 5 | T ≥ THIGH |
| `has_tlow_flt` | 4 | T ≤ TLOW |

#### SDD-ADT-1.11 — `adt7410_sample_t`

| Field | Type | Unit / format | Range |
|:---|:---|:---|:---|
| `die_temp_mdegc` | `int32_t` | milli-°C | nominal −55 000..150 000 |
| `die_temp_q8_7` | `int16_t` | Q8.7 °C (value / 2⁷) | −32 768..32 767 ↔ −256.0..+255.9921875 °C |
| `has_inline_flags` | `bool` | — | true in 13-bit mode |
| `is_tcrit_active` / `is_thigh_active` / `is_tlow_active` | `bool` | — | Temperature register bits 2 / 1 / 0 in 13-bit mode, else false |

#### SDD-ADT-1.12 — `adt7410_dev_t`

| Field | Type | Meaning |
|:---|:---|:---|
| `p_iic_bus` | `axi_iic_dev_t const *` | Bus instance. Must outlive the device context. |
| `slave_addr_7b` | `uint8_t` | 0x48..0x4B (A1/A0 strap) |
| `active_res` | `adt7410_resolution_t` | Software copy of config bit 7, used to decode the temperature register |
| `init_state` | `adt7410_init_state_t` | SDD-ADT-1.2 |

### 4.2 Fixed-point formats and conversions

**Q8.7** (per [RULES §11.1], `m` excludes the sign bit): `int16_t`, 1 sign + 8
integer + 7 fraction bits, `°C = q / 128`, LSB = 0.0078125 °C.

- 16-bit mode: the temperature register *is* a Q8.7 word.
- 13-bit mode: bits [15:3] hold sign + 12 bits at 0.0625 °C. Since
  0.0625 = 8 / 128, clearing bits [2:0] leaves a valid Q8.7 word. Both modes
  share one decode path.

| Conversion | Formula (rounded half away from zero where integer) | Overflow / exactness analysis |
|:---|:---|:---|
| reg → Q8.7 (3.16) | `q = w` if `w < 0x8000`, else `q = w − 65536` | Avoids implementation-defined `uint16_t → int16_t` (C99 §6.3.1.3p3) |
| Q8.7 → reg (3.17) | `w = q` if `q ≥ 0`, else `w = q + 65536` | Result in 0..65535, so the cast is exact |
| Q8.7 → mdegC (3.18) | `(q·1000 ± 64) / 128` in `int32_t` | Absolute value of `q·1000` ≤ 32 768 000 < 2³¹ |
| mdegC → Q8.7 (3.19) | `(m·128 ± 500) / 1000` in `int32_t` | Input pre-limited to absolute value ≤ 150 000, so `m·128` ≤ 19 200 000 < 2³¹ |
| Q8.7 → float degC (3.21) | `(float)q × 0.0078125f` (2⁻⁷) | Exact in IEEE-754 binary32: a 16-bit integer fits in the 24-bit significand and 2⁻⁷ only shifts the exponent |

Worked examples (they match the host test vectors):

| Temperature | Q8.7 | Register word | mdegC | float degC |
|:---|---:|:---:|---:|---:|
| +150 °C | 19 200 | `0x4B00` | 150 000 | 150.0 |
| +147 °C (TCRIT default) | 18 816 | `0x4980` | 147 000 | 147.0 |
| +64 °C (THIGH default) | 8 192 | `0x2000` | 64 000 | 64.0 |
| +25 °C | 3 200 | `0x0C80` | 25 000 | 25.0 |
| +1 LSB | 1 | `0x0001` | 8 (7.8125 rounded) | 0.0078125 |
| −1 LSB | −1 | `0xFFFF` | −8 | −0.0078125 |
| −25 °C | −3 200 | `0xF380` | −25 000 | −25.0 |
| −55 °C | −7 040 | `0xE480` | −55 000 | −55.0 |
| 13-bit, 25 °C + TCRIT + TLOW | 3 200 | `0x0C85` | 25 000, flags T/F/T | 25.0 |

### 4.3 Interface design

All functions are blocking and polled, not ISR-safe, and reentrant across
distinct device/bus pairs. The I2C transactions use the helpers in §4.4.
Let `P` = `timeout_polls`.

| ID | Function | Validation (in order) | Transactions | Success action | WCET (polls) |
|:---|:---|:---|:---|:---|:---|
| SDD-ADT-2.1 | `adt7410_init` | `p_dev` NULL → PARAM; set UNINIT; `p_iic_bus`/`p_cfg` NULL or addr ∉ 0x48..0x4B → PARAM; `!is_config_valid` → PARAM | R8(0x0B), W8(0x03) | `active_res = cfg.res`, `READY` | 12·P |
| SDD-ADT-2.2 | `adt7410_soft_reset` | NULL → PARAM; STATE | CMD(0x2F) | `active_res = 13BIT` | 4·P |
| SDD-ADT-2.3 | `adt7410_read_id` | 3 × NULL → PARAM; STATE | R8(0x0B) | outputs; DEV_ID if mfg ≠ 0x19 | 7·P |
| SDD-ADT-2.4 | `adt7410_set_config` | NULL → PARAM; `!is_config_valid` → PARAM; STATE | W8(0x03) | `active_res = cfg.res` | 5·P |
| SDD-ADT-2.5 | `adt7410_get_config` | NULL → PARAM; STATE | R8(0x03) | `reg_to_config()` | 7·P |
| SDD-ADT-2.6 | `adt7410_read_status` | NULL → PARAM; STATE | R8(0x02) | `reg_to_status_flags()` | 7·P |
| SDD-ADT-2.7 | `adt7410_trigger_one_shot` | NULL → PARAM; STATE | R8(0x03), W8(0x03) | bits [6:5] ← 01b, other bits kept | 12·P |
| SDD-ADT-2.8 | `adt7410_read_temperature` | NULL → PARAM; STATE | R16(0x00) | `reg16_to_sample()` | 8·P |
| SDD-ADT-2.9 | `adt7410_set_temp_setpoint` | `p_dev` NULL → PARAM; range → PARAM; STATE; selector → PARAM | W16(reg) | — | 6·P |
| SDD-ADT-2.10 | `adt7410_get_temp_setpoint` | NULL → PARAM; STATE; selector → PARAM | R16(reg) | Q8.7 → mdegC | 8·P |
| SDD-ADT-2.11 | `adt7410_set_hysteresis` | NULL → PARAM; > 15 → PARAM; STATE | W8(0x0A) | — | 5·P |
| SDD-ADT-2.12 | `adt7410_get_hysteresis` | NULL → PARAM; STATE | R8(0x0A) | bits [3:0] | 7·P |
| SDD-ADT-2.13 | `adt7410_read_temp_degc` | NULL → PARAM; STATE (via 2.8) | R16(0x00) | `adt7410_read_temperature()` + `q8_7_to_degc()` → `*p_die_temp_degc` | 8·P |

Transaction legend: W8 = 2-byte write `[reg, data]` (HAL n = 2 → 5·P);
W16 = 3-byte write `[reg, MSB, LSB]` (6·P); CMD = 1-byte write (4·P);
R8 = write-read n = 1 (7·P); R16 = write-read n = 2, MSB first (8·P).

> **Wall-clock note:** `P` counts poll iterations, not time. Pick `P` so that
> `P × t_poll` is longer than the slowest single wait (≈ 0.4 ms for a 2-byte
> read at 100 kHz, plus any slave clock stretching). See SDD-APP-4.

#### Sequence: `adt7410_read_temperature()` (16-bit mode)

```
App            ADT7410 driver               AXI IIC HAL                 Bus / ADT7410
 |  read_temp()      |                            |                          |
 |------------------>| check NULL / READY         |                          |
 |                   | read_reg16(0x00)           |                          |
 |                   |--------------------------->| write_read(0x48,0x00,2)  |
 |                   |                            |  S 0x90 A 0x00 A         |
 |                   |                            |  Sr 0x91 A MSB A LSB N P |
 |                   |<------- OK, w = MSB<<8|LSB |<-------------------------|
 |                   | reg16_to_sample(w, 16BIT)  |                          |
 |                   |  q = reg16_to_q8_7(w)      |                          |
 |                   |  mdegC = q8_7_to_mdegc(q)  |                          |
 |<---- OK, sample --|                            |                          |
```

`adt7410_read_temp_degc()` performs the same sequence and then applies
`q8_7_to_degc()` to `sample.die_temp_q8_7`.

#### Device lifecycle

```
            adt7410_init() OK
  UNINIT ----------------------> READY --+ set_config / soft_reset /
    ^  ^                           |    | read_* / get_* / set_* (stay READY)
    |  +-- init() any failure ------+<---+
    |
    +-- any other value (SEU) is treated as not READY -> ERR_STATE
```

### 4.4 Internal design

| ID | Item | Purpose | Key design points |
|:---|:---|:---|:---|
| SDD-ADT-3.1 | `op_mode_lut[4]` | Config bits [6:5] → `adt7410_op_mode_t` | `static const`. Avoids an integer→enum cast (MISRA 10.5) |
| SDD-ADT-3.2 | `fault_queue_lut[4]` | Config bits [1:0] → `adt7410_fault_queue_t` | Same as 3.1 |
| SDD-ADT-3.3 | `map_iic_status()` | HAL → driver status | Every non-OK HAL code → `ERR_I2C_COMM` (§2.3) |
| SDD-ADT-3.4 | `write_reg8()` | `[reg, byte]` write | Data coupling: takes only bus + address, not `adt7410_dev_t` |
| SDD-ADT-3.5 | `write_reg16()` | `[reg, MSB, LSB]` write | Explicit big-endian serialisation (no struct overlay) |
| SDD-ADT-3.6 | `read_reg8()` | 1-byte read with repeated START | Output written only on success |
| SDD-ADT-3.7 | `read_reg16()` | 2-byte read, `w = (MSB << 8) + LSB` | Device auto-increments the pointer |
| SDD-ADT-3.8 | `read_id_fields()` | Read 0x0B, split [7:3]/[2:0], compare 0x19 | Shared by `init` (before READY) and `read_id` |
| SDD-ADT-3.9 | `is_slave_addr_valid()` | 0x48 ≤ a ≤ 0x4B | Nested `if` (MC/DC, uncoupled) |
| SDD-ADT-3.10 | `is_temp_in_range()` | −55 000 ≤ m ≤ 150 000 | Nested `if` (MC/DC, uncoupled) |
| SDD-ADT-3.11 | `is_config_valid()` | Each enum field ≤ its maximum code | Two decisions × 3 conditions; enums cast to `uint32_t`, so negative values are rejected too |
| SDD-ADT-3.12 | `config_to_reg()` | Encode register 0x03 | `res<<7`, `mode<<5`, `evt<<4`, `int<<3`, `ct<<2`, `fq` OR-ed together |
| SDD-ADT-3.13 | `reg_to_config()` | Decode register 0x03 | 1-bit fields via `if/else`; 2-bit fields via LUT (index < 4 by mask, ASSERT) |
| SDD-ADT-3.14 | `reg_to_status_flags()` | Decode register 0x02 | Single relational per flag (no compound boolean assignment) |
| SDD-ADT-3.15 | `setpoint_to_reg()` | Selector → MSB register | `switch` with 3 cases; `default` → `ERR_PARAM` (reachable from caller input, so not tagged DEFENSIVE) |
| SDD-ADT-3.16 | `reg16_to_q8_7()` | Two's complement decode | §4.2 |
| SDD-ADT-3.17 | `q8_7_to_reg16()` | Two's complement encode | §4.2, ASSERT on the int16 range |
| SDD-ADT-3.18 | `q8_7_to_mdegc()` | Q8.7 → mdegC | §4.2 |
| SDD-ADT-3.19 | `mdegc_to_q8_7()` | mdegC → Q8.7 | §4.2 |
| SDD-ADT-3.20 | `reg16_to_sample()` | Build `adt7410_sample_t` | `switch(active_res)`: 13-bit masks `0xFFF8` and extracts flags; 16-bit takes the full word; `default` is DEFENSIVE → `ERR_STATE`. Builds a local struct and copies it to `*p_sample` only on OK |
| SDD-ADT-3.21 | `q8_7_to_degc()` | Q8.7 → float degC | `(float)temp_q8_7 × 0.0078125f` (§4.2); exact for both 13-bit and 16-bit modes |

#### Configuration encoding examples

| Input | Register 0x03 |
|:---|:---:|
| 16-bit, continuous, comparator, INT high, CT low, FQ 3 | `0x9A` |
| One-shot trigger applied to `0x9B` (RMW, bits [6:5] ← 01) | `0xBB` |

---

## 5. Reference application design (`SDD-APP`, `src/main.c`)

### SDD-APP-1.0 — Module overview

`src/main.c` is a reference health monitor that shows the intended use of the
driver. It runs two bounded examples in sequence: a 10 ms polling loop
(comparator mode) and an INT-pin interrupt loop (interrupt mode). Each loop
iteration stands for one 10 ms RTOS task activation. Traces: LLR-APP-001 … 005.

### 5.1 Data design

#### SDD-APP-1.1 — `app_health_snapshot_t`

| Field | Type | Unit / range | Written by |
|:---|:---|:---|:---|
| `last_die_temp_degc` | `float` | degC, nominal −55.0..150.0 | `poll_sensor_once`, `service_int_alert` |
| `sample_count` | `uint32_t` | valid samples | same |
| `comm_error_count` | `uint32_t` | recoveries started | `recover_bus_and_sensor` |
| `is_tcrit_tripped` / `is_thigh_tripped` / `is_tlow_tripped` | `bool` | Status register bits 6 / 5 / 4 | `poll_sensor_once`, `service_int_alert` |

Only one temperature representation (degC) is stored, and both paths write
every field together after both reads succeed, so the snapshot is always
self-consistent.

#### SDD-APP-1.2 — File-scope objects

| Object | Type | Purpose |
|:---|:---|:---|
| `g_iic_bus` | `axi_iic_dev_t` | AXI IIC HAL instance (`static`) |
| `g_sensor` | `adt7410_dev_t` | ADT7410 context, address 0x48 (`static`) |
| `g_health_snapshot` | `app_health_snapshot_t` | Latest telemetry (`static`) |
| `g_is_int_alert_pending` | `volatile bool` | Set by the ISR, consumed by the task (`static`) |

### 5.2 Interface design

| ID | Function | Design |
|:---|:---|:---|
| SDD-APP-2.1 | `adt7410_int_pin_isr()` | One `volatile` store `g_is_int_alert_pending = true`. No I2C, no loop, constant WCET. |
| SDD-APP-2.2 | `main()` | `run_polling_example()`; if OK, `run_interrupt_example()`; returns 0 when both succeed, else 1. |

### 5.3 Internal design

| ID | Function | Design |
|:---|:---|:---|
| SDD-APP-3.1 | `setup_bus_and_sensor(event_mode)` | `axi_iic_init(0x40800000, APP_IIC_TIMEOUT_POLLS)` (error → `ERR_I2C_COMM`); `adt7410_init` (16-bit, continuous, `event_mode`, INT/CT active low, FQ 2); then `configure_alarm_thresholds()`. ≤ 35·P. |
| SDD-APP-3.2 | `configure_alarm_thresholds()` | THIGH 85 000, TLOW −10 000, TCRIT 105 000 mdegC, THYST 5 °C; stops at the first error. ≤ 23·P. |
| SDD-APP-3.3 | `recover_bus_and_sensor(event_mode)` | `comm_error_count++`, then `setup_bus_and_sensor()`. ≤ 35·P. |
| SDD-APP-3.4 | `poll_sensor_once()` | `adt7410_read_status` then `adt7410_read_temp_degc`; snapshot updated only if both succeed. ≤ 15·P. |
| SDD-APP-3.5 | `run_polling_example()` | `while` loop (MISRA 14.2), ≤ 10 cycles. Each cycle does **either** a poll **or** a pending recovery, never both. A recovery still pending after the last cycle returns the poll error. |
| SDD-APP-3.6 | `service_int_alert()` | `adt7410_read_temp_degc` then `adt7410_read_status` (unlatches INT); snapshot updated only if both succeed. ≤ 15·P. |
| SDD-APP-3.7 | `run_interrupt_example()` | `while` loop, ≤ 10 cycles, simulated INT in cycle 3. Each cycle does either `service_pending_alert()` or a pending recovery. |
| SDD-APP-3.8 | `service_pending_alert()` | Reads the `volatile` flag once into a local. If set: clear it, then `service_int_alert()`. On failure the flag is set again, because the INT pin stays latched and no new edge will arrive. |

Cycle state machine (both examples):

```
             poll / alert OK
        +-------------------------+
        v                         |
   [ NORMAL ] --poll/alert fails--+--> [ RECOVERY PENDING ]
        ^                                   |
        |      next cycle: recover() OK     |
        +-----------------------------------+
                recover() fails -> example ends with error
```

### SDD-APP-4 — Timing analysis

Let `t_poll` be the duration of one poll iteration (about one ISR read and one
SR read over AXI4-Lite, target dependent).

| Bound | Normal cycle | Recovery cycle |
|:---|:---|:---|
| Analytic (every wait at its limit `P`) | 15·P polls | 35·P polls |
| Fault case (healthy transfers take real bus time; the first failing wait aborts the call) | ≈ 0.85 ms bus time + 1 × P·t_poll | ≈ 2.2 ms bus time + 1 × P·t_poll |

Constraints on `P = APP_IIC_TIMEOUT_POLLS`:

1. **No false timeout:** `P × t_poll_min` > longest single wait ≈ 0.4 ms
   (2-byte read: ~37 SCL clocks + START / repeated START / STOP set-up).
   `P = 5000` satisfies this only for `t_poll ≥ 80 ns`.
2. **10 ms period:** with the analytic bound, `35 × P × t_poll_max ≤ 10 ms`.
   For `P = 5000` this needs `t_poll ≤ 57 ns`, which conflicts with (1).

The analytic bound therefore cannot prove the 10 ms period with `P = 5000`.
The fault-case bound (a few ms) does fit. Closing this needs either a
measurement-based WCET argument on the target, or a HAL deadline based on a
hardware timer. Tracked as **`OI-08`**. Deferring recovery to its own cycle
(SDD-APP-3.5, 3.7) already lowers the per-cycle analytic bound from 50·P
(poll + recovery) to 35·P.

---

## 6. Resource usage

### 6.1 Stack usage (per function, `-fstack-usage -O1 -fno-inline`)

> ⚠️ The figures below come from a **host x86-64 build**. Regenerate them
> with the qualified ARM cross-compiler before using them as WCSU evidence
> (`OI-04`).

| Function | Bytes | Function | Bytes |
|:---|---:|:---|---:|
| `adt7410_init` | 64 | `axi_iic_write_read_bytes` | 48 |
| `adt7410_read_temp_degc` | 64 | `axi_iic_write_bytes` | 40 |
| `reg16_to_sample` | 56 | `push_tx_fifo_word` / `pop_rx_fifo_byte` | 40 |
| `read_id_fields` | 48 | `wait_tx_complete` / `axi_iic_init` | 32 |
| `adt7410_read_temperature` / `_get_config` / `_read_status` / `_set_temp_setpoint` / `_get_temp_setpoint` | 48 | other HAL functions | 8 … 24 |
| `read_reg8` / `read_reg16` / `write_reg8` / `write_reg16` | 32 | `setup_bus_and_sensor` / `run_interrupt_example` (app) | 48 |
| other driver functions | 8 … 32 | other app functions | 16 … 32 |

Deepest call chains (host build):

```
Driver only:
adt7410_read_temp_degc (64) -> adt7410_read_temperature (48) -> read_reg16 (32)
 -> axi_iic_write_read_bytes (48) -> pop_rx_fifo_byte (40)
 -> check_isr_errors (16) -> clear_tx_fifo_and_errors (16) -> reg_write32 (8)
 = 272 bytes

Whole program (reference application):
main (16) -> run_interrupt_example (48) -> recover_bus_and_sensor (16)
 -> setup_bus_and_sensor (48) -> adt7410_init (64) -> read_id_fields (48)
 -> read_reg8 (32) -> axi_iic_write_read_bytes (48) -> pop_rx_fifo_byte (40)
 -> check_isr_errors (16) -> clear_tx_fifo_and_errors (16) -> reg_write32 (8)
 = 400 bytes
```

No function exceeds the per-function `-Wstack-usage=256` limit. There is no
recursion and no function pointer, so the call graph is static.

### 6.2 Timing

The WCET in poll iterations is given per function in SDD-IIC-2.2/2.3, in the
§4.3 table and in SDD-APP-4. Each iteration costs 2–3 MMIO reads, plus one
recovery sequence when an error is found.

---

## 7. Deviations, defensive code and assertions

### 7.1 MISRA deviation log

| ID | Rule | Location | Reason | Mitigation |
|:---|:---|:---|:---|:---|
| DEV-01 | MISRA C:2012 Rule 11.4 (integer → pointer cast) | `src/axi_iic_hal.c`: `reg_read32()`, `reg_write32()` | MMIO register access needs an address-to-pointer conversion | Only in these two `static inline` accessors. Address = validated `base_addr` + compile-time offset. One `volatile` access per call. |

### 7.2 Defensive branches (structural coverage)

| Location | Fault guarded | Coverage approach |
|:---|:---|:---|
| `reg16_to_sample()` `default:` | `active_res` corrupted by SEU / RAM fault | Fault-injection tests TC-ADT-033 (`read_temperature`) and TC-ADT-036 (`read_temp_degc`) — LLR-ADT-003.6 |
| All `init_state ≠ READY` checks | Uninitialised or corrupted lifecycle state | Normal test (call before init) plus 32-bit single-bit-flip sweep |

### 7.3 Assertions

`ASSERT(expr)` compiles to `((void)0)` unless `DEBUG_BUILD` is defined, so it
creates no object code or coverage gap in flight builds. It guards only
internal contracts of `static` helpers: non-NULL pointers, LUT index < 4, and
the int16 range in `q8_7_to_reg16()`.

> **Open item `OI-05`:** in `DEBUG_BUILD` the trap body is empty. Connect it
> to the project fault handler.

---

## 8. Traceability matrix (LLR ↔ SDD ↔ source)

| LLR | SDD | Source element(s) |
|:---|:---|:---|
| LLR-IIC-001 | SDD-IIC-1.1, 1.2, 3.1, 3.2 | `axi_iic_status_t`, `axi_iic_init_state_t`, `reg_read32`, `reg_write32` |
| LLR-IIC-002 | SDD-IIC-1.3, 2.1, 3.3, 3.9 | `axi_iic_dev_t`, `axi_iic_init`, `clear_tx_fifo_and_errors`, `is_timeout_valid` |
| LLR-IIC-003 | SDD-IIC-2.2, 3.4, 3.5, 3.6, 3.8, 3.10, 3.11, 3.12 | `axi_iic_write_bytes`, `wait_bus_not_busy`, `check_isr_errors`, `push_tx_fifo_word`, `wait_tx_complete`, `is_xfer_len_valid`, `validate_xfer_args`, `is_tx_idle` |
| LLR-IIC-004 | SDD-IIC-2.3, 3.7, 3.10, 3.11, 3.13 … 3.17 | `axi_iic_write_read_bytes`, `pop_rx_fifo_byte`, `is_xfer_len_valid`, `validate_xfer_args`, `check_rx_wait_errors`, `check_arb_lost`, `clear_master_nack_flag`, `wait_rx_complete`, `rx_timeout_status` |
| LLR-ADT-001 | SDD-ADT-1.1, 1.2, 1.12, 2.1, 2.2, 2.3, 3.3, 3.6, 3.8, 3.9 | `adt7410_status_t`, `adt7410_init_state_t`, `adt7410_dev_t`, `adt7410_init`, `adt7410_soft_reset`, `adt7410_read_id`, `map_iic_status`, `read_reg8`, `read_id_fields`, `is_slave_addr_valid` |
| LLR-ADT-002 | SDD-ADT-1.3 … 1.7, 1.9, 2.4, 2.5, 2.7, 3.1, 3.2, 3.4, 3.6, 3.11, 3.12, 3.13 | config enums, `adt7410_config_t`, `adt7410_set_config`, `adt7410_get_config`, `adt7410_trigger_one_shot`, `op_mode_lut`, `fault_queue_lut`, `write_reg8`, `read_reg8`, `is_config_valid`, `config_to_reg`, `reg_to_config` |
| LLR-ADT-003 | SDD-ADT-1.10, 1.11, 2.6, 2.8, 2.13, 3.6, 3.7, 3.14, 3.16, 3.18, 3.20, 3.21 | `adt7410_status_flags_t`, `adt7410_sample_t`, `adt7410_read_status`, `adt7410_read_temperature`, `adt7410_read_temp_degc`, `read_reg8`, `read_reg16`, `reg_to_status_flags`, `reg16_to_q8_7`, `q8_7_to_mdegc`, `q8_7_to_degc`, `reg16_to_sample` |
| LLR-ADT-004 | SDD-ADT-1.8, 2.9, 2.10, 3.5, 3.7, 3.10, 3.15 … 3.19 | `adt7410_setpoint_id_t`, `adt7410_set_temp_setpoint`, `adt7410_get_temp_setpoint`, `write_reg16`, `read_reg16`, `is_temp_in_range`, `setpoint_to_reg`, `reg16_to_q8_7`, `q8_7_to_reg16`, `q8_7_to_mdegc`, `mdegc_to_q8_7` |
| LLR-ADT-005 | SDD-ADT-2.11, 2.12, 3.4, 3.6 | `adt7410_set_hysteresis`, `adt7410_get_hysteresis`, `write_reg8`, `read_reg8` |
| LLR-APP-001 | SDD-APP-1.2, 3.1, 3.2 | `g_iic_bus`, `g_sensor`, `setup_bus_and_sensor`, `configure_alarm_thresholds` |
| LLR-APP-002 | SDD-APP-1.1, 1.2, 3.3, 3.4, 3.5 | `app_health_snapshot_t`, `g_health_snapshot`, `recover_bus_and_sensor`, `poll_sensor_once`, `run_polling_example` |
| LLR-APP-003 | SDD-APP-1.1, 1.2, 2.1, 3.3, 3.6, 3.7, 3.8 | `g_is_int_alert_pending`, `adt7410_int_pin_isr`, `recover_bus_and_sensor`, `service_int_alert`, `run_interrupt_example`, `service_pending_alert` |
| LLR-APP-004 | SDD-APP-3.5, 3.7, SDD-APP-4 | `APP_IIC_TIMEOUT_POLLS`, `run_polling_example`, `run_interrupt_example` |
| LLR-APP-005 | SDD-APP-1.0, 2.2 | `main` |

Reverse check: every function and file-scope object in the five source files
carries a `@req` / `@design` pair that appears in this matrix. There is no
orphan code.

---

## 9. Open items

All open items are tracked in `OPEN_ITEMS.md`. Items that affect this
design: `OI-02` (`ADT7410_ERR_NOT_READY`), `OI-03` (closed in code: TX_ERR at
the end of a master receive, SDD-IIC-3.13 … 3.17), `OI-04` (target stack / WCET figures), `OI-05`
(`ASSERT` trap), `OI-06` (datasheet references), `OI-08` (poll budget and
10 ms timing, SDD-APP-4).
