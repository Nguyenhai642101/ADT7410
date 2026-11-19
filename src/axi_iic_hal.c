/**
 * @file    axi_iic_hal.c
 * @brief   Xilinx AXI IIC Hardware Abstraction Layer implementation.
 * @req     LLR-IIC-001, LLR-IIC-002, LLR-IIC-003, LLR-IIC-004
 * @design  SDD-IIC-1.0
 */

#include "axi_iic_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project assertion macro: compiles out in flight/release builds
 * [DO §6.4.4] */
#if defined(DEBUG_BUILD)
#define ASSERT(expr) do { if (!(expr)) { /* trap */ } } while (0)
#else /* DEBUG_BUILD */
#define ASSERT(expr) ((void)0)
#endif /* DEBUG_BUILD */

/* Xilinx AXI IIC Register Offsets (PG090 Table 2-3) */
#define AXI_IIC_REG_ISR_OFFSET      (0x020U) /* Interrupt Status Register   */
#define AXI_IIC_REG_SOFTR_OFFSET    (0x040U) /* Soft Reset Register         */
#define AXI_IIC_REG_CR_OFFSET       (0x100U) /* Control Register            */
#define AXI_IIC_REG_SR_OFFSET       (0x104U) /* Status Register             */
#define AXI_IIC_REG_TX_FIFO_OFFSET  (0x108U) /* Transmit FIFO               */
#define AXI_IIC_REG_RX_FIFO_OFFSET  (0x10CU) /* Receive FIFO                */
#define AXI_IIC_REG_RX_PIRQ_OFFSET  (0x120U) /* RX FIFO Programmable Depth  */
#define AXI_IIC_REG_TSUSTA_OFFSET   (0x128U) /* Start Setup Time            */
#define AXI_IIC_REG_TSUSTO_OFFSET   (0x12CU) /* Stop Setup Time             */
#define AXI_IIC_REG_THDSTA_OFFSET   (0x130U) /* Start Hold Time             */
#define AXI_IIC_REG_TSUDAT_OFFSET   (0x134U) /* Data Setup Time             */
#define AXI_IIC_REG_TBUF_OFFSET     (0x138U) /* Bus Free Time               */
#define AXI_IIC_REG_THIGH_OFFSET    (0x13CU) /* SCL High Time               */
#define AXI_IIC_REG_TLOW_OFFSET     (0x140U) /* SCL Low Time                */
#define AXI_IIC_REG_THDDAT_OFFSET   (0x144U) /* Data Hold Time              */

/* Soft Reset Key (PG090 §2) */
#define AXI_IIC_SOFTR_KEY           (0x0000000AU)

/* Control Register Masks (PG090 CR Offset 0x100) */
#define AXI_IIC_CR_EN_MASK          (0x00000001U) /* Enable AXI IIC          */
#define AXI_IIC_CR_TX_RST_MASK      (0x00000002U) /* TX FIFO Reset           */

/* Status Register Masks (PG090 SR Offset 0x104).
 * Layout: [7] TX_EMPTY [6] RX_EMPTY [5] RX_FULL [4] TX_FULL [3] SRW
 *         [2] BB [1] AAS [0] ABGC. TX_FULL is bit 4 (not bit 5, which is
 *         RX_FULL): waiting on bit 5 would let a > 16-word transfer
 *         overflow the TX FIFO. */
#define AXI_IIC_SR_BB_MASK          (0x00000004U) /* Bus Busy                */
#define AXI_IIC_SR_TX_FULL_MASK     (0x00000010U) /* TX FIFO Full            */
#define AXI_IIC_SR_RX_EMPTY_MASK    (0x00000040U) /* RX FIFO Empty           */
#define AXI_IIC_SR_TX_EMPTY_MASK    (0x00000080U) /* TX FIFO Empty           */

/* Interrupt Status Register Masks (PG090 ISR Offset 0x020) */
#define AXI_IIC_ISR_ARB_LOST_MASK   (0x00000001U) /* Arbitration Lost        */
#define AXI_IIC_ISR_TX_ERR_MASK     (0x00000002U) /* Transmit Error / NACK   */
#define AXI_IIC_ISR_ERR_FLAGS_MASK  (AXI_IIC_ISR_ARB_LOST_MASK \
                                     | AXI_IIC_ISR_TX_ERR_MASK)

/* TX_FIFO Dynamic Controller Bits (PG090 §2) */
#define AXI_IIC_DYN_START_MASK      (0x00000100U) /* Generate START / RSTART */
#define AXI_IIC_DYN_STOP_MASK       (0x00000200U) /* Generate STOP           */

/* I2C address byte layout: [7:1] = 7-bit address, [0] = R/W */
#define AXI_IIC_ADDR_RW_SHIFT       (1U)          /* Address above R/W bit   */
#define AXI_IIC_RW_WRITE_BIT        (0x00U)       /* Master Write (R/W = 0)  */
#define AXI_IIC_RW_READ_BIT         (0x01U)       /* Master Read (R/W = 1)   */

/* RX FIFO (PG090 RX_FIFO Offset 0x10C, RX_FIFO_PIRQ Offset 0x120) */
#define AXI_IIC_RX_FIFO_DATA_MASK   (0x000000FFU) /* Received byte [7:0]     */
#define AXI_IIC_RX_PIRQ_MAX_DEPTH   (0x0000000FU) /* Max RX FIFO depth (16)  */

/*
 * Standard Mode (100 kHz) Timing Counts at 100 MHz AXI Clock (10 ns tick).
 * Ref: Xilinx PG090 Table 2-20 & Analog Devices ADT7410 Table 2 (p. 4):
 * - TSUSTA >= 4700 ns (ADT7410 min 600 ns)  -> 570 ticks (5.70 us)
 * - TSUSTO >= 4000 ns (ADT7410 min 600 ns)  -> 500 ticks (5.00 us)
 * - THDSTA >= 4000 ns (ADT7410 min 600 ns)  -> 430 ticks (4.30 us)
 * - TSUDAT >= 250 ns  (ADT7410 min 250 ns)  ->  55 ticks (0.55 us)
 * - TBUF   >= 4700 ns (ADT7410 min 1300 ns) -> 500 ticks (5.00 us)
 * - THIGH  =  5000 ns (100 kHz half period) -> 493 ticks (PG090 formula)
 * - TLOW   =  5000 ns (100 kHz half period) -> 493 ticks (PG090 formula)
 * - THDDAT >= 300 ns  (ADT7410 min 0 ns)    ->  30 ticks (0.30 us)
 */
#define AXI_IIC_TIMING_TSUSTA_TICKS (570U)
#define AXI_IIC_TIMING_TSUSTO_TICKS (500U)
#define AXI_IIC_TIMING_THDSTA_TICKS (430U)
#define AXI_IIC_TIMING_TSUDAT_TICKS (55U)
#define AXI_IIC_TIMING_TBUF_TICKS   (500U)
#define AXI_IIC_TIMING_THIGH_TICKS  (493U)
#define AXI_IIC_TIMING_TLOW_TICKS   (493U)
#define AXI_IIC_TIMING_THDDAT_TICKS (30U)

/* Static helper prototypes */
static inline uint32_t reg_read32(uintptr_t base_addr, uint32_t offset_bytes);
static inline void reg_write32(uintptr_t base_addr,
                               uint32_t offset_bytes,
                               uint32_t reg_word);
static bool is_timeout_valid(uint32_t timeout_polls);
static bool is_xfer_len_valid(size_t len_bytes);
static axi_iic_status_t validate_xfer_args(axi_iic_dev_t const * p_dev,
                                           uint8_t const * p_bytes,
                                           uint8_t slave_addr_7b,
                                           size_t len_bytes);
static bool is_tx_idle(uint32_t sr_flags);
static void clear_tx_fifo_and_errors(uintptr_t base_addr);
static axi_iic_status_t wait_bus_not_busy(axi_iic_dev_t const * p_dev);
static axi_iic_status_t check_isr_errors(uintptr_t base_addr);
static axi_iic_status_t push_tx_fifo_word(axi_iic_dev_t const * p_dev,
                                          uint32_t fifo_word);
static axi_iic_status_t pop_rx_fifo_byte(axi_iic_dev_t const * p_dev,
                                         uint8_t * p_out_byte);
static axi_iic_status_t wait_tx_complete(axi_iic_dev_t const * p_dev);
static axi_iic_status_t check_rx_wait_errors(uintptr_t base_addr);
static axi_iic_status_t rx_timeout_status(uintptr_t base_addr);
static axi_iic_status_t check_arb_lost(uintptr_t base_addr);
static void clear_master_nack_flag(uintptr_t base_addr);
static axi_iic_status_t wait_rx_complete(axi_iic_dev_t const * p_dev);


/* Public API implementations (Doxygen contract in axi_iic_hal.h) */

axi_iic_status_t axi_iic_init(axi_iic_dev_t * p_dev,
                              uintptr_t base_addr,
                              uint32_t timeout_polls)
{
    axi_iic_status_t result = AXI_IIC_OK;
    bool const is_polls_ok = is_timeout_valid(timeout_polls);

    if ((NULL == p_dev)
        || (0U == base_addr)
        || (!is_polls_ok))
    {
        result = AXI_IIC_ERR_PARAM;
    }
    else
    {
        p_dev->base_addr = base_addr;
        p_dev->timeout_polls = timeout_polls;
        p_dev->init_state = AXI_IIC_STATE_UNINIT;

        /* Issue soft reset to AXI IIC core */
        reg_write32(base_addr, AXI_IIC_REG_SOFTR_OFFSET, AXI_IIC_SOFTR_KEY);

        /* Configure 100 MHz -> 100 kHz Standard Mode timing registers */
        reg_write32(base_addr,
                    AXI_IIC_REG_TSUSTA_OFFSET,
                    AXI_IIC_TIMING_TSUSTA_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_TSUSTO_OFFSET,
                    AXI_IIC_TIMING_TSUSTO_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_THDSTA_OFFSET,
                    AXI_IIC_TIMING_THDSTA_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_TSUDAT_OFFSET,
                    AXI_IIC_TIMING_TSUDAT_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_TBUF_OFFSET,
                    AXI_IIC_TIMING_TBUF_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_THIGH_OFFSET,
                    AXI_IIC_TIMING_THIGH_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_TLOW_OFFSET,
                    AXI_IIC_TIMING_TLOW_TICKS);
        reg_write32(base_addr,
                    AXI_IIC_REG_THDDAT_OFFSET,
                    AXI_IIC_TIMING_THDDAT_TICKS);

        /* Set RX FIFO programmable depth to maximum (16 bytes) */
        reg_write32(base_addr,
                    AXI_IIC_REG_RX_PIRQ_OFFSET,
                    AXI_IIC_RX_PIRQ_MAX_DEPTH);

        /* Reset TX FIFO, enable controller, and clear ISR flags */
        clear_tx_fifo_and_errors(base_addr);

        p_dev->init_state = AXI_IIC_STATE_READY;
    }

    return result;
}


axi_iic_status_t axi_iic_write_bytes(axi_iic_dev_t const * p_dev,
                                     uint8_t slave_addr_7b,
                                     uint8_t const * p_tx_bytes,
                                     size_t tx_len_bytes)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t fifo_word = 0U;
    size_t idx = 0U;

    result = validate_xfer_args(p_dev,
                                p_tx_bytes,
                                slave_addr_7b,
                                tx_len_bytes);

    if (AXI_IIC_OK == result)
    {
        clear_tx_fifo_and_errors(p_dev->base_addr);
        result = wait_bus_not_busy(p_dev);

        if (AXI_IIC_OK == result)
        {
            /* Frame 1: START + 7-bit Slave Address + Write bit (0) */
            fifo_word = (((uint32_t)slave_addr_7b << AXI_IIC_ADDR_RW_SHIFT)
                         | AXI_IIC_RW_WRITE_BIT
                         | AXI_IIC_DYN_START_MASK);
            result = push_tx_fifo_word(p_dev, fifo_word);
        }
        else
        {
            /* Bus busy error propagated */
        }

        /* Bounded by tx_len_bytes <= AXI_IIC_MAX_XFER_BYTES (validated) */
        while ((idx < tx_len_bytes)
               && (AXI_IIC_OK == result))
        {
            fifo_word = (uint32_t)p_tx_bytes[idx];
            if ((idx + 1U) == tx_len_bytes)
            {
                /* Tag last byte with STOP condition */
                fifo_word |= AXI_IIC_DYN_STOP_MASK;
            }
            else
            {
                /* Intermediate byte: no STOP */
            }
            result = push_tx_fifo_word(p_dev, fifo_word);
            idx++;
        }

        if (AXI_IIC_OK == result)
        {
            result = wait_tx_complete(p_dev);
        }
        else
        {
            /* Error during FIFO push propagated */
        }
    }
    else
    {
        /* Invalid argument or state: no bus access performed */
    }

    return result;
}


axi_iic_status_t axi_iic_write_read_bytes(axi_iic_dev_t const * p_dev,
                                          uint8_t slave_addr_7b,
                                          uint8_t reg_addr,
                                          uint8_t * p_rx_bytes,
                                          size_t rx_len_bytes)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t fifo_word = 0U;
    size_t idx = 0U;

    result = validate_xfer_args(p_dev,
                                p_rx_bytes,
                                slave_addr_7b,
                                rx_len_bytes);

    if (AXI_IIC_OK == result)
    {
        clear_tx_fifo_and_errors(p_dev->base_addr);
        result = wait_bus_not_busy(p_dev);

        if (AXI_IIC_OK == result)
        {
            /* Frame 1: START + 7-bit Slave Address + Write bit (0) */
            fifo_word = (((uint32_t)slave_addr_7b << AXI_IIC_ADDR_RW_SHIFT)
                         | AXI_IIC_RW_WRITE_BIT
                         | AXI_IIC_DYN_START_MASK);
            result = push_tx_fifo_word(p_dev, fifo_word);
        }
        else
        {
            /* Bus busy error propagated */
        }

        if (AXI_IIC_OK == result)
        {
            /* Frame 2: Address Pointer Register byte (no STOP -> RSTART) */
            fifo_word = (uint32_t)reg_addr;
            result = push_tx_fifo_word(p_dev, fifo_word);
        }
        else
        {
            /* Propagate error */
        }

        if (AXI_IIC_OK == result)
        {
            /* Frame 3: Repeated START + 7-bit Slave Address + Read bit (1) */
            fifo_word = (((uint32_t)slave_addr_7b << AXI_IIC_ADDR_RW_SHIFT)
                         | AXI_IIC_RW_READ_BIT
                         | AXI_IIC_DYN_START_MASK);
            result = push_tx_fifo_word(p_dev, fifo_word);
        }
        else
        {
            /* Propagate error */
        }

        if (AXI_IIC_OK == result)
        {
            /* Frame 4: Dynamic byte count + STOP bit triggers NACK + STOP */
            fifo_word = ((uint32_t)rx_len_bytes | AXI_IIC_DYN_STOP_MASK);
            result = push_tx_fifo_word(p_dev, fifo_word);
        }
        else
        {
            /* Propagate error */
        }

        /* Bounded by rx_len_bytes <= AXI_IIC_MAX_XFER_BYTES (validated) */
        while ((idx < rx_len_bytes)
               && (AXI_IIC_OK == result))
        {
            result = pop_rx_fifo_byte(p_dev, &p_rx_bytes[idx]);
            idx++;
        }

        if (AXI_IIC_OK == result)
        {
            /* All bytes received: TX_ERR now is the master's own final
             * NACK, not a slave error (OI-03) */
            result = wait_rx_complete(p_dev);
        }
        else
        {
            /* Propagate error */
        }
    }
    else
    {
        /* Invalid argument or state: no bus access performed */
    }

    return result;
}


/* Private helper implementations */

/**
 * @brief       Read a 32-bit memory-mapped AXI IIC register.
 * @req         LLR-IIC-001
 * @design      SDD-IIC-3.1
 * @param[in]   base_addr     Base address of the AXI IIC peripheral.
 * @param[in]   offset_bytes  Register offset in bytes.
 * @return      32-bit register contents.
 * @note        Single volatile access per §5 and §9. ISR-safe. Reentrant.
 */
static inline uint32_t reg_read32(uintptr_t base_addr, uint32_t offset_bytes)
{
    uintptr_t const reg_addr = base_addr + (uintptr_t)offset_bytes;
    /* MISRA deviation: Rule 11.4 — MMIO register access
     * — isolated in HAL accessor */
    uint32_t volatile const * const p_reg =
        (uint32_t volatile const *)reg_addr;
    uint32_t const reg_word = *p_reg;

    return reg_word;
}


/**
 * @brief       Write a 32-bit memory-mapped AXI IIC register.
 * @req         LLR-IIC-001
 * @design      SDD-IIC-3.2
 * @param[in]   base_addr     Base address of the AXI IIC peripheral.
 * @param[in]   offset_bytes  Register offset in bytes.
 * @param[in]   reg_word      32-bit word to write.
 * @note        Single volatile access per §5 and §9. ISR-safe. Reentrant.
 */
static inline void reg_write32(uintptr_t base_addr,
                               uint32_t offset_bytes,
                               uint32_t reg_word)
{
    uintptr_t const reg_addr = base_addr + (uintptr_t)offset_bytes;
    /* MISRA deviation: Rule 11.4 — MMIO register access
     * — isolated in HAL accessor */
    uint32_t volatile * const p_reg = (uint32_t volatile *)reg_addr;

    *p_reg = reg_word;
}


/**
 * @brief       Check that a poll budget lies in 1..AXI_IIC_MAX_POLL_LIMIT.
 * @req         LLR-IIC-002
 * @design      SDD-IIC-3.9
 * @param[in]   timeout_polls  Candidate poll budget, iterations.
 * @return      true if in range, false otherwise.
 * @note        Nested decisions keep each condition uncoupled for MC/DC.
 *              ISR-safe. Reentrant.
 */
static bool is_timeout_valid(uint32_t timeout_polls)
{
    bool is_valid = false;

    if (0U != timeout_polls)
    {
        if (AXI_IIC_MAX_POLL_LIMIT >= timeout_polls)
        {
            is_valid = true;
        }
        else
        {
            /* Above WCET poll limit */
        }
    }
    else
    {
        /* Zero budget would make every wait fail immediately */
    }

    return is_valid;
}


/**
 * @brief       Check that a transfer length lies in 1..MAX_XFER_BYTES.
 * @req         LLR-IIC-003, LLR-IIC-004
 * @design      SDD-IIC-3.10
 * @param[in]   len_bytes  Candidate transfer length, bytes.
 * @return      true if in range, false otherwise.
 * @note        Nested decisions keep each condition uncoupled for MC/DC.
 *              ISR-safe. Reentrant.
 */
static bool is_xfer_len_valid(size_t len_bytes)
{
    bool is_valid = false;

    if (0U != len_bytes)
    {
        if (AXI_IIC_MAX_XFER_BYTES >= len_bytes)
        {
            is_valid = true;
        }
        else
        {
            /* Above maximum transfer size */
        }
    }
    else
    {
        /* Zero-length transfer not supported */
    }

    return is_valid;
}


/**
 * @brief       Validate common arguments of the public transfer functions.
 * @req         LLR-IIC-003, LLR-IIC-004
 * @design      SDD-IIC-3.11
 * @param[in]   p_dev          Device pointer to validate (may be NULL).
 * @param[in]   p_bytes        Payload buffer pointer to validate (may be NULL).
 * @param[in]   slave_addr_7b  7-bit slave address, 0x00..0x7F.
 * @param[in]   len_bytes      Transfer length, 1..AXI_IIC_MAX_XFER_BYTES.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_PARAM, or AXI_IIC_ERR_STATE.
 * @note        Runtime checks (not ASSERT): inputs come from public API.
 *              ISR-safe. Reentrant.
 */
static axi_iic_status_t validate_xfer_args(axi_iic_dev_t const * p_dev,
                                           uint8_t const * p_bytes,
                                           uint8_t slave_addr_7b,
                                           size_t len_bytes)
{
    axi_iic_status_t result = AXI_IIC_OK;
    bool const is_len_ok = is_xfer_len_valid(len_bytes);

    if ((NULL == p_dev)
        || (NULL == p_bytes)
        || (slave_addr_7b > AXI_IIC_7BIT_ADDR_MAX))
    {
        result = AXI_IIC_ERR_PARAM;
    }
    else if (!is_len_ok)
    {
        result = AXI_IIC_ERR_PARAM;
    }
    else if (AXI_IIC_STATE_READY != p_dev->init_state)
    {
        result = AXI_IIC_ERR_STATE;
    }
    else
    {
        result = AXI_IIC_OK;
    }

    return result;
}


/**
 * @brief       Decide from a Status Register snapshot whether TX is done.
 * @req         LLR-IIC-003
 * @design      SDD-IIC-3.12
 * @param[in]   sr_flags  Snapshot of the AXI IIC Status Register.
 * @return      true when TX FIFO is empty and the bus is idle.
 * @note        if / else if chain avoids a coupled compound decision on
 *              sr_flags (MC/DC). ISR-safe. Reentrant.
 */
static bool is_tx_idle(uint32_t sr_flags)
{
    bool is_idle = false;

    if (0U == (sr_flags & AXI_IIC_SR_TX_EMPTY_MASK))
    {
        /* Bytes still queued in TX FIFO */
        is_idle = false;
    }
    else if (0U != (sr_flags & AXI_IIC_SR_BB_MASK))
    {
        /* FIFO drained but STOP not yet completed on the bus */
        is_idle = false;
    }
    else
    {
        is_idle = true;
    }

    return is_idle;
}


/**
 * @brief       Flush TX FIFO and clear latched ISR error bits.
 * @req         LLR-IIC-002
 * @design      SDD-IIC-3.3
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @note        Internal helper. Leaves controller enabled. Not ISR-safe.
 */
static void clear_tx_fifo_and_errors(uintptr_t base_addr)
{
    uint32_t isr_flags = 0U;

    ASSERT(0U != base_addr);

    reg_write32(base_addr,
                AXI_IIC_REG_CR_OFFSET,
                AXI_IIC_CR_TX_RST_MASK | AXI_IIC_CR_EN_MASK);
    reg_write32(base_addr,
                AXI_IIC_REG_CR_OFFSET,
                AXI_IIC_CR_EN_MASK);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);
    if (0U != (isr_flags & AXI_IIC_ISR_ERR_FLAGS_MASK))
    {
        /* Toggle-on-write to clear latched ARB_LOST and TX_ERR bits */
        reg_write32(base_addr,
                    AXI_IIC_REG_ISR_OFFSET,
                    isr_flags & AXI_IIC_ISR_ERR_FLAGS_MASK);
    }
    else
    {
        /* No error flags set; nothing to toggle */
    }
}


/**
 * @brief       Wait until the I2C bus is idle (BB == 0).
 * @req         LLR-IIC-003
 * @design      SDD-IIC-3.4
 * @param[in]   p_dev  Pointer to initialized AXI IIC device.
 * @return      AXI_IIC_OK or AXI_IIC_ERR_BUS_BUSY.
 * @note        Bounded by p_dev->timeout_polls <= AXI_IIC_MAX_POLL_LIMIT.
 */
static axi_iic_status_t wait_bus_not_busy(axi_iic_dev_t const * p_dev)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t sr_flags = 0U;
    uint32_t remaining_polls = 0U;

    ASSERT(NULL != p_dev);

    sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    remaining_polls = p_dev->timeout_polls;

    while ((0U != (sr_flags & AXI_IIC_SR_BB_MASK))
           && (0U < remaining_polls))
    {
        remaining_polls--;
        sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    }

    if (0U != (sr_flags & AXI_IIC_SR_BB_MASK))
    {
        result = AXI_IIC_ERR_BUS_BUSY;
    }
    else
    {
        result = AXI_IIC_OK;
    }

    return result;
}


/**
 * @brief       Check ISR register for NACK or Arbitration Lost errors.
 * @req         LLR-IIC-003
 * @design      SDD-IIC-3.5
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @return      AXI_IIC_OK or AXI_IIC_ERR_NACK_ARB.
 * @note        Clears TX FIFO and error flags if an error is detected.
 */
static axi_iic_status_t check_isr_errors(uintptr_t base_addr)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t isr_flags = 0U;

    ASSERT(0U != base_addr);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);
    if (0U != (isr_flags & AXI_IIC_ISR_ERR_FLAGS_MASK))
    {
        clear_tx_fifo_and_errors(base_addr);
        result = AXI_IIC_ERR_NACK_ARB;
    }
    else
    {
        result = AXI_IIC_OK;
    }

    return result;
}


/**
 * @brief       Wait for space in TX_FIFO and push one 16-bit dynamic word.
 * @req         LLR-IIC-003
 * @design      SDD-IIC-3.6
 * @param[in]   p_dev      Pointer to initialized AXI IIC device.
 * @param[in]   fifo_word  Dynamic command/data word to write to TX_FIFO.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_NACK_ARB, or AXI_IIC_ERR_TIMEOUT.
 * @note        Bounded by p_dev->timeout_polls <= AXI_IIC_MAX_POLL_LIMIT.
 */
static axi_iic_status_t push_tx_fifo_word(axi_iic_dev_t const * p_dev,
                                          uint32_t fifo_word)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t sr_flags = 0U;
    uint32_t remaining_polls = 0U;

    ASSERT(NULL != p_dev);

    sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    remaining_polls = p_dev->timeout_polls;

    while ((0U != (sr_flags & AXI_IIC_SR_TX_FULL_MASK))
           && (0U < remaining_polls)
           && (AXI_IIC_OK == result))
    {
        remaining_polls--;
        result = check_isr_errors(p_dev->base_addr);
        sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    }

    if (AXI_IIC_OK != result)
    {
        /* NACK / arbitration error already captured in result */
    }
    else if (0U != (sr_flags & AXI_IIC_SR_TX_FULL_MASK))
    {
        result = AXI_IIC_ERR_TIMEOUT;
    }
    else
    {
        reg_write32(p_dev->base_addr,
                    AXI_IIC_REG_TX_FIFO_OFFSET,
                    fifo_word);
    }

    return result;
}


/**
 * @brief       Wait for RX_FIFO data and pop one received byte.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.7
 * @param[in]   p_dev       Pointer to initialized AXI IIC device.
 * @param[out]  p_out_byte  Destination pointer for received byte.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_NACK_ARB, or AXI_IIC_ERR_TIMEOUT.
 * @note        Bounded by p_dev->timeout_polls <= AXI_IIC_MAX_POLL_LIMIT.
 */
static axi_iic_status_t pop_rx_fifo_byte(axi_iic_dev_t const * p_dev,
                                         uint8_t * p_out_byte)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t sr_flags = 0U;
    uint32_t rx_word = 0U;
    uint32_t remaining_polls = 0U;

    ASSERT(NULL != p_dev);
    ASSERT(NULL != p_out_byte);

    sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    remaining_polls = p_dev->timeout_polls;

    while ((0U != (sr_flags & AXI_IIC_SR_RX_EMPTY_MASK))
           && (0U < remaining_polls)
           && (AXI_IIC_OK == result))
    {
        remaining_polls--;
        result = check_rx_wait_errors(p_dev->base_addr);
        sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    }

    if (AXI_IIC_OK != result)
    {
        /* NACK / arbitration error already captured in result */
    }
    else if (0U != (sr_flags & AXI_IIC_SR_RX_EMPTY_MASK))
    {
        /* No data within P polls: NACK if TX_ERR latched, else timeout */
        result = rx_timeout_status(p_dev->base_addr);
    }
    else
    {
        rx_word = reg_read32(p_dev->base_addr, AXI_IIC_REG_RX_FIFO_OFFSET);
        *p_out_byte = (uint8_t)(rx_word & AXI_IIC_RX_FIFO_DATA_MASK);
    }

    return result;
}


/**
 * @brief       Wait for TX_FIFO to empty and bus to become idle after STOP.
 * @req         LLR-IIC-003
 * @design      SDD-IIC-3.8
 * @param[in]   p_dev  Pointer to initialized AXI IIC device.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_NACK_ARB, or AXI_IIC_ERR_TIMEOUT.
 * @note        Bounded by p_dev->timeout_polls <= AXI_IIC_MAX_POLL_LIMIT.
 */
static axi_iic_status_t wait_tx_complete(axi_iic_dev_t const * p_dev)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t sr_flags = 0U;
    uint32_t remaining_polls = 0U;
    bool is_done = false;

    ASSERT(NULL != p_dev);

    /* First snapshot is evaluated before the loop (no dead store) */
    sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    is_done = is_tx_idle(sr_flags);
    remaining_polls = p_dev->timeout_polls;

    while ((!is_done)
           && (0U < remaining_polls)
           && (AXI_IIC_OK == result))
    {
        remaining_polls--;
        result = check_isr_errors(p_dev->base_addr);
        sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
        is_done = is_tx_idle(sr_flags);
    }

    if (AXI_IIC_OK != result)
    {
        /* NACK / arbitration error already captured in result */
    }
    else if (!is_done)
    {
        result = AXI_IIC_ERR_TIMEOUT;
    }
    else
    {
        /* A NACK can latch in the same cycle the bus goes idle */
        result = check_isr_errors(p_dev->base_addr);
    }

    return result;
}


/**
 * @brief       Check ISR while waiting for RX data of a master receive.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.13
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @return      AXI_IIC_OK or AXI_IIC_ERR_NACK_ARB.
 * @note        ARB_LOST is always an error. A latched TX_ERR is judged from
 *              SR, independent of the order in which the core shows the
 *              flag and the data (board: both within one register read):
 *              data in the RX FIFO, or transfer still running (BB = 1) ->
 *              possibly the master's own final NACK (OI-03), keep waiting;
 *              bus released (BB = 0) with no data -> slave NACK -> error.
 *              Recovery as in check_isr_errors().
 */
static axi_iic_status_t check_rx_wait_errors(uintptr_t base_addr)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t isr_flags = 0U;
    uint32_t sr_flags = 0U;

    ASSERT(0U != base_addr);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);

    if (0U != (isr_flags & AXI_IIC_ISR_ARB_LOST_MASK))
    {
        clear_tx_fifo_and_errors(base_addr);
        result = AXI_IIC_ERR_NACK_ARB;
    }
    else if (0U == (isr_flags & AXI_IIC_ISR_TX_ERR_MASK))
    {
        /* No error latched */
        result = AXI_IIC_OK;
    }
    else
    {
        /* TX_ERR latched: SR tells who sent the NACK */
        sr_flags = reg_read32(base_addr, AXI_IIC_REG_SR_OFFSET);
        if (0U == (sr_flags & AXI_IIC_SR_RX_EMPTY_MASK))
        {
            /* Data waiting: expected master NACK of the last byte */
            result = AXI_IIC_OK;
        }
        else if (0U != (sr_flags & AXI_IIC_SR_BB_MASK))
        {
            /* Transfer still running: the byte may not be visible yet */
            result = AXI_IIC_OK;
        }
        else
        {
            /* Bus released and no data: the slave did not acknowledge */
            clear_tx_fifo_and_errors(base_addr);
            result = AXI_IIC_ERR_NACK_ARB;
        }
    }

    return result;
}


/**
 * @brief       Check ISR for Arbitration Lost only.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.14
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @return      AXI_IIC_OK or AXI_IIC_ERR_NACK_ARB.
 * @note        Used after every byte of a master receive has arrived, when
 *              TX_ERR is no longer a slave error. Recovery on error.
 */
static axi_iic_status_t check_arb_lost(uintptr_t base_addr)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t isr_flags = 0U;

    ASSERT(0U != base_addr);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);
    if (0U != (isr_flags & AXI_IIC_ISR_ARB_LOST_MASK))
    {
        clear_tx_fifo_and_errors(base_addr);
        result = AXI_IIC_ERR_NACK_ARB;
    }
    else
    {
        result = AXI_IIC_OK;
    }

    return result;
}


/**
 * @brief       Clear a TX_ERR latched by the master's own final NACK.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.15
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @note        Toggle-on-write clear (PG090 ISR), so the expected flag does
 *              not stay latched after a successful receive.
 */
static void clear_master_nack_flag(uintptr_t base_addr)
{
    uint32_t isr_flags = 0U;

    ASSERT(0U != base_addr);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);
    if (0U != (isr_flags & AXI_IIC_ISR_TX_ERR_MASK))
    {
        reg_write32(base_addr,
                    AXI_IIC_REG_ISR_OFFSET,
                    AXI_IIC_ISR_TX_ERR_MASK);
    }
    else
    {
        /* No master NACK flag latched; nothing to clear */
    }
}


/**
 * @brief       Wait for bus idle at the end of a master receive.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.16
 * @param[in]   p_dev  Pointer to initialized AXI IIC device.
 * @return      AXI_IIC_OK, AXI_IIC_ERR_NACK_ARB, or AXI_IIC_ERR_TIMEOUT.
 * @note        Same loop as wait_tx_complete(), but every byte has already
 *              been received, so only ARB_LOST is an error; a latched
 *              TX_ERR is the master's final NACK and is cleared on success.
 *              Bounded by p_dev->timeout_polls <= AXI_IIC_MAX_POLL_LIMIT.
 */
static axi_iic_status_t wait_rx_complete(axi_iic_dev_t const * p_dev)
{
    axi_iic_status_t result = AXI_IIC_OK;
    uint32_t sr_flags = 0U;
    uint32_t remaining_polls = 0U;
    bool is_done = false;

    ASSERT(NULL != p_dev);

    sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
    is_done = is_tx_idle(sr_flags);
    remaining_polls = p_dev->timeout_polls;

    while ((!is_done)
           && (0U < remaining_polls)
           && (AXI_IIC_OK == result))
    {
        remaining_polls--;
        result = check_arb_lost(p_dev->base_addr);
        sr_flags = reg_read32(p_dev->base_addr, AXI_IIC_REG_SR_OFFSET);
        is_done = is_tx_idle(sr_flags);
    }

    if (AXI_IIC_OK != result)
    {
        /* Arbitration lost already captured in result */
    }
    else if (!is_done)
    {
        result = AXI_IIC_ERR_TIMEOUT;
    }
    else
    {
        result = check_arb_lost(p_dev->base_addr);
    }

    if (AXI_IIC_OK == result)
    {
        clear_master_nack_flag(p_dev->base_addr);
    }
    else
    {
        /* Error path: recovery already cleared the error flags */
    }

    return result;
}


/**
 * @brief       Classify an RX wait that ended without data.
 * @req         LLR-IIC-004
 * @design      SDD-IIC-3.17
 * @param[in]   base_addr  Base address of the AXI IIC peripheral.
 * @return      AXI_IIC_ERR_NACK_ARB if TX_ERR is latched (slave NACK while
 *              the bus was still held), else AXI_IIC_ERR_TIMEOUT.
 * @note        Recovery (TX FIFO reset, error flags cleared) on NACK.
 */
static axi_iic_status_t rx_timeout_status(uintptr_t base_addr)
{
    axi_iic_status_t result = AXI_IIC_ERR_TIMEOUT;
    uint32_t isr_flags = 0U;

    ASSERT(0U != base_addr);

    isr_flags = reg_read32(base_addr, AXI_IIC_REG_ISR_OFFSET);
    if (0U != (isr_flags & AXI_IIC_ISR_TX_ERR_MASK))
    {
        clear_tx_fifo_and_errors(base_addr);
        result = AXI_IIC_ERR_NACK_ARB;
    }
    else
    {
        result = AXI_IIC_ERR_TIMEOUT;
    }

    return result;
}
