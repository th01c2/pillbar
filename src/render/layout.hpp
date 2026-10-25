#pragma once

#include <vector>

#include "app/config.hpp"
#include "model/state.hpp"
#include "wayland/shm.hpp"

namespace pillbar {

struct ItemBox {
  Item item = Item::None;
  Rect rect; // bar-local logical pixels
};

struct WorkspaceSlot {
  int id = 0;
  int state_index = -1; // index into AppState::workspaces.list
  double cx = 0.0;      // bar-local logical pixels
  double cy = 0.0;
  double diameter = 0.0;
};

struct BarLayout {
  int bar_w = 0;
  int bar_h = 0;
  int screen_x = 0; // top-left in output logical coordinates
  int screen_y = 0;
  std::vector<ItemBox> items;
  std::vector<WorkspaceSlot> workspace_slots;
  int workspace_focused_index = -1;
  // Animated focus indicator, positioned by the app so it can slide between
  // workspace numerals instead of jumping.
  bool ws_focus_visible = false;
  double ws_focus_x = 0.0;
  double ws_focus_d = 0.0;

  const Rect* rect_for(Item item) const;
};

} // namespace pillbar
