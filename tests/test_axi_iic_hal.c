/**
 * @file    test_axi_iic_hal.c
 * @brief   Host unit tests for axi_iic_hal.c on a static register model.
 *
 * Each test case traces to LLR clauses in docs/LLR_ADT7410_AXI_IIC.md.
 * See regmodel.h for what this model can and cannot show.
 */

#include "axi_iic_hal.h"
#include "regmodel.h"
#include "test_harness.h"

#define TEST_POLLS  (8U)   /* Small poll budget keeps timeout tests fast */

static axi_iic_dev_t iic;


/* Init the HAL on a clean model, then set the SR "hardware situation". */
static void setup_ready(uint32_t sr_flags, uint32_t isr_flags)
{
    axi_iic_status_t status;

    rm_reset();
    status = axi_iic_init(&iic, rm_base(), TEST_POLLS);
    TH_CHECK(AXI_IIC_OK == status);
    rm_regs[RM_SR] = sr_flags;
    rm_regs[RM_ISR] = isr_flags;
    rm_regs[RM_TX_FIFO] = 0xDEADU; /* Marker: "nothing pushed yet" */
}


/* ===================== LLR-IIC-001 / 002: init ========================= */

static void tc_iic_001(void)
{
    axi_iic_status_t status;

    th_begin("TC-IIC-001", "LLR-IIC-002.1",
             "init rejects NULL / base 0 / polls 0 / polls > max");
    rm_reset();
    status = axi_iic_init(NULL, rm_base(), TEST_POLLS);
    TH_CHECK(AXI_IIC_ERR_PARAM == status);
    status = axi_iic_init(&iic, 0U, TEST_POLLS);
    TH_CHECK(AXI_IIC_ERR_PARAM == status);
    status = axi_iic_init(&iic, rm_base(), 0U);
    TH_CHECK(AXI_IIC_ERR_PARAM == status);
    status = axi_iic_init(&iic, rm_base(), AXI_IIC_MAX_POLL_LIMIT + 1U);
    TH_CHECK(AXI_IIC_ERR_PARAM == status);
    TH_CHECK(0U == rm_regs[RM_SOFTR]); /* No register access happened */
    TH_CHECK(0U == rm_regs[RM_CR]);

    status = axi_iic_init(&iic, rm_base(), 1U);
    TH_CHECK(AXI_IIC_OK == status);
    status = axi_iic_init(&iic, rm_base(), AXI_IIC_MAX_POLL_LIMIT);
    TH_CHECK(AXI_IIC_OK == status);
    th_end();
}


static void tc_iic_002(void)
{
    axi_iic_status_t status;

    th_begin("TC-IIC-002", "LLR-IIC-002.2..002.7",
             "init programs SOFTR, timing, PIRQ, CR; sets READY");
    rm_reset();
    status = axi_iic_init(&iic, rm_base(), TEST_POLLS);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK(rm_base() == iic.base_addr);
    TH_CHECK(TEST_POLLS == iic.timeout_polls);
    TH_CHECK(AXI_IIC_STATE_READY == iic.init_state);
    TH_CHECK(0x0AU == rm_regs[RM_SOFTR]);
    TH_CHECK(570U == rm_regs[RM_TSUSTA]);
    TH_CHECK(500U == rm_regs[RM_TSUSTO]);
    TH_CHECK(430U == rm_regs[RM_THDSTA]);
    TH_CHECK(55U == rm_regs[RM_TSUDAT]);
    TH_CHECK(500U == rm_regs[RM_TBUF]);
    TH_CHECK(493U == rm_regs[RM_THIGH]);
    TH_CHECK(493U == rm_regs[RM_TLOW]);
    TH_CHECK(30U == rm_regs[RM_THDDAT]);
    TH_CHECK(0x0FU == rm_regs[RM_RX_PIRQ]);
    TH_CHECK(0x01U == rm_regs[RM_CR]);  /* Enabled, TX_FIFO reset released */
    th_end();
}


static void tc_iic_003(void)
{
    axi_iic_status_t status;

    th_begin("TC-IIC-003", "LLR-IIC-002.6",
             "init clears only latched ARB_LOST/TX_ERR bits");
    rm_reset();
    rm_regs[RM_ISR] = 0x07U;  /* ARB_LOST | TX_ERR | unrelated bit 2 */
    status = axi_iic_init(&iic, rm_base(), TEST_POLLS);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK(0x03U == rm_regs[RM_ISR]); /* Toggle-write of error bits only */

    rm_reset();
    rm_regs[RM_ISR] = 0x04U;  /* No error bit set: no ISR write */
    status = axi_iic_init(&iic, rm_base(), TEST_POLLS);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK(0x04U == rm_regs[RM_ISR]);
    th_end();
}


/* ===================== LLR-IIC-003: write ============================== */

static void tc_iic_010(void)
{
    static uint8_t const payload[32] = { 0U };

    th_begin("TC-IIC-010", "LLR-IIC-003.1",
             "write_bytes PARAM checks and boundaries");
    setup_ready(RM_SR_TX_EMPTY, 0U);
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_bytes(NULL, 0x48U, payload, 1U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_bytes(&iic, 0x48U, NULL, 1U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_bytes(&iic, 0x80U, payload, 1U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_bytes(&iic, 0x48U, payload, 0U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_bytes(&iic, 0x48U, payload, 33U));
    TH_CHECK(0xDEADU == rm_regs[RM_TX_FIFO]); /* No bus access */

    TH_CHECK(AXI_IIC_OK == axi_iic_write_bytes(&iic, 0x7FU, payload, 1U));
    TH_CHECK(AXI_IIC_OK == axi_iic_write_bytes(&iic, 0x00U, payload, 32U));
    th_end();
}


static void tc_iic_011(void)
{
    static uint8_t const payload[1] = { 0x00U };
    axi_iic_status_t status;
    uint32_t bit;

    th_begin("TC-IIC-011", "LLR-IIC-001.3,003.2",
             "write_bytes ERR_STATE when UNINIT or 1-bit SEU");
    setup_ready(RM_SR_TX_EMPTY, 0U);
    iic.init_state = AXI_IIC_STATE_UNINIT;
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 1U);
    TH_CHECK(AXI_IIC_ERR_STATE == status);
    for (bit = 0U; bit < 32U; bit++)
    {
        iic.init_state =
            (axi_iic_init_state_t)((uint32_t)AXI_IIC_STATE_READY
                                   ^ (1UL << bit));
        status = axi_iic_write_bytes(&iic, 0x48U, payload, 1U);
        TH_CHECK(AXI_IIC_ERR_STATE == status);
    }
    TH_CHECK(0xDEADU == rm_regs[RM_TX_FIFO]);
    th_end();
}


static void tc_iic_012(void)
{
    static uint8_t const payload[2] = { 0x03U, 0x9AU };
    axi_iic_status_t status;

    th_begin("TC-IIC-012", "LLR-IIC-003.3",
             "write_bytes ERR_BUS_BUSY when SR.BB stays set");
    setup_ready(RM_SR_BB | RM_SR_TX_EMPTY, 0U);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_BUS_BUSY == status);
    TH_CHECK(0xDEADU == rm_regs[RM_TX_FIFO]); /* No START queued */
    th_end();
}


static void tc_iic_013(void)
{
    static uint8_t const payload[2] = { 0x03U, 0x9AU };
    axi_iic_status_t status;

    th_begin("TC-IIC-013", "LLR-IIC-003.5,003.8",
             "TX_FULL (SR bit 4) timeout; RX_FULL (bit 5) does not block");
    setup_ready(RM_SR_TX_FULL, 0U);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_TIMEOUT == status);
    TH_CHECK(0xDEADU == rm_regs[RM_TX_FIFO]);

    /* SR bit 5 is RX_FULL (PG090), not TX_FULL: it must not block TX */
    setup_ready(RM_SR_TX_EMPTY | 0x20U, 0U);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_OK == status);
    th_end();
}


static void tc_iic_014(void)
{
    static uint8_t const payload[2] = { 0x03U, 0x9AU };
    axi_iic_status_t status;

    th_begin("TC-IIC-014", "LLR-IIC-003.6",
             "NACK / ARB_LOST detected while waiting on TX_FULL");
    setup_ready(RM_SR_TX_FULL, RM_ISR_TX_ERR);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);
    TH_CHECK(0x01U == rm_regs[RM_CR]); /* Recovery left controller on */

    setup_ready(RM_SR_TX_FULL, RM_ISR_ARB_LOST);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);
    th_end();
}


static void tc_iic_015(void)
{
    static uint8_t const payload[2] = { 0x03U, 0x9AU };
    axi_iic_status_t status;

    th_begin("TC-IIC-015", "LLR-IIC-003.6,003.7",
             "completion: timeout, NACK while busy, NACK at idle");
    setup_ready(0U, 0U); /* TX never empties */
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_TIMEOUT == status);

    setup_ready(0U, RM_ISR_TX_ERR); /* NACK found inside completion loop */
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);

    setup_ready(RM_SR_TX_EMPTY, RM_ISR_TX_ERR); /* Idle, NACK latched */
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);
    th_end();
}


static void tc_iic_016(void)
{
    static uint8_t const payload[3] = { 0x04U, 0x20U, 0x5AU };
    axi_iic_status_t status;

    th_begin("TC-IIC-016", "LLR-IIC-003.4 (partial)",
             "write_bytes OK; last FIFO word = last byte | STOP");
    setup_ready(RM_SR_TX_EMPTY, 0U);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 3U);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK((RM_DYN_STOP | 0x5AU) == rm_regs[RM_TX_FIFO]);

    setup_ready(RM_SR_TX_EMPTY, 0U);
    status = axi_iic_write_bytes(&iic, 0x48U, payload, 1U);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK((RM_DYN_STOP | 0x04U) == rm_regs[RM_TX_FIFO]);
    th_end();
}


/* ===================== LLR-IIC-004: write-read ========================= */

static void tc_iic_020(void)
{
    uint8_t rx_bytes[32] = { 0U };
    axi_iic_status_t status;

    th_begin("TC-IIC-020", "LLR-IIC-004.1",
             "write_read PARAM / STATE checks");
    setup_ready(RM_SR_TX_EMPTY, 0U);
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_read_bytes(NULL, 0x48U, 0U, rx_bytes, 2U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_read_bytes(&iic, 0x48U, 0U, NULL, 2U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_read_bytes(&iic, 0xFFU, 0U, rx_bytes, 2U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_read_bytes(&iic, 0x48U, 0U, rx_bytes, 0U));
    TH_CHECK(AXI_IIC_ERR_PARAM
             == axi_iic_write_read_bytes(&iic, 0x48U, 0U, rx_bytes, 33U));
    iic.init_state = AXI_IIC_STATE_UNINIT;
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_STATE == status);
    TH_CHECK(0xDEADU == rm_regs[RM_TX_FIFO]);
    th_end();
}


static void tc_iic_021(void)
{
    uint8_t rx_bytes[32];
    axi_iic_status_t status;
    uint32_t i;
    uint32_t mismatch = 0U;

    th_begin("TC-IIC-021", "LLR-IIC-004.2 (partial),004.3",
             "write_read OK: n bytes, RX bits [7:0], STOP|n word");
    setup_ready(RM_SR_TX_EMPTY, 0U);
    rm_regs[RM_RX_FIFO] = 0xFFFFFFA5U; /* Upper bits must be masked off */
    (void)memset(rx_bytes, 0, sizeof(rx_bytes));
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 32U);
    TH_CHECK(AXI_IIC_OK == status);
    for (i = 0U; i < 32U; i++)
    {
        if (0xA5U != rx_bytes[i])
        {
            mismatch++;
        }
    }
    TH_CHECK(0U == mismatch);
    TH_CHECK((RM_DYN_STOP | 32U) == rm_regs[RM_TX_FIFO]);

    setup_ready(RM_SR_TX_EMPTY, 0U);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x0BU, rx_bytes, 1U);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK((RM_DYN_STOP | 1U) == rm_regs[RM_TX_FIFO]);
    th_end();
}


static void tc_iic_022(void)
{
    uint8_t rx_bytes[2] = { 0x11U, 0x22U };
    axi_iic_status_t status;

    th_begin("TC-IIC-022", "LLR-IIC-004.3,004.4,004.5",
             "write_read RX timeout, NACK in RX wait, bus busy");
    setup_ready(RM_SR_TX_EMPTY | RM_SR_RX_EMPTY, 0U);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_TIMEOUT == status);
    TH_CHECK(0x11U == rx_bytes[0]); /* Nothing stored */

    setup_ready(RM_SR_TX_EMPTY | RM_SR_RX_EMPTY, RM_ISR_TX_ERR);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status); /* Found in RX wait loop */

    setup_ready(RM_SR_TX_FULL, RM_ISR_ARB_LOST);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);

    setup_ready(RM_SR_BB, 0U);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_BUS_BUSY == status);

    setup_ready(0U, 0U); /* RX data present but transfer never completes */
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x00U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_TIMEOUT == status);
    th_end();
}


static void tc_iic_023(void)
{
    uint8_t rx_bytes[2] = { 0x11U, 0x22U };
    axi_iic_status_t status;

    th_begin("TC-IIC-023", "LLR-IIC-004.4,004.6",
             "read: master's final NACK (TX_ERR + data) is not an error");
    /* OI-03: TX_ERR latched while the received bytes are in the RX FIFO */
    setup_ready(RM_SR_TX_EMPTY, RM_ISR_TX_ERR);
    rm_regs[RM_RX_FIFO] = 0xCBU;
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x0BU, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_OK == status);
    TH_CHECK((0xCBU == rx_bytes[0]) && (0xCBU == rx_bytes[1]));

    /* Same situation but arbitration lost: still an error */
    setup_ready(RM_SR_TX_EMPTY, RM_ISR_TX_ERR | RM_ISR_ARB_LOST);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x0BU, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);

    /* Arbitration lost while waiting for RX data: error */
    setup_ready(RM_SR_TX_EMPTY | RM_SR_RX_EMPTY, RM_ISR_ARB_LOST);
    status = axi_iic_write_read_bytes(&iic, 0x48U, 0x0BU, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);

    /* Write path unchanged: TX_ERR at completion is a slave NACK */
    setup_ready(RM_SR_TX_EMPTY, RM_ISR_TX_ERR);
    status = axi_iic_write_bytes(&iic, 0x48U, rx_bytes, 2U);
    TH_CHECK(AXI_IIC_ERR_NACK_ARB == status);
    th_end();
}


int main(void)
{
    tc_iic_001();
    tc_iic_002();
    tc_iic_003();
    tc_iic_010();
    tc_iic_011();
    tc_iic_012();
    tc_iic_013();
    tc_iic_014();
    tc_iic_015();
    tc_iic_016();
    tc_iic_020();
    tc_iic_021();
    tc_iic_022();
    tc_iic_023();

    return th_summary("axi_iic_hal");
}
