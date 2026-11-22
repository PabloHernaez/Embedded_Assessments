#include "environment.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#define SI7021_I2C_ADDR 0x40U

#define SI7021_CMD_READ_USER          0xE7U
#define SI7021_CMD_WRITE_USER         0xE6U
#define SI7021_CMD_MEASURE_RH_NO_HOLD 0xF5U /* humedad y, de paso, temperatura */
#define SI7021_CMD_READ_PREVIOUS_TEMP 0xE0U /* temperatura de la ultima medida RH */

/* Registro de usuario: RES1/RES0 (bits 7 y 0) y calentador (bit 2) a cero = RH12/T14 sin calentador. */
#define SI7021_CONFIG_MASK 0x85U

#define SI7021_CRC_POLYNOMIAL 0x31U   /* x^8 + x^5 + x^4 + 1, semilla 0x00 */
#define SI7021_DATA_MASK      0xFFFCU /* los dos bits bajos son de estado */

static const struct device *const environment_bus = DEVICE_DT_GET(DT_NODELABEL(i2c2));

int environment_init(void)
{
    uint8_t user;

    if (!device_is_ready(environment_bus)) {
        return -ENODEV;
    }

    int ret = i2c_reg_read_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_READ_USER, &user);
    if (ret < 0 || (user & SI7021_CONFIG_MASK) == 0U) {
        return ret; /* error, o ya esta como queremos (valor de fabrica) */
    }

    /* Leer-modificar-escribir para conservar los bits reservados, y verificar. */
    ret = i2c_reg_write_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_WRITE_USER,
                             user & (uint8_t)~SI7021_CONFIG_MASK);
    if (ret == 0) {
        ret = i2c_reg_read_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_READ_USER, &user);
    }
    if (ret == 0 && (user & SI7021_CONFIG_MASK) != 0U) {
        ret = -EIO;
    }
    return ret;
}

int environment_start(void)
{
    const uint8_t command = SI7021_CMD_MEASURE_RH_NO_HOLD;

    return i2c_write(environment_bus, &command, sizeof(command), SI7021_I2C_ADDR);
}

int environment_read(EnvironmentReading *out)
{
    uint8_t humidity[3]; /* MSB, LSB, CRC */
    uint8_t temperature[2];
    const uint8_t command = SI7021_CMD_READ_PREVIOUS_TEMP;

    /* Sin hold: si la conversion no ha terminado el sensor responde NACK. */
    int ret = i2c_read(environment_bus, humidity, sizeof(humidity), SI7021_I2C_ADDR);
    if (ret < 0) {
        return ret;
    }
    if (crc8(humidity, 2U, SI7021_CRC_POLYNOMIAL, 0x00U, false) != humidity[2]) {
        return -EBADMSG;
    }

    ret = i2c_write_read(environment_bus, SI7021_I2C_ADDR, &command, sizeof(command),
                         temperature, sizeof(temperature));
    if (ret < 0) {
        return ret;
    }

    /* Formulas del datasheet; la humedad se acota a 0-100 % como indica el fabricante. */
    const uint16_t rh_code = sys_get_be16(humidity) & SI7021_DATA_MASK;
    const uint16_t t_code = sys_get_be16(temperature) & SI7021_DATA_MASK;

    *out = (EnvironmentReading){
        .temperature_c = 175.72f * (float)t_code / 65536.0f - 46.85f,
        .humidity_pct = CLAMP(125.0f * (float)rh_code / 65536.0f - 6.0f, 0.0f, 100.0f),
    };
    return 0;
}
