### Pin Assignment

| Pin | Name   | Comments                                                                     |
| --- | ------ | ---------------------------------------------------------------------------- |
| 1   | SDA    | Serial data; input / output                                                  |
| 2   | ADDR   | Address pin; input; connect to either VDD or VSS, do not leave floating      |
| 3   | ALERT  | Indicates alarm condition; output; must be left floating if unused           |
| 4   | SCL    | Serial clock; input / output                                                 |
| 5   | VDD    | Supply voltage; input                                                        |
| 6   | nRESET | Reset pin active low; Input; if not used it is recommended to connect to VDD |
| 7   | R      | No electrical function; recommended to connect to VSS                        |
| 8   | VSS    | Ground                                                                       |

### Status Register

| Bit | Field Description                                                                                                                                            | Default Value |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------- |
| 0   | Write data checksum status '0': checksum of last write transfer was correct, '1': checksum of last write transfer failed                                     | '0'           |
| 1   | Command status '0': last command executed successfully, '1': last command not processed. It was either invalid, failed the integrated command checksum       | '0'           |
| 2:3 | Reserved                                                                                                                                                     | '00'          |
| 4   | System reset detected '0': no reset detected since last 'clear status register' command, '1': reset detected (hard reset, soft reset command or supply fail) | '1'           |
| 5:9 | Reserved                                                                                                                                                     | '00000'       |
| 10  | T tracking alert '0' : no alert, '1' : alert                                                                                                                 | '0'           |
| 11  | RH tracking alert '0' : no alert, '1' : alert                                                                                                                | '0'           |
| 12  | Reserved                                                                                                                                                     | '0'           |
| 13  | Heater status '0' : Heater OFF, '1' : Heater ON                                                                                                              | '0'           |
| 14  | Reserved                                                                                                                                                     | '0'           |
| 15  | Alert pending status '0': no pending alerts, '1': at least one pending alert                                                                                 | '1'           |

### Measurement Commands for Periodic Data Acquisition Mode

In this mode one issued measurement command yields a stream of data pairs. Each data pair consists of one 16 bit temperature and one 16 bit humidity value (in this order). In periodic mode different measurement commands can be selected. The corresponding 16 bit commands are shown in Table 9. They differ with respect to repeatability (low, medium and high) and data acquisition frequency (0.5, 1, 2, 4 & 10 measurements per second, mps). Clock stretching cannot be selected in this mode. The data acquisition frequency and the repeatability setting influences the measurement duration and the current consumption of the sensor. This is explained in section 2.2 of this datasheet.

| Repeatability | mps | MSB  | LSB  |
| ------------- | --- | ---- | ---- |
| High          | 0.5 | 0x20 | 0x32 |
| Medium        | 0.5 | 0x20 | 0x24 |
| Low           | 0.5 | 0x20 | 0x2F |
| High          | 1   | 0x21 | 0x30 |
| Medium        | 1   | 0x21 | 0x26 |
| Low           | 1   | 0x21 | 0x2D |
| High          | 2   | 0x22 | 0x36 |
| Medium        | 2   | 0x22 | 0x20 |
| Low           | 2   | 0x22 | 0x2B |
| High          | 4   | 0x23 | 0x34 |
| Medium        | 4   | 0x23 | 0x22 |
| Low           | 4   | 0x23 | 0x29 |
| High          | 10  | 0x27 | 0x37 |
| Medium        | 10  | 0x27 | 0x21 |
| Low           | 10  | 0x27 | 0x2A |

### What does τ63% mean?

> What is this math symbol? It's from the Microsoft Windows Symbol font. The character code is 0x74. It's used like this response time T63% >2s

In engineering, especially in control systems and sensor datasheets, **τ** denotes the **time constant** of a first-order system.

The notation:

**Response time τ63% > 2 s**

(or more commonly **τ63% ≈ 2 s** or **τ63% < 2 s**)

means:

* After a step change in temperature, the sensor reaches **63.2%** of the final value after one time constant **τ**.
* In your SHT3x datasheet, **τ63% > 2 s** means the characteristic response time is greater than 2 seconds under the specified test conditions.

The 63.2% comes from the exponential response of a first-order system:

$$
y(t) = 1 - e^{-t/\tau}
$$

At (t=τ):

$$
1 - e^{-1} \approx 0.632
$$

or **63.2%** of the final value.

For example, if the temperature suddenly changes from **20 °C** to **30 °C**, then after one time constant (2 s):

* Final change = 10 °C
* 63.2% of 10 °C = 6.32 °C
* Sensor reads about **26.3 °C**

After additional time constants:

| Time | Percent of final value |
| ----:| ----------------------:|
| 1τ   | 63.2%                  |
| 2τ   | 86.5%                  |
| 3τ   | 95.0%                  |
| 4τ   | 98.2%                  |
| 5τ   | 99.3%                  |

So the symbol itself is simply **τ (tau)**, and in this context it represents the sensor's **time constant**, with the subscript "63%" reminding you that one time constant corresponds to reaching approximately 63% of the final response.

> I figured as much. It's very reminiscent of an RC time constant for capacitor charging.

Exactly. In fact, it's the **same mathematics**.

An RC charging circuit is the canonical first-order system:

$$
V(t) = V_\infty \left(1 - e^{-t/\tau}\right)
$$

where

$$
\tau = RC
$$

A temperature sensor behaves almost identically. The sensor has thermal mass (analogous to capacitance) and thermal resistance to its surroundings (analogous to electrical resistance). The governing equation is:

$$
T(t) = T_\infty + \left(T_0 - T_\infty\right)e^{-t/\tau}
$$

The only difference is what the variables represent:

| RC Circuit         | Temperature Sensor                  |
| ------------------ | ----------------------------------- |
| Voltage            | Temperature                         |
| Current            | Heat flow                           |
| Resistance (R)     | Thermal resistance                  |
| Capacitance (C)    | Thermal capacitance (heat capacity) |
| Time constant (RC) | Thermal time constant               |

This analogy is so strong that electrical engineers routinely model thermal systems as equivalent RC networks.

For an SHT3x sensor, the datasheet's τ is essentially saying:

"Treat this sensor as a first-order low-pass filter with a time constant of about 2 seconds."

So if you suddenly move it from a 20 °C room into a 30 °C room, it doesn't jump instantly. It follows the familiar exponential curve:

- 2 s → 63%
- 4 s → 86%
- 6 s → 95%
- 8 s → 98%
- 10 s → 99%

The exact percentages are identical to an RC capacitor charging because they're both solutions to the same first-order differential equation.

One interesting consequence is that if you ever need to simulate an SHT3x in Wokwi, you don't need a complicated thermal model. You can update the simulated temperature each time step using the same exponential filter you'd use for an RC circuit:

```c
sensorTemp += (ambientTemp - sensorTemp) * (1 - exp(-dt / tau));
```

Or, for a fixed update interval, precompute the coefficient:

```c
sensorTemp += alpha * (ambientTemp - sensorTemp);
```

where

```text
alpha = 1 - e^(-dt / tau)
```

That's exactly how many embedded systems simulate sensors and implement digital low-pass filters.
