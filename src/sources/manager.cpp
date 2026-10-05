#include "sources/manager.hpp"

namespace pillbar {

SourceManager::SourceManager(EventLoop& loop, std::function<void(Item)> deliver)
    : loop_(loop), deliver_(std::move(deliver)) {
  if (debounce_.valid()) {
    loop_.add(debounce_.get(), EPOLLIN, [this](std::uint32_t) { on_timer(); });
    debounce_.disarm();
  }
}

NotifyFn SourceManager::notify_fn() {
  return [this](Item item) { notify(item); };
}

void SourceManager::add(Source* source) { sources_.push_back(source); }

void SourceManager::notify(Item item) {
  if (item == Item::None) return;
  const bool was_idle = pending_ == Item::None;
  pending_ |= item;
  if (was_idle && debounce_.valid()) {
    debounce_.arm_relative_ms(kDebounceMs, true);
  }
}

void SourceManager::on_timer() {
  debounce_.consume();
  flush();
}

void SourceManager::flush() {
  if (pending_ == Item::None) return;
  const Item items = pending_;
  pending_ = Item::None;
  if (deliver_) deliver_(items);
}

void SourceManager::start_all() {
  for (Source* source : sources_) {
    source->start(loop_);
  }
}

void SourceManager::refresh_all() {
  for (Source* source : sources_) {
    source->refresh();
  }
}

}  // namespace pillbar
