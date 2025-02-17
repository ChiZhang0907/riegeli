// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RIEGELI_BYTES_CORD_BACKWARD_WRITER_H_
#define RIEGELI_BYTES_CORD_BACKWARD_WRITER_H_

#include <stddef.h>

#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/strings/cord.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/buffer.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/bytes/backward_writer.h"

namespace riegeli {

// Template parameter independent part of `CordBackwardWriter`.
class CordBackwardWriterBase : public BackwardWriter {
 public:
  class Options {
   public:
    Options() noexcept {}

    // If `true`, prepends to existing contents of the destination.
    //
    // If `false`, replaces existing contents of the destination, clearing it
    // first.
    //
    // Default: `false`.
    Options& set_prepend(bool prepend) & {
      prepend_ = prepend;
      return *this;
    }
    Options&& set_prepend(bool prepend) && {
      return std::move(set_prepend(prepend));
    }
    bool prepend() const { return prepend_; }

    // Expected final size, or `absl::nullopt` if unknown. This may improve
    // performance and memory usage.
    //
    // If the size hint turns out to not match reality, nothing breaks.
    //
    // Default: `absl::nullopt`.
    Options& set_size_hint(absl::optional<Position> size_hint) & {
      size_hint_ = size_hint;
      return *this;
    }
    Options&& set_size_hint(absl::optional<Position> size_hint) && {
      return std::move(set_size_hint(size_hint));
    }
    absl::optional<Position> size_hint() const { return size_hint_; }

    // Minimal size of a block of allocated data.
    //
    // This is used initially, while the destination is small.
    //
    // Default: `kMinBufferSize` (256).
    Options& set_min_block_size(size_t min_block_size) & {
      min_block_size_ = min_block_size;
      return *this;
    }
    Options&& set_min_block_size(size_t min_block_size) && {
      return std::move(set_min_block_size(min_block_size));
    }
    size_t min_block_size() const { return min_block_size_; }

    // Maximal size of a block of allocated data.
    //
    // This is for performance tuning, not a guarantee: does not apply to
    // objects allocated separately and then written to this
    // `CordBackwardWriter`.
    //
    // Default: `kMaxBufferSize` (64K).
    Options& set_max_block_size(size_t max_block_size) & {
      RIEGELI_ASSERT_GT(max_block_size, 0u)
          << "Failed precondition of "
             "CordBackwardWriterBase::Options::set_max_block_size(): "
             "zero block size";
      max_block_size_ = max_block_size;
      return *this;
    }
    Options&& set_max_block_size(size_t max_block_size) && {
      return std::move(set_max_block_size(max_block_size));
    }
    size_t max_block_size() const { return max_block_size_; }

   private:
    bool prepend_ = false;
    absl::optional<Position> size_hint_;
    size_t min_block_size_ = kMinBufferSize;
    size_t max_block_size_ = kMaxBufferSize;
  };

  // Returns the `absl::Cord` being written to. Unchanged by `Close()`.
  virtual absl::Cord* dest_cord() = 0;
  virtual const absl::Cord* dest_cord() const = 0;

  bool SupportsTruncate() override { return true; }

 protected:
  CordBackwardWriterBase() noexcept : BackwardWriter(kInitiallyClosed) {}

  explicit CordBackwardWriterBase(const Options& options);

  CordBackwardWriterBase(CordBackwardWriterBase&& that) noexcept;
  CordBackwardWriterBase& operator=(CordBackwardWriterBase&& that) noexcept;

  void Reset();
  void Reset(const Options& options);
  void Initialize(absl::Cord* dest, bool prepend);

  void Done() override;
  bool PushSlow(size_t min_length, size_t recommended_length) override;
  using BackwardWriter::WriteSlow;
  bool WriteSlow(const Chain& src) override;
  bool WriteSlow(Chain&& src) override;
  bool WriteSlow(const absl::Cord& src) override;
  bool WriteSlow(absl::Cord&& src) override;
  bool WriteZerosSlow(Position length) override;
  bool FlushImpl(FlushType flush_type) override;
  bool TruncateImpl(Position new_size) override;

 private:
  static constexpr size_t kShortBufferSize = 64;

  // If the buffer is not empty, prepends it to `dest`.
  void SyncBuffer(absl::Cord& dest);

  size_t size_hint_ = 0;
  size_t min_block_size_ = kMinBufferSize;
  size_t max_block_size_ = kMaxBufferSize;

  // Buffered data to be prepended, in either `buffer_` or `short_buffer_`.
  Buffer buffer_;
  char short_buffer_[kShortBufferSize];

  // Invariants:
  //   `limit() == nullptr` or `limit() == buffer_.data()`
  //       or `limit() == short_buffer_`
  //   if `healthy()` then `start_pos() == dest_cord()->size()`
};

// A `Writer` which prepends to an `absl::Cord`.
//
// The `Dest` template parameter specifies the type of the object providing and
// possibly owning the `absl::Cord` being written to. `Dest` must support
// `Dependency<absl::Cord*, Dest>`, e.g. `absl::Cord*` (not owned, default),
// `absl::Cord` (owned).
//
// By relying on CTAD the template argument can be deduced as the value type of
// the first constructor argument, except that CTAD is deleted if the first
// constructor argument is an `absl::Cord&` or `const absl::Cord&` (to avoid
// writing to an unintentionally separate copy of an existing object). This
// requires C++17.
//
// The `absl::Cord` must not be accessed until the `CordBackwardWriter` is
// closed or no longer used.
template <typename Dest = absl::Cord*>
class CordBackwardWriter : public CordBackwardWriterBase {
 public:
  // Creates a closed `CordBackwardWriter`.
  CordBackwardWriter() noexcept {}

  // Will prepend to the `absl::Cord` provided by `dest`.
  explicit CordBackwardWriter(const Dest& dest, Options options = Options());
  explicit CordBackwardWriter(Dest&& dest, Options options = Options());

  // Will prepend to the `absl::Cord` provided by a `Dest` constructed from
  // elements of `dest_args`. This avoids constructing a temporary `Dest` and
  // moving from it.
  template <typename... DestArgs>
  explicit CordBackwardWriter(std::tuple<DestArgs...> dest_args,
                              Options options = Options());

  CordBackwardWriter(CordBackwardWriter&& that) noexcept;
  CordBackwardWriter& operator=(CordBackwardWriter&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `CordBackwardWriter`. This
  // avoids constructing a temporary `CordBackwardWriter` and moving from it.
  void Reset();
  void Reset(const Dest& dest, Options options = Options());
  void Reset(Dest&& dest, Options options = Options());
  template <typename... DestArgs>
  void Reset(std::tuple<DestArgs...> dest_args, Options options = Options());

  // Returns the object providing and possibly owning the `absl::Cord` being
  // written to. Unchanged by `Close()`.
  Dest& dest() { return dest_.manager(); }
  const Dest& dest() const { return dest_.manager(); }
  absl::Cord* dest_cord() override { return dest_.get(); }
  const absl::Cord* dest_cord() const override { return dest_.get(); }

 private:
  // The object providing and possibly owning the `absl::Cord` being written to.
  Dependency<absl::Cord*, Dest> dest_;
};

// Support CTAD.
#if __cpp_deduction_guides
CordBackwardWriter()->CordBackwardWriter<DeleteCtad<>>;
template <typename Dest>
explicit CordBackwardWriter(
    const Dest& dest,
    CordBackwardWriterBase::Options options = CordBackwardWriterBase::Options())
    -> CordBackwardWriter<std::conditional_t<
        std::is_convertible<const Dest*, const absl::Cord*>::value,
        DeleteCtad<const Dest&>, std::decay_t<Dest>>>;
template <typename Dest>
explicit CordBackwardWriter(
    Dest&& dest,
    CordBackwardWriterBase::Options options = CordBackwardWriterBase::Options())
    -> CordBackwardWriter<std::conditional_t<
        std::is_lvalue_reference<Dest>::value &&
            std::is_convertible<std::remove_reference_t<Dest>*,
                                const absl::Cord*>::value,
        DeleteCtad<Dest&&>, std::decay_t<Dest>>>;
template <typename... DestArgs>
explicit CordBackwardWriter(
    std::tuple<DestArgs...> dest_args,
    CordBackwardWriterBase::Options options = CordBackwardWriterBase::Options())
    -> CordBackwardWriter<DeleteCtad<std::tuple<DestArgs...>>>;
#endif

// Implementation details follow.

inline CordBackwardWriterBase::CordBackwardWriterBase(const Options& options)
    : BackwardWriter(kInitiallyOpen),
      size_hint_(SaturatingIntCast<size_t>(options.size_hint().value_or(0))),
      min_block_size_(options.min_block_size()),
      max_block_size_(options.max_block_size()) {}

inline CordBackwardWriterBase::CordBackwardWriterBase(
    CordBackwardWriterBase&& that) noexcept
    : BackwardWriter(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      size_hint_(that.size_hint_),
      min_block_size_(that.min_block_size_),
      max_block_size_(that.max_block_size_),
      buffer_(std::move(that.buffer_)) {
  if (limit() == that.short_buffer_) {
    std::memcpy(short_buffer_, that.short_buffer_, kShortBufferSize);
    set_buffer(short_buffer_, buffer_size(), written_to_buffer());
  }
}

inline CordBackwardWriterBase& CordBackwardWriterBase::operator=(
    CordBackwardWriterBase&& that) noexcept {
  BackwardWriter::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  size_hint_ = that.size_hint_;
  min_block_size_ = that.min_block_size_;
  max_block_size_ = that.max_block_size_;
  buffer_ = std::move(that.buffer_);
  if (limit() == that.short_buffer_) {
    std::memcpy(short_buffer_, that.short_buffer_, kShortBufferSize);
    set_buffer(short_buffer_, buffer_size(), written_to_buffer());
  }
  return *this;
}

inline void CordBackwardWriterBase::Reset() {
  BackwardWriter::Reset(kInitiallyClosed);
  size_hint_ = 0;
  min_block_size_ = kMinBufferSize;
  max_block_size_ = kMaxBufferSize;
}

inline void CordBackwardWriterBase::Reset(const Options& options) {
  BackwardWriter::Reset(kInitiallyOpen);
  size_hint_ = SaturatingIntCast<size_t>(options.size_hint().value_or(0));
  min_block_size_ = options.min_block_size();
  max_block_size_ = options.max_block_size();
}

inline void CordBackwardWriterBase::Initialize(absl::Cord* dest, bool prepend) {
  RIEGELI_ASSERT(dest != nullptr)
      << "Failed precondition of CordBackwardWriter: null Cord pointer";
  if (prepend) {
    set_start_pos(dest->size());
    const size_t buffer_length = UnsignedMin(
        kShortBufferSize, std::numeric_limits<size_t>::max() - dest->size());
    if (size_hint_ <= dest->size() + buffer_length) {
      set_buffer(short_buffer_, buffer_length);
    }
  } else {
    dest->Clear();
    if (size_hint_ <= kShortBufferSize) {
      set_buffer(short_buffer_, kShortBufferSize);
    }
  }
}

template <typename Dest>
inline CordBackwardWriter<Dest>::CordBackwardWriter(const Dest& dest,
                                                    Options options)
    : CordBackwardWriterBase(options), dest_(dest) {
  Initialize(dest_.get(), options.prepend());
}

template <typename Dest>
inline CordBackwardWriter<Dest>::CordBackwardWriter(Dest&& dest,
                                                    Options options)
    : CordBackwardWriterBase(options), dest_(std::move(dest)) {
  Initialize(dest_.get(), options.prepend());
}

template <typename Dest>
template <typename... DestArgs>
inline CordBackwardWriter<Dest>::CordBackwardWriter(
    std::tuple<DestArgs...> dest_args, Options options)
    : CordBackwardWriterBase(options), dest_(std::move(dest_args)) {
  Initialize(dest_.get(), options.prepend());
}

template <typename Dest>
inline CordBackwardWriter<Dest>::CordBackwardWriter(
    CordBackwardWriter&& that) noexcept
    : CordBackwardWriterBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      dest_(std::move(that.dest_)) {}

template <typename Dest>
inline CordBackwardWriter<Dest>& CordBackwardWriter<Dest>::operator=(
    CordBackwardWriter&& that) noexcept {
  CordBackwardWriterBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  dest_ = std::move(that.dest_);
  return *this;
}

template <typename Dest>
inline void CordBackwardWriter<Dest>::Reset() {
  CordBackwardWriterBase::Reset();
  dest_.Reset();
}

template <typename Dest>
inline void CordBackwardWriter<Dest>::Reset(const Dest& dest, Options options) {
  CordBackwardWriterBase::Reset(options);
  dest_.Reset(dest);
  Initialize(dest_.get(), options.prepend());
}

template <typename Dest>
inline void CordBackwardWriter<Dest>::Reset(Dest&& dest, Options options) {
  CordBackwardWriterBase::Reset(options);
  dest_.Reset(std::move(dest));
  Initialize(dest_.get(), options.prepend());
}

template <typename Dest>
template <typename... DestArgs>
inline void CordBackwardWriter<Dest>::Reset(std::tuple<DestArgs...> dest_args,
                                            Options options) {
  CordBackwardWriterBase::Reset(options);
  dest_.Reset(std::move(dest_args));
  Initialize(dest_.get(), options.prepend());
}

}  // namespace riegeli

#endif  // RIEGELI_BYTES_CORD_BACKWARD_WRITER_H_
