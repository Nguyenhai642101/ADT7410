# Hướng Dẫn Tổng Thể & Quy Trình Kiểm Tra Chéo Dành Cho AI Agent

Tài liệu này là **điểm nhập chuẩn (Master Onboarding & Cross-Check Guide)**
dành cho bất kỳ AI Agent nào (Antigravity, Claude, Codex, Cursor, Gemini CLI,
v.v.) khi tiếp nhận, đọc hiểu, phát triển tiếp hoặc **kiểm tra chéo (audit /
cross-check)** project **ADT7410 Temperature Sensor Driver & Xilinx AXI IIC HAL**.

---

## 1. Tóm Tắt Dự Án & Kiến Trúc Cốt Lõi

- **Mục tiêu**: Driver ngôn ngữ **C99** cho cảm biến nhiệt độ I2C 16-bit
  Analog Devices **ADT7410**, chạy trên tầng **Xilinx AXI IIC HAL** (chế độ
  polled master, dynamic FIFO mode, AXI clock 100 MHz, I2C SCL 100 kHz, địa chỉ
  7-bit `0x48..0x4B`).
- **Mức chuẩn áp dụng**: **MISRA C:2012 + BARR-C:2018 + DO-178C (tới DAL A)**
  được hợp nhất trong [docs/C_rules_v3_addDO178C.md](docs/C_rules_v3_addDO178C.md).
- **Kiến trúc 4 lớp**:
  ```text
  Application / Health Monitor (src/main.c — Polling 10 ms & INT Pin ISR)
      │  adt7410_* API (float degC, mdegC, Q8.7, status flags)
      ▼
  ADT7410 Driver (include/adt7410_driver.h, src/adt7410_driver.c)
      │  axi_iic_write_bytes / axi_iic_write_read_bytes
      ▼
  AXI IIC HAL (include/axi_iic_hal.h, src/axi_iic_hal.c)
      │  reg_read32 / reg_write32 (32-bit volatile MMIO)
      ▼
  Xilinx AXI IIC IP (PL)  <-- I2C 100 kHz -->  ADT7410 Sensor
  ```

---

## 2. Bản Đồ Thư Mục & Thứ Tự Đọc Bắt Buộc Cho AI Agent

Khi kiểm tra chéo, AI Agent **phải đọc trực tiếp file trên đĩa theo đúng thứ tự
sau** (không suy đoán từ tên file hoặc trí nhớ):

### Bước 1 — Ngữ cảnh Workspace & Luật Code
1. [GEMINI.md](GEMINI.md) — Cấu trúc repo, quy tắc tóm tắt, lệnh build/test.
2. [docs/C_rules_v3_addDO178C.md](docs/C_rules_v3_addDO178C.md) — Bộ luật C đầy
   đủ (§0–§14). *(Lưu ý Antigravity IDE: do giới hạn 24 KB/file rule, bộ luật
   này được tách sang `.agents/rules/01-c-rules-core.md` và
   `.agents/rules/02-c-rules-safety.md`).*

### Bước 2 — Đặc tả Yêu cầu & Thiết kế
3. [docs/LLR_ADT7410_AXI_IIC.md](docs/LLR_ADT7410_AXI_IIC.md) — Yêu cầu mức thấp:
   - `LLR-IIC-001`..`LLR-IIC-004` (AXI IIC HAL)
   - `LLR-ADT-001`..`LLR-ADT-005` (ADT7410 Driver, bao gồm `003.8` cho `float`)
   - `LLR-APP-001`..`LLR-APP-005` (ứng dụng tham chiếu `src/main.c`)
4. [docs/SDD_ADT7410_AXI_IIC.md](docs/SDD_ADT7410_AXI_IIC.md) — Thiết kế chi tiết,
   công thức fixed-point, thiết kế ứng dụng & phân tích thời gian (§5,
   `SDD-APP-4`), stack usage (§6.1), MISRA deviation log (§7.1), nhánh phòng
   thủ (§7.2) và ma trận truy vết (§8).
5. [docs/HLR_ADT7410_AXI_IIC.md](docs/HLR_ADT7410_AXI_IIC.md) — Bản nháp HLR giữ chỗ
   (`OI-01`, chưa phải tài liệu phê duyệt chính thức).
6. [docs/OPEN_ITEMS.md](docs/OPEN_ITEMS.md) — 8 mục mở (`OI-01`..`OI-08`).

### Bước 3 — Giao diện Public, Mã nguồn Production & Ứng dụng mẫu
7. [include/axi_iic_hal.h](include/axi_iic_hal.h) & [src/axi_iic_hal.c](src/axi_iic_hal.c)
   (3 public APIs + 17 `static` helpers).
8. [include/adt7410_driver.h](include/adt7410_driver.h) & [src/adt7410_driver.c](src/adt7410_driver.c)
   (13 public APIs + 2 LUTs + 19 `static` helpers).
9. [src/main.c](src/main.c) & [docs/DRIVER_USAGE_GUIDE.md](docs/DRIVER_USAGE_GUIDE.md)
   (Ví dụ chuẩn Polling 10 ms + Interrupt chân `INT` dùng cờ `volatile` và bottom-half).

### Bước 4 — Kiểm thử, Build System & Báo cáo hiện trạng
10. [tests/test_harness.h](tests/test_harness.h), [tests/regmodel.h](tests/regmodel.h),
    [tests/mock_axi_iic.h](tests/mock_axi_iic.h), [tests/mock_axi_iic.c](tests/mock_axi_iic.c).
11. [tests/test_axi_iic_hal.c](tests/test_axi_iic_hal.c) (`14` TCs: `TC-IIC-001..023`),
    [tests/test_adt7410_driver.c](tests/test_adt7410_driver.c) (`31` TCs: `TC-ADT-001..051`),
    [tests/test_integration.c](tests/test_integration.c) (`2` TCs: `TC-INT-001..002`),
    [tests/test_app.c](tests/test_app.c) (`10` TCs: `TC-APP-001..010`).
12. [CMakeLists.txt](CMakeLists.txt), [Makefile](Makefile),
    [tests/run_tests.sh](tests/run_tests.sh), [tests/run_tests.ps1](tests/run_tests.ps1),
    [.vscode/settings.json](.vscode/settings.json),
    [.vscode/c_cpp_properties.json](.vscode/c_cpp_properties.json).
13. [docs/test_report.md](docs/test_report.md) — Trạng thái thực tế (`PASS` vs `PENDING` tại §7).

---

## 3. Quy Trình Kiểm Tra Chéo 6 Trục (Cross-Check Protocol)

Khi được yêu cầu *"kiểm tra chéo project"*, AI Agent thực hiện tuần tự 6 trục
dưới đây và ghi nhận bằng chứng theo từng mục (`file:line` cụ thể).

### Trục 1: Tuân Thủ C Coding Rules (`C_rules_v3_addDO178C.md` §0–§14)
- **Định dạng bắt buộc (§0)**:
  - Dấu ngoặc nhọn kiểu Allman trên mọi khối lệnh (`if`/`else`/`for`/`while`/`switch`).
  - Tuyệt đối không dùng comment `//`, không lồng `/* ... */`.
  - Độ dài mỗi dòng `<= 80` ký tự; thụt lề 4 spaces (không TAB); kết thúc đúng 1 `\n`.
- **Kiểu dữ liệu & Biểu thức (§1, §5, §11.1)**:
  - Chỉ dùng kiểu cố định `<stdint.h>`, `<stdbool.h>`, `size_t`.
  - Hằng số nằm bên trái `==` / `!=` (ví dụ `if (NULL == p_dev)`).
  - Không `float`/`double` so sánh `==` hoặc `!=`; không dùng `float` trong ISR.
  - Mọi đại lượng vật lý mang đơn vị ở tên biến, Doxygen và struct (`_mdegc`, `_q8_7`, `_degc`, `_polls`).
- **Luồng điều khiển & Bộ nhớ (§4, §6, §8, §13)**:
  - Mỗi hàm đúng **1 lệnh `return` ở cuối hàm**; không `goto`, `continue`, đệ quy, VLA, `malloc`/`free`, không biến `extern` chia sẻ giữa các module.
  - Chuỗi `if ... else if` phải kết thúc bằng `else`; `switch` phải có `default` và `break` (ít nhất 2 nhánh `case`).
  - **MC/DC (§5)**: tối đa `3` (giới hạn cứng `4`) điều kiện boolean đơn trong một câu điều kiện; không lặp lại cùng 1 biến trong cùng quyết định; không dùng `? :` cho logic an toàn.
  - Không đọc/ghi quá 1 biến `volatile` trong cùng một biểu thức/câu lệnh.
  - Không `#ifdef` rải bên trong thân hàm (§7) — trừ macro `ASSERT` ở đầu file.

### Trục 2: Truy Vết 2 Chiều DO-178C (`LLR <-> SDD <-> Code <-> Tests`)
- **Kiểm tra từ Code ngược lên SDD/LLR**:
  - Mọi hàm public, hàm `static`, LUT file-scope, typedef public trong `include/` và `src/` (HAL, driver và `src/main.c`) đều phải có `@req` và `@design` khớp bảng §8 trong `docs/SDD_ADT7410_AXI_IIC.md`. Không được tồn tại *orphan code*.
  - `src/main.c` (ứng dụng tham chiếu, `Application / Health Monitor`) truy vết tới `LLR-APP-xxx` / `SDD-APP-x.y` và được kiểm thử bởi `tests/test_app.c`; đồng thời kiểm tra không gọi sai hợp đồng API nào của `include/*.h`.
- **Kiểm tra từ LLR/SDD xuống Test**:
  - Mọi nhóm `LLR-IIC-001..004`, `LLR-ADT-001..005` (kể cả `LLR-ADT-003.8` cho `adt7410_read_temp_degc()`) và `LLR-APP-001..005` phải có test case `TC-...` kiểm chứng tương ứng (ma trận ở `docs/test_report.md` §6).
- **Kiểm tra ngoại lệ & phòng thủ**:
  - Ngoại lệ MISRA Rule 11.4 chỉ được phép xuất hiện tại `reg_read32()` / `reg_write32()` (`DEV-01` trong `SDD §7.1`).
  - Nhánh phòng thủ bất khả thi trong điều kiện thường phải có comment `/* DEFENSIVE [DO-178C]: ... */` (`SDD §7.2`: `reg16_to_sample()` `default:`) và có test fault-injection đi kèm (`TC-ADT-033`, `TC-ADT-036`).

### Trục 3: Độ Chính Xác Thanh Ghi Phần Cứng & Toán Học Fixed-Point / Float
Đối chiếu với Datasheet **ADT7410** và Xilinx **PG090**:
- **Xilinx AXI IIC (`src/axi_iic_hal.c`)**:
  - Offset thanh ghi: `ISR 0x020`, `SOFTR 0x040` (key `0x0A`), `CR 0x100`, `SR 0x104`, `TX_FIFO 0x108` (`START=0x100`, `STOP=0x200`), `RX_FIFO 0x10C`, `RX_PIRQ 0x120` (`0x0F`), timing `0x128..0x144` cho AXI 100 MHz / SCL 100 kHz.
  - `validate_xfer_args()` chặn `addr > 0x7F`, `len` ngoài `1..32`, và kiểm tra `init_state == AXI_IIC_STATE_READY` (`0x5A3C96E1U`, Hamming distance `31` so với `UNINIT`).
- **ADT7410 (`src/adt7410_driver.c`)**:
  - Địa chỉ hợp lệ `0x48..0x4B`; ID register `0x0B` có Manufacturer ID `bits [7:3] == 0x19U`.
  - Soft reset gửi `0x2F` và đặt `active_res = ADT7410_RES_13BIT`.
  - Chuyển đổi dữ liệu:
    - 13-bit: che mặt nạ `0xFFF8U` (`bits [2:0]` là cờ `TCRIT`/`THIGH`/`TLOW`), tạo thành số Q8.7 chuẩn vì $0.0625 = 8/128$.
    - 16-bit: dùng đủ 16 bit bù 2, không có inline flags.
    - `reg16_to_q8_7()`: chuyển `uint16_t` sang `int32_t` không dính UB của ép kiểu tràn dấu (C99 §6.3.1.3p3).
    - `q8_7_to_mdegc()` & `mdegc_to_q8_7()`: làm tròn *half away from zero*.
    - `q8_7_to_degc()`: nhân `(float)temp_q8_7 * 0.0078125f` ($2^{-7}$ là lũy thừa của 2 nên chính xác tuyệt đối trong IEEE-754 `float`).

### Trục 4: Chống Treo CPU, Timeout & An Toàn Ngắt (`Polling & ISR`)
- Kiểm tra mọi vòng `while` chờ phần cứng trong `src/axi_iic_hal.c` (`wait_bus_not_busy`, `push_tx_fifo_word`, `pop_rx_fifo_byte`, `wait_tx_complete`):
  - Phải đọc thanh ghi trước khi vào vòng lặp.
  - Phải giảm `remaining_polls--` và thoát khi `0U == remaining_polls` hoặc khi `check_isr_errors()` phát hiện `ISR.TX_ERR` (NACK) / `ISR.ARB_LOST` (`AXI_IIC_ERR_NACK_ARB`).
  - Riêng đường đọc (`write_read`): `TX_ERR` chỉ là lỗi khi RX FIFO trống **và** bus đã nhả (SR.BB = 0); nếu có dữ liệu hoặc bus còn bận thì chờ tiếp (`check_rx_wait_errors()`), hết thời gian mà vẫn có `TX_ERR` → NACK (`rx_timeout_status()`); sau khi đã nhận đủ byte, `TX_ERR` là NACK cuối của chính master và được xóa (`wait_rx_complete()`, `OI-03`). Trên board, `TX_ERR` hiện ra cùng lúc với byte cuối (raw probe), nên không được giả định thứ tự. `ARB_LOST` luôn là lỗi.
  - Bit SR: `TX_FULL` là bit 4 (`0x10`), bit 5 (`0x20`) là `RX_FULL` (PG090).
  - Xác nhận tổng vòng lặp không vượt quá giới hạn WCET (`(n + 3) * P` cho write, `(n + 6) * P` cho write-read).
- Kiểm tra `src/main.c`:
  - `adt7410_int_pin_isr()` tuyệt đối không gọi hàm I2C nào, chỉ gán `g_is_int_alert_pending = true;`.
  - Bottom-half `service_int_alert()` đọc lại `0x02` để nhả chốt chân `INT`.
  - Recovery chạy ở chu kỳ riêng, không bao giờ cùng chu kỳ với lần poll / xử lý alert bị lỗi; khi xử lý alert lỗi, cờ được bật lại để xử lý lại sau recovery (chân `INT` vẫn đang chốt).
  - Tham số `APP_IIC_TIMEOUT_POLLS` (`5000U`): đối chiếu phân tích thời gian `SDD-APP-4` và `OI-08` (ràng buộc "không timeout giả" `P × t_poll ≥ 0.4 ms` so với giới hạn chu kỳ `10 ms`); giá trị cuối cùng phải dựa trên `t_poll` đo trên target.

### Trục 5: Hệ Thống Build, Test & Cấu Hình Antigravity IDE
- Kiểm tra tính nhất quán giữa `include/`, `src/`, `tests/`, `docs/` trên cả 4 hệ thống build/test: `CMakeLists.txt`, `Makefile`, `tests/run_tests.sh`, `tests/run_tests.ps1`.
- Kiểm tra cấu hình IDE: `.vscode/*.json` (5 file JSON hợp lệ, include path trỏ tới `include/` và `tests/`), `.editorconfig`, `.gitignore`, `.gitattributes`.
- Kiểm tra giới hạn rule Antigravity IDE: mỗi file trong `.agents/rules/*.md` và `GEMINI.md` **bắt buộc `< 24,000` bytes** để không bị cắt cụt context.

### Trục 6: Đối Chiếu Open Items & Trung Thực Báo Cáo (`OI-01`..`OI-08`)
- Đối chiếu `docs/OPEN_ITEMS.md`:
  - `OI-01` (HLR chính thức), `OI-02` (`ADT7410_ERR_NOT_READY` chưa dùng), `OI-03` (đã sửa trong code: `ISR.TX_ERR` do master NACK byte cuối, đã xác nhận trên phần cứng, chờ test lại trên board), `OI-04` (ARM stack/WCET), `OI-05` (`ASSERT` trap rỗng trong `DEBUG_BUILD`), `OI-06` (rev datasheet), `OI-07` (derived requirements), `OI-08` (đo `t_poll` trên target và chốt `APP_IIC_TIMEOUT_POLLS` so với chu kỳ 10 ms).
- **Nguyên tắc trung thực tuyệt đối của AI Agent**:
  - Chỉ báo cáo `PASS` cho các kiểm tra **thực sự đã chạy thành công sau lần sửa cuối cùng** (ví dụ `tests/run_tests.ps1 -FormatOnly` nếu máy chưa cài `gcc`).
  - Nếu máy chưa có `gcc` / `gcov` / phần cứng HIL, phải giữ nguyên trạng thái `PENDING` như trong `docs/test_report.md` §7, tuyệt đối không suy diễn hoặc bịa kết quả test.

---

## 4. Quy Trình Thực Thi Kiểm Tra Bằng Lệnh

1. **Trên Windows (kể cả khi chưa cài GCC)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 -FormatOnly
   ```
2. **Trên Windows (khi đã cài MSYS2/MinGW-w64 GCC)**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1
   ```
3. **Trên Linux / WSL / MSYS2 Bash**:
   ```bash
   bash tests/run_tests.sh
   ```
4. **Qua CMake / CTest**:
   ```bash
   cmake -S . -B build -DENABLE_COVERAGE=ON
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

---

## 5. Mẫu Báo Cáo Đầu Ra & Prompt Giao Việc Cho AI Agent Khác

### A. Prompt mẫu bạn có thể copy đưa cho AI Agent khác

> Hãy đọc `AI_AGENT_GUIDE.md` ở thư mục gốc và thực hiện quy trình kiểm tra
> chéo đầy đủ 6 trục trên toàn bộ project `ADT7410/`. Đọc trực tiếp từng file
> theo đúng thứ tự trong §2, chạy các lệnh kiểm tra khả dụng trên môi trường
> hiện tại (§4), và xuất báo cáo gồm: (1) Trạng thái từng trục PASS/WARN/FAIL
> kèm `file:line`, (2) Bất kỳ sai lệch nào giữa code, SDD, LLR, test, build
> config, và (3) Đề xuất sửa cụ thể nếu có lỗi. Không bịa kết quả test nếu chưa
> chạy được toolchain.

### B. Mẫu cấu trúc báo cáo phản hồi của AI Agent kiểm tra chéo

```markdown
## Báo Cáo Kiểm Tra Chéo Project ADT7410
- Trục 1 (C Coding Rules §0–§14): [PASS / WARN / FAIL] — bằng chứng `file:line`
- Trục 2 (DO-178C Traceability LLR/SDD/Test): [PASS / WARN / FAIL] — bằng chứng
- Trục 3 (Thanh ghi PG090/ADT7410 & Fixed-point/Float): [PASS / WARN / FAIL]
- Trục 4 (Timeout, Anti-hang & ISR Safety): [PASS / WARN / FAIL]
- Trục 5 (Build, Test Scripts & Antigravity IDE Config): [PASS / WARN / FAIL]
- Trục 6 (Open Items OI-01..08 & Báo cáo test_report.md): [PASS / WARN / FAIL]
- Lệnh đã chạy thực tế & kết quả quan sát được: <command + exit code + output>
- Danh sách sai lệch / rủi ro tìm thấy (nếu có): <mô tả + vị trí + cách khắc phục>
```
