/**
 * @file    test_integration.c
 * @brief   Software/software integration: real adt7410_driver.c on top of
 *          the real axi_iic_hal.c, using the static register model.
 *
 * Verifies that both modules link, agree on the interface (status codes,
 * byte order, frame lengths) and propagate HAL faults to the driver.
 */

#include "adt7410_driver.h"
#include "axi_iic_hal.h"
#include "regmodel.h"
#include "test_harness.h"

static adt7410_config_t const cfg_default =
{
    .resolution   = ADT7410_RES_16BIT,
    .op_mode      = ADT7410_MODE_CONTINUOUS,
    .event_mode   = ADT7410_EVENT_COMPARATOR,
    .int_polarity = ADT7410_POL_ACTIVE_HIGH,
    .ct_polarity  = ADT7410_POL_ACTIVE_LOW,
    .fault_queue  = ADT7410_FAULT_QUEUE_3
};


static void tc_int_001(void)
{
    axi_iic_dev_t iic;
    adt7410_dev_t dev;
    adt7410_sample_t sample;
    float die_temp_degc = 0.0f;
    adt7410_status_t status;

    th_begin("TC-INT-001", "LLR-ADT-001.5,001.6,003.1,003.8,IIC-004",
             "init + read_temperature / read_temp_degc through the real HAL");
    rm_reset();
    TH_CHECK(AXI_IIC_OK == axi_iic_init(&iic, rm_base(), 16U));
    rm_regs[RM_SR] = RM_SR_TX_EMPTY;
    rm_regs[RM_RX_FIFO] = 0xCBU; /* Every received byte = ID register */

    status = adt7410_init(&dev, &iic, 0x48U, &cfg_default);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(ADT7410_STATE_READY == dev.init_state);
    /* Last HAL word of init = config byte 0x9A tagged with STOP */
    TH_CHECK((RM_DYN_STOP | 0x9AU) == rm_regs[RM_TX_FIFO]);

    /* Both bytes 0xCB -> 0xCBCB = -13365 LSB -> -104414.0625 mdegC */
    status = adt7410_read_temperature(&dev, &sample);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK(-13365 == sample.die_temp_q8_7);
    TH_CHECK(-104414 == sample.die_temp_mdegc);
    TH_CHECK((RM_DYN_STOP | 2U) == rm_regs[RM_TX_FIFO]);

    status = adt7410_read_temp_degc(&dev, &die_temp_degc);
    TH_CHECK(ADT7410_OK == status);
    TH_CHECK((-104.415f < die_temp_degc) && (-104.414f > die_temp_degc));
    th_end();
}


static void tc_int_002(void)
{
    axi_iic_dev_t iic;
    adt7410_dev_t dev;
    adt7410_status_t status;
    uint8_t hyst_degc = 0U;

    th_begin("TC-INT-002", "LLR-ADT-001.2,IIC-003.3,003.6",
             "HAL faults (NACK, bus busy, uninit bus) -> ERR_I2C_COMM");
    rm_reset();
    TH_CHECK(AXI_IIC_OK == axi_iic_init(&iic, rm_base(), 16U));
    rm_regs[RM_SR] = RM_SR_TX_EMPTY;
    rm_regs[RM_RX_FIFO] = 0xCBU;
    TH_CHECK(ADT7410_OK == adt7410_init(&dev, &iic, 0x48U, &cfg_default));

    /* Sensor stops ACKing: slave NACK -> TX_ERR with no data received */
    rm_regs[RM_ISR] = RM_ISR_TX_ERR;
    rm_regs[RM_SR] = RM_SR_TX_EMPTY | RM_SR_RX_EMPTY;
    status = adt7410_get_hysteresis(&dev, &hyst_degc);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);

    rm_regs[RM_ISR] = 0U;
    rm_regs[RM_SR] = RM_SR_BB;               /* Bus stuck low */
    status = adt7410_set_hysteresis(&dev, 2U);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);

    iic.init_state = AXI_IIC_STATE_UNINIT;   /* Bus not initialised */
    rm_regs[RM_SR] = RM_SR_TX_EMPTY;
    status = adt7410_get_hysteresis(&dev, &hyst_degc);
    TH_CHECK(ADT7410_ERR_I2C_COMM == status);
    th_end();
}


int main(void)
{
    tc_int_001();
    tc_int_002();

    return th_summary("integration");
}
