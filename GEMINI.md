# ADT7410 Driver & Xilinx AXI IIC HAL — Antigravity Project Guide

This repository contains the safety-critical C99 driver for the Analog Devices
**ADT7410** 16-bit digital I2C temperature sensor and the polled **Xilinx AXI IIC**
Hardware Abstraction Layer (HAL), targeting DO-178C up to DAL A, MISRA C:2012,
and BARR-C:2018 compliance.

## 1. Repository Layout

- `include/` — Public C99 module headers:
  - `include/axi_iic_hal.h` — Polled AXI IIC HAL interface.
  - `include/adt7410_driver.h` — ADT7410 sensor driver interface.
- `src/` — Production flight C99 implementations & reference entry point:
  - `src/axi_iic_hal.c` — Polled AXI IIC HAL (100 MHz AXI clk, 100 kHz SCL, 7-bit addressing, dynamic mode).
  - `src/adt7410_driver.c` — ADT7410 driver built on top of `axi_iic_hal`.
  - `src/main.c` — Reference application demonstrating periodic 10 ms Polling mode and deferred Interrupt (`INT` pin ISR + task) mode.
- `tests/` — Host unit & integration test suites and verification scripts:
  - `tests/test_harness.h` — Minimal host test harness (`TH_CHECK`, `TC-...` tracing).
  - `tests/regmodel.h` — Static RAM MMIO register model for testing `axi_iic_hal.c`.
  - `tests/mock_axi_iic.h`, `tests/mock_axi_iic.c` — Mock AXI IIC HAL emulating ADT7410 registers.
  - `tests/test_axi_iic_hal.c`, `tests/test_adt7410_driver.c`, `tests/test_integration.c`, `tests/test_app.c` (reference application, `main()` renamed to `app_main()` in the test build only).
  - `tests/run_tests.sh` (Bash/Linux/MSYS2/WSL) and `tests/run_tests.ps1` (Windows PowerShell).
- `docs/` — Requirements, design, coding rules, and verification documentation:
  - `docs/C_rules_v3_addDO178C.md` — Full unified C coding standard (also split under `.agents/rules/` so no rule file exceeds Antigravity's 24 KB cap).
  - `docs/LLR_ADT7410_AXI_IIC.md` — Low-Level Requirements (`LLR-IIC-001..004`, `LLR-ADT-001..005`, `LLR-APP-001..005`).
  - `docs/SDD_ADT7410_AXI_IIC.md` — Software Design Description (`SDD-IIC-x.y`, `SDD-ADT-x.y`), stack table, MISRA deviation log (`DEV-01`), traceability matrix.
  - `docs/HLR_ADT7410_AXI_IIC.md` — Draft HLR placeholder (see Open Item `OI-01`).
  - `docs/DRIVER_USAGE_GUIDE.md` — Step-by-step integration & API usage guide for the Application / Health Monitor layer.
  - `docs/test_report.md` — Host verification results (static checks, test suites, coverage, LLR matrix) and register-model limits.
  - `docs/OPEN_ITEMS.md` — Consolidated open items (`OI-01`..`OI-08`).
- `AI_AGENT_GUIDE.md` — Master onboarding & 6-axis cross-check playbook for external/review AI agents.
- `.agents/` — Antigravity IDE rules (`.agents/rules/`) and workflow skills (`.agents/skills/`).
- `.vscode/` — IDE settings, IntelliSense (`c_cpp_properties.json`), build/test tasks, and debug configs.

## 2. Mandatory Coding & Design Rules

Full rules are enforced by `.agents/rules/01-c-rules-core.md` and
`.agents/rules/02-c-rules-safety.md` (mirroring `docs/C_rules_v3_addDO178C.md`).
Never violate these core constraints in `include/` and `src/`:

1. **Formatting**: Allman braces on every block, **no `//` comments** (use `/* ... */` only), **max 80 columns** per line, 4 spaces (no tabs), single trailing newline.
2. **Language & Compiler**: Strict C99 (`-std=c99 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wcast-qual -Wcast-align=strict -Wvla -Wstack-usage=256 -fstack-usage -fno-common`). Deterministic optimization only (`-O0` or `-O1 -fno-inline`).
3. **Safety & Control Flow**: Single `return` at function end; no `goto`, `continue`, recursion, `malloc`/`free`, VLAs, or `extern` globals; `if ... else if` ends with `else`; `switch` has `default` and `break`; all wait loops are statically bounded by `timeout_polls <= AXI_IIC_MAX_POLL_LIMIT`.
4. **MC/DC & SEU Robustness**: Max 3 atomic boolean conditions per decision (no variable used twice in one decision); no `? :` or compound boolean variable assignment for control-flow decisions; critical `init_state` enums use 32-bit patterns with Hamming distance 31; defensive unreachable branches require `/* DEFENSIVE [DO-178C]: ... */`.
5. **Physical Units & MMIO**: Every physical quantity carries its unit (`_mdegc`, `_q8_7`, `_degc`, `_polls`, `_hz`, `_bytes`); raw register bytes stay inside `src/adt7410_driver.c`; MMIO access in `src/axi_iic_hal.c` goes solely through `reg_read32()` / `reg_write32()` (MISRA Rule 11.4 deviation `DEV-01`).
6. **Bi-directional Traceability**: Every public/static function, public type, and file-scope object in `include/` and `src/` must carry `@req LLR-...` and `@design SDD-...` tags synchronized with `docs/LLR_ADT7410_AXI_IIC.md` and `docs/SDD_ADT7410_AXI_IIC.md`.

## 3. Build, Static Checks, and Tests

- **Windows (PowerShell)**:
  `powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1`
- **Linux / WSL / MSYS2 Bash**:
  `bash tests/run_tests.sh`
- **CMake / CTest**:
  `cmake -S . -B build -DENABLE_COVERAGE=ON && cmake --build build && ctest --test-dir build --output-on-failure`
- **Make**:
  `make static-check && make test && make coverage`

## 4. Known Open Items (Do Not Silently Alter)

See `docs/OPEN_ITEMS.md` before changing related behavior:
- `OI-01`: HLR is a draft placeholder (`docs/HLR_ADT7410_AXI_IIC.md`); LLR groups are linked to proposed HLR IDs, approval pending.
- `OI-02`: `ADT7410_ERR_NOT_READY` is defined in `adt7410_status_t` but currently unused.
- `OI-03`: Closed in code — `ISR.TX_ERR` is latched by the master's final NACK (confirmed on hardware); re-test the fix on the board.
- `OI-04`: Regenerate `.su` stack usage and WCET figures with the target ARM cross-compiler.
- `OI-05`: `ASSERT(expr)` under `DEBUG_BUILD` currently has an empty `/* trap */` body pending project fault handler hookup.
- `OI-06`: Verify cited `[ADT7410]` and `[PG090]` table/page numbers against configuration-controlled datasheet revisions.
- `OI-07`: Submit derived requirements to the system safety assessment.
- `OI-08`: Measure `t_poll` on target and fix `APP_IIC_TIMEOUT_POLLS` vs the 10 ms period (`SDD-APP-4`).
