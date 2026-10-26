#include "environment.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#define SI7021_I2C_ADDR 0x40U

#define SI7021_CMD_READ_USER  0xE7U
#define SI7021_CMD_WRITE_USER 0xE6U

/* Resolucion: bits 7 y 0. Calentador: bit 2. */
#define SI7021_RESOLUTION_MASK 0x81U
#define SI7021_HEATER_MASK     0x04U

#define SI7021_CONFIG_MASK \
    (SI7021_RESOLUTION_MASK | SI7021_HEATER_MASK)

#define SI7021_CMD_MEASURE_RH_NO_HOLD 0xF5U
#define SI7021_CMD_READ_PREVIOUS_TEMP 0xE0U

/* Polinomio CRC-8 del Si7021, sin el coeficiente x^8. */
#define SI7021_CRC_POLYNOMIAL 0x31U
#define SI7021_DATA_MASK      0xFFFCU

static const struct device *const environment_bus = DEVICE_DT_GET(DT_NODELABEL(i2c2));

int environment_init(void)
{
    uint8_t current;
    uint8_t configured;
    uint8_t readback;
    int ret;

    if (!device_is_ready(environment_bus)) { return -ENODEV; }

    /* Leer primero para conservar los bits reservados. */
    ret = i2c_reg_read_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_READ_USER, &current);
    if (ret < 0) { return ret; }

    /* RH12/T14 y calentador apagado: no hace falta escribir si ya coinciden. */
    if ((current & SI7021_CONFIG_MASK) == 0U) { return 0; }

    /*
     * Poner a cero:
     * - RES1 y RES0: humedad 12 bits / temperatura 14 bits.
     * - HTRE: calentador apagado.
     *
     * Los demas bits conservan su valor.
     */
    configured = (uint8_t)(current & ~SI7021_CONFIG_MASK);

    ret = i2c_reg_write_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_WRITE_USER, configured);
    if (ret < 0) { return ret; }

    /* Comprobar los bits que acabamos de configurar. */
    ret = i2c_reg_read_byte(environment_bus, SI7021_I2C_ADDR, SI7021_CMD_READ_USER, &readback);
    if (ret < 0) { return ret; }

    if ((readback & SI7021_CONFIG_MASK) != 0U) { return -EIO; }

    return 0;
}

int environment_start(void)
{
    const uint8_t command = SI7021_CMD_MEASURE_RH_NO_HOLD;
    return i2c_write(environment_bus, &command, sizeof(command), SI7021_I2C_ADDR);
}

/**
 *  Función de lectura del ambiente
 *      La lectura del Si7021 arroja una lectura de 3 bytes:
 *          humidity_frame[0] -> parte alta del número de humedad
 *          humidity_frame[1] -> parte baja del número de humedad
 *          humidity_frame[2] -> número CRC de control
 *              CRC -> comprobacion de redundancia ciclica de los bytes 0 y 1
 */
int environment_read(EnvironmentReading *out)
{
    uint8_t humidity_frame[3];
    uint8_t temperature_frame[2];
    int ret;

    /**
     *  humidity_frame es el arreglo que recibe los 3 bytes del sensor
     *  Recoger humedad y CRC, sin iniciar otra conversion. 
     */
    ret = i2c_read(environment_bus, humidity_frame,
                    sizeof(humidity_frame), SI7021_I2C_ADDR);
    if (ret < 0) {
        return ret;
    }

    /**
     *  Zephyr calcula el CRC de los dos bytes originales, antes de quitar
     *  los bits de estado. Inicializacion 0x00, sin reflexion de bits.
     *  Si no coincide, no leer temperatura ni modificar la salida.
     */
    if (crc8(humidity_frame, 2U, SI7021_CRC_POLYNOMIAL, 0x00U, false) != humidity_frame[2]) {
        return -EBADMSG;
    }

    /* 0xE0 recupera dos bytes, sin CRC ni otra conversion, de la misma medicion. */
    const uint8_t command = SI7021_CMD_READ_PREVIOUS_TEMP;

    ret = i2c_write_read(environment_bus, SI7021_I2C_ADDR,
                          &command, sizeof(command),
                          temperature_frame,
                          sizeof(temperature_frame));
    if (ret < 0) {
        return ret;
    }

    // Los bytes llegan en orden big-endian; la mascara retira los bits de estado.
    uint16_t humidity_code = (uint16_t)(sys_get_be16(humidity_frame) & SI7021_DATA_MASK);
    uint16_t temperature_code = (uint16_t)(sys_get_be16(temperature_frame) & SI7021_DATA_MASK);

    // Formulas del fabricante para convertir el codigo a unidades fisicas.
    EnvironmentReading result = {
        .temperature_c = 175.72f * (float)temperature_code / 65536.0f - 46.85f,
        .humidity_pct = 125.0f * (float)humidity_code / 65536.0f - 6.0f,
    };

    // Post-procesamiento de los valores. No hay humedad menor a 0 ni mayor a 100
    if (result.humidity_pct < 0.0f) {
        result.humidity_pct = 0.0f;
    } else if (result.humidity_pct > 100.0f) {
        result.humidity_pct = 100.0f;
    }

    /* Publicar solamente cuando el par completo es correcto. */
    *out = result;

    return 0;
}
