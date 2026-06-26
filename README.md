# ESPHome Electrolux NIU Climate

An ESPHome external component that replaces the Electrolux/Frigidaire NIU-LIGHT Wi-Fi module and controls the air conditioner locally over its internal UART bus.

## Hardware status

Tested with:

- Frigidaire GHWQ125WD1
- Frigidaire GHWQ105WD1
- Frigidaire GHWQ085WD1

The protocol may be specific to the NIU-LIGHT (2AIBX-NIULL) WiFi board found in these models. Even though the AC itself is a white-labelled Midea unit, Frigidaire replaced the Midea Wifi board with their own NIU. It's possible it may work with other Frigidaire / Electrolux AC units, though I havent tested

## Wiring

The NIU connector is basic UART. It has 5V, ground, TX, and RX. Remove the original NIU before connecting the ESP — I replaced the JST on the NIU end with dupont connectors.

The appliance side uses 5V logic, but the ESP32 uses 3.3V logic. You'll need a level shifter if you don't want to accidentally fry your ESP. Marked colours apply to the AC units I tested, but may differ on yours.

| Colour | ESP |
|---|---|
| Red | 5V |
| Black | TX -> AC |
| Orange | RX -> AC |
| Brown | GND |


## Installation

Include the following in your YAML

```yaml
external_components:
  - source: github://gtwok/frigidaire-ac-niu@main
    components: [ac_niu_climate]
```

See [`ac_niu.yaml`](ac_niu.yaml) for a complete ESP32 configuration. The component itself needs only a correctly configured UART and a climate entry:

```yaml
uart:
  id: ac_bus
  tx_pin: GPIO5
  rx_pin: GPIO4
  baud_rate: 9600
  data_bits: 8
  parity: EVEN
  stop_bits: 1

climate:
  - platform: ac_niu_climate
    # Use the ESPHome friendly name without adding a second entity suffix.
    name: None
    uart_id: ac_bus
```

## Optional entities

Optional child entities are added beneath the climate entry. These will add sensors to the device in Home Assistant for each included. You can omit any you dont want.

```yaml
climate:
  - platform: ac_niu_climate
    name: None
    uart_id: ac_bus
    child_lock:
      name: "Child Lock"
    pureair_filter_installed:
      name: "PureAir Filter Installed"
    coil_temperature:
      name: "Coil Temperature"
    air_quality_raw:
      name: "Air Quality Raw"
    air_quality:
      name: "Air Quality"
    filter_status:
      name: "Filter Status"
    sleep_mode:
      name: "Sleep Mode"
```

## Supported controls

- Power off / on
- Cool, Dry, and Fan-only modes
- Low, Medium, High, and Auto fan speeds
- Vertical swing
- Eco preset
- Target and current temperature
- Physical-panel state synchronization

Auto fan speed is ignored in Fan-only mode as the AC doesnt support it.

## Protocol

The reverse-engineered frame format, handshake, register map, and captures are documented in [`PROTOCOL.md`](PROTOCOL.md). The implementation reproduces the NIU discovery, initialization, polls, heartbeat, event acknowledgements, sequence/type counters, and confirmed SET transactions.

Unmapped registers, markers, malformed events, and unexpected values on mapped registers are logged at `INFO` with the complete raw frame.

## Protocol development

The example configuration registers a Home Assistant action named `esphome.ac_niu_set_ac_register` for testing SET registers. Its `command` field contains the two-byte register followed by one or more value bytes, written as hexadecimal. For ex, `04 26 01` enables Eco mode.

This is an advanced development tool: only write registers whose behavior is understood.

## Known limitations

- Child lock, filter, air-quality, and coil-temperature entities are read-only.
- Dust filter change warning is included, but PureAir filter change warning is not (yet)
- Schedule set is likely a boolean in the dump somewhere, though unknown where yet (and is moot, anyway)
