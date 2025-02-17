// Copyright 2019 Google LLC
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

#ifndef RIEGELI_SNAPPY_SNAPPY_READER_H_
#define RIEGELI_SNAPPY_SNAPPY_READER_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/bytes/chain_reader.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"

namespace riegeli {

// Template parameter independent part of `SnappyReader`.
class SnappyReaderBase : public ChainReader<Chain> {
 public:
  class Options {
   public:
    Options() noexcept {}

    // If `absl::nullopt`, the compressed `Reader` must support `Size()`.
    //
    // If not `absl::nullopt`, overrides that size.
    //
    // Default: `absl::nullopt`.
    Options& set_assumed_size(absl::optional<Position> assumed_size) & {
      assumed_size_ = assumed_size;
      return *this;
    }
    Options&& set_assumed_size(absl::optional<Position> assumed_size) && {
      return std::move(set_assumed_size(assumed_size));
    }
    absl::optional<Position> assumed_size() const { return assumed_size_; }

   private:
    absl::optional<Position> assumed_size_;
  };

  // Returns the compressed `Reader`. Unchanged by `Close()`.
  virtual Reader* src_reader() = 0;
  virtual const Reader* src_reader() const = 0;

 protected:
  SnappyReaderBase(InitiallyClosed) noexcept {}

  explicit SnappyReaderBase(InitiallyOpen);

  SnappyReaderBase(SnappyReaderBase&& that) noexcept;
  SnappyReaderBase& operator=(SnappyReaderBase&& that) noexcept;

  void Reset(InitiallyClosed);
  void Reset(InitiallyOpen);
  void Initialize(Reader* src, absl::optional<Position> assumed_size);

  void Done() override;
  // `SnappyReaderBase` overrides `Reader::AnnotateFailure()` to annotate the
  // status with the current position, clarifying that this is the uncompressed
  // position. A status propagated from `*src_reader()` might carry annotation
  // with the compressed position.
  ABSL_ATTRIBUTE_COLD void AnnotateFailure(absl::Status& status) override;
};

// A `Reader` which decompresses data with Snappy after getting it from another
// `Reader`.
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the compressed `Reader`. `Src` must support
// `Dependency<Reader*, Src>`, e.g. `Reader*` (not owned, default),
// `std::unique_ptr<Reader>` (owned), `ChainReader<>` (owned).
//
// By relying on CTAD the template argument can be deduced as the value type of
// the first constructor argument. This requires C++17.
//
// The compressed `Reader` must support `Size()` if
// `Options::assumed_size() == absl::nullopt`.
//
// The compressed `Reader` must not be accessed until the `SnappyReader` is
// closed or no longer used.
//
// `SnappyReader` does not decompress incrementally but reads compressed data
// and decompresses them all in the constructor.
//
// `SnappyReader` does not support reading from a growing source. If source is
// truncated, decompression fails.
template <typename Src = Reader*>
class SnappyReader : public SnappyReaderBase {
 public:
  // Creates a closed `SnappyReader`.
  SnappyReader() noexcept : SnappyReaderBase(kInitiallyClosed) {}

  // Will read from the compressed `Reader` provided by `src`.
  explicit SnappyReader(const Src& src, Options options = Options());
  explicit SnappyReader(Src&& src, Options options = Options());

  // Will read from the compressed `Reader` provided by a `Src` constructed from
  // elements of `src_args`. This avoids constructing a temporary `Src` and
  // moving from it.
  template <typename... SrcArgs>
  explicit SnappyReader(std::tuple<SrcArgs...> src_args,
                        Options options = Options());

  SnappyReader(SnappyReader&& that) noexcept;
  SnappyReader& operator=(SnappyReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `SnappyReader`. This avoids
  // constructing a temporary `SnappyReader` and moving from it.
  void Reset();
  void Reset(const Src& src, Options options = Options());
  void Reset(Src&& src, Options options = Options());
  template <typename... SrcArgs>
  void Reset(std::tuple<SrcArgs...> src_args, Options options = Options());

  // Returns the object providing and possibly owning the compressed `Reader`.
  // Unchanged by `Close()`.
  Src& src() { return src_.manager(); }
  const Src& src() const { return src_.manager(); }
  Reader* src_reader() override { return src_.get(); }
  const Reader* src_reader() const override { return src_.get(); }

  void VerifyEnd() override;

 protected:
  void Done() override;

 private:
  // The object providing and possibly owning the compressed `Reader`.
  Dependency<Reader*, Src> src_;
};

// Support CTAD.
#if __cpp_deduction_guides
SnappyReader()->SnappyReader<DeleteCtad<>>;
template <typename Src>
explicit SnappyReader(const Src& src, SnappyReaderBase::Options options =
                                          SnappyReaderBase::Options())
    -> SnappyReader<std::decay_t<Src>>;
template <typename Src>
explicit SnappyReader(
    Src&& src, SnappyReaderBase::Options options = SnappyReaderBase::Options())
    -> SnappyReader<std::decay_t<Src>>;
template <typename... SrcArgs>
explicit SnappyReader(
    std::tuple<SrcArgs...> src_args,
    SnappyReaderBase::Options options = SnappyReaderBase::Options())
    -> SnappyReader<DeleteCtad<std::tuple<SrcArgs...>>>;
#endif

// Options for `SnappyDecompress()`.
class SnappyDecompressOptions {
 public:
  SnappyDecompressOptions() noexcept {}

  // If `absl::nullopt`, the compressed `Reader` must support `Size()`.
  //
  // If not `absl::nullopt`, overrides that size.
  //
  // Default: `absl::nullopt`.
  SnappyDecompressOptions& set_assumed_size(
      absl::optional<Position> assumed_size) & {
    assumed_size_ = assumed_size;
    return *this;
  }
  SnappyDecompressOptions&& set_assumed_size(
      absl::optional<Position> assumed_size) && {
    return std::move(set_assumed_size(assumed_size));
  }
  absl::optional<Position> assumed_size() const { return assumed_size_; }

 private:
  absl::optional<Position> assumed_size_;
};

// An alternative interface to Snappy which avoids buffering uncompressed data.
// Calling `SnappyDecompress()` is equivalent to copying all data from a
// `SnappyReader<Src>` to `dest`.
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the compressed `Reader`. `Src` must support
// `Dependency<Reader*, Src>`, e.g. `Reader&` (not owned),
// `Reader*` (not owned), `std::unique_ptr<Reader>` (owned),
// `ChainReader<>` (owned).
//
// The `Dest` template parameter specifies the type of the object providing and
// possibly owning the uncompressed `Writer`. `Dest` must support
// `Dependency<Writer*, Dest>`, e.g. `Writer&` (not owned),
// `Writer*` (not owned), `std::unique_ptr<Writer>` (owned),
// `ChainWriter<>` (owned).
//
// The compressed `Reader` must support `Size()` if
// `SnappyDecompressOptions::assumed_size() == absl::nullopt`.
template <typename Src, typename Dest>
absl::Status SnappyDecompress(
    const Src& src, const Dest& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest>
absl::Status SnappyDecompress(
    const Src& src, Dest&& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest, typename... DestArgs>
absl::Status SnappyDecompress(
    const Src& src, std::tuple<DestArgs...> dest_args,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest>
absl::Status SnappyDecompress(
    Src&& src, const Dest& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest>
absl::Status SnappyDecompress(
    Src&& src, Dest&& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest, typename... DestArgs>
absl::Status SnappyDecompress(
    Src&& src, std::tuple<DestArgs...> dest_args,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest, typename... SrcArgs>
absl::Status SnappyDecompress(
    std::tuple<SrcArgs...> src_args, const Dest& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest, typename... SrcArgs>
absl::Status SnappyDecompress(
    std::tuple<SrcArgs...> src_args, Dest&& dest,
    SnappyDecompressOptions options = SnappyDecompressOptions());
template <typename Src, typename Dest, typename... SrcArgs,
          typename... DestArgs>
absl::Status SnappyDecompress(
    std::tuple<SrcArgs...> src_args, std::tuple<DestArgs...> dest_args,
    SnappyDecompressOptions options = SnappyDecompressOptions());

// Returns the claimed uncompressed size of Snappy-compressed data.
//
// Returns `absl::nullopt` on failure.
//
// The current position of `src` is unchanged.
absl::optional<size_t> SnappyUncompressedSize(Reader& src);

// Implementation details follow.

inline SnappyReaderBase::SnappyReaderBase(InitiallyOpen)
    // Empty `Chain` as the `ChainReader` source is a placeholder, it will be
    // set by `Initialize()`.
    : ChainReader(std::forward_as_tuple()) {}

inline SnappyReaderBase::SnappyReaderBase(SnappyReaderBase&& that) noexcept
    : ChainReader(std::move(that)) {}

inline SnappyReaderBase& SnappyReaderBase::operator=(
    SnappyReaderBase&& that) noexcept {
  ChainReader::operator=(std::move(that));
  return *this;
}

inline void SnappyReaderBase::Reset(InitiallyClosed) { ChainReader::Reset(); }

inline void SnappyReaderBase::Reset(InitiallyOpen) {
  // Empty `Chain` as the `ChainReader` source is a placeholder, it will be set
  // by `Initialize()`.
  ChainReader::Reset(std::forward_as_tuple());
}

template <typename Src>
inline SnappyReader<Src>::SnappyReader(const Src& src, Options options)
    : SnappyReaderBase(kInitiallyOpen), src_(src) {
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
inline SnappyReader<Src>::SnappyReader(Src&& src, Options options)
    : SnappyReaderBase(kInitiallyOpen), src_(std::move(src)) {
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
template <typename... SrcArgs>
inline SnappyReader<Src>::SnappyReader(std::tuple<SrcArgs...> src_args,
                                       Options options)
    : SnappyReaderBase(kInitiallyOpen), src_(std::move(src_args)) {
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
inline SnappyReader<Src>::SnappyReader(SnappyReader&& that) noexcept
    : SnappyReaderBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      src_(std::move(that.src_)) {}

template <typename Src>
inline SnappyReader<Src>& SnappyReader<Src>::operator=(
    SnappyReader&& that) noexcept {
  SnappyReaderBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  src_ = std::move(that.src_);
  return *this;
}

template <typename Src>
inline void SnappyReader<Src>::Reset() {
  SnappyReaderBase::Reset(kInitiallyClosed);
  src_.Reset();
}

template <typename Src>
inline void SnappyReader<Src>::Reset(const Src& src, Options options) {
  SnappyReaderBase::Reset(kInitiallyOpen);
  src_.Reset(src);
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
inline void SnappyReader<Src>::Reset(Src&& src, Options options) {
  SnappyReaderBase::Reset(kInitiallyOpen);
  src_.Reset(std::move(src));
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
template <typename... SrcArgs>
inline void SnappyReader<Src>::Reset(std::tuple<SrcArgs...> src_args,
                                     Options options) {
  SnappyReaderBase::Reset(kInitiallyOpen);
  src_.Reset(std::move(src_args));
  Initialize(src_.get(), options.assumed_size());
}

template <typename Src>
void SnappyReader<Src>::Done() {
  SnappyReaderBase::Done();
  if (src_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!src_->Close())) Fail(*src_);
  }
}

template <typename Src>
void SnappyReader<Src>::VerifyEnd() {
  SnappyReaderBase::VerifyEnd();
  if (src_.is_owning() && ABSL_PREDICT_TRUE(healthy())) src_->VerifyEnd();
}

namespace internal {

absl::Status SnappyDecompressImpl(Reader& src, Writer& dest,
                                  SnappyDecompressOptions options);

template <typename Src, typename Dest>
inline absl::Status SnappyDecompressUsingDependency(
    Dependency<Reader*, Src> src, Dependency<Writer*, Dest> dest,
    SnappyDecompressOptions options) {
  absl::Status status = SnappyDecompressImpl(*src, *dest, std::move(options));
  if (dest.is_owning()) {
    if (ABSL_PREDICT_FALSE(!dest->Close())) {
      if (ABSL_PREDICT_TRUE(status.ok())) status = dest->status();
    }
  }
  if (src.is_owning()) {
    if (ABSL_PREDICT_FALSE(!src->Close())) {
      if (ABSL_PREDICT_TRUE(status.ok())) status = src->status();
    }
  }
  return status;
}

}  // namespace internal

template <typename Src, typename Dest>
inline absl::Status SnappyDecompress(const Src& src, const Dest& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, const Src&>(src),
      Dependency<Writer*, const Dest&>(dest), std::move(options));
}

template <typename Src, typename Dest>
inline absl::Status SnappyDecompress(const Src& src, Dest&& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, const Src&>(src),
      Dependency<Writer*, Dest&&>(std::forward<Dest>(dest)),
      std::move(options));
}

template <typename Src, typename Dest, typename... DestArgs>
inline absl::Status SnappyDecompress(const Src& src,
                                     std::tuple<DestArgs...> dest_args,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, const Src&>(src),
      Dependency<Writer*, Dest>(std::move(dest_args)), std::move(options));
}

template <typename Src, typename Dest>
inline absl::Status SnappyDecompress(Src&& src, const Dest& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src&&>(std::forward<Src>(src)),
      Dependency<Writer*, const Dest&>(dest), std::move(options));
}

template <typename Src, typename Dest>
inline absl::Status SnappyDecompress(Src&& src, Dest&& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src&&>(std::forward<Src>(src)),
      Dependency<Writer*, Dest&&>(std::forward<Dest>(dest)),
      std::move(options));
}

template <typename Src, typename Dest, typename... DestArgs>
inline absl::Status SnappyDecompress(Src&& src,
                                     std::tuple<DestArgs...> dest_args,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src&&>(std::forward<Src>(src)),
      Dependency<Writer*, Dest>(std::move(dest_args)), std::move(options));
}

template <typename Src, typename Dest, typename... SrcArgs>
inline absl::Status SnappyDecompress(std::tuple<SrcArgs...> src_args,
                                     const Dest& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src>(std::move(src_args)),
      Dependency<Writer*, const Dest&>(dest), std::move(options));
}

template <typename Src, typename Dest, typename... SrcArgs>
inline absl::Status SnappyDecompress(std::tuple<SrcArgs...> src_args,
                                     Dest&& dest,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src>(std::move(src_args)),
      Dependency<Writer*, Dest&&>(std::forward<Dest>(dest)),
      std::move(options));
}

template <typename Src, typename Dest, typename... SrcArgs,
          typename... DestArgs>
inline absl::Status SnappyDecompress(std::tuple<SrcArgs...> src_args,
                                     std::tuple<DestArgs...> dest_args,
                                     SnappyDecompressOptions options) {
  return internal::SnappyDecompressUsingDependency(
      Dependency<Reader*, Src>(std::move(src_args)),
      Dependency<Writer*, Dest>(std::move(dest_args)), std::move(options));
}

}  // namespace riegeli

#endif  // RIEGELI_SNAPPY_SNAPPY_READER_H_
