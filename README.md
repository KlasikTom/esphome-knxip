# esphome-knxip

# ESPHome KNX/IP Component

Nativní KNX/IP komponenta pro ESP32 — žádné externí knihovny, čistý UDP multicast dle KNXnet/IP standardu (ISO 22510).

## Funkce

* KNXnet/IP Routing (multicast 224.0.23.12:3671)
* DPT 1.x — boolean (spínač, tlačítko)
* DPT 5.x — 8-bit unsigned (stmívač 0–255, procenta)
* DPT 9.x — 2-byte KNX float (teplota, vlhkost, CO₂...)
* DPT 14.x — IEEE754 4-byte float (výkon, energie...)
* Příjem i vysílání telegramů
* Přímé napojení na GPIO, I²C/SPI senzory, PWM/SSR výstupy, display

\---

## Instalace

```yaml
external\_components:
  - source:
      type: git
      url: https://github.com/YOUR\_USER/esphome-knxip
    refresh: 0s
    components: \[ knxip ]
```

\---

## Základní konfigurace

```yaml
knxip:
  individual\_address: "1.1.50"      # KNX individuální adresa zařízení
  # multicast\_group: "224.0.23.12"  # výchozí KNX IP multicast
  # port: 3671                       # výchozí port
```

\---

## Senzory (příjem hodnot ze sběrnice)

### Teplota / vlhkost — DPT 9.x

```yaml
sensor:
  - platform: knxip
    name: "Teplota obývák"
    group\_address: "1/2/10"
    dpt: "9"
    unit\_of\_measurement: "°C"
    accuracy\_decimals: 1

  - platform: knxip
    name: "Vlhkost obývák"
    group\_address: "1/2/11"
    dpt: "9"
    unit\_of\_measurement: "%"
    accuracy\_decimals: 0

  - platform: knxip
    name: "CO₂"
    group\_address: "1/2/20"
    dpt: "9"
    unit\_of\_measurement: "ppm"
```

### Výkon / energie — DPT 14.x (IEEE754)

```yaml
  - platform: knxip
    name: "Výkon FV"
    group\_address: "1/5/1"
    dpt: "14"
    unit\_of\_measurement: "W"
    accuracy\_decimals: 0
```

### Procenta / poloha — DPT 5.x

```yaml
  - platform: knxip
    name: "Pozice žaluzie"
    group\_address: "1/4/5"
    dpt: "5"
    unit\_of\_measurement: "%"
```

\---

## Binární senzory (příjem boolean ze sběrnice)

```yaml
binary\_sensor:
  - platform: knxip
    name: "Pohybové čidlo"
    group\_address: "1/3/5"

  - platform: knxip
    name: "Okno balkón"
    group\_address: "1/3/10"
    device\_class: window
```

\---

## Přepínače / relé — propojení s GPIO

```yaml
switch:
  - platform: knxip
    name: "Světlo obývák"
    group\_address\_command: "1/1/1"   # GA pro zápis příkazu
    group\_address\_state:   "1/1/2"   # GA pro čtení stavu (feedback)
    pin:
      number: GPIO26                 # volitelný GPIO výstup (relé)
      inverted: false

  - platform: knxip
    name: "Světlo chodba"
    group\_address\_command: "1/1/3"
    pin:
      number: GPIO27
```

**Bez GPIO** — čistě softwarový switch (přes HA nebo jiný KNX zdroj):

```yaml
  - platform: knxip
    name: "Virtuální spínač"
    group\_address\_command: "1/1/10"
    group\_address\_state:   "1/1/11"
```

\---

## Výstup / stmívač — DPT 5 → PWM

```yaml
output:
  - platform: knxip
    id: dimmer\_out
    group\_address: "1/1/20"    # přijímá DPT5 hodnoty 0–255

light:
  - platform: monochromatic
    name: "Stmívač obývák"
    output: dimmer\_out
```

\---

## Napojení fyzických senzorů na KNX sběrnici

ESP32 může číst lokální senzory a posílat hodnoty na KNX:

### I²C senzor (BME280, SHT31, BMP390...)

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: bme280\_i2c
    address: 0x76
    temperature:
      name: "BME280 teplota"
      on\_value:
        - lambda: |-
            // Pošli hodnotu na KNX GA 1/2/16
            id(knxip\_comp).send\_dpt9(0x0910, x);
    humidity:
      name: "BME280 vlhkost"
      on\_value:
        - lambda: |-
            id(knxip\_comp).send\_dpt9(0x0C08, x);

knxip:
  id: knxip\_comp
  individual\_address: "1.1.50"
```

### SPI senzor (MAX31855 termočlánek, ADS1118...)

```yaml
spi:
  clk\_pin: GPIO18
  mosi\_pin: GPIO23
  miso\_pin: GPIO19

sensor:
  - platform: max31855
    name: "Termočlánek"
    cs\_pin: GPIO5
    on\_value:
      - lambda: |-
          id(knxip\_comp).send\_dpt9(0x0912, x);  # GA 1/2/18
```

### 1-Wire / DS18B20

```yaml
one\_wire:
  - platform: gpio
    pin: GPIO4

sensor:
  - platform: dallas\_temp
    name: "DS18B20 teplota"
    on\_value:
      - lambda: |-
          id(knxip\_comp).send\_dpt9(0x0910, x);
```

\---

## PWM výstup — řízení výkonu (bojler, topení) přes SSR

```yaml
output:
  - platform: ledc
    pin: GPIO25
    id: boiler\_pwm
    frequency: 50Hz       # pro SSR s zero-cross detekcí
    min\_power: 0.0
    max\_power: 1.0

sensor:
  # Přijímá setpoint z KNX (0.0–100.0 %)
  - platform: knxip
    name: "Bojler setpoint"
    group\_address: "1/7/1"
    dpt: "9"
    on\_value:
      - lambda: |-
          id(boiler\_pwm).set\_level(clamp(x / 100.0f, 0.0f, 1.0f));
```

Pro vyšší rozlišení PWM (12-bit = 4096 kroků) přidej do YAML:

```yaml
esphome:
  platformio\_options:
    build\_flags:
      - "-DLEDC\_TIMER\_BIT\_NUM=12"
```

\---

## Display + dotyková obrazovka (Olimex MOD-LCD2.8RTP)

MOD-LCD2.8RTP na UEXT konektoru ESP32-EVB: ILI9341 + XPT2046 resistive touch, SPI rozhraní.

```yaml
spi:
  clk\_pin: GPIO14     # UEXT SCK
  mosi\_pin: GPIO13    # UEXT MOSI
  miso\_pin: GPIO12    # UEXT MISO

font:
  - file: "gfonts://Roboto"
    id: font\_s
    size: 16
  - file: "gfonts://Roboto"
    id: font\_l
    size: 28

color:
  - id: col\_white
    red: 100%
    green: 100%
    blue: 100%
  - id: col\_green
    green: 100%
  - id: col\_red
    red: 100%
  - id: col\_blue
    blue: 100%

display:
  - platform: ili9xxx
    model: ILI9341
    id: main\_display
    cs\_pin: GPIO15      # UEXT CS
    dc\_pin: GPIO2       # ověř pinout MOD-LCD
    reset\_pin: GPIO4
    rotation: 90
    lambda: |-
      // Hlavička
      it.fill(Color(0, 0, 50));
      it.print(10, 8, id(font\_s), id(col\_white), "KNX/IP Monitor");

      // Teplota
      if (id(temp\_sensor).has\_state()) {
        it.printf(10, 50, id(font\_l), id(col\_green),
                  "%.1f°C", id(temp\_sensor).state);
      }

      // KNX status
      it.printf(10, 100, id(font\_s),
                id(knxip\_comp).is\_started() ? id(col\_green) : id(col\_red),
                "KNX: %s", id(knxip\_comp).is\_started() ? "OK" : "Čekám...");

      // Tlačítka (virtuální)
      it.filled\_rectangle(10,  140, 140, 50, Color(0, 100, 0));
      it.print(50, 155, id(font\_s), id(col\_white), "Světlo ZAP");
      it.filled\_rectangle(170, 140, 140, 50, Color(100, 0, 0));
      it.print(205, 155, id(font\_s), id(col\_white), "Světlo VYP");

touchscreen:
  - platform: xpt2046
    id: touch
    cs\_pin: GPIO33      # ověř pinout MOD-LCD
    calibration:
      x\_min: 300
      x\_max: 3800
      y\_min: 300
      y\_max: 3800
    on\_touch:
      - lambda: |-
          auto tp = touch.touches\[0];
          ESP\_LOGI("touch", "x=%d y=%d", tp.x, tp.y);
          // Tlačítko ZAP (x: 10-150, y: 140-190)
          if (tp.x > 10 \&\& tp.x < 150 \&\& tp.y > 140 \&\& tp.y < 190)
              id(knxip\_comp).send\_bool(0x0801, true);   // GA 1/4/1 ZAP
          // Tlačítko VYP (x: 170-310, y: 140-190)
          if (tp.x > 170 \&\& tp.x < 310 \&\& tp.y > 140 \&\& tp.y < 190)
              id(knxip\_comp).send\_bool(0x0801, false);  // GA 1/4/1 VYP
```

\---

## Odesílání hodnot z ESP32 na KNX z kódu

```cpp
// V lambda nebo on\_value:
id(knxip\_comp).send\_bool(0x0801, true);        // GA 1/4/1, bool
id(knxip\_comp).send\_dpt9(0x0910, 21.5f);       // GA 1/2/16, teplota
id(knxip\_comp).send\_dpt5(0x0915, 128.0f);      // GA 1/2/21, 0-255
id(knxip\_comp).send\_dpt14(0x0A01, 3500.0f);    // GA 1/5/1, výkon W
```

GA adresa se zadává jako hex: `0xAABB` kde `AA = (area<<3 | line)`, `BB = member`.
Příklad: `1/2/10` → `(1<<11)|(2<<8)|10` = `0x090A` = `0x090A`.

Helper pro výpočet: `ga = (area << 11) | (line << 8) | member`

\---

## Testování přes Home Assistant

```yaml
# V HA Developer Tools → Services → knx.send
service: knx.send
data:
  address: "1/2/10"
  payload: 21.5
  type: "temperature"
```

## Testování přes Python (xknx)

```bash
pip install xknx
```

```python
import asyncio
from xknx import XKNX
from xknx.telegram import Telegram
from xknx.telegram.address import GroupAddress
from xknx.telegram.apci import GroupValueWrite
from xknx.dpt import DPTTemperature

async def main():
    xknx = XKNX()
    await xknx.start()
    await asyncio.sleep(1)
    t = Telegram(
        destination\_address=GroupAddress("1/2/10"),
        payload=GroupValueWrite(DPTTemperature.to\_knx(21.5))
    )
    await xknx.telegrams.put(t)
    await asyncio.sleep(1)
    await xknx.stop()

asyncio.run(main())
```

## Wireshark filtr

```
udp.port == 3671
```

\---

## Struktura souborů

```
components/knxip/
├── \_\_init\_\_.py          # hlavní komponenta, konfigurace
├── knxip.h              # KNXnet/IP stack: parser, builder, DPT encode/decode
├── knxip\_component.h    # UDP multicast core, listener registry
├── knxip\_sensor.h       # ESPHome sensor (DPT9/14/5)
├── knxip\_binary\_sensor.h# ESPHome binary sensor (DPT1)
├── knxip\_switch.h       # ESPHome switch + GPIO
├── knxip\_output.h       # ESPHome float output (DPT5, dimmer)
├── sensor.py
├── binary\_sensor.py
├── switch.py
└── output.py
```

\---

## Podporované DPT typy

|DPT|Popis|Příklad použití|
|-|-|-|
|1.x|Boolean|Spínač, pohyb, okno|
|5.x|8-bit uint (0–255)|Jas, poloha žaluzie|
|9.x|2-byte KNX float|Teplota, vlhkost, CO₂|
|14.x|IEEE754 4-byte float|Výkon W, energie kWh|



