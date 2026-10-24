#include "app/config.hpp"

namespace pillbar {

// Item slots are compiled in: item order comes from Config::order and each
// item's horizontal span is a fraction of the pill. There is no config file;
// change the defaults in this file (or in Config in config.hpp) and rebuild.
void config_apply_default_slots(Config& config) {
  config.slots.clear();
  config.slots["battery"] = ItemSlot{true};
  config.slots["volume"] = ItemSlot{true};
  config.slots["wifi"] = ItemSlot{true};
  config.slots["bluetooth"] = ItemSlot{true};
  config.slots["workspaces"] = ItemSlot{true};
  config.slots["window"] = ItemSlot{true};
  config.slots["clock"] = ItemSlot{true};
  config.slots["recorder"] = ItemSlot{true};
}

}  // namespace pillbar
