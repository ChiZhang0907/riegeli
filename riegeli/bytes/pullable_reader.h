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

#ifndef RIEGELI_BYTES_PULLABLE_READER_H_
#define RIEGELI_BYTES_PULLABLE_READER_H_

#include <stddef.h>

#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/strings/cord.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/object.h"
#include "riegeli/bytes/backward_writer.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"

namespace riegeli {

// Abstract class `PullableReader` helps to implement
// `Reader::PullSlow(min_length, recommended_length)` with `min_length > 1`.
//
// `PullableReader` accumulates pulled data in a scratch buffer if needed.
class PullableReader : public Reader {
 private:
  struct Scratch;

 protected:
  // Helps to implement move constructor or move assignment if scratch is used.
  //
  // Moving the source should be in scope of a `BehindScratch` local variable,
  // unless source buffer pointers are known to remain unchanged during a move
  // or their change does not need to be reflected elsewhere.
  //
  // This temporarily reveals the relationship between the source and the buffer
  // pointers, in case it was hidden behind scratch usage. In a `BehindScratch`
  // scope, scratch is not used, and buffer pointers may be changed. The current
  // position reflects what has been read from the source and must not be
  // changed.
  class BehindScratch {
   public:
    explicit BehindScratch(PullableReader* context);

    BehindScratch(const BehindScratch&) = delete;
    BehindScratch& operator=(const BehindScratch&) = delete;

    ~BehindScratch();

   private:
    void Enter();
    void Leave();

    PullableReader* context_;
    std::unique_ptr<Scratch> scratch_;
    size_t read_from_scratch_;
  };

  // Creates a `PullableReader` with the given initial state.
  explicit PullableReader(InitiallyClosed) noexcept
      : Reader(kInitiallyClosed) {}
  explicit PullableReader(InitiallyOpen) noexcept : Reader(kInitiallyOpen) {}

  PullableReader(PullableReader&& that) noexcept;
  PullableReader& operator=(PullableReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `PullableReader`. This
  // avoids constructing a temporary `PullableReader` and moving from it.
  // Derived classes which redefine `Reset()` should include a call to
  // `PullableReader::Reset()`.
  void Reset(InitiallyClosed);
  void Reset(InitiallyOpen);

  // Returns `true` if scratch is used, which means that buffer pointers are
  // temporarily unrelated to the source. This is exposed for assertions.
  bool scratch_used() const;

  // `PullableReader::{Done,SyncImpl}()` seek the source back to the current
  // position if scratch is used but not all data from scratch were read.
  // This is feasible only if `SupportsRandomAccess()`.
  //
  // Warning: if `!SupportsRandomAccess()`, the source will have an
  // unpredictable amount of extra data consumed because of buffering.
  //
  // For propagating `{Close,Sync}()` to dependencies, `{Done,SyncImpl}()`
  // should be overridden to call `PullableReader::{Done,SyncImpl}()` and then
  // close/sync the dependencies.

  // Implementation of `PullSlow(1, 0)`, called while scratch is not used.
  //
  // Preconditions:
  //   `available() == 0`
  //   `!scratch_used()`
  virtual bool PullBehindScratch() = 0;

  // Implementation of `ReadSlow()`, `CopySlow()`, `ReadHintSlow()`, and
  // `SeekSlow()`, called while scratch is not used.
  //
  // By default they are implemented analogously to the corresponding `Reader`
  // functions.
  //
  // Preconditions:
  //   like the corresponding `Reader` functions
  //   `!scratch_used()`
  virtual bool ReadBehindScratch(size_t length, char* dest);
  virtual bool ReadBehindScratch(size_t length, Chain& dest);
  virtual bool ReadBehindScratch(size_t length, absl::Cord& dest);
  virtual bool CopyBehindScratch(Position length, Writer& dest);
  virtual bool CopyBehindScratch(size_t length, BackwardWriter& dest);
  virtual void ReadHintBehindScratch(size_t length);
  virtual bool SeekBehindScratch(Position new_pos);

  void Done() override;
  bool PullSlow(size_t min_length, size_t recommended_length) override;
  bool ReadSlow(size_t length, char* dest) override;
  bool ReadSlow(size_t length, Chain& dest) override;
  bool ReadSlow(size_t length, absl::Cord& dest) override;
  bool CopySlow(Position length, Writer& dest) override;
  bool CopySlow(size_t length, BackwardWriter& dest) override;
  void ReadHintSlow(size_t length) override;
  bool SyncImpl(SyncType sync_type) override;
  bool SeekSlow(Position new_pos) override;

 private:
  struct Scratch {
    ChainBlock buffer;
    const char* original_start = nullptr;
    size_t original_buffer_size = 0;
    size_t original_read_from_buffer = 0;
  };

  void SyncScratch();

  // Stops using scratch and returns `true` if all remaining data in scratch
  // come from a single fragment of the original source.
  bool ScratchEnds();

  std::unique_ptr<Scratch> scratch_;

  // Invariants if `scratch_used()`:
  //   `start() == scratch_->buffer.data()`
  //   `buffer_size() == scratch_->buffer.size()`
};

// Implementation details follow.

inline PullableReader::PullableReader(PullableReader&& that) noexcept
    : Reader(std::move(that)),
      // Using `that` after it was moved is correct because only the base class
      // part was moved.
      scratch_(std::move(that.scratch_)) {}

inline PullableReader& PullableReader::operator=(
    PullableReader&& that) noexcept {
  Reader::operator=(std::move(that));
  // Using `that` after it was moved is correct because only the base class part
  // was moved.
  scratch_ = std::move(that.scratch_);
  return *this;
}

inline void PullableReader::Reset(InitiallyClosed) {
  Reader::Reset(kInitiallyClosed);
  if (ABSL_PREDICT_FALSE(scratch_used())) scratch_->buffer.Clear();
}

inline void PullableReader::Reset(InitiallyOpen) {
  Reader::Reset(kInitiallyOpen);
  if (ABSL_PREDICT_FALSE(scratch_used())) scratch_->buffer.Clear();
}

inline bool PullableReader::scratch_used() const {
  return scratch_ != nullptr && !scratch_->buffer.empty();
}

inline PullableReader::BehindScratch::BehindScratch(PullableReader* context)
    : context_(context) {
  if (ABSL_PREDICT_FALSE(context_->scratch_used())) Enter();
}

inline PullableReader::BehindScratch::~BehindScratch() {
  if (ABSL_PREDICT_FALSE(scratch_ != nullptr)) Leave();
}

}  // namespace riegeli

#endif  // RIEGELI_BYTES_PULLABLE_READER_H_
