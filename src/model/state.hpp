#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pillbar {

// Every independently-redrawable region of the bar. A Source emits at most
// one Item per value change; the renderer only damages the affected rects.
enum class Item : std::uint32_t {
  None = 0,
  Battery = 1u << 0,
  Volume = 1u << 1,
  Wifi = 1u << 2,
  Bluetooth = 1u << 3,
  Workspaces = 1u << 4,
  ActiveWindow = 1u << 5,
  Clock = 1u << 6,
  Cpu = 1u << 7,
  Gpu = 1u << 8,
  All = 0xFFFFFFFFu,
};

inline Item operator|(Item a, Item b) {
  return static_cast<Item>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline Item& operator|=(Item& a, Item b) { return a = a | b; }
inline bool has_item(Item set, Item bit) {
  return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(bit)) != 0;
}

struct BatteryState {
  bool present = false;
  int percent = 0;
  bool charging = false;
  bool full = false;
  bool ac_online = false;
  long energy_now = 0;   // uWh
  long energy_full = 0;  // uWh
  long power_now = 0;    // uW
  int health = 0;        // percent
  bool operator==(const BatteryState&) const = default;
};

struct VolumeState {
  bool available = false;
  bool muted = false;
  int percent = 0;
  std::string sink_name;
  std::string sink_desc;
  bool operator==(const VolumeState&) const = default;
};

struct WifiState {
  bool present = false;
  bool connected = false;
  bool wired = false;  // an ethernet device is up/configured
  bool enabled = true;
  int signal = 0;  // 0..100
  std::string ssid;
  std::string ifname;
  std::string wired_ifname;
  std::string band;
  int freq = 0;  // MHz
  int rate = 0;  // kbit/s
  bool operator==(const WifiState&) const = default;
};

struct BluetoothDevice {
  std::string name;
  std::string address;
  bool connected = false;
  int battery = -1;  // -1 unknown
  bool operator==(const BluetoothDevice&) const = default;
};

struct BluetoothState {
  bool present = false;
  bool powered = false;
  std::vector<BluetoothDevice> devices;
  bool operator==(const BluetoothState&) const = default;
};

struct WorkspaceState {
  int id = 0;
  std::string name;
  bool occupied = false;
  bool focused = false;
  int windows = 0;
  std::vector<std::string> classes;
  bool operator==(const WorkspaceState&) const = default;
};

struct WorkspacesState {
  std::vector<WorkspaceState> list;  // ordered by numeric id, includes empty 1..5
  bool operator==(const WorkspacesState&) const = default;
};

struct WindowState {
  bool present = false;
  std::string title;
  std::string cls;
  int workspace = 0;
  int pid = 0;
  bool operator==(const WindowState&) const = default;
};

struct ClockState {
  int hour = 0;
  int minute = 0;
  bool operator==(const ClockState&) const = default;
};

struct CpuCoreState {
  double usage = 0.0;  // percent
  bool operator==(const CpuCoreState&) const = default;
};

struct CpuState {
  bool present = false;
  double total = 0.0;       // percent
  double frequency = 0.0;   // MHz
  double temperature = 0.0; // Celsius
  std::vector<CpuCoreState> cores;
  bool operator==(const CpuState&) const = default;
};

struct GpuState {
  bool present = false;
  std::string vendor;         // amd / nvidia / intel
  double temperature = 0.0;   // Celsius
  double utilization = 0.0;   // percent
  long vram_used = 0;         // bytes
  long vram_total = 0;        // bytes
  bool operator==(const GpuState&) const = default;
};

// The single source of truth. Sources never touch this directly; the
// SourceManager applies typed updates.
struct AppState {
  BatteryState battery;
  VolumeState volume;
  WifiState wifi;
  BluetoothState bluetooth;
  WorkspacesState workspaces;
  WindowState window;
  ClockState clock;
  CpuState cpu;
  GpuState gpu;
};

}  // namespace pillbar
