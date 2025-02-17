// Copyright 2021 Google LLC
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

#ifndef RIEGELI_BYTES_DIGESTING_READER_H_
#define RIEGELI_BYTES_DIGESTING_READER_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/base/reset.h"
#include "riegeli/bytes/digesting_common.h"
#include "riegeli/bytes/reader.h"

namespace riegeli {

// Template parameter independent part of `DigestingReader`.
class DigestingReaderBase : public Reader {
 public:
  // Returns the original `Reader`. Unchanged by `Close()`.
  virtual Reader* src_reader() = 0;
  virtual const Reader* src_reader() const = 0;

  bool SupportsSize() override;

 protected:
  explicit DigestingReaderBase(InitiallyClosed) noexcept
      : Reader(kInitiallyClosed) {}
  explicit DigestingReaderBase(InitiallyOpen) noexcept
      : Reader(kInitiallyOpen) {}

  DigestingReaderBase(DigestingReaderBase&& that) noexcept;
  DigestingReaderBase& operator=(DigestingReaderBase&& that) noexcept;

  void Initialize(Reader* src);

  virtual void DigesterWrite(absl::string_view src) = 0;
  void DigesterWrite(const Chain& src);
  void DigesterWrite(const absl::Cord& src);

  void Done() override;
  bool PullSlow(size_t min_length, size_t recommended_length) override;
  using Reader::ReadSlow;
  bool ReadSlow(size_t length, char* dest) override;
  bool ReadSlow(size_t length, Chain& dest) override;
  bool ReadSlow(size_t length, absl::Cord& dest) override;
  void ReadHintSlow(size_t length) override;
  absl::optional<Position> SizeImpl() override;

  // Sets cursor of `src` to cursor of `*this`, digesting what has been read
  // from the buffer (until `cursor()`).
  void SyncBuffer(Reader& src);

  // Sets buffer pointers of `*this` to buffer pointers of `src`, adjusting
  // `start()` to hide data already digested. Fails `*this` if `src` failed.
  void MakeBuffer(Reader& src);

 private:
  // Invariants if `is_open()`:
  //   `start() == src_reader()->cursor()`
  //   `limit() == src_reader()->limit()`
  //   `limit_pos() == src_reader()->limit_pos()`
};

// A `Reader` which reads from another `Reader`, and lets another object observe
// data being read and return some data called a digest, e.g. a checksum.
//
// The `Digester` template parameter specifies how data are being digested.
// `DigestingReader` forwards basic operations to `Digester`: constructor
// with forwarded parameters after `src`, move constructor, move assignment,
// destructor, and optionally `Reset()`. Apart from that, `Digester` should
// support:
//
// ```
//   // Called with consecutive fragments of data.
//   void Write(absl::string_view src);
//
//   // `WriteZeros()` is not used by `DigestingReader` but is used by .
//   // `DigestingWriter`.
//
//   // Called when nothing more will be digested. Resources can be freed.
//   //
//   // This method is optional. If that is not defined, nothing is done.
//   void Close();
//
//   // Returns the digest. Its type and meaning depends on the `Digester`.
//   // Unchanged by `Close()`.
//   //
//   // This method is optional. If that is not defined, nothing is done and
//   // `void` is returned.
//   DigestType Digest();
// ```
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the original `Reader`. `Src` must support
// `Dependency<Reader*, Src>`, e.g. `Reader*` (not owned, default),
// `std::unique_ptr<Reader>` (owned), `ChainReader<>` (owned).
//
// By relying on CTAD the template argument can be deduced as the value type of
// the first constructor argument. This requires C++17.
//
// The original `Reader` must not be accessed until the `DigestingReader` is
// closed or no longer used.
template <typename Digester, typename Src = Reader*>
class DigestingReader : public DigestingReaderBase {
 public:
  // The type of the digest.
  using DigestType = internal::DigestType<Digester>;

  // Creates a closed `DigestingReader`.
  DigestingReader() noexcept : DigestingReaderBase(kInitiallyClosed) {}

  // Will read from the original `Reader` provided by `src`. Constructs a
  // `Digester` from `digester_args`.
  template <typename... DigesterArgs>
  explicit DigestingReader(const Src& src, DigesterArgs&&... digester_args);
  template <typename... DigesterArgs>
  explicit DigestingReader(Src&& src, DigesterArgs&&... digester_args);

  // Will read from the original `Reader` provided by a `Src` constructed from
  // elements of `src_args`. This avoids constructing a temporary `Src` and
  // moving from it.
  template <typename... SrcArgs, typename... DigesterArgs>
  explicit DigestingReader(std::tuple<SrcArgs...> src_args,
                           DigesterArgs&&... digester_args);

  DigestingReader(DigestingReader&& that) noexcept;
  DigestingReader& operator=(DigestingReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `DigestingReader`. This
  // avoids constructing a temporary `DigestingReader` and moving from it.
  void Reset();
  template <typename... DigesterArgs>
  void Reset(const Src& src, DigesterArgs&&... digester_args);
  template <typename... DigesterArgs>
  void Reset(Src&& src, DigesterArgs&&... digester_args);
  template <typename... SrcArgs, typename... DigesterArgs>
  void Reset(std::tuple<SrcArgs...> src_args, DigesterArgs&&... digester_args);

  // Digests buffered data if needed, and returns the digest.
  DigestType Digest();

  // Returns the object providing and possibly owning the original `Reader`.
  // Unchanged by `Close()`.
  Src& src() { return src_.manager(); }
  const Src& src() const { return src_.manager(); }
  Reader* src_reader() override { return src_.get(); }
  const Reader* src_reader() const override { return src_.get(); }

  void VerifyEnd() override;

 protected:
  void Done() override;
  bool SyncImpl(SyncType sync_type) override;

  using DigestingReaderBase::DigesterWrite;
  void DigesterWrite(absl::string_view src) override;

 private:
  void MoveSrc(DigestingReader&& that);

  Digester digester_;
  // The object providing and possibly owning the original `Reader`.
  Dependency<Reader*, Src> src_;
};

// Support CTAD.
#if __cpp_deduction_guides
DigestingReader()->DigestingReader<void, DeleteCtad<>>;
template <typename Digester, typename Src>
explicit DigestingReader(const Src& src, Digester&& digester)
    -> DigestingReader<std::decay_t<Digester>, std::decay_t<Src>>;
template <typename Digester, typename Src>
explicit DigestingReader(Src&& src, Digester&& digester)
    -> DigestingReader<std::decay_t<Digester>, std::decay_t<Src>>;
template <typename Digester, typename... SrcArgs>
explicit DigestingReader(std::tuple<SrcArgs...> src_args, Digester&& digester)
    -> DigestingReader<void, DeleteCtad<std::tuple<SrcArgs...>>>;
#endif

// Implementation details follow.

inline DigestingReaderBase::DigestingReaderBase(
    DigestingReaderBase&& that) noexcept
    : Reader(std::move(that)) {}

inline DigestingReaderBase& DigestingReaderBase::operator=(
    DigestingReaderBase&& that) noexcept {
  Reader::operator=(std::move(that));
  return *this;
}

inline void DigestingReaderBase::Initialize(Reader* src) {
  RIEGELI_ASSERT(src != nullptr)
      << "Failed precondition of DigestingReader: null Reader pointer";
  MakeBuffer(*src);
}

inline void DigestingReaderBase::SyncBuffer(Reader& src) {
  RIEGELI_ASSERT(start() == src.cursor())
      << "Failed invariant of DigestingReaderBase: "
         "cursor of the original Reader changed unexpectedly";
  if (read_from_buffer() > 0) {
    DigesterWrite(absl::string_view(start(), read_from_buffer()));
  }
  src.set_cursor(cursor());
}

inline void DigestingReaderBase::MakeBuffer(Reader& src) {
  set_buffer(src.cursor(), src.available());
  set_limit_pos(src.pos() + src.available());
  if (ABSL_PREDICT_FALSE(!src.healthy())) FailWithoutAnnotation(src);
}

template <typename Digester, typename Src>
template <typename... DigesterArgs>
inline DigestingReader<Digester, Src>::DigestingReader(
    const Src& src, DigesterArgs&&... digester_args)
    : DigestingReaderBase(kInitiallyOpen),
      digester_(std::forward<DigesterArgs>(digester_args)...),
      src_(src) {
  Initialize(src_.get());
}

template <typename Digester, typename Src>
template <typename... DigesterArgs>
inline DigestingReader<Digester, Src>::DigestingReader(
    Src&& src, DigesterArgs&&... digester_args)
    : DigestingReaderBase(kInitiallyOpen),
      digester_(std::forward<DigesterArgs>(digester_args)...),
      src_(std::move(src)) {
  Initialize(src_.get());
}

template <typename Digester, typename Src>
template <typename... SrcArgs, typename... DigesterArgs>
inline DigestingReader<Digester, Src>::DigestingReader(
    std::tuple<SrcArgs...> src_args, DigesterArgs&&... digester_args)
    : DigestingReaderBase(kInitiallyOpen),
      digester_(std::forward<DigesterArgs>(digester_args)...),
      src_(std::move(src_args)) {
  Initialize(src_.get());
}

template <typename Digester, typename Src>
inline DigestingReader<Digester, Src>::DigestingReader(
    DigestingReader&& that) noexcept
    : DigestingReaderBase(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      digester_(std::move(that.digester_)) {
  MoveSrc(std::move(that));
}

template <typename Digester, typename Src>
inline DigestingReader<Digester, Src>&
DigestingReader<Digester, Src>::operator=(DigestingReader&& that) noexcept {
  DigestingReaderBase::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  digester_ = std::move(that.digester_);
  MoveSrc(std::move(that));
  return *this;
}

template <typename Digester, typename Src>
inline void DigestingReader<Digester, Src>::Reset() {
  DigestingReaderBase::Reset(kInitiallyClosed);
  riegeli::Reset(digester_);
  src_.Reset();
}

template <typename Digester, typename Src>
template <typename... DigesterArgs>
inline void DigestingReader<Digester, Src>::Reset(
    const Src& src, DigesterArgs&&... digester_args) {
  DigestingReaderBase::Reset(kInitiallyOpen);
  riegeli::Reset(digester_, std::forward<DigesterArgs>(digester_args)...);
  src_.Reset(src);
  Initialize(src_.get());
}

template <typename Digester, typename Src>
template <typename... DigesterArgs>
inline void DigestingReader<Digester, Src>::Reset(
    Src&& src, DigesterArgs&&... digester_args) {
  DigestingReaderBase::Reset(kInitiallyOpen);
  riegeli::Reset(digester_, std::forward<DigesterArgs>(digester_args)...);
  src_.Reset(std::move(src));
  Initialize(src_.get());
}

template <typename Digester, typename Src>
template <typename... SrcArgs, typename... DigesterArgs>
inline void DigestingReader<Digester, Src>::Reset(
    std::tuple<SrcArgs...> src_args, DigesterArgs&&... digester_args) {
  DigestingReaderBase::Reset(kInitiallyOpen);
  riegeli::Reset(digester_, std::forward<DigesterArgs>(digester_args)...);
  src_.Reset(std::move(src_args));
  Initialize(src_.get());
}

template <typename Digester, typename Src>
inline void DigestingReader<Digester, Src>::MoveSrc(DigestingReader&& that) {
  if (src_.kIsStable()) {
    src_ = std::move(that.src_);
  } else {
    // Buffer pointers are already moved so `SyncBuffer()` is called on `*this`,
    // `src_` is not moved yet so `src_` is taken from `that`.
    SyncBuffer(*that.src_);
    src_ = std::move(that.src_);
    MakeBuffer(*src_);
  }
}

template <typename Digester, typename Src>
inline typename DigestingReader<Digester, Src>::DigestType
DigestingReader<Digester, Src>::Digest() {
  if (read_from_buffer() > 0) {
    DigesterWrite(absl::string_view(start(), read_from_buffer()));
    set_buffer(cursor(), available());
  }
  return internal::DigesterDigest(digester_);
}

template <typename Digester, typename Src>
void DigestingReader<Digester, Src>::Done() {
  DigestingReaderBase::Done();
  if (src_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!src_->Close())) FailWithoutAnnotation(*src_);
  }
  internal::DigesterClose(digester_);
}

template <typename Digester, typename Src>
void DigestingReader<Digester, Src>::VerifyEnd() {
  DigestingReaderBase::VerifyEnd();
  if (src_.is_owning() && ABSL_PREDICT_TRUE(healthy())) {
    SyncBuffer(*src_);
    src_->VerifyEnd();
    MakeBuffer(*src_);
  }
}

template <typename Digester, typename Src>
bool DigestingReader<Digester, Src>::SyncImpl(SyncType sync_type) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  SyncBuffer(*src_);
  bool ok = true;
  if (sync_type != SyncType::kFromObject || src_.is_owning()) {
    ok = src_->Sync(sync_type);
  }
  MakeBuffer(*src_);
  return ok;
}

template <typename Digester, typename Src>
void DigestingReader<Digester, Src>::DigesterWrite(absl::string_view src) {
  digester_.Write(src);
}

}  // namespace riegeli

#endif  // RIEGELI_BYTES_DIGESTING_READER_H_
