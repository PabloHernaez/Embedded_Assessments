#include "imu.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>

/* Direccion I2C de 7 bits, suponiendo AD0 a nivel bajo. */
#define MPU_I2C_ADDR    0x68U
#define MPU_EXPECTED_ID 0x68U

/* Valores compartidos por inicializacion y comprobacion. */
#define MPU_EXPECTED_PWR_MGMT_1   0x09U
#define MPU_EXPECTED_SMPLRT_DIV   19U
#define MPU_EXPECTED_CONFIG       0x04U
#define MPU_EXPECTED_GYRO_CONFIG  0x08U
#define MPU_EXPECTED_ACCEL_CONFIG 0x08U

/**
 * Establecimos un rango de funcionamiento noemal de +-4G
 * El fabricante del sensor determina 8192 cuentas por cada G
 *      Es decir, 8192 es 1G
 * 
 * Establecimos un rango de +-500 °/s.
 * El giroscopio mide velocidad angular (rapidez de giro en cada eje)
 *      65.5 cuentas por cada grado por segundo
 *          Es decir, si la lectura es de 6550, es 100°/s
 * 
 */
#define MPU_ACCEL_MPS2_PER_LSB (9.80665f / 8192.0f)
#define MPU_GYRO_DPS_PER_LSB   (1.0f / 65.5f)

/* Valor máximo para calcular saturación. Máximo de la escala: 32767*/
#define MPU_RAW_SATURATION 32760
#define MPU_READ_SIZE      15U

enum {
    MPU_REG_INT_STATUS      = 0x3A,
    MPU_DATA_RDY_FLAG       = 0x01,
    MPU_REG_SMPLRT_DIV      = 0x19,
    MPU_REG_CONFIG          = 0x1A,
    MPU_REG_GYRO_CONFIG     = 0x1B,
    MPU_REG_ACCEL_CONFIG    = 0x1C,
    MPU_REG_FIFO_EN         = 0x23,
    MPU_REG_INT_PIN_CFG     = 0x37,
    MPU_REG_INT_ENABLE      = 0x38,
    MPU_REG_PWR_MGMT_1      = 0x6B,
    MPU_REG_PWR_MGMT_2      = 0x6C,
    MPU_REG_WHO_AM_I        = 0x75,

    MPU_DEVICE_RESET        = 0x80,
    MPU_DATA_RDY_ENABLE     = 0x01
};

static const struct device *const imu_bus =
    DEVICE_DT_GET(DT_NODELABEL(i2c2));

typedef struct {
    uint8_t reg;
    uint8_t value;
} RegisterSetting;

static const RegisterSetting initial_config[] = {
    /* PLL del gyro X, despierto, temperatura interna desactivada. */
    { MPU_REG_PWR_MGMT_1, MPU_EXPECTED_PWR_MGMT_1 },

    /* Todos los ejes de aceleracion y giro habilitados. */
    { MPU_REG_PWR_MGMT_2, 0x00 },

    /* Muestreo nominal de 50 Hz con DLPF habilitado. */
    { MPU_REG_SMPLRT_DIV, MPU_EXPECTED_SMPLRT_DIV },

    /* Filtro interno DLPF4. */
    { MPU_REG_CONFIG, MPU_EXPECTED_CONFIG },

    /* Giroscopio: rango de +/-500 grados/s. */
    { MPU_REG_GYRO_CONFIG, MPU_EXPECTED_GYRO_CONFIG },

    /* Acelerometro: rango de +/-4 g. */
    { MPU_REG_ACCEL_CONFIG, MPU_EXPECTED_ACCEL_CONFIG },

    /* DATA_RDY se limpia leyendo INT_STATUS. */
    { MPU_REG_INT_PIN_CFG, 0x00 },

    /* Habilitar el indicador de nueva muestra. */
    { MPU_REG_INT_ENABLE, MPU_DATA_RDY_ENABLE },

    /* No introducir medidas en el FIFO. */
    { MPU_REG_FIFO_EN, 0x00 },
};

/**
 *  Comprobación de escritura de configuraciones sobre el I2C
 *  reg     -> registro sobre el que se va a escribir
 *  value   -> valor que se va a escribir de prueba
 *  return != 0 -> error posible 
 *  Este método se llama por cada una de las configuraciones de initial_config[]
 */
static int write_checked(uint8_t reg, uint8_t value)
{
    uint8_t readback;
    int ret;

    ret = i2c_reg_write_byte(imu_bus, MPU_I2C_ADDR, reg, value);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, reg, &readback);
    if (ret < 0) {
        return ret;
    }

    if (readback != value) {
        return -EIO;
    }

    return 0;
}

/**
 *  Inicialización del IMU: No recibe nada.
 *  1. Se consulta WHO_AM_I en 0x75, se espera 0x68 (MPU_EXPECTED_ID)
 *  2. Se escribe el bit de reset. Este se borra automáticamente, así que
 *      no se hace comprobación de lectura. Luego hay un k_sleep de 100ms
 *  3. Se recorren la initial_config y se les hace write_checked()
 *      Se espera 40ms con k_sleep para que haya estabilización
 */
int imu_init(void)
{
    uint8_t identity;
    int ret;

    if (!device_is_ready(imu_bus)) {
        return -ENODEV;
    }

    ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, MPU_REG_WHO_AM_I, &identity);
    if (ret < 0) {
        return ret;
    }

    if (identity != MPU_EXPECTED_ID) {
        return -ENODEV;
    }

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

    k_sleep(K_MSEC(40));

    return 0;
}


/**
 *  Evaluación de saturación del valor raw
 *      Si el valor es negativo, le da valor negativo
 *      comprueba luego si la magnitud es mayor que la saturación
 *      Si si, devuelve True  
 */

static bool raw_is_saturated(int16_t raw)
{
    int32_t magnitude = raw;

    if (magnitude < 0) {
        magnitude = -magnitude;
    }

    return magnitude >= MPU_RAW_SATURATION;
}

/**
 *  Función de lectura de IMU. Recibe/Escribe en puntero out Imureading
 *      Devuelve estado
 *  i2c_burst_read genera un frame de datos, el cual escribe posiciones:
 *      0       -> indicador de muestra nueva
 *      1-6     -> Aceleración en X/Y/Z: Escribe 1, 3, 5 en raw_accel
 *      7-8     -> Temperatura interna. No se usa
 *      9-14    -> Giro en X/Y/Z: Escribe 9, 11, 13 en raw_gyro
 * 
 * 
 */

int imu_read(ImuReading *out)
{
    uint8_t frame[MPU_READ_SIZE];

    int ret = i2c_burst_read(imu_bus, MPU_I2C_ADDR,
                             MPU_REG_INT_STATUS,
                             frame, sizeof(frame));
    if (ret < 0) {
        return ret;
    }

    if ((frame[0] & MPU_DATA_RDY_FLAG) == 0) {
        return IMU_NO_NEW;
    }

    int16_t raw_accel[3] = {
        (int16_t)sys_get_be16(&frame[1]),
        (int16_t)sys_get_be16(&frame[3]),
        (int16_t)sys_get_be16(&frame[5]),
    };

    int16_t raw_gyro[3] = {
        (int16_t)sys_get_be16(&frame[9]),
        (int16_t)sys_get_be16(&frame[11]),
        (int16_t)sys_get_be16(&frame[13]),
    };

    bool range_limited = false;

    // Si los datos de aceleración o giro están saturados, limita el rango
    for (size_t i = 0; i < ARRAY_SIZE(raw_accel); i++) {
        if (raw_is_saturated(raw_accel[i]) ||
            raw_is_saturated(raw_gyro[i])) {
            range_limited = true;
            break;
        }
    }

    // Llena los resultados en result, los multiplica por la gravedad y por 
    ImuReading result = {
        .accel_mps2 = {
            .x = raw_accel[0] * MPU_ACCEL_MPS2_PER_LSB,
            .y = raw_accel[1] * MPU_ACCEL_MPS2_PER_LSB,
            .z = raw_accel[2] * MPU_ACCEL_MPS2_PER_LSB,
        },
        .gyro_dps = {
            .x = raw_gyro[0] * MPU_GYRO_DPS_PER_LSB,
            .y = raw_gyro[1] * MPU_GYRO_DPS_PER_LSB,
            .z = raw_gyro[2] * MPU_GYRO_DPS_PER_LSB,
        },
        .range_limited = range_limited,
    };

    *out = result;
    return IMU_NEW;
}


/**
 *  Esta función verifica que las configuraciones del IMU estén correctas
 */
int imu_check_configuration(bool *matches)
{
    uint8_t power;
    uint8_t config[4];
    int ret;

    if (!device_is_ready(imu_bus)) { return -ENODEV; }

    ret = i2c_reg_read_byte(imu_bus, MPU_I2C_ADDR, MPU_REG_PWR_MGMT_1, &power);
    if (ret < 0) { return ret; }

    /*
     * Cuatro registros consecutivos:
     * [0] SMPLRT_DIV
     * [1] CONFIG
     * [2] GYRO_CONFIG
     * [3] ACCEL_CONFIG
     */
    ret = i2c_burst_read(imu_bus, MPU_I2C_ADDR, MPU_REG_SMPLRT_DIV, config, sizeof(config));
    if (ret < 0) { return ret; }

    bool result =
        power     == MPU_EXPECTED_PWR_MGMT_1 &&
        config[0] == MPU_EXPECTED_SMPLRT_DIV &&
        config[1] == MPU_EXPECTED_CONFIG &&
        config[2] == MPU_EXPECTED_GYRO_CONFIG &&
        config[3] == MPU_EXPECTED_ACCEL_CONFIG;

    *matches = result;

    return 0;
}