/*
  Derived from https://github.com/Kaze-nomi/SmartGreenhouse/blob/main/sht30.chip.c
*/

#include "wokwi-api.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// https://rosettacode.org/wiki/Pseudo-random_numbers/PCG32#C
const uint64_t N = 6364136223846793005;

static uint64_t state = 0x853c49e6748fea9b;
static uint64_t inc = 0xda3e39cb94b95bdb;

uint32_t pcg32_int()
{
  uint64_t old = state;
  state = old * N + inc;
  uint32_t shifted = (uint32_t)(((old >> 18) ^ old) >> 27);
  uint32_t rot = old >> 59;
  return (shifted >> rot) | (shifted << ((~rot + 1) & 31));
}

double pcg32_float()
{
  return ldexp(pcg32_int(), -32);
  // return ((double)pcg32_int()) / (1LL << 32);
}

void pcg32_seed(uint64_t seed_state, uint64_t seed_sequence)
{
  state = 0;
  inc = (seed_sequence << 1) | 1;
  pcg32_int();
  state = state + seed_state;
  pcg32_int();
}

// https://en.wikipedia.org/wiki/Marsaglia_polar_method#C++
double gaussian(double mean, double stdDev)
{
  static double spare;
  static bool hasSpare = false;

  if (hasSpare)
  {
    hasSpare = false;
    return spare * stdDev + mean;
  }
  else
  {
    double u, v, s;
    do
    {
      u = pcg32_float() * 2.0 - 1.0;
      v = pcg32_float() * 2.0 - 1.0;
      s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    s = sqrt(-2.0 * log(s) / s);
    spare = v * s;
    hasSpare = true;
    return mean + stdDev * u * s;
  }
}

static inline void set_flag(uint16_t *state, uint16_t flag)
{
  *state |= flag;
}

static inline void clear_flag(uint16_t *state, uint16_t flag)
{
  *state &= ~flag;
}

static inline bool test_flag(uint16_t state, uint16_t flag)
{
  return (state & flag) != 0;
}

static float clamp_float(float value, float min_value, float max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }
  return value;
}

static uint8_t sht30_crc8(uint8_t msb, uint8_t lsb)
{
  uint8_t crc = 0xff;
  uint8_t data[2] = {msb, lsb};

  for (int i = 0; i < 2; i++)
  {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++)
    {
      if (crc & 0x80)
      {
        crc = (uint8_t)((crc << 1) ^ 0x31);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

#define SHT3X_HUMIDITY_LIMIT_MSK 0xFE00U
#define SHT3X_TEMPERATURE_LIMIT_MSK 0x01FFU

// Macro versions of the functions below used to validate numbers in static_assert later on.
#define TEMPERATURE_TO_TICK(temperature) ((uint16_t)(((int32_t)(temperature) * 12271 + 552195000) >> 15))

#define HUMIDITY_TO_TICK(humidity) ((uint16_t)((((int32_t)(humidity) * 21474) >> 15)))

#define ALERT_THRESHOLD_WORD(humidity, temperature)                                                                                                                                \
  ((HUMIDITY_TO_TICK(humidity) & SHT3X_HUMIDITY_LIMIT_MSK) | ((TEMPERATURE_TO_TICK(temperature) >> 7) & SHT3X_TEMPERATURE_LIMIT_MSK))

/*
 * Copyright (c) 2018, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
// See Also: https://github.com/Sensirion/embedded-sht/blob/94967774f25985bf65356bad3689fec4e8fef2b8/sht3x/sht3x.c#L256
static inline void tick_to_temperature(uint16_t tick, int32_t *temperature)
{
  *temperature = ((21875 * (int32_t)tick) >> 13) - 45000;
}

static inline void tick_to_humidity(uint16_t tick, int32_t *humidity)
{
  *humidity = ((12500 * (int32_t)tick) >> 13);
}

static inline void temperature_to_tick(int32_t temperature, uint16_t *tick)
{
  *tick = (uint16_t)((temperature * 12271 + 552195000) >> 15);
}

static inline void humidity_to_tick(int32_t humidity, uint16_t *tick)
{
  *tick = (uint16_t)((humidity * 21474) >> 15);
}

pin_t pin_addr;
pin_t pin_rst;
pin_t pin_alrt;

typedef struct
{
  double temperature_offset; // fixed for lifetime
  double humidity_offset;

  double temperature_sigma; // depends on repeatability
  double humidity_sigma;
} sensor_error_model_t;

typedef struct
{
  int32_t temperature; // 25.0C = 25000; -5.0C = -5000
  int32_t humidity;    // 60.0 = 60000
  uint16_t word;
} alert_threshold_t;

// See Also: https://github.com/Sensirion/embedded-sht/blob/94967774f25985bf65356bad3689fec4e8fef2b8/sht3x/sht3x.c#L170
static void alert_threshold_raw_to_word(alert_threshold_t *alert_threshold)
{
  uint16_t raw_humidity;
  uint16_t raw_temperature;

  temperature_to_tick(alert_threshold->temperature, &raw_temperature);
  humidity_to_tick(alert_threshold->humidity, &raw_humidity);

  alert_threshold->word = 0U;
  /* convert inputs to alert threshold word */
  alert_threshold->word = (raw_humidity & SHT3X_HUMIDITY_LIMIT_MSK);
  alert_threshold->word |= ((raw_temperature >> 7) & SHT3X_TEMPERATURE_LIMIT_MSK);
}

// See Also: https://github.com/Sensirion/embedded-sht/blob/94967774f25985bf65356bad3689fec4e8fef2b8/sht3x/sht3x.c#L212
static void alert_threshold_word_to_raw(alert_threshold_t *alert_threshold)
{
  uint16_t raw_humidity = (alert_threshold->word & SHT3X_HUMIDITY_LIMIT_MSK);
  uint16_t raw_temperature = ((alert_threshold->word & SHT3X_TEMPERATURE_LIMIT_MSK) << 7);

  tick_to_humidity(raw_humidity, &alert_threshold->humidity);
  tick_to_temperature(raw_temperature, &alert_threshold->temperature);
}

typedef uint16_t status_register_t;

enum
{
  WRITE_DATA_CHECKSUM_STATUS = 1u << 0,
  COMMAND_STATUS = 1u << 1,
  SYSTEM_RESET_DETECTED = 1u << 4,
  TEMP_ALERT_EN = 1u << 10,
  HUM_ALERT_EN = 1u << 11,
  HEATER_EN = 1u << 13,
  ALERT_PENDING = 1u << 15,
};

enum
{
  STATUS_REGISTER_DEFAULT = SYSTEM_RESET_DETECTED | ALERT_PENDING,
};

typedef struct
{
  uint16_t raw_temperature;
  uint16_t raw_humidity;
} hardware_state_t;

enum
{
  MPS_NONE = 0,
  MPS_ZERO_DOT_FIVE = 2000000,
  MPS_ONE = 1000000,
  MPS_TWO = 500000,
  MPS_FOUR = 250000,
  MPS_FOUR_ART = 250000,
  MPS_TEN = 100000
};

/*
  Standard values for the Alert Limits
  During power-up or after resets pre-defined limits are loaded
  into the register, see Table 1. The standard limits are
  “guarding” the grey area depicted in Figure 4.

  | Alert Limit      | Physical Value (RH/T) | Hex Value |
  |------------------|-----------------------|-----------|
  | high set limit   | 80% / 60°C            | 0xCD33    |
  | high clear limit | 79% / 58°C            | 0xC92D    |
  | low clear limit  | 22% / -9°C            | 0x3869    |
  | low set limit    | 20% / -10°C           | 0x3466    |
*/

enum
{
  ALERT_HIGH_SET_LIMIT_DEFAULT = 0xCD33,
  ALERT_HIGH_CLEAR_LIMIT_DEFAULT = 0xC92D,
  ALERT_LOW_CLEAR_LIMIT_DEFAULT = 0x3869,
  ALERT_LOW_SET_LIMIT_DEFAULT = 0x3466
};

// Some values adjusted to account for integer quantization.
// These are the physical values that encode to the factory defaults.
// Values are fixed point decimal (e.g. 80.000 = 80000)
static_assert(ALERT_THRESHOLD_WORD(80000, 60000) == ALERT_HIGH_SET_LIMIT_DEFAULT, "High set limit incorrect");

static_assert(ALERT_THRESHOLD_WORD(78500, 58000) == ALERT_HIGH_CLEAR_LIMIT_DEFAULT, "High clear limit incorrect");

static_assert(ALERT_THRESHOLD_WORD(22000, -9000) == ALERT_LOW_CLEAR_LIMIT_DEFAULT, "Low clear limit incorrect");

static_assert(ALERT_THRESHOLD_WORD(20500, -10000) == ALERT_LOW_SET_LIMIT_DEFAULT, "Low set limit incorrect");

typedef struct
{
  uint16_t high_set;
  uint16_t high_clear;
  uint16_t low_clear;
  uint16_t low_set;
} alert_limits_t;

static timer_t pda_timer;

typedef struct
{
  uint32_t period;
  bool running;
} periodic_data_acquisition_state_t;

typedef struct
{
  uint32_t temperature_attr;
  uint32_t humidity_attr;
  uint8_t command[2];
  uint8_t command_len;
  uint8_t response[6];
  uint8_t response_len;
  uint8_t response_pos;
  hardware_state_t hardware_state;
  status_register_t status_register;
  alert_limits_t alert_limits;
  periodic_data_acquisition_state_t pda_state;
  sensor_error_model_t sensor_error_model;
} chip_state_t;

static void run_measurement(chip_state_t *chip)
{
  float temperature = attr_read_float(chip->temperature_attr);
  float humidity = clamp_float(attr_read_float(chip->humidity_attr), 0.0f, 100.0f);

  // TODO: Add response lag
  // Temperature Sensor Response time   τ63% >2 seconds
  // Humidity Sensor Response time  τ63% 8 seconds
  // With activated ART function (see section 4.7) the response time can be improved by a factor of 2.
  // chip->internal_temperature += alpha * (environment_temperature - chip->internal_temperature);

  if (test_flag(chip->status_register, HEATER_EN))
  {
    // Add +3.0C if heater is enabled.
    temperature = temperature + 3.0f;
  }
  temperature = clamp_float(temperature, -40.0f, 125.0f);

  temperature_to_tick(temperature * 1000.0f, &chip->hardware_state.raw_temperature);
  humidity_to_tick(humidity * 1000.0f, &chip->hardware_state.raw_humidity);
}

static void periodic_data_acquisition_timer_callback(void *user_data)
{
  chip_state_t *chip = (chip_state_t *)(user_data);
  if (!chip->pda_state.running)
  {
    return;
  }

  run_measurement(chip);

  /*
    Activation and Deactivation of the Alert Mode

    Whenever the sensor operates in periodic data acquisiton
    mode the alert mode is active. It is possible to deactivate the
    limit for temperature and humidity individually, by setting the
    Minimum set point to values higher than the Maximum set
    point (LowSet>HighSet for deactivation of the alert mode).
  */
  // TODO: Set alert pins
}

static void periodic_data_acquisition_timer_start(chip_state_t *chip, uint32_t mps)
{
  chip->pda_state.period = mps;

  if (chip->pda_state.running)
  {
    timer_stop(pda_timer);
  }

  timer_start(pda_timer, mps, true);

  chip->pda_state.running = true;
}

static void periodic_data_acquisition_timer_stop(chip_state_t *chip)
{
  chip->pda_state.period = MPS_NONE;

  if (chip->pda_state.running)
  {
    timer_stop(pda_timer);
  }

  chip->pda_state.running = false;
}

static void chip_reset(chip_state_t *chip)
{
  memset(&chip->command, 0, 2);
  chip->command_len = 0;

  memset(&chip->response, 0, 6);
  chip->response_len = 0;
  chip->response_pos = 0;

  chip->hardware_state.raw_humidity = 0;
  chip->hardware_state.raw_temperature = 0;

  chip->status_register = STATUS_REGISTER_DEFAULT;

  periodic_data_acquisition_timer_stop(chip);
}

static void prepare_measurement(chip_state_t *chip)
{
  chip->response[0] = (uint8_t)(chip->hardware_state.raw_temperature >> 8);
  chip->response[1] = (uint8_t)(chip->hardware_state.raw_temperature & 0xff);
  chip->response[2] = sht30_crc8(chip->response[0], chip->response[1]);
  chip->response[3] = (uint8_t)(chip->hardware_state.raw_humidity >> 8);
  chip->response[4] = (uint8_t)(chip->hardware_state.raw_humidity & 0xff);
  chip->response[5] = sht30_crc8(chip->response[3], chip->response[4]);
  chip->response_len = 6;
  chip->response_pos = 0;
}

static void prepare_status(chip_state_t *chip)
{
  chip->response[0] = (uint8_t)(chip->status_register >> 8);
  chip->response[1] = (uint8_t)(chip->status_register & 0xff);
  chip->response[2] = sht30_crc8(chip->response[0], chip->response[1]);
  chip->response_len = 3;
  chip->response_pos = 0;
}

static void empty_response(chip_state_t *chip)
{
  chip->response_len = 0;
  chip->response_pos = 0;
}

static bool process_command(chip_state_t *chip)
{
  uint16_t command = ((uint16_t)chip->command[0] << 8) | chip->command[1];

  switch (command)
  {
    case 0x2c06: // Measurement High Repeatability with Clock Stretch Enabled
    case 0x2400: // Measurement High Repeatability with Clock Stretch Disabled
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      run_measurement(chip);
      prepare_measurement(chip);
      break;
    case 0x2c0d: // Measurement Medium Repeatability with Clock Stretch Enabled
    case 0x240b: // Measurement Medium Repeatability with Clock Stretch Disabled
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      run_measurement(chip);
      prepare_measurement(chip);
      break;
    case 0x2c10: // Measurement Low Repeatability with Clock Stretch Enabled
    case 0x2416: // Measurement Low Repeatability with Clock Stretch Disabled
      chip->sensor_error_model.temperature_sigma = 0.16;
      chip->sensor_error_model.humidity_sigma = 0.2;
      run_measurement(chip);
      prepare_measurement(chip);
      break;

    case 0xe000: // Readout of Measurement Results for Periodic Mode
      prepare_measurement(chip);
      break;

    // 0.5 mps
    case 0x2032: // High Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_ZERO_DOT_FIVE);
      empty_response(chip);
      break;
    case 0x2024: // Medium Repeatability
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      periodic_data_acquisition_timer_start(chip, MPS_ZERO_DOT_FIVE);
      empty_response(chip);
      break;
    case 0x202F: // Low Repeatability
      chip->sensor_error_model.temperature_sigma = 0.16;
      chip->sensor_error_model.humidity_sigma = 0.2;
      periodic_data_acquisition_timer_start(chip, MPS_ZERO_DOT_FIVE);
      empty_response(chip);
      break;

    // 1 mps
    case 0x2130: // High Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_ONE);
      empty_response(chip);
      break;
    case 0x2126: // Medium Repeatability
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      periodic_data_acquisition_timer_start(chip, MPS_ONE);
      empty_response(chip);
      break;
    case 0x212D: // Low Repeatability
      chip->sensor_error_model.temperature_sigma = 0.16;
      chip->sensor_error_model.humidity_sigma = 0.2;
      periodic_data_acquisition_timer_start(chip, MPS_ONE);
      empty_response(chip);
      break;

    // 2 mps
    case 0x2236: // High Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_TWO);
      empty_response(chip);
      break;
    case 0x2220: // Medium Repeatability
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      periodic_data_acquisition_timer_start(chip, MPS_TWO);
      empty_response(chip);
      break;
    case 0x222B: // Low Repeatability
      chip->sensor_error_model.temperature_sigma = 0.16;
      chip->sensor_error_model.humidity_sigma = 0.2;
      periodic_data_acquisition_timer_start(chip, MPS_TWO);
      empty_response(chip);
      break;

    // 4 mps
    case 0x2334: // High Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_FOUR);
      empty_response(chip);
      break;
    case 0x2322: // Medium Repeatability
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      periodic_data_acquisition_timer_start(chip, MPS_FOUR);
      empty_response(chip);
      break;
    case 0x2329: // Low Repeatability
      chip->sensor_error_model.temperature_sigma = 0.16;
      chip->sensor_error_model.humidity_sigma = 0.2;
      periodic_data_acquisition_timer_start(chip, MPS_FOUR);
      empty_response(chip);
      break;

    // 10 mps
    case 0x2737: // High Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_TEN);
      empty_response(chip);
      break;
    case 0x2721: // Medium Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_TEN);
      empty_response(chip);
      break;
    case 0x272A: // Low Repeatability
      chip->sensor_error_model.temperature_sigma = 0.04;
      chip->sensor_error_model.humidity_sigma = 0.07;
      periodic_data_acquisition_timer_start(chip, MPS_TEN);
      empty_response(chip);
      break;

    case 0x2B32: // Periodic Measurement with ART
      // The unique ART (accelerated response time) feature
      // can be activated by issuing the command in Table 11.
      // After issuing the ART command the sensor will start
      // acquiring data with a frequency of 4Hz. The ART command is structurally similar to any other
      // command in Table 9. Hence section 4.5 applies for
      // starting a measurement, section 4.6 for reading out data
      // and section 4.8 for stopping the periodic data acquisition.
      // 4 mps
      chip->sensor_error_model.temperature_sigma = 0.07;
      chip->sensor_error_model.humidity_sigma = 0.15;
      periodic_data_acquisition_timer_start(chip, MPS_FOUR_ART);
      empty_response(chip);
      break;

    case 0x3093: // Break command / Stop Periodic Data Acquisition Mode
      periodic_data_acquisition_timer_stop(chip);
      empty_response(chip);
      break;

    case 0xf32d: // Get Status Register
      prepare_status(chip);
      break;
    case 0x3041: // Clear Status Register
      // All flags (Bit 15, 11, 10, 4) in the status register can be cleared (set to zero)
      clear_flag(&chip->status_register, SYSTEM_RESET_DETECTED);
      clear_flag(&chip->status_register, TEMP_ALERT_EN);
      clear_flag(&chip->status_register, HUM_ALERT_EN);
      clear_flag(&chip->status_register, ALERT_PENDING);
      empty_response(chip);
      break;

    case 0x30a2: // Soft Reset
      chip_reset(chip);
      empty_response(chip);
      break;

    case 0x306d: // Enable Heater
      set_flag(&chip->status_register, HEATER_EN);
      empty_response(chip);
      break;
    case 0x3066: // Disable Heater
      clear_flag(&chip->status_register, HEATER_EN);
      empty_response(chip);
      break;

    default: // Invalid Command
      set_flag(&chip->status_register, COMMAND_STATUS);
      empty_response(chip);
      break;
  }

  return true;
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read)
{
  (void)address;
  chip_state_t *chip = (chip_state_t *)user_data;

  if (read)
  {
    chip->response_pos = 0;
  }
  else
  {
    chip->command_len = 0;
  }

  return true;
}

static uint8_t on_i2c_read(void *user_data)
{
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->response_pos < chip->response_len)
  {
    return chip->response[chip->response_pos++];
  }

  return 0xff;
}

static bool on_i2c_write(void *user_data, uint8_t data)
{
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->command_len < 2)
  {
    chip->command[chip->command_len++] = data;
  }

  if (chip->command_len == 2)
  {
    return process_command(chip);
  }

  return true;
}

static void on_i2c_disconnect(void *user_data)
{
  (void)user_data;
}

void chip_init(void)
{
  chip_state_t *chip = (chip_state_t *)malloc(sizeof(chip_state_t));
  chip->temperature_attr = attr_init_float("temperature", 25.0f);
  chip->humidity_attr = attr_init_float("humidity", 60.0f);

  // pcg32_seed(42U, 54U);

  chip->sensor_error_model.temperature_offset = gaussian(0.0, 0.15);
  chip->sensor_error_model.humidity_offset = gaussian(0.0, 2.0);

  // Pin names follow the Stemma QT version of the breakout board
  pin_init("VCC", INPUT);
  pin_init("GND", INPUT);

  // This pin has a 10K pull down resistor to make the default I2C address 0x44.
  pin_addr = pin_init("ADDR", INPUT_PULLDOWN);
  // This pin has a 10K pullup on it to make the chip active by default. Connect to ground to do a hardware reset
  pin_rst = pin_init("RST", INPUT_PULLUP);
  // Active push-pull output.
  // Can cause significant heating if driving strong currents directly.
  // We can't simulate that here because we don't know what it's attached to.
  pin_alrt = pin_init("ALRT", OUTPUT);

  timer_config_t pda_timer_config = {.user_data = chip, .callback = periodic_data_acquisition_timer_callback};
  pda_timer = timer_init(&pda_timer_config);

  i2c_config_t i2c_config = {.user_data = chip,
                             // You can tie this pin to Vin to make the address 0x45.
                             .address = pin_read(pin_addr) ? 0x45 : 0x44,
                             // This pin has a 10K pullup resistor attached to Vin
                             .scl = pin_init("SCL", INPUT_PULLUP),
                             // This pin has a 10K pullup resistor attached to Vin
                             .sda = pin_init("SDA", INPUT_PULLUP),
                             .connect = on_i2c_connect,
                             .read = on_i2c_read,
                             .write = on_i2c_write,
                             .disconnect = on_i2c_disconnect};

  i2c_init(&i2c_config);

  chip_reset(chip);
}
