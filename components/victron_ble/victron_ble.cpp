#include "victron_ble.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

#include <aes/esp_aes.h>

namespace esphome {
namespace victron_ble {

static const char *const TAG = "victron_ble";

void VictronBle::dump_config() {
  ESP_LOGCONFIG(TAG, "Victron BLE:");
  ESP_LOGCONFIG(TAG, "  Address: %s", this->address_str().c_str());
}

// Submit update to sensors & callbacks.
void VictronBle::update() {
  if (this->last_package_updated_) {
    this->last_package_updated_ = false;
    this->on_message_callback_.call(&this->last_package_);
  }
  if (this->battery_monitor_updated_) {
    this->battery_monitor_updated_ = false;
    this->on_battery_monitor_message_callback_.call(&this->last_package_.data.battery_monitor);
  }
  if (this->solar_charger_updated_) {
    this->solar_charger_updated_ = false;
    this->on_solar_charger_message_callback_.call(&this->last_package_.data.solar_charger);
  }
  if (this->inverter_updated_) {
    this->inverter_updated_ = false;
    this->on_inverter_message_callback_.call(&this->last_package_.data.inverter);
  }
  if (this->dcdc_converter_updated_) {
    this->dcdc_converter_updated_ = false;
    this->on_dcdc_converter_message_callback_.call(&this->last_package_.data.dcdc_converter);
  }
  if (this->smart_lithium_updated_) {
    this->smart_lithium_updated_ = false;
    this->on_smart_lithium_message_callback_.call(&this->last_package_.data.smart_lithium);
  }
  if (this->inverter_rs_updated_) {
    this->inverter_rs_updated_ = false;
    this->on_inverter_rs_message_callback_.call(&this->last_package_.data.inverter_rs);
  }
  if (this->smart_battery_protect_updated_) {
    this->smart_battery_protect_updated_ = false;
    this->on_smart_battery_protect_message_callback_.call(&this->last_package_.data.smart_battery_protect);
  }
  if (this->lynx_smart_bms_updated_) {
    this->lynx_smart_bms_updated_ = false;
    this->on_lynx_smart_bms_message_callback_.call(&this->last_package_.data.lynx_smart_bms);
  }
  if (this->multi_rs_updated_) {
    this->multi_rs_updated_ = false;
    this->on_multi_rs_message_callback_.call(&this->last_package_.data.multi_rs);
  }
  if (this->ve_bus_updated_) {
    this->ve_bus_updated_ = false;
    this->on_ve_bus_message_callback_.call(&this->last_package_.data.ve_bus);
  }
  if (this->dc_energy_meter_updated_) {
    this->dc_energy_meter_updated_ = false;
    this->on_dc_energy_meter_message_callback_.call(&this->last_package_.data.dc_energy_meter);
  }
}

/**
 * Parse all incoming BLE payloads to see if it is a Victron BLE advertisement.
 */

bool VictronBle::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_) {
    return false;
  }

  const auto &manu_datas = device.get_manufacturer_datas();
  if (manu_datas.size() != 1) {
    return false;
  }

  const auto &manu_data = manu_datas[0];
  if (manu_data.uuid != esp32_ble_tracker::ESPBTUUID::from_uint16(VICTRON_MANUFACTURER_ID)) {
    return false;
  }
  if (manu_data.data.size() < sizeof(VICTRON_BLE_RECORD_BASE)) {
    return false;
  }

  // Parse the unencrypted data.
  const auto *victron_data = (const VICTRON_BLE_RECORD_BASE *) manu_data.data.data();

  if (victron_data->manufacturer_base.manufacturer_record_type !=
      VICTRON_MANUFACTURER_RECORD_TYPE::PRODUCT_ADVERTISEMENT) {
    return false;
  }

  if (victron_data->encryption_key_0 != this->bindkey_[0]) {
    ESP_LOGW(TAG, "[%s] Incorrect Bindkey. Must start with %02X", this->address_str().c_str(), this->bindkey_[0]);
    return false;
  }

  // Filter out duplicate messages
  if ((victron_data->data_counter_lsb | (victron_data->data_counter_msb << 8)) == this->last_package_.data_counter) {
    return false;
  }

  const u_int8_t *crypted_data = manu_data.data.data() + sizeof(VICTRON_BLE_RECORD_BASE);
  const u_int8_t crypted_len = manu_data.data.size() - sizeof(VICTRON_BLE_RECORD_BASE);
  ESP_LOGVV(TAG, "[%s] Cryted message: %s", this->address_str().c_str(),
            format_hex_pretty(crypted_data, crypted_len).c_str());

  if (!this->is_record_type_supported_(victron_data->record_type, crypted_len)) {
    // Error logging is done by is_record_type_supported_.
    return false;
  }

  // Max documented message size is 16 byte. 32 byte gives enough room.
  u_int8_t encrypted_data[32] = {0};

  if (crypted_len > sizeof(encrypted_data)) {
    ESP_LOGW(TAG, "[%s] Record is too long %u", this->address_str().c_str(), crypted_len);
    return false;
  }

  if (!this->encrypt_message_(crypted_data, crypted_len, encrypted_data, victron_data->data_counter_lsb,
                              victron_data->data_counter_msb)) {
    // Error logging is done by encrypt_message_.
    return false;
  }

  this->handle_record_(victron_data->record_type, encrypted_data);

  // Save the last recieved data counter
  this->last_package_.data_counter = victron_data->data_counter_lsb | (victron_data->data_counter_msb << 8);
  return true;
}

bool VictronBle::encrypt_message_(const u_int8_t *crypted_data, const u_int8_t crypted_len, u_int8_t encrypted_data[32],
                                  const u_int8_t data_counter_lsb, const u_int8_t data_counter_msb) {
  esp_aes_context ctx;
  esp_aes_init(&ctx);
  auto status = esp_aes_setkey(&ctx, this->bindkey_.data(), this->bindkey_.size() * 8);
  if (status != 0) {
    ESP_LOGE(TAG, "[%s] Error during esp_aes_setkey operation (%i).", this->address_str().c_str(), status);
    esp_aes_free(&ctx);
    return false;
  }

  size_t nc_offset = 0;
  u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
  u_int8_t stream_block[16] = {0};

  status = esp_aes_crypt_ctr(&ctx, crypted_len, &nc_offset, nonce_counter, stream_block, crypted_data, encrypted_data);
  if (status != 0) {
    ESP_LOGE(TAG, "[%s] Error during esp_aes_crypt_ctr operation (%i).", this->address_str().c_str(), status);
    esp_aes_free(&ctx);
    return false;
  }

  esp_aes_free(&ctx);
  ESP_LOGV(TAG, "[%s] Enrypted message: %s", this->address_str().c_str(),
           format_hex_pretty(encrypted_data, crypted_len).c_str());
  return true;
}

bool VictronBle::is_record_type_supported_(const VICTRON_BLE_RECORD_TYPE record_type, const u_int8_t crypted_len) {
  switch (record_type) {
    case VICTRON_BLE_RECORD_TYPE::SOLAR_CHARGER:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_SOLAR_CHARGER)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::BATTERY_MONITOR:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_BATTERY_MONITOR)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::INVERTER:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_INVERTER)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::DCDC_CONVERTER:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_DCDC_CONVERTER)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::SMART_LITHIUM:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_SMART_LITHIUM)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::INVERTER_RS:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_INVERTER_RS)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::SMART_BATTERY_PROTECT:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_SMART_BATTERY_PROTECT)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::LYNX_SMART_BMS:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_LYNX_SMART_BMS)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::MULTI_RS:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_MULTI_RS)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::VE_BUS:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_VE_BUS)) {
        return true;
      }
      break;
    case VICTRON_BLE_RECORD_TYPE::DC_ENERGY_METER:
      if (crypted_len >= sizeof(VICTRON_BLE_RECORD_DC_ENERGY_METER)) {
        return true;
      }
      break;
    default:
      ESP_LOGW(TAG, "[%s] Unsupported record type %02X", this->address_str().c_str(), (u_int8_t) record_type);
      return false;
      break;
  }
  ESP_LOGW(TAG, "[%s] Record type %02X message is too short (%u).", this->address_str().c_str(), (u_int8_t) record_type,
           crypted_len);
  return false;
}

void VictronBle::handle_record_(const VICTRON_BLE_RECORD_TYPE record_type, const u_int8_t encrypted_data[32]) {
  this->last_package_.record_type = record_type;
  switch (record_type) {
    case VICTRON_BLE_RECORD_TYPE::SOLAR_CHARGER:
      this->last_package_.data.solar_charger = *(const VICTRON_BLE_RECORD_SOLAR_CHARGER *) encrypted_data;
      this->solar_charger_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved SOLAR_CHARGER message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::BATTERY_MONITOR:
      this->last_package_.data.battery_monitor = *(const VICTRON_BLE_RECORD_BATTERY_MONITOR *) encrypted_data;
      this->battery_monitor_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved BATTERY_MONITOR message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::INVERTER:
      this->last_package_.data.inverter = *(const VICTRON_BLE_RECORD_INVERTER *) encrypted_data;
      this->inverter_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved INVERTER message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::DCDC_CONVERTER:
      this->last_package_.data.dcdc_converter = *(const VICTRON_BLE_RECORD_DCDC_CONVERTER *) encrypted_data;
      this->dcdc_converter_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved DCDC_CONVERTER message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::SMART_LITHIUM:
      this->last_package_.data.smart_lithium = *(const VICTRON_BLE_RECORD_SMART_LITHIUM *) encrypted_data;
      this->smart_lithium_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved SMART_LITHIUM message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::INVERTER_RS:
      this->last_package_.data.inverter_rs = *(const VICTRON_BLE_RECORD_INVERTER_RS *) encrypted_data;
      this->inverter_rs_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved INVERTER_RS message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::SMART_BATTERY_PROTECT:
      this->last_package_.data.smart_battery_protect =
          *(const VICTRON_BLE_RECORD_SMART_BATTERY_PROTECT *) encrypted_data;
      this->smart_battery_protect_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved SMART_BATTERY_PROTECT message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::LYNX_SMART_BMS:
      this->last_package_.data.lynx_smart_bms = *(const VICTRON_BLE_RECORD_LYNX_SMART_BMS *) encrypted_data;
      this->lynx_smart_bms_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved LYNX_SMART_BMS message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::MULTI_RS:
      this->last_package_.data.multi_rs = *(const VICTRON_BLE_RECORD_MULTI_RS *) encrypted_data;
      this->multi_rs_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved MULTI_RS message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::VE_BUS:
      this->last_package_.data.ve_bus = *(const VICTRON_BLE_RECORD_VE_BUS *) encrypted_data;
      this->ve_bus_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved VE_BUS message.", this->address_str().c_str());
      break;
    case VICTRON_BLE_RECORD_TYPE::DC_ENERGY_METER:
      this->last_package_.data.dc_energy_meter = *(const VICTRON_BLE_RECORD_DC_ENERGY_METER *) encrypted_data;
      this->dc_energy_meter_updated_ = true;
      ESP_LOGD(TAG, "[%s] Recieved DC_ENERGY_METER message.", this->address_str().c_str());
      break;
    default:
      break;
  }
  this->last_package_updated_ = true;
  if (this->submit_sensor_data_asap_) {
    // Call update for every data.
    this->update();
  }
}

}  // namespace victron_ble
}  // namespace esphome

#endif
