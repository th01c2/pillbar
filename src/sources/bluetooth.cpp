#include "sources/bluetooth.hpp"

#include <cstring>
#include <map>

#include "app/logging.hpp"

namespace pillbar {

BluetoothSource::BluetoothSource(AppState& state, NotifyFn notify, DbusBus& bus)
    : state_(state), notify_(std::move(notify)), bus_(bus) {}

int BluetoothSource::on_signal(sd_bus_message*, void* userdata, sd_bus_error*) {
  auto* self = static_cast<BluetoothSource*>(userdata);
  self->rescan();
  return 0;
}

bool BluetoothSource::start(EventLoop& loop) {
  (void)loop;
  if (!bus_.valid()) return false;
  bus_.add_match(
      "type='signal',sender='org.bluez',"
      "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'",
      [this](sd_bus_message* m) { return on_signal(m, this, nullptr); });
  bus_.add_match(
      "type='signal',sender='org.bluez',"
      "interface='org.freedesktop.DBus.ObjectManager'",
      [this](sd_bus_message* m) { return on_signal(m, this, nullptr); });
  rescan();
  return state_.bluetooth.present;
}

void BluetoothSource::refresh() { rescan(); }

void BluetoothSource::rescan() {
  if (bus_.bus() == nullptr) return;
  sd_bus_message* reply = nullptr;
  if (!bus_.call_method("org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                        "GetManagedObjects", &reply, "")) {
    if (state_.bluetooth.present) {
      state_.bluetooth = BluetoothState{};
      if (notify_) notify_(Item::Bluetooth);
    }
    return;
  }

  BluetoothState next;
  struct DeviceInfo {
    BluetoothDevice device;
    bool has_battery = false;
  };
  std::map<std::string, DeviceInfo> devices;

  sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
  while (sd_bus_message_enter_container(reply, 'r', "oa{sa{sv}}") > 0) {
    const char* path = nullptr;
    sd_bus_message_read(reply, "o", &path);
    bool is_adapter = false;
    bool is_device = false;
    std::string device_name;
    std::string device_address;
    bool device_connected = false;
    int device_battery = -1;

    sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
    while (sd_bus_message_enter_container(reply, 'r', "sa{sv}") > 0) {
      const char* iface = nullptr;
      sd_bus_message_read(reply, "s", &iface);
      if (std::strcmp(iface, "org.bluez.Adapter1") == 0) is_adapter = true;
      if (std::strcmp(iface, "org.bluez.Device1") == 0) is_device = true;
      sd_bus_message_enter_container(reply, 'a', "{sv}");
      while (sd_bus_message_enter_container(reply, 'r', "sv") > 0) {
        const char* prop = nullptr;
        sd_bus_message_read(reply, "s", &prop);
        sd_bus_message_enter_container(reply, 'v', nullptr);
        if (std::strcmp(prop, "Powered") == 0) {
          int powered = 0;
          if (sd_bus_message_read(reply, "b", &powered) >= 0) {
            next.present = true;
            if (powered != 0) next.powered = true;
          }
        } else if (std::strcmp(prop, "Name") == 0 || std::strcmp(prop, "Alias") == 0) {
          const char* value = nullptr;
          if (sd_bus_message_read(reply, "s", &value) >= 0 && value != nullptr &&
              device_name.empty()) {
            device_name = value;
          }
        } else if (std::strcmp(prop, "Address") == 0) {
          const char* value = nullptr;
          if (sd_bus_message_read(reply, "s", &value) >= 0 && value != nullptr) {
            device_address = value;
          }
        } else if (std::strcmp(prop, "Connected") == 0) {
          int connected = 0;
          if (sd_bus_message_read(reply, "b", &connected) >= 0) device_connected = connected != 0;
        } else if (std::strcmp(prop, "Percentage") == 0) {
          std::uint8_t percentage = 0;
          if (sd_bus_message_read(reply, "y", &percentage) >= 0) device_battery = percentage;
        }
        sd_bus_message_exit_container(reply);
        sd_bus_message_exit_container(reply);
      }
      sd_bus_message_exit_container(reply);
      sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);
    sd_bus_message_exit_container(reply);

    (void)is_adapter;
    if (is_device) {
      DeviceInfo info;
      info.device.name = device_name;
      info.device.address = device_address;
      info.device.connected = device_connected;
      info.device.battery = device_battery;
      devices[path != nullptr ? path : ""] = std::move(info);
    }
  }
  sd_bus_message_exit_container(reply);
  sd_bus_message_unref(reply);

  for (auto& entry : devices) {
    if (entry.second.device.connected) next.devices.push_back(entry.second.device);
  }

  if (!(next == state_.bluetooth)) {
    state_.bluetooth = next;
    if (notify_) notify_(Item::Bluetooth);
  }
}

std::vector<std::string> BluetoothSource::detail() const {
  std::vector<std::string> lines;
  const BluetoothState& bt = state_.bluetooth;
  if (!bt.present) {
    lines.push_back("Bluetooth: no adapter");
    return lines;
  }
  lines.push_back(std::string("Adapter: ") + (bt.powered ? "powered" : "off"));
  if (bt.devices.empty()) {
    lines.push_back("No connected devices");
  } else {
    for (const BluetoothDevice& device : bt.devices) {
      std::string line = device.name.empty() ? device.address : device.name;
      if (device.battery >= 0) line += "  " + std::to_string(device.battery) + "%";
      lines.push_back(line);
    }
  }
  return lines;
}

}  // namespace pillbar

