#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wayland/shm.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "app/logging.hpp"

namespace pillbar {

Rect Rect::union_with(const Rect& other) const {
  if (empty()) return other;
  if (other.empty()) return *this;
  const int x0 = std::min(x, other.x);
  const int y0 = std::min(y, other.y);
  const int x1 = std::max(x + w, other.x + other.w);
  const int y1 = std::max(y + h, other.y + other.h);
  return Rect{x0, y0, x1 - x0, y1 - y0};
}

const wl_buffer_listener ShmBuffer::listener_ = {&ShmBuffer::ev_release};

ShmBuffer::~ShmBuffer() {
  if (surface_ != nullptr) cairo_surface_destroy(surface_);
  if (map_ != nullptr) ::munmap(map_, size_);
  if (buffer_ != nullptr) wl_buffer_destroy(buffer_);
}

void ShmBuffer::ev_release(void* data, wl_buffer*) {
  auto* self = static_cast<ShmBuffer*>(data);
  self->busy_ = false;
  if (self->on_release_) self->on_release_();
}

bool ShmBuffer::create(wl_shm* shm, int width, int height, std::function<void()> on_release) {
  if (shm == nullptr || width <= 0 || height <= 0) return false;
  on_release_ = std::move(on_release);
  width_ = width;
  height_ = height;
  stride_ = width * 4;
  size_ = static_cast<std::size_t>(stride_) * static_cast<std::size_t>(height);

  const int fd = ::memfd_create("pillbar-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    LOG_ERR("memfd_create: %s", std::strerror(errno));
    return false;
  }
  fd_.reset(fd);
  if (::ftruncate(fd_.get(), static_cast<off_t>(size_)) != 0) {
    LOG_ERR("ftruncate: %s", std::strerror(errno));
    return false;
  }
  map_ = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.get(), 0);
  if (map_ == MAP_FAILED) {
    map_ = nullptr;
    LOG_ERR("mmap: %s", std::strerror(errno));
    return false;
  }
  wl_shm_pool* pool = wl_shm_create_pool(shm, fd_.get(), static_cast<int32_t>(size_));
  buffer_ = wl_shm_pool_create_buffer(pool, 0, width_, height_, stride_, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  if (buffer_ == nullptr) {
    LOG_ERR("wl_shm_pool_create_buffer failed");
    return false;
  }
  wl_buffer_add_listener(buffer_, &listener_, this);
  surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>(map_),
                                                 CAIRO_FORMAT_ARGB32, width_, height_, stride_);
  if (cairo_surface_status(surface_) != CAIRO_STATUS_SUCCESS) {
    LOG_ERR("cairo_image_surface_create_for_data failed");
    return false;
  }
  return true;
}

Canvas::Canvas(wl_shm* shm, wl_surface* surface) : shm_(shm), surface_(surface) {}

Canvas::~Canvas() = default;

int Canvas::free_index(int except) const {
  for (int i = 0; i < 2; ++i) {
    if (i == except) continue;
    if (buffers_[i] != nullptr && !buffers_[i]->busy()) return i;
  }
  return -1;
}

bool Canvas::resize(int width, int height) {
  if (width <= 0 || height <= 0) return false;
  if (width == width_ && height == height_ && buffers_[0] != nullptr &&
      buffers_[1] != nullptr) {
    return false;
  }
  if (ctx_ != nullptr) {
    cairo_destroy(ctx_);
    ctx_ = nullptr;
    draw_index_ = -1;
  }
  width_ = width;
  height_ = height;
  for (int i = 0; i < 2; ++i) {
    buffers_[i] = std::make_unique<ShmBuffer>();
    buffers_[i]->set_on_release(on_buffer_free_);
    if (!buffers_[i]->create(shm_, width_, height_, [this]() {
          if (on_buffer_free_) on_buffer_free_();
        })) {
      LOG_ERR("failed to allocate shm buffer %d (%dx%d)", i, width_, height_);
    }
  }
  full_damage_ = true;
  damage_.clear();
  return true;
}

cairo_t* Canvas::begin() {
  if (ctx_ != nullptr) {
    cairo_destroy(ctx_);
    ctx_ = nullptr;
    draw_index_ = -1;
  }
  draw_index_ = free_index();
  if (draw_index_ < 0) return nullptr;
  ctx_ = cairo_create(buffers_[draw_index_]->surface());
  return ctx_;
}

void Canvas::damage(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  damage_.push_back(Rect{x, y, w, h});
}

void Canvas::damage(const Rect& rect) {
  if (!rect.empty()) damage_.push_back(rect);
}

void Canvas::damage_all() { full_damage_ = true; }

bool Canvas::commit() {
  if (draw_index_ < 0) return false;
  ShmBuffer* buffer = buffers_[draw_index_].get();
  if (buffer == nullptr || buffer->buffer() == nullptr) return false;
  if (ctx_ != nullptr) {
    cairo_destroy(ctx_);
    ctx_ = nullptr;
  }
  cairo_surface_flush(buffer->surface());
  wl_surface_attach(surface_, buffer->buffer(), 0, 0);
  if (full_damage_) {
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
  } else {
    for (const Rect& rect : damage_) {
      wl_surface_damage_buffer(surface_, rect.x, rect.y, rect.w, rect.h);
    }
  }
  wl_surface_commit(surface_);
  buffer->mark_busy();
  draw_index_ = -1;
  full_damage_ = false;
  damage_.clear();
  return true;
}

}  // namespace pillbar
