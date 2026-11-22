#include "imu.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

/* Direccion de 7 bits con AD0 a nivel bajo. WHO_AM_I devuelve el mismo valor. */
#define MPU_I2C_ADDR    0x68U
#define MPU_EXPECTED_ID 0x68U

/* Registros del MPU6050 usados por el driver. */
#define MPU_REG_SMPLRT_DIV   0x19U
#define MPU_REG_CONFIG       0x1AU
#define MPU_REG_GYRO_CONFIG  0x1BU
#define MPU_REG_ACCEL_CONFIG 0x1CU
#define MPU_REG_FIFO_EN      0x23U
#define MPU_REG_INT_PIN_CFG  0x37U
#define MPU_REG_INT_ENABLE   0x38U
#define MPU_REG_INT_STATUS   0x3AU
#define MPU_REG_PWR_MGMT_1   0x6BU
#define MPU_REG_PWR_MGMT_2   0x6CU
#define MPU_REG_WHO_AM_I     0x75U

/* Valores escritos. */
#define MPU_DEVICE_RESET   0x80U /* PWR_MGMT_1: reset completo */
#define MPU_PWR_PLL_NOTEMP 0x09U /* PWR_MGMT_1: reloj PLL gyro X, sensor de temperatura apagado */
#define MPU_SMPLRT_50HZ    19U   /* 1 kHz / (1 + 19) = 50 Hz */
#define MPU_DLPF_20HZ      0x04U /* CONFIG: DLPF4, ~20 Hz de ancho de banda */
#define MPU_GYRO_500DPS    0x08U /* FS_SEL = 1 */
#define MPU_ACCEL_4G       0x08U /* AFS_SEL = 1 */
#define MPU_DATA_RDY       0x01U /* INT_ENABLE e INT_STATUS: bit DATA_RDY */

/* Escalas del datasheet: +-4 g -> 8192 LSB/g; +-500 grados/s -> 65,5 LSB/(grados/s). */
#define MPU_ACCEL_MPS2_PER_LSB (9.80665f / 8192.0f)
#define MPU_GYRO_DPS_PER_LSB   (1.0f / 65.5f)

/* Margen bajo el fondo de escala (32767) para marcar saturacion. */
#define MPU_RAW_SATURATION 32760

static const struct device *const imu_bus = DEVICE_DT_GET(DT_NODELABEL(i2c2));
static const struct gpio_dt_spec imu_scl = GPIO_DT_SPEC_GET(DT_NODELABEL(i2c2), scl_gpios);
static const struct gpio_dt_spec imu_sda = GPIO_DT_SPEC_GET(DT_NODELABEL(i2c2), sda_gpios);

static const struct {
    uint8_t reg;
    uint8_t value;
} initial_config[] = {
    { MPU_REG_PWR_MGMT_1, MPU_PWR_PLL_NOTEMP },
    { MPU_REG_PWR_MGMT_2, 0x00 },             /* seis ejes activos */
    { MPU_REG_SMPLRT_DIV, MPU_SMPLRT_50HZ },
    { MPU_REG_CONFIG, MPU_DLPF_20HZ },
    { MPU_REG_GYRO_CONFIG, MPU_GYRO_500DPS },
    { MPU_REG_ACCEL_CONFIG, MPU_ACCEL_4G },
    { MPU_REG_INT_PIN_CFG, 0x00 },            /* DATA_RDY se borra al leer INT_STATUS */
    { MPU_REG_INT_ENABLE, MPU_DATA_RDY },
    { MPU_REG_FIFO_EN, 0x00 },                /* sin FIFO */
};

/* Escribe un registro y lo relee para confirmar el valor. */
static int write_checked(uint8_t reg, uint8_t value)
{
    uint8_t readback;
    int ret = i2c_reg_write_byte(imu_bus, MPU_I2C_ADDR, reg, value);

    if (ret == 0) {
        ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, reg, &readback);
    }
    if (ret == 0 && readback != value) {
        ret = -EIO;
    }
    return ret;
}

int imu_init(void)
{
    uint8_t identity;

    if (!device_is_ready(imu_bus)) {
        return -ENODEV;
    }

    int ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, MPU_REG_WHO_AM_I, &identity);
    if (ret < 0) {
        return ret;
    }
    if (identity != MPU_EXPECTED_ID) {
        return -ENODEV;
    }

    /* El bit de reset se borra solo: no se relee. */
    ret = i2c_reg_write_byte(imu_bus, MPU_I2C_ADDR, MPU_REG_PWR_MGMT_1, MPU_DEVICE_RESET);
    if (ret < 0) {
        return ret;
    }
    k_sleep(K_MSEC(100));

    for (size_t i = 0; i < ARRAY_SIZE(initial_config); i++) {
        ret = write_checked(initial_config[i].reg, initial_config[i].value);
        if (ret < 0) {
            return ret;
        }
    }

    k_sleep(K_MSEC(40)); /* estabilizacion de giroscopo y DLPF */
    return 0;
}

int imu_read(ImuReading *out)
{
    /*
     * Lectura en rafaga desde INT_STATUS (0x3A):
     * [0] INT_STATUS, [1..6] ACCEL X/Y/Z, [7..8] TEMP (no usada), [9..14] GYRO X/Y/Z.
     * Todos los valores son int16 big-endian.
     */
    uint8_t frame[15];

    int ret = i2c_burst_read(imu_bus, MPU_I2C_ADDR, MPU_REG_INT_STATUS, frame, sizeof(frame));
    if (ret < 0) {
        return ret;
    }
    if ((frame[0] & MPU_DATA_RDY) == 0U) {
        return IMU_NO_NEW;
    }

    int16_t raw[6];
    for (size_t i = 0; i < 3; i++) {
        raw[i] = (int16_t)sys_get_be16(&frame[1 + 2 * i]);
        raw[3 + i] = (int16_t)sys_get_be16(&frame[9 + 2 * i]);
    }

    bool saturated = false;
    for (size_t i = 0; i < ARRAY_SIZE(raw); i++) {
        saturated = saturated || abs(raw[i]) >= MPU_RAW_SATURATION;
    }

    *out = (ImuReading){
        .accel_mps2 = {
            raw[0] * MPU_ACCEL_MPS2_PER_LSB,
            raw[1] * MPU_ACCEL_MPS2_PER_LSB,
            raw[2] * MPU_ACCEL_MPS2_PER_LSB,
        },
        .gyro_dps = {
            raw[3] * MPU_GYRO_DPS_PER_LSB,
            raw[4] * MPU_GYRO_DPS_PER_LSB,
            raw[5] * MPU_GYRO_DPS_PER_LSB,
        },
        .range_limited = saturated,
    };
    return IMU_NEW;
}

int imu_check_configuration(bool *matches)
{
    uint8_t power;
    uint8_t config[4]; /* SMPLRT_DIV, CONFIG, GYRO_CONFIG, ACCEL_CONFIG consecutivos */

    if (!device_is_ready(imu_bus)) {
        return -ENODEV;
    }

    int ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, MPU_REG_PWR_MGMT_1, &power);
    if (ret == 0) {
        ret = i2c_burst_read(imu_bus, MPU_I2C_ADDR, MPU_REG_SMPLRT_DIV, config, sizeof(config));
    }
    if (ret == 0) {
        *matches = power == MPU_PWR_PLL_NOTEMP && config[0] == MPU_SMPLRT_50HZ &&
                   config[1] == MPU_DLPF_20HZ && config[2] == MPU_GYRO_500DPS &&
                   config[3] == MPU_ACCEL_4G;
    }
    return ret;
}

int imu_bus_is_blocked(bool *blocked)
{
    if (!gpio_is_ready_dt(&imu_scl) || !gpio_is_ready_dt(&imu_sda)) {
        return -ENODEV;
    }

    const int scl = gpio_pin_get_dt(&imu_scl);
    const int sda = gpio_pin_get_dt(&imu_sda);
    if (scl < 0 || sda < 0) {
        return scl < 0 ? scl : sda;
    }

    *blocked = scl == 0 || sda == 0;
    return 0;
}

int imu_recover_bus(void)
{
    if (!device_is_ready(imu_bus)) {
        return -ENODEV;
    }
    return i2c_recover_bus(imu_bus);
}
