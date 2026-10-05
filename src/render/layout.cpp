#include "render/layout.hpp"

namespace pillbar {

const Rect* BarLayout::rect_for(Item item) const {
  for (const ItemBox& box : items) {
    if (box.item == item) return &box.rect;
  }
  return nullptr;
}

}  // namespace pillbar
