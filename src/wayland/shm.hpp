#pragma once

#include <wayland-client.h>

#include <cairo.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "app/event_loop.hpp"

namespace pillbar {

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  Rect union_with(const Rect& other) const;
};

// A single wl_shm (ARGB8888) buffer backed by a memfd mmap and a Cairo image
// surface. `busy` is cleared when the compositor releases the buffer.
class ShmBuffer {
 public:
  ShmBuffer() = default;
  ~ShmBuffer();
  ShmBuffer(const ShmBuffer&) = delete;
  ShmBuffer& operator=(const ShmBuffer&) = delete;

  bool create(wl_shm* shm, int width, int height, std::function<void()> on_release);
  wl_buffer* buffer() const { return buffer_; }
  cairo_surface_t* surface() const { return surface_; }
  unsigned char* data() const { return static_cast<unsigned char*>(map_); }
  int width() const { return width_; }
  int height() const { return height_; }
  int stride() const { return stride_; }
  bool busy() const { return busy_; }
  void mark_busy() { busy_ = true; }
  void set_on_release(std::function<void()> cb) { on_release_ = std::move(cb); }

 private:
  static void ev_release(void* data, wl_buffer* buffer);
  static const wl_buffer_listener listener_;

  wl_buffer* buffer_ = nullptr;
  cairo_surface_t* surface_ = nullptr;
  void* map_ = nullptr;
  std::size_t size_ = 0;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
  bool busy_ = false;
  Fd fd_;
  std::function<void()> on_release_;
};

// Double-buffered wl_surface target. Draw into cairo(), record damage, then
// commit(). When both buffers are busy commit() returns false and the caller
// must retry from its on_buffer_free callback.
class Canvas {
 public:
  Canvas(wl_shm* shm, wl_surface* surface);
  ~Canvas();
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;

  // Returns true when the size changed and buffers were reallocated.
  bool resize(int width, int height);
  int width() const { return width_; }
  int height() const { return height_; }

  // Begins a frame; returns the back-buffer Cairo context or nullptr when no
  // buffer is free. The context's origin is the top-left pixel.
  cairo_t* begin();
  // Records damage in buffer pixels.
  void damage(int x, int y, int w, int h);
  void damage(const Rect& rect);
  void damage_all();
  bool has_damage() const { return !damage_.empty(); }
  // Attaches the drawn buffer, applies damage and commits the surface.
  bool commit();

  void set_on_buffer_free(std::function<void()> cb) { on_buffer_free_ = std::move(cb); }

  wl_surface* surface() const { return surface_; }

 private:
  int free_index(int except = -1) const;

  wl_shm* shm_ = nullptr;
  wl_surface* surface_ = nullptr;
  std::unique_ptr<ShmBuffer> buffers_[2];
  cairo_t* ctx_ = nullptr;
  int draw_index_ = -1;
  int width_ = 0;
  int height_ = 0;
  bool full_damage_ = false;
  std::vector<Rect> damage_;
  std::function<void()> on_buffer_free_;
};

}  // namespace pillbar
