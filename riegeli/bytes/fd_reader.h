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

#ifndef RIEGELI_BYTES_FD_READER_H_
#define RIEGELI_BYTES_FD_READER_H_

#include <fcntl.h>
#include <stddef.h>

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/bytes/buffered_reader.h"
#include "riegeli/bytes/chain_reader.h"
#include "riegeli/bytes/fd_dependency.h"

namespace riegeli {

// Template parameter independent part of `FdReader`.
class FdReaderBase : public BufferedReader {
 public:
  class Options {
   public:
    Options() noexcept {}

    // If `absl::nullopt`, the current position reported by `pos()` corresponds
    // to the current fd position if possible, otherwise 0 is assumed as the
    // initial position. Random access is supported if the fd supports random
    // access.
    //
    // If not `absl::nullopt`, this position is assumed initially, to be
    // reported by `pos()`. It does not need to correspond to the current fd
    // position. Random access is not supported.
    //
    // `assumed_pos()` and `independent_pos()` must not be both set.
    //
    // Default: `absl::nullopt`.
    Options& set_assumed_pos(absl::optional<Position> assumed_pos) & {
      assumed_pos_ = assumed_pos;
      return *this;
    }
    Options&& set_assumed_pos(absl::optional<Position> assumed_pos) && {
      return std::move(set_assumed_pos(assumed_pos));
    }
    absl::optional<Position> assumed_pos() const { return assumed_pos_; }

    // If `absl::nullopt`, `FdReader` reads at the current fd position.
    //
    // If not `absl::nullopt`, `FdReader` reads starting from this position,
    // without disturbing the current fd position. This is useful for multiple
    // readers concurrently reading from the same fd. The fd must support
    // `pread()`.
    //
    // `assumed_pos()` and `independent_pos()` must not be both set.
    //
    // Default: `absl::nullopt`.
    Options& set_independent_pos(absl::optional<Position> independent_pos) & {
      independent_pos_ = independent_pos;
      return *this;
    }
    Options&& set_independent_pos(absl::optional<Position> independent_pos) && {
      return std::move(set_independent_pos(independent_pos));
    }
    absl::optional<Position> independent_pos() const {
      return independent_pos_;
    }

    // Tunes how much data is buffered after reading from the file.
    //
    // Default: `kDefaultBufferSize` (64K).
    Options& set_buffer_size(size_t buffer_size) & {
      RIEGELI_ASSERT_GT(buffer_size, 0u)
          << "Failed precondition of FdReaderBase::Options::set_buffer_size(): "
             "zero buffer size";
      buffer_size_ = buffer_size;
      return *this;
    }
    Options&& set_buffer_size(size_t buffer_size) && {
      return std::move(set_buffer_size(buffer_size));
    }
    size_t buffer_size() const { return buffer_size_; }

   private:
    absl::optional<Position> assumed_pos_;
    absl::optional<Position> independent_pos_;
    size_t buffer_size_ = kDefaultBufferSize;
  };

  // Returns the fd being read from. If the fd is owned then changed to -1 by
  // `Close()`, otherwise unchanged.
  virtual int src_fd() const = 0;

  // Returns the original name of the file being read from (or "/dev/stdin" or
  // "/proc/self/fd/<fd>" if fd was given). Unchanged by `Close()`.
  const std::string& filename() const { return filename_; }

  bool SupportsRandomAccess() override { return supports_random_access_; }

 protected:
  FdReaderBase() noexcept {}

  explicit FdReaderBase(size_t buffer_size);

  FdReaderBase(FdReaderBase&& that) noexcept;
  FdReaderBase& operator=(FdReaderBase&& that) noexcept;

  void Reset();
  void Reset(size_t buffer_size);
  void Initialize(int src, absl::optional<Position> assumed_pos,
                  absl::optional<Position> independent_pos);
  int OpenFd(absl::string_view filename, int flags);
  void InitializePos(int src, absl::optional<Position> assumed_pos,
                     absl::optional<Position> independent_pos);
  ABSL_ATTRIBUTE_COLD bool FailOperation(absl::string_view operation);

  void AnnotateFailure(absl::Status& status) override;
  bool ReadInternal(size_t min_length, size_t max_length, char* dest) override;
  bool SeekBehindBuffer(Position new_pos) override;
  absl::optional<Position> SizeImpl() override;

 private:
  void SetFilename(int src);
  bool SeekInternal(int dest, Position new_pos);

  std::string filename_;
  bool supports_random_access_ = false;
  bool has_independent_pos_ = false;

  // Invariant: `limit_pos() <= std::numeric_limits<off_t>::max()`
};

// Template parameter independent part of `FdMMapReader`.
class FdMMapReaderBase : public ChainReader<Chain> {
 public:
  class Options {
   public:
    Options() noexcept {}

    // If `absl::nullopt`, `FdMMapReader` reads starting from the current fd
    // position. The `FdMMapReader` position is synchronized back to the fd by
    // `Close()` and `Sync()`.
    //
    // If not `absl::nullopt`, `FdMMapReader` reads starting from this position,
    // without disturbing the current fd position. This is useful for multiple
    // readers concurrently reading from the same fd.
    //
    // Default: `absl::nullopt`.
    Options& set_independent_pos(absl::optional<Position> independent_pos) & {
      independent_pos_ = independent_pos;
      return *this;
    }
    Options&& set_independent_pos(absl::optional<Position> independent_pos) && {
      return std::move(set_independent_pos(independent_pos));
    }
    absl::optional<Position> independent_pos() const {
      return independent_pos_;
    }

   private:
    absl::optional<Position> independent_pos_;
  };

  // Returns the fd being read from. If the fd is owned then changed to -1 by
  // `Close()`, otherwise unchanged.
  virtual int src_fd() const = 0;

  // Returns the original name of the file being read from (or "/dev/stdin" or
  // "/proc/self/fd/<fd>" if fd was given). Unchanged by `Close()`.
  const std::string& filename() const { return filename_; }

  void AnnotateFailure(absl::Status& status) override;

 protected:
  FdMMapReaderBase() noexcept {}

  explicit FdMMapReaderBase(bool has_independent_pos);

  FdMMapReaderBase(FdMMapReaderBase&& that) noexcept;
  FdMMapReaderBase& operator=(FdMMapReaderBase&& that) noexcept;

  void Reset();
  void Reset(bool has_independent_pos);
  void Initialize(int src, absl::optional<Position> independent_pos);
  int OpenFd(absl::string_view filename, int flags);
  void InitializePos(int src, absl::optional<Position> independent_pos);
  ABSL_ATTRIBUTE_COLD bool FailOperation(absl::string_view operation);

  void Done() override;
  bool SyncImpl(SyncType sync_type) override;

 private:
  void SetFilename(int src);

  std::string filename_;
  bool has_independent_pos_ = false;
};

// A `Reader` which reads from a file descriptor.
//
// The fd must support:
//  * `close()` - if the fd is owned
//  * `read()`  - if `Options::independent_pos() == absl::nullopt`
//  * `pread()` - if `Options::independent_pos() != absl::nullopt`
//  * `lseek()` - for `Seek()` or `Size()`
//                if `Options::independent_pos() == absl::nullopt`
//  * `fstat()` - for `Seek()` or `Size()`
//
// `FdReader` supports random access if
// `Options::assumed_pos() == absl::nullopt` and the fd supports random access
// (this is assumed if `Options::independent_pos() != absl::nullopt`, otherwise
// this is checked by calling `lseek()`).
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the fd being read from. `Src` must support
// `Dependency<int, Src>`, e.g. `OwnedFd` (owned, default), `UnownedFd`
// (not owned).
//
// By relying on CTAD the template argument can be deduced as `OwnedFd` if the
// first constructor argument is a filename or an `int`, otherwise as the value
// type of the first constructor argument. This requires C++17.
//
// Warning: if random access is not supported and the fd is not owned, it will
// have an unpredictable amount of extra data consumed because of buffering.
//
// Until the `FdReader` is closed or no longer used, the fd must not be closed;
// additionally, if `Options::independent_pos() == absl::nullopt`, the fd must
// not have its position changed.
template <typename Src = OwnedFd>
class FdReader : public FdReaderBase {
 public:
  // Creates a closed `FdReader`.
  FdReader() noexcept {}

  // Will read from the fd provided by `src`.
  explicit FdReader(const Src& src, Options options = Options());
  explicit FdReader(Src&& src, Options options = Options());

  // Will read from the fd provided by a `Src` constructed from elements of
  // `src_args`. This avoids constructing a temporary `Src` and moving from it.
  template <typename... SrcArgs>
  explicit FdReader(std::tuple<SrcArgs...> src_args,
                    Options options = Options());

  // Opens a file for reading.
  //
  // `flags` is the second argument of `open()`, typically `O_RDONLY`.
  //
  // `flags` must include either `O_RDONLY` or `O_RDWR`.
  //
  // If opening the file fails, `FdReader` will be failed and closed.
  explicit FdReader(absl::string_view filename, int flags,
                    Options options = Options());

  FdReader(FdReader&& that) noexcept;
  FdReader& operator=(FdReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `FdReader`. This avoids
  // constructing a temporary `FdReader` and moving from it.
  void Reset();
  void Reset(const Src& src, Options options = Options());
  void Reset(Src&& src, Options options = Options());
  template <typename... SrcArgs>
  void Reset(std::tuple<SrcArgs...> src_args, Options options = Options());
  void Reset(absl::string_view filename, int flags,
             Options options = Options());

  // Returns the object providing and possibly owning the fd being read from. If
  // the fd is owned then changed to -1 by `Close()`, otherwise unchanged.
  Src& src() { return src_.manager(); }
  const Src& src() const { return src_.manager(); }
  int src_fd() const override { return src_.get(); }

 protected:
  void Done() override;

 private:
  using FdReaderBase::Initialize;
  void Initialize(absl::string_view filename, int flags, Options&& options);

  // The object providing and possibly owning the fd being read from.
  Dependency<int, Src> src_;
};

// Support CTAD.
#if __cpp_deduction_guides
FdReader()->FdReader<DeleteCtad<>>;
template <typename Src>
explicit FdReader(const Src& src,
                  FdReaderBase::Options options = FdReaderBase::Options())
    -> FdReader<std::conditional_t<std::is_convertible<const Src&, int>::value,
                                   OwnedFd, std::decay_t<Src>>>;
template <typename Src>
explicit FdReader(Src&& src,
                  FdReaderBase::Options options = FdReaderBase::Options())
    -> FdReader<std::conditional_t<std::is_convertible<Src&&, int>::value,
                                   OwnedFd, std::decay_t<Src>>>;
template <typename... SrcArgs>
explicit FdReader(std::tuple<SrcArgs...> src_args,
                  FdReaderBase::Options options = FdReaderBase::Options())
    -> FdReader<DeleteCtad<std::tuple<SrcArgs...>>>;
explicit FdReader(absl::string_view filename, int flags,
                  FdReaderBase::Options options = FdReaderBase::Options())
    ->FdReader<>;
#endif

// A `Reader` which reads from a file descriptor by mapping the whole file to
// memory. It supports random access.
//
// The fd must support:
//  * `close()` - if the fd is owned
//  * `fstat()`
//  * `mmap()`
//  * `lseek()` - if `Options::independent_pos() == absl::nullopt`
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the fd being read from. `Src` must support
// `Dependency<int, Src>`, e.g. `OwnedFd` (owned, default), `UnownedFd`
// (not owned).
//
// By relying on CTAD the template argument can be deduced as `OwnedFd` if the
// first constructor argument is a filename or an `int`, otherwise as the value
// type of the first constructor argument. This requires C++17.
//
// The fd must not be closed until the `FdMMapReader` is closed or no longer
// used. `File` contents must not be changed while data read from the file is
// accessed without a memory copy.
template <typename Src = OwnedFd>
class FdMMapReader : public FdMMapReaderBase {
 public:
  // Creates a closed `FdMMapReader`.
  FdMMapReader() noexcept {}

  // Will read from the fd provided by `src`.
  explicit FdMMapReader(const Src& src, Options options = Options());
  explicit FdMMapReader(Src&& src, Options options = Options());

  // Will read from the fd provided by a `Src` constructed from elements of
  // `src_args`. This avoids constructing a temporary `Src` and moving from it.
  template <typename... SrcArgs>
  explicit FdMMapReader(std::tuple<SrcArgs...> src_args,
                        Options options = Options());

  // Opens a file for reading.
  //
  // `flags` is the second argument of `open()`, typically `O_RDONLY`.
  //
  // `flags` must include either `O_RDONLY` or `O_RDWR`.
  //
  // If opening the file fails, `FdMMapReader` will be failed and closed.
  explicit FdMMapReader(absl::string_view filename, int flags,
                        Options options = Options());

  FdMMapReader(FdMMapReader&& that) noexcept;
  FdMMapReader& operator=(FdMMapReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `FdMMapReader`. This avoids
  // constructing a temporary `FdMMapReader` and moving from it.
  void Reset();
  void Reset(const Src& src, Options options = Options());
  void Reset(Src&& src, Options options = Options());
  template <typename... SrcArgs>
  void Reset(std::tuple<SrcArgs...> src_args, Options options = Options());
  void Reset(absl::string_view filename, int flags,
             Options options = Options());

  // Returns the object providing and possibly owning the fd being read from. If
  // the fd is owned then changed to -1 by `Close()`, otherwise unchanged.
  Src& src() { return src_.manager(); }
  const Src& src() const { return src_.manager(); }
  int src_fd() const override { return src_.get(); }

 protected:
  void Done() override;

 private:
  using FdMMapReaderBase::Initialize;
  void Initialize(absl::string_view filename, int flags, Options&& options);

  // The object providing and possibly owning the fd being read from.
  Dependency<int, Src> src_;
};

// Support CTAD.
#if __cpp_deduction_guides
FdMMapReader()->FdMMapReader<DeleteCtad<>>;
template <typename Src>
explicit FdMMapReader(const Src& src, FdMMapReaderBase::Options options =
                                          FdMMapReaderBase::Options())
    -> FdMMapReader<
        std::conditional_t<std::is_convertible<const Src&, int>::value, OwnedFd,
                           std::decay_t<Src>>>;
template <typename Src>
explicit FdMMapReader(
    Src&& src, FdMMapReaderBase::Options options = FdMMapReaderBase::Options())
    -> FdMMapReader<std::conditional_t<std::is_convertible<Src&&, int>::value,
                                       OwnedFd, std::decay_t<Src>>>;
template <typename... SrcArgs>
explicit FdMMapReader(
    std::tuple<SrcArgs...> src_args,
    FdMMapReaderBase::Options options = FdMMapReaderBase::Options())
    -> FdMMapReader<DeleteCtad<std::tuple<SrcArgs...>>>;
explicit FdMMapReader(
    absl::string_view filename, int flags,
    FdMMapReaderBase::Options options = FdMMapReaderBase::Options())
    ->FdMMapReader<>;
#endif

// Implementation details follow.

inline FdReaderBase::FdReaderBase(size_t buffer_size)
    : BufferedReader(buffer_size) {}

inline FdReaderBase::FdReaderBase(FdReaderBase&& that) noexcept
    : BufferedReader(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      filename_(std::move(that.filename_)),
      supports_random_access_(that.supports_random_access_),
      has_independent_pos_(that.has_independent_pos_) {}

inline FdReaderBase& FdReaderBase::operator=(FdReaderBase&& that) noexcept {
  BufferedReader::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  filename_ = std::move(that.filename_);
  supports_random_access_ = that.supports_random_access_;
  has_independent_pos_ = that.has_independent_pos_;
  return *this;
}

inline void FdReaderBase::Reset() {
  BufferedReader::Reset();
  filename_.clear();
  supports_random_access_ = false;
  has_independent_pos_ = false;
}

inline void FdReaderBase::Reset(size_t buffer_size) {
  BufferedReader::Reset(buffer_size);
  // `filename_` was set by `OpenFd()` or will be set by `Initialize()`.
  supports_random_access_ = false;
  has_independent_pos_ = false;
}

inline FdMMapReaderBase::FdMMapReaderBase(bool has_independent_pos)
    // Empty `Chain` as the `ChainReader` source is a placeholder, it will be
    // set by `Initialize()`.
    : ChainReader(std::forward_as_tuple()),
      has_independent_pos_(has_independent_pos) {}

inline FdMMapReaderBase::FdMMapReaderBase(FdMMapReaderBase&& that) noexcept
    : ChainReader(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      filename_(std::move(that.filename_)),
      has_independent_pos_(that.has_independent_pos_) {}

inline FdMMapReaderBase& FdMMapReaderBase::operator=(
    FdMMapReaderBase&& that) noexcept {
  ChainReader::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  filename_ = std::move(that.filename_);
  has_independent_pos_ = that.has_independent_pos_;
  return *this;
}

inline void FdMMapReaderBase::Reset() {
  ChainReader::Reset();
  filename_.clear();
  has_independent_pos_ = false;
}

inline void FdMMapReaderBase::Reset(bool has_independent_pos) {
  // Empty `Chain` as the `ChainReader` source is a placeholder, it will be set
  // by `Initialize()`.
  ChainReader::Reset(std::forward_as_tuple());
  // `filename_` was set by `OpenFd()` or will be set by `Initialize()`.
  has_independent_pos_ = has_independent_pos;
}

template <typename Src>
inline FdReader<Src>::FdReader(const Src& src, Options options)
    : FdReaderBase(options.buffer_size()), src_(src) {
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
inline FdReader<Src>::FdReader(Src&& src, Options options)
    : FdReaderBase(options.buffer_size()), src_(std::move(src)) {
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
template <typename... SrcArgs>
inline FdReader<Src>::FdReader(std::tuple<SrcArgs...> src_args, Options options)
    : FdReaderBase(options.buffer_size()), src_(std::move(src_args)) {
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
inline FdReader<Src>::FdReader(absl::string_view filename, int flags,
                               Options options) {
  Initialize(filename, flags, std::move(options));
}

template <typename Src>
inline FdReader<Src>::FdReader(FdReader&& that) noexcept
    : FdReaderBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      src_(std::move(that.src_)) {}

template <typename Src>
inline FdReader<Src>& FdReader<Src>::operator=(FdReader&& that) noexcept {
  FdReaderBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  src_ = std::move(that.src_);
  return *this;
}

template <typename Src>
inline void FdReader<Src>::Reset() {
  FdReaderBase::Reset();
  src_.Reset();
}

template <typename Src>
inline void FdReader<Src>::Reset(const Src& src, Options options) {
  FdReaderBase::Reset(options.buffer_size());
  src_.Reset(src);
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
inline void FdReader<Src>::Reset(Src&& src, Options options) {
  FdReaderBase::Reset(options.buffer_size());
  src_.Reset(std::move(src));
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
template <typename... SrcArgs>
inline void FdReader<Src>::Reset(std::tuple<SrcArgs...> src_args,
                                 Options options) {
  FdReaderBase::Reset(options.buffer_size());
  src_.Reset(std::move(src_args));
  Initialize(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
inline void FdReader<Src>::Reset(absl::string_view filename, int flags,
                                 Options options) {
  Reset();
  Initialize(filename, flags, std::move(options));
}

template <typename Src>
void FdReader<Src>::Initialize(absl::string_view filename, int flags,
                               Options&& options) {
  const int src = OpenFd(filename, flags);
  if (ABSL_PREDICT_FALSE(src < 0)) return;
  FdReaderBase::Reset(options.buffer_size());
  src_.Reset(std::forward_as_tuple(src));
  InitializePos(src_.get(), options.assumed_pos(), options.independent_pos());
}

template <typename Src>
void FdReader<Src>::Done() {
  FdReaderBase::Done();
  if (src_.is_owning()) {
    const int src = src_.Release();
    if (ABSL_PREDICT_FALSE(internal::CloseFd(src) < 0) &&
        ABSL_PREDICT_TRUE(healthy())) {
      FailOperation(internal::kCloseFunctionName);
    }
  }
}

template <typename Src>
inline FdMMapReader<Src>::FdMMapReader(const Src& src, Options options)
    : FdMMapReaderBase(options.independent_pos() != absl::nullopt), src_(src) {
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
inline FdMMapReader<Src>::FdMMapReader(Src&& src, Options options)
    : FdMMapReaderBase(options.independent_pos() != absl::nullopt),
      src_(std::move(src)) {
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
template <typename... SrcArgs>
inline FdMMapReader<Src>::FdMMapReader(std::tuple<SrcArgs...> src_args,
                                       Options options)
    : FdMMapReaderBase(options.independent_pos() != absl::nullopt),
      src_(std::move(src_args)) {
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
inline FdMMapReader<Src>::FdMMapReader(absl::string_view filename, int flags,
                                       Options options) {
  Initialize(filename, flags, std::move(options));
}

template <typename Src>
inline FdMMapReader<Src>::FdMMapReader(FdMMapReader&& that) noexcept
    : FdMMapReaderBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      src_(std::move(that.src_)) {}

template <typename Src>
inline FdMMapReader<Src>& FdMMapReader<Src>::operator=(
    FdMMapReader&& that) noexcept {
  FdMMapReaderBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  src_ = std::move(that.src_);
  return *this;
}

template <typename Src>
inline void FdMMapReader<Src>::Reset() {
  FdMMapReaderBase::Reset();
  src_.Reset();
}

template <typename Src>
inline void FdMMapReader<Src>::Reset(const Src& src, Options options) {
  FdMMapReaderBase::Reset(options.independent_pos() != absl::nullopt);
  src_.Reset(src);
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
inline void FdMMapReader<Src>::Reset(Src&& src, Options options) {
  FdMMapReaderBase::Reset(options.independent_pos() != absl::nullopt);
  src_.Reset(std::move(src));
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
template <typename... SrcArgs>
inline void FdMMapReader<Src>::Reset(std::tuple<SrcArgs...> src_args,
                                     Options options) {
  FdMMapReaderBase::Reset(options.independent_pos() != absl::nullopt);
  src_.Reset(std::move(src_args));
  Initialize(src_.get(), options.independent_pos());
}

template <typename Src>
inline void FdMMapReader<Src>::Reset(absl::string_view filename, int flags,
                                     Options options) {
  Reset();
  Initialize(filename, flags, std::move(options));
}

template <typename Src>
void FdMMapReader<Src>::Initialize(absl::string_view filename, int flags,
                                   Options&& options) {
  const int src = OpenFd(filename, flags);
  if (ABSL_PREDICT_FALSE(src < 0)) return;
  FdMMapReaderBase::Reset(options.independent_pos() != absl::nullopt);
  src_.Reset(std::forward_as_tuple(src));
  InitializePos(src_.get(), options.independent_pos());
}

template <typename Src>
void FdMMapReader<Src>::Done() {
  FdMMapReaderBase::Done();
  if (src_.is_owning()) {
    const int src = src_.Release();
    if (ABSL_PREDICT_FALSE(internal::CloseFd(src) < 0) &&
        ABSL_PREDICT_TRUE(healthy())) {
      FailOperation(internal::kCloseFunctionName);
    }
  }
}

}  // namespace riegeli

#endif  // RIEGELI_BYTES_FD_READER_H_
