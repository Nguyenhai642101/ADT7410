# Open Items Tracker — ADT7410 & AXI IIC Project

Consolidated from `docs/LLR_ADT7410_AXI_IIC.md` (§1.3, §6) and
`docs/SDD_ADT7410_AXI_IIC.md` (§5, §6.1, §7.3, §9).

| ID | Description | Source Reference | Status |
| :--- | :--- | :--- | :--- |
| `OI-01` | Parent HLR document is a draft placeholder (`docs/HLR_ADT7410_AXI_IIC.md`). Every LLR group (`LLR-IIC-001..004`, `LLR-ADT-001..005`, `LLR-APP-001..005`) is now linked to a proposed HLR ID (LLR §1.3); the HLR text must be approved by system engineering before baseline. | `LLR §1.3, §6`; `HLR` | Open (LLR links done, HLR approval pending) |
| `OI-02` | `ADT7410_ERR_NOT_READY` (`5U`) is defined in `adt7410_status_t` (`include/adt7410_driver.h`) and `LLR-ADT-001.1`, but no function currently returns it. Decide whether to check `Status.RDY` and return it or remove the enumerator. | `LLR-ADT-001.1`; `SDD §4.1, §9` | Open |
| `OI-03` | `ISR.TX_ERR` is asserted at the end of a master receive (the master's own NACK of the last byte). **Confirmed on hardware:** board diagnostic (2026-11-18) at 0x48 returned `ERR_NACK_ARB` with the ID byte `0xCB` received (SR `0xCC`, ISR `0xD0`); raw probe without HAL (2026-11-19): ISR before `0xD0`, ISR when the data byte was first seen `0xD2`, ISR at end `0xD2` (TX_ERR = 1, ARB_LOST = 0), SR at end `0xC8`. Fixed in `src/axi_iic_hal.c` (`LLR-IIC-004.4`, `004.6`; `SDD-IIC-3.13 … 3.17`), independent of the flag/data order. Re-run `adt7410_bus_diag` and the application on the board with the fix. | `LLR-IIC-004.4`, `004.6`; `SDD §3.3` | Closed in code (hardware re-test pending) |
| `OI-04` | Regenerate per-function stack usage (`.su`) and WCET measurements using the qualified target ARM cross-compiler and project optimization profile (`SDD §6.1` figures are host x86-64 only). | `SDD §6.1, §9` | Open (Needs ARM toolchain) |
| `OI-05` | In `DEBUG_BUILD`, `ASSERT(expr)` in `src/adt7410_driver.c` and `src/axi_iic_hal.c` currently has an empty `/* trap */` body. Connect it to the project fault handler. | `SDD §7.3, §9`; `src/*.c` | Open |
| `OI-06` | Verify the `[ADT7410]` and `[PG090]` table and page numbers cited in source comments against the exact datasheet revisions under configuration control. | `SDD §9` | Open |
| `OI-07` | Submit derived requirements (`LLR-IIC-001.3`, `LLR-IIC-004.6`, `LLR-ADT-001.2`, `LLR-ADT-001.10`, `LLR-ADT-003.6`, `LLR-APP-002.3`, `LLR-APP-003.4`, `LLR-APP-004`) to the system safety assessment process per DO-178C §5.2.2. | `LLR §1.1`; `HLR §2` | Open |
| `OI-08` | Application timing: measure `t_poll` (one ISR + one SR AXI4-Lite read) on the target and choose `APP_IIC_TIMEOUT_POLLS`. With `P = 5000`, the no-false-timeout constraint needs `t_poll >= 80 ns`, while the analytic 10 ms bound (`35 x P` polls per recovery cycle) needs `t_poll <= 57 ns`. Close with a measurement-based WCET argument or a timer-based HAL deadline. | `SDD-APP-4`; `LLR-APP-004`; `src/main.c` | Open (Needs target measurement) |
