# ESPHome victron_ble component

This ESPHome component supports both official Victron Bluetooth BLE Protocols:

- <https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html>
  (Recommended) Component `victron_ble`. See <https://github.com/keshavdv/victron-ble> how to fetch your encryption keys.
  - Smart Shunt
  - Smart Solar
  - More to come

- <https://community.victronenergy.com/questions/93919/victron-bluetooth-ble-protocol-publication.html>
  Component `victron_ble_connect`.
  This solution is using the [PR#4258 of ESPHome](https://github.com/esphome/esphome/pull/4258) for connecting to Victron Smart Shunt.
  Supported Devices:
  - Smart Shunt

## `victron_ble` component (Recommended)

### Steps

1. Update your Victron device.
2. Enable `Instant readout via Bluetooth`.
3. Get Mac and Encryption key from Victron Software. See <https://github.com/keshavdv/victron-ble> for details.

### Example

See [victron_ble.yaml](/victron_ble.yaml) for a full example.

```yaml
esphome:
  name: "victron-ble"

external_components:
  - source: github://Fabian-Schmidt/esphome-victron_ble

esp32:
  board: mhetesp32devkit

logger:
  level: DEBUG

esp32_ble_tracker:

victron_ble:
  - id: MySmartShunt
    mac_address: "MY SMART SHUNT MAC"    
    bindkey: "MY AES ENCRYPTION KEY"
  - id: MySmartSolar
    mac_address: "MY SMART SOLAR MAC"
    bindkey: "MY AES ENCRYPTION KEY"

sensor:
  # MySmartShunt
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Time remaining"
    type: BATTERY_MONITOR_TIME_TO_GO
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Battery voltage"
    type: BATTERY_MONITOR_BATTERY_VOLTAGE
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Alarm"
    type: BATTERY_MONITOR_ALARM_REASON
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Starter Battery"
    # BATTERY_MONITOR_AUX_VOLTAGE or BATTERY_MONITOR_MID_VOLTAGE or BATTERY_MONITOR_TEMPERATURE.
    # Depending on configuration of SmartShunt
    type: BATTERY_MONITOR_AUX_VOLTAGE
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Current"
    type: BATTERY_MONITOR_BATTERY_CURRENT
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "Consumed Ah"
    type: BATTERY_MONITOR_CONSUMED_AH
  - platform: victron_ble
    victron_ble_id: MySmartShunt
    name: "State of charge"
    type: BATTERY_MONITOR_STATE_OF_CHARGE

  # MySmartSolar
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Device state"
    type: SOLAR_CHARGER_DEVICE_STATE
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Charger error"
    type: SOLAR_CHARGER_CHARGER_ERROR
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Battery Voltage"
    type: SOLAR_CHARGER_BATTERY_VOLTAGE
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Battery Current"
    type: SOLAR_CHARGER_BATTERY_CURRENT
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Yield Today"
    type: SOLAR_CHARGER_YIELD_TODAY
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "PV Power"
    type: SOLAR_CHARGER_PV_POWER
  - platform: victron_ble
    victron_ble_id: MySmartSolar
    name: "Load Current"
    type: SOLAR_CHARGER_LOAD_CURRENT
```

## `victron_ble_connect` component

### Steps

1. Use a SmartShunt, other devices don't support this yet.
2. Use VictronConnect v5.42 or newer.
3. Update the SmartShunt to version v2.31 or later.
4. Connect to the SmartShunt using VictronConnect, and enable this protocol (screenshot below)
5. Power cycle the SmartShunt
6. Use `victron_scanner` to find mac address of your SmartShunt.

![The setting to enable this "third party implementation"-protocol](/img/VictronAppEnableGATT.png)

### Find the MAC Adress of your Smart Shunt

Use the Victron scanner component to find the MAC address of your Victron Smart Shunt.

```yaml
esphome:
  name: "victron-scanner"

external_components:
  # https://github.com/esphome/esphome/pull/4258
  - source: github://pr#4258
    components: [ble_client, esp32_ble, esp32_ble_client, esp32_ble_tracker]
    #refresh: always
  - source: github://Fabian-Schmidt/esphome-victron_ble

esp32:
  board: mhetesp32devkit

logger:
  level: INFO

esp32_ble_tracker:

victron_scanner:
```

Check the console of ESP Home to find the message like this:

```txt
[I][victron_scanner:044]: FOUND SMART SHUNT 500A/50mV 'My SmartShunt' at AB:CD:EF:01:02:03
```

### Example ESPHome config

```yaml
esphome:
  name: "victron-ble"

external_components:
  # https://github.com/esphome/esphome/pull/4258
  - source: github://pr#4258
    components: [ble_client, esp32_ble, esp32_ble_client, esp32_ble_tracker]
    refresh: always
  - source: github://Fabian-Schmidt/esphome-victron_ble


esp32_ble:
  io_capability: keyboard_only

ble_client:
- mac_address: <MY VICTORN SMART SHUNT MAC ADDRESS>
  id: victron_smart_shunt_ble_client_id
  on_passkey_request:
    then:
      - ble_client.passkey_reply:
          id: victron_smart_shunt_ble_client_id
          passkey: !secret ble_passkey

sensor:
  - platform: victron_ble_connect
    ble_client_id: victron_smart_shunt_ble_client_id
    state_of_charge: 
      name: "State of Charge"
    voltage: 
      name: "Voltage"
    power: 
      name: "Power Consumption"
    current: 
      name: "Current"
    ah: 
      name: "Consumed Ah"
    starter_battery_voltage: 
      name: "Starter Battery Voltage"
    remaining_time: 
      name: "Remaining Time"
```

See [victron_ble_all.yaml](/victron_ble_all.yaml) for a full example.
Assumption is you are having a `secret.yaml` in the same folder.

## TODO

- In `notify` mode Victron SmartShunt submits on change max every second one value. It send to the sensor api with `update_interval` (default 2min). Currently the last value is submitted. Implement an average function between sensor submits.  
