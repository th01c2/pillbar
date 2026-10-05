#include "render/layout.hpp"

#include <algorithm>
#include <cmath>

namespace pillbar {

const Rect* BarLayout::rect_for(Item item) const {
  for (const ItemBox& box : items) {
    if (box.item == item) return &box.rect;
  }
  return nullptr;
}

const BarLayout& LayoutEngine::compute(const Config& config, const AppState& state, int output_w,
                                       int output_h) {
  layout_.items.clear();
  layout_.workspace_slots.clear();
  layout_.workspace_focused_index = -1;

  layout_.bar_h = static_cast<int>(std::lround(config.height_px));
  layout_.bar_w = static_cast<int>(std::lround(static_cast<double>(output_w) * config.width_frac));
  layout_.bar_w = std::max(layout_.bar_w, 1);
  layout_.screen_x = (output_w - layout_.bar_w) / 2;
  layout_.screen_y = static_cast<int>(
      std::lround(static_cast<double>(output_h) * config.margin_top_frac));

  auto item_of = [](const std::string& name) -> Item {
    if (name == "battery") return Item::Battery;
    if (name == "volume") return Item::Volume;
    if (name == "wifi") return Item::Wifi;
    if (name == "bluetooth") return Item::Bluetooth;
    if (name == "workspaces") return Item::Workspaces;
    if (name == "window") return Item::ActiveWindow;
    if (name == "clock") return Item::Clock;
    if (name == "cpu") return Item::Cpu;
    if (name == "gpu") return Item::Gpu;
    return Item::None;
  };

  for (const std::string& name : config.order) {
    const auto it = config.slots.find(name);
    if (it == config.slots.end() || !it->second.enabled) continue;
    const Item item = item_of(name);
    if (item == Item::None) continue;
    const int x0 = static_cast<int>(std::lround(it->second.x0 * layout_.bar_w));
    const int x1 = static_cast<int>(std::lround(it->second.x1 * layout_.bar_w));
    layout_.items.push_back(ItemBox{item, Rect{x0, 0, std::max(1, x1 - x0), layout_.bar_h}});
  }

  // Workspaces: lay out N numerals evenly across the slot.
  const Rect* ws = layout_.rect_for(Item::Workspaces);
  if (ws != nullptr) {
    int count = config.min_workspaces > 0 ? config.min_workspaces : 5;
    int max_id = 0;
    for (const WorkspaceState& w : state.workspaces.list) {
      max_id = std::max(max_id, w.id);
    }
    count = std::max(count, max_id);
    if (count <= 0) count = 5;
    const double diameter =
        std::min(static_cast<double>(layout_.bar_w) * 0.047,
                 static_cast<double>(layout_.bar_h) * 0.62);
    const double cy = layout_.bar_h / 2.0;
    for (int i = 0; i < count; ++i) {
      const double cx = static_cast<double>(ws->x) +
                        (static_cast<double>(i) + 0.5) * static_cast<double>(ws->w) /
                            static_cast<double>(count);
      WorkspaceSlot slot;
      slot.id = 0;  // filled by renderer from state order
      slot.cx = cx;
      slot.cy = cy;
      slot.diameter = diameter > 1.0 ? diameter : 1.0;
      layout_.workspace_slots.push_back(slot);
    }
    // Map slot index -> workspace id using state order (or 1..count).
    for (std::size_t i = 0; i < layout_.workspace_slots.size(); ++i) {
      if (i < state.workspaces.list.size()) {
        layout_.workspace_slots[i].id = state.workspaces.list[i].id;
        if (state.workspaces.list[i].focused) {
          layout_.workspace_focused_index = static_cast<int>(i);
        }
      } else {
        layout_.workspace_slots[i].id = static_cast<int>(i) + 1;
      }
    }
  }

  return layout_;
}

}  // namespace pillbar
