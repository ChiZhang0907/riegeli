// Copyright 2018 Google LLC
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

#include "riegeli/bytes/limiting_backward_writer.h"

#include <stddef.h>

#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "riegeli/base/base.h"
#include "riegeli/base/chain.h"
#include "riegeli/bytes/backward_writer.h"

namespace riegeli {

// Before C++17 if a constexpr static data member is ODR-used, its definition at
// namespace scope is required. Since C++17 these definitions are deprecated:
// http://en.cppreference.com/w/cpp/language/static
#if __cplusplus < 201703
constexpr Position LimitingBackwardWriterBase::kNoSizeLimit;
#endif

void LimitingBackwardWriterBase::Done() {
  if (ABSL_PREDICT_TRUE(healthy())) {
    BackwardWriter& dest = *dest_writer();
    SyncBuffer(dest);
  }
  BackwardWriter::Done();
}

bool LimitingBackwardWriterBase::SizeLimitExceeded() {
  return Fail(absl::ResourceExhaustedError(
      absl::StrCat("Size limit exceeded: ", size_limit_)));
}

bool LimitingBackwardWriterBase::PushSlow(size_t min_length,
                                          size_t recommended_length) {
  RIEGELI_ASSERT_LT(available(), min_length)
      << "Failed precondition of BackwardWriter::PushSlow(): "
         "enough space available, use Push() instead";
  RIEGELI_ASSERT_LE(start_pos(), size_limit_)
      << "Failed invariant of LimitingBackwardWriterBase: "
         "position exceeds size limit";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  BackwardWriter& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!SyncBuffer(dest))) return false;
  const bool ok = dest.Push(min_length, recommended_length);
  MakeBuffer(dest);
  return ok;
}

bool LimitingBackwardWriterBase::WriteSlow(absl::string_view src) {
  RIEGELI_ASSERT_LT(available(), src.size())
      << "Failed precondition of BackwardWriter::WriteSlow(string_view): "
         "enough space available, use Write(string_view) instead";
  return WriteInternal(src);
}

bool LimitingBackwardWriterBase::WriteSlow(const Chain& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of BackwardWriter::WriteSlow(Chain): "
         "enough space available, use Write(Chain) instead";
  return WriteInternal(src);
}

bool LimitingBackwardWriterBase::WriteSlow(Chain&& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of BackwardWriter::WriteSlow(Chain&&): "
         "enough space available, use Write(Chain&&) instead";
  return WriteInternal(std::move(src));
}

bool LimitingBackwardWriterBase::WriteSlow(const absl::Cord& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of BackwardWriter::WriteSlow(Cord): "
         "enough space available, use Write(Cord) instead";
  return WriteInternal(src);
}

bool LimitingBackwardWriterBase::WriteSlow(absl::Cord&& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of BackwardWriter::WriteSlow(Cord&&): "
         "enough space available, use Write(Cord&&) instead";
  return WriteInternal(std::move(src));
}

template <typename Src>
inline bool LimitingBackwardWriterBase::WriteInternal(Src&& src) {
  RIEGELI_ASSERT_LE(start_pos(), size_limit_)
      << "Failed invariant of LimitingBackwardWriterBase: "
         "position exceeds size limit";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  BackwardWriter& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!SyncBuffer(dest))) return false;
  if (ABSL_PREDICT_FALSE(src.size() > size_limit_ - pos())) {
    return SizeLimitExceeded();
  }
  const bool ok = dest.Write(std::forward<Src>(src));
  MakeBuffer(dest);
  return ok;
}

bool LimitingBackwardWriterBase::WriteZerosSlow(Position length) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), length)
      << "Failed precondition of BackwardWriter::WriteZerosSlow(): "
         "enough space available, use WriteZeros() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  BackwardWriter& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!SyncBuffer(dest))) return false;
  if (ABSL_PREDICT_FALSE(length > size_limit_ - pos())) {
    return SizeLimitExceeded();
  }
  const bool ok = dest.WriteZeros(length);
  MakeBuffer(dest);
  return ok;
}

void LimitingBackwardWriterBase::WriteHintSlow(size_t length) {
  RIEGELI_ASSERT_LT(available(), length)
      << "Failed precondition of BackwardWriter::WriteHintSlow(): "
         "enough space available, use WriteHint() instead";
  RIEGELI_ASSERT_LE(start_pos(), size_limit_)
      << "Failed invariant of LimitingBackwardWriterBase: "
         "position exceeds size limit";
  if (ABSL_PREDICT_FALSE(!healthy())) return;
  BackwardWriter& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!SyncBuffer(dest))) return;
  dest.WriteHint(UnsignedMin(length, size_limit_ - pos()));
  MakeBuffer(dest);
}

bool LimitingBackwardWriterBase::PrefersCopying() const {
  const BackwardWriter* const dest = dest_writer();
  return dest != nullptr && dest->PrefersCopying();
}

bool LimitingBackwardWriterBase::SupportsTruncate() {
  BackwardWriter* const dest = dest_writer();
  return dest != nullptr && dest->SupportsTruncate();
}

bool LimitingBackwardWriterBase::Truncate(Position new_size) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  BackwardWriter& dest = *dest_writer();
  if (ABSL_PREDICT_FALSE(!SyncBuffer(dest))) return false;
  const bool ok = dest.Truncate(new_size);
  MakeBuffer(dest);
  return ok;
}

}  // namespace riegeli
