#include "app/config.hpp"

namespace pillbar {

// Item slots are compiled in: item order comes from Config::order and each
// item's horizontal span is a fraction of the pill. There is no config file;
// change the defaults in this file (or in Config in config.hpp) and rebuild.
void config_apply_default_slots(Config& config) {
  config.slots.clear();
  config.slots["battery"] = ItemSlot{true, 0.045, 0.112};
  config.slots["volume"] = ItemSlot{true, 0.132, 0.199};
  config.slots["wifi"] = ItemSlot{true, 0.225, 0.244};
  config.slots["bluetooth"] = ItemSlot{true, 0.250, 0.269};
  config.slots["workspaces"] = ItemSlot{true, 0.262, 0.430};
  config.slots["window"] = ItemSlot{true, 0.450, 0.910};
  config.slots["clock"] = ItemSlot{true, 0.922, 0.986};
  config.slots["recorder"] = ItemSlot{true, 0.0, 0.0};
}

}  // namespace pillbar
