# ESPHome KNX/IP Component

Nativní KNX/IP komponenta pro ESP32 — žádné externí knihovny, čistý UDP multicast dle KNXnet/IP standardu (ISO 22510).

## Funkce

- KNXnet/IP Routing (multicast 224.0.23.12:3671)
- DPT 1.x — boolean (spínač, tlačítko)
- DPT 5.x — 8-bit unsigned (stmívač 0–255, procenta)
- DPT 9.x — 2-byte KNX float (teplota, vlhkost, CO₂...)
- DPT 14.x — IEEE754 4-byte float (výkon, energie...)
- Příjem i vysílání telegramů
- Přímé napojení na GPIO, I²C/SPI senzory, PWM/SSR výstupy, display

---

## Instalace

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USER/esphome-knxip
    refresh: 0s
    components: [ knxip ]
```

---

## Základní konfigurace

```yaml
knxip:
  individual_address: "1.1.50"      # KNX individuální adresa zařízení
  # multicast_group: "224.0.23.12"  # výchozí KNX IP multicast
  # port: 3671                       # výchozí port
```

---

## Senzory (příjem hodnot ze sběrnice)

### Teplota / vlhkost — DPT 9.x

```yaml
sensor:
  - platform: knxip
    name: "Teplota obývák"
    group_address: "1/2/10"
    dpt: "9"
    unit_of_measurement: "°C"
    accuracy_decimals: 1

  - platform: knxip
    name: "Vlhkost obývák"
    group_address: "1/2/11"
    dpt: "9"
    unit_of_measurement: "%"
    accuracy_decimals: 0

  - platform: knxip
    name: "CO₂"
    group_address: "1/2/20"
    dpt: "9"
    unit_of_measurement: "ppm"
```

### Výkon / energie — DPT 14.x (IEEE754)

```yaml
  - platform: knxip
    name: "Výkon FV"
    group_address: "1/5/1"
    dpt: "14"
    unit_of_measurement: "W"
    accuracy_decimals: 0
```

### Procenta / poloha — DPT 5.x

```yaml
  - platform: knxip
    name: "Pozice žaluzie"
    group_address: "1/4/5"
    dpt: "5"
    unit_of_measurement: "%"
```

---

## Binární senzory (příjem boolean ze sběrnice)

```yaml
binary_sensor:
  - platform: knxip
    name: "Pohybové čidlo"
    group_address: "1/3/5"

  - platform: knxip
    name: "Okno balkón"
    group_address: "1/3/10"
    device_class: window
```

---

## Přepínače / relé — propojení s GPIO

```yaml
switch:
  - platform: knxip
    name: "Světlo obývák"
    group_address_command: "1/1/1"   # GA pro zápis příkazu
    group_address_state:   "1/1/2"   # GA pro čtení stavu (feedback)
    pin:
      number: GPIO26                 # volitelný GPIO výstup (relé)
      inverted: false

  - platform: knxip
    name: "Světlo chodba"
    group_address_command: "1/1/3"
    pin:
      number: GPIO27
```

**Bez GPIO** — čistě softwarový switch (přes HA nebo jiný KNX zdroj):

```yaml
  - platform: knxip
    name: "Virtuální spínač"
    group_address_command: "1/1/10"
    group_address_state:   "1/1/11"
```

---

## Výstup / stmívač — DPT 5 → PWM

```yaml
output:
  - platform: knxip
    id: dimmer_out
    group_address: "1/1/20"    # přijímá DPT5 hodnoty 0–255

light:
  - platform: monochromatic
    name: "Stmívač obývák"
    output: dimmer_out
```

---

## Napojení fyzických senzorů na KNX sběrnici

ESP32 může číst lokální senzory a posílat hodnoty na KNX:

### I²C senzor (BME280, SHT31, BMP390...)

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: bme280_i2c
    address: 0x76
    temperature:
      name: "BME280 teplota"
      on_value:
        - lambda: |-
            // Pošli hodnotu na KNX GA 1/2/16
            id(knxip_comp).send_dpt9("1/2/16", x);
    humidity:
      name: "BME280 vlhkost"
      on_value:
        - lambda: |-
            id(knxip_comp).send_dpt9("1/6/8", x);

knxip:
  id: knxip_comp
  individual_address: "1.1.50"
```

### SPI senzor (MAX31855 termočlánek, ADS1118...)

```yaml
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

sensor:
  - platform: max31855
    name: "Termočlánek"
    cs_pin: GPIO5
    on_value:
      - lambda: |-
          id(knxip_comp).send_dpt9("1/2/18", x);  # GA 1/2/18
```

### 1-Wire / DS18B20

```yaml
one_wire:
  - platform: gpio
    pin: GPIO4

sensor:
  - platform: dallas_temp
    name: "DS18B20 teplota"
    on_value:
      - lambda: |-
          id(knxip_comp).send_dpt9("1/2/16", x);
```

---

## PWM výstup — řízení výkonu (bojler, topení) přes SSR

```yaml
output:
  - platform: ledc
    pin: GPIO25
    id: boiler_pwm
    frequency: 50Hz       # pro SSR s zero-cross detekcí
    min_power: 0.0
    max_power: 1.0

sensor:
  # Přijímá setpoint z KNX (0.0–100.0 %)
  - platform: knxip
    name: "Bojler setpoint"
    group_address: "1/7/1"
    dpt: "9"
    on_value:
      - lambda: |-
          id(boiler_pwm).set_level(clamp(x / 100.0f, 0.0f, 1.0f));
```

Pro vyšší rozlišení PWM (12-bit = 4096 kroků) přidej do YAML:

```yaml
esphome:
  platformio_options:
    build_flags:
      - "-DLEDC_TIMER_BIT_NUM=12"
```

---

## Display + dotyková obrazovka (Olimex MOD-LCD2.8RTP)

MOD-LCD2.8RTP na UEXT konektoru ESP32-EVB: ILI9341 + XPT2046 resistive touch, SPI rozhraní.

```yaml
spi:
  clk_pin: GPIO14     # UEXT SCK
  mosi_pin: GPIO13    # UEXT MOSI
  miso_pin: GPIO12    # UEXT MISO

font:
  - file: "gfonts://Roboto"
    id: font_s
    size: 16
  - file: "gfonts://Roboto"
    id: font_l
    size: 28

color:
  - id: col_white
    red: 100%
    green: 100%
    blue: 100%
  - id: col_green
    green: 100%
  - id: col_red
    red: 100%
  - id: col_blue
    blue: 100%

display:
  - platform: ili9xxx
    model: ILI9341
    id: main_display
    cs_pin: GPIO15      # UEXT CS
    dc_pin: GPIO2       # ověř pinout MOD-LCD
    reset_pin: GPIO4
    rotation: 90
    lambda: |-
      // Hlavička
      it.fill(Color(0, 0, 50));
      it.print(10, 8, id(font_s), id(col_white), "KNX/IP Monitor");

      // Teplota
      if (id(temp_sensor).has_state()) {
        it.printf(10, 50, id(font_l), id(col_green),
                  "%.1f°C", id(temp_sensor).state);
      }

      // KNX status
      it.printf(10, 100, id(font_s),
                id(knxip_comp).is_started() ? id(col_green) : id(col_red),
                "KNX: %s", id(knxip_comp).is_started() ? "OK" : "Čekám...");

      // Tlačítka (virtuální)
      it.filled_rectangle(10,  140, 140, 50, Color(0, 100, 0));
      it.print(50, 155, id(font_s), id(col_white), "Světlo ZAP");
      it.filled_rectangle(170, 140, 140, 50, Color(100, 0, 0));
      it.print(205, 155, id(font_s), id(col_white), "Světlo VYP");

touchscreen:
  - platform: xpt2046
    id: touch
    cs_pin: GPIO33      # ověř pinout MOD-LCD
    calibration:
      x_min: 300
      x_max: 3800
      y_min: 300
      y_max: 3800
    on_touch:
      - lambda: |-
          auto tp = touch.touches[0];
          ESP_LOGI("touch", "x=%d y=%d", tp.x, tp.y);
          // Tlačítko ZAP (x: 10-150, y: 140-190)
          if (tp.x > 10 && tp.x < 150 && tp.y > 140 && tp.y < 190)
              id(knxip_comp).send_bool("1/4/1", true);   // GA 1/4/1 ZAP
          // Tlačítko VYP (x: 170-310, y: 140-190)
          if (tp.x > 170 && tp.x < 310 && tp.y > 140 && tp.y < 190)
              id(knxip_comp).send_bool("1/4/1", false);  // GA 1/4/1 VYP
```

---

## Odesílání hodnot z ESP32 na KNX z kódu

```cpp
// V lambda nebo on_value:
id(knxip_comp).send_bool("1/4/1", true);        // GA 1/4/1, bool
id(knxip_comp).send_dpt9(0x0910, 21.5f);       // GA 1/2/16, teplota
id(knxip_comp).send_dpt5(0x0915, 128.0f);      // GA 1/2/21, 0-255
id(knxip_comp).send_dpt14(0x0A01, 3500.0f);    // GA 1/5/1, výkon W
```

GA adresa se zadává jako hex: `0xAABB` kde `AA = (area<<3 | line)`, `BB = member`.
Příklad: `1/2/10` → `(1<<11)|(2<<8)|10` = `0x090A` = `0x090A`.

Helper pro výpočet: `ga = (area << 11) | (line << 8) | member`

---

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
        destination_address=GroupAddress("1/2/10"),
        payload=GroupValueWrite(DPTTemperature.to_knx(21.5))
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

---

## Struktura souborů

```
components/knxip/
├── __init__.py          # hlavní komponenta, konfigurace
├── knxip.h              # KNXnet/IP stack: parser, builder, DPT encode/decode
├── knxip_component.h    # UDP multicast core, listener registry
├── knxip_sensor.h       # ESPHome sensor (DPT9/14/5)
├── knxip_binary_sensor.h# ESPHome binary sensor (DPT1)
├── knxip_switch.h       # ESPHome switch + GPIO
├── knxip_output.h       # ESPHome float output (DPT5, dimmer)
├── sensor.py
├── binary_sensor.py
├── switch.py
└── output.py
```

---

## Podporované DPT typy

| DPT   | Popis                  | Příklad použití             |
|-------|------------------------|-----------------------------|
| 1.x   | Boolean                | Spínač, pohyb, okno         |
| 5.x   | 8-bit uint (0–255)     | Jas, poloha žaluzie         |
| 9.x   | 2-byte KNX float       | Teplota, vlhkost, CO₂       |
| 14.x  | IEEE754 4-byte float   | Výkon W, energie kWh        |


---

## Jak správně použít id() v lambdách

ESPHome generuje `id(xxx)` jako pointer na komponentu. Aby fungovalo
`id(knxip_comp).send_dpt9(...)`, musí mít komponenta `id:` v YAML:

```yaml
knxip:
  id: knxip_comp          # ← toto je klíčové!
  individual_address: "1.1.50"
```

Pak v lambdě:
```cpp
// String GA — nejjednodušší, žádný hex výpočet
id(knxip_comp).send_bool("1/1/1", true);
id(knxip_comp).send_dpt9("1/2/10", 21.5f);
id(knxip_comp).send_dpt5("1/4/5", 128.0f);    // 0–255
id(knxip_comp).send_dpt14("1/5/1", 3500.0f);  // IEEE754
```

---

## Kompletní příklad — ESP32 s BME280 → KNX + GPIO relé

```yaml
esphome:
  name: esp32-knxip-demo

esp32:
  board: esp32dev
  framework:
    type: arduino

external_components:
  - source:
      type: git
      url: https://github.com/YOUR_USER/esphome-knxip
    refresh: 0s
    components: [ knxip ]

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:
  level: DEBUG
api:
ota:
  - platform: esphome

# ── KNX/IP ───────────────────────────────────────────────────────────────────
knxip:
  id: knxip_comp
  individual_address: "1.1.50"

# ── I²C senzor → KNX ─────────────────────────────────────────────────────────
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: bme280_i2c
    address: 0x76
    update_interval: 60s
    temperature:
      name: "Teplota"
      on_value:
        - lambda: id(knxip_comp).send_dpt9("1/2/10", x);
    humidity:
      name: "Vlhkost"
      on_value:
        - lambda: id(knxip_comp).send_dpt9("1/2/11", x);
    pressure:
      name: "Tlak"
      on_value:
        - lambda: id(knxip_comp).send_dpt9("1/2/12", x);

  # Příjem hodnot ze sběrnice
  - platform: knxip
    name: "Setpoint topení"
    group_address: "1/8/1"
    dpt: "9"
    unit_of_measurement: "°C"

# ── GPIO relé ovládané z KNX ─────────────────────────────────────────────────
switch:
  - platform: knxip
    name: "Relé 1"
    group_address_command: "1/1/1"
    group_address_state:   "1/1/2"
    pin:
      number: GPIO26
      inverted: false

  - platform: knxip
    name: "Relé 2"
    group_address_command: "1/1/3"
    pin:
      number: GPIO27

# ── Stmívač PWM → KNX ────────────────────────────────────────────────────────
output:
  - platform: ledc
    pin: GPIO25
    id: dimmer_out
    frequency: 1000Hz

  - platform: knxip
    id: knx_dimmer
    group_address: "1/1/20"    # přijímá DPT5 0–255

light:
  - platform: monochromatic
    name: "Stmívač"
    output: knx_dimmer
```

---

## Tlačítko BUT1 (GPIO34) na Olimex ESP32-POE-ISO

GPIO34 je input-only pin s externím pull-up na desce — při stisku jde na GND (active LOW).

### Varianta A — binární senzor (momentální stav stisknuto/puštěno)

Posílá na KNX `true` při stisku, `false` při puštění:

```yaml
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO34
      inverted: true      # active LOW - stisk = GND = true
      mode:
        input: true
        # NE pullup/pulldown - GPIO34 nemá interní pull resistory
    name: "BUT1"
    filters:
      - delayed_on: 20ms   # debounce
      - delayed_off: 20ms
    on_press:
      - lambda: id(knxip_comp).send_bool("1/3/1", true);
    on_release:
      - lambda: id(knxip_comp).send_bool("1/3/1", false);
```

### Varianta B — přepínač stavu (toggle on/off)

Každý stisk přepne stav — vhodné pro ovládání světla:

```yaml
globals:
  - id: light_state
    type: bool
    restore_value: true
    initial_value: 'false'

binary_sensor:
  - platform: gpio
    pin:
      number: GPIO34
      inverted: true
      mode:
        input: true
    name: "BUT1 toggle"
    filters:
      - delayed_on: 20ms
    on_press:
      - lambda: |-
          id(light_state) = !id(light_state);
          id(knxip_comp).send_bool("1/1/1", id(light_state));
          ESP_LOGI("but1", "Toggle → %d", id(light_state));
```

### Varianta C — ovládá KNX switch entitu přímo

Stisk tlačítka přepíná stav KNX switch komponenty:

```yaml
switch:
  - platform: knxip
    id: knx_light
    name: "Světlo BUT1"
    group_address_command: "1/1/1"
    group_address_state:   "1/1/2"
    pin:
      number: GPIO26   # volitelné GPIO relé

binary_sensor:
  - platform: gpio
    pin:
      number: GPIO34
      inverted: true
      mode:
        input: true
    name: "BUT1"
    filters:
      - delayed_on: 20ms
    on_press:
      - switch.toggle: knx_light
```

### Varianta D — dlouhý vs krátký stisk

```yaml
binary_sensor:
  - platform: gpio
    pin:
      number: GPIO34
      inverted: true
      mode:
        input: true
    name: "BUT1 multi"
    filters:
      - delayed_on: 20ms
    on_click:
      - min_length: 50ms
        max_length: 350ms
        then:
          - lambda: id(knxip_comp).send_bool("1/1/1", true);   # krátký = ZAP
      - min_length: 500ms
        max_length: 3000ms
        then:
          - lambda: id(knxip_comp).send_bool("1/1/1", false);  # dlouhý = VYP
```
