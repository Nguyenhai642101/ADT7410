/**
 * @file    regmodel.h
 * @brief   Static RAM model of the AXI IIC register block (host tests only).
 *
 * The HAL takes base_addr as an integer and accesses base + offset through
 * volatile pointers. On the host we pass the address of this RAM array, so
 * the real axi_iic_hal.c runs unchanged.
 *
 * Limits of this model (see test_report.md §6):
 *  - Registers are plain memory: SR/ISR do not change by themselves while
 *    the HAL polls, so each test sets a fixed "hardware situation".
 *  - TX_FIFO keeps only the last word written; the full frame sequence is
 *    checked only on the target or in an HIL / RTL co-simulation.
 *  - ISR toggle-on-write is not emulated (the written value is stored).
 */

#ifndef REGMODEL_H
#define REGMODEL_H

#include <stdint.h>
#include <string.h>

#define RM_WORDS            (0x150U / 4U)
#define RM_ISR              (0x020U / 4U)
#define RM_SOFTR            (0x040U / 4U)
#define RM_CR               (0x100U / 4U)
#define RM_SR               (0x104U / 4U)
#define RM_TX_FIFO          (0x108U / 4U)
#define RM_RX_FIFO          (0x10CU / 4U)
#define RM_RX_PIRQ          (0x120U / 4U)
#define RM_TSUSTA           (0x128U / 4U)
#define RM_TSUSTO           (0x12CU / 4U)
#define RM_THDSTA           (0x130U / 4U)
#define RM_TSUDAT           (0x134U / 4U)
#define RM_TBUF             (0x138U / 4U)
#define RM_THIGH            (0x13CU / 4U)
#define RM_TLOW             (0x140U / 4U)
#define RM_THDDAT           (0x144U / 4U)

/* SR / ISR bits (PG090) */
#define RM_SR_BB            (0x04U)
#define RM_SR_TX_FULL       (0x10U)   /* PG090 SR bit 4 */
#define RM_SR_RX_EMPTY      (0x40U)
#define RM_SR_TX_EMPTY      (0x80U)
#define RM_ISR_ARB_LOST     (0x01U)
#define RM_ISR_TX_ERR       (0x02U)
#define RM_DYN_START        (0x100U)
#define RM_DYN_STOP         (0x200U)

static volatile uint32_t rm_regs[RM_WORDS];

static uintptr_t rm_base(void)
{
    return (uintptr_t)&rm_regs[0];
}

static void rm_reset(void)
{
    uint32_t i;

    for (i = 0U; i < RM_WORDS; i++)
    {
        rm_regs[i] = 0U;
    }
}

#endif /* REGMODEL_H */
