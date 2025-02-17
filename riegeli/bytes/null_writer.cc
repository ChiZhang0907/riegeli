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

#include "riegeli/bytes/null_writer.h"

#include <stddef.h>

#include <limits>

#include "absl/base/optimization.h"
#include "absl/strings/cord.h"
#include "riegeli/base/base.h"
#include "riegeli/base/buffer.h"
#include "riegeli/base/chain.h"

namespace riegeli {

bool NullWriter::PushSlow(size_t min_length, size_t recommended_length) {
  RIEGELI_ASSERT_LT(available(), min_length)
      << "Failed precondition of Writer::PushSlow(): "
         "enough space available, use Push() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  SyncBuffer();
  return MakeBuffer(min_length);
}

bool NullWriter::WriteSlow(const Chain& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of Writer::WriteSlow(Chain): "
         "enough space available, use Write(Chain) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  if (ABSL_PREDICT_FALSE(src.size() >
                         std::numeric_limits<Position>::max() - pos())) {
    return FailOverflow();
  }
  SyncBuffer();
  move_start_pos(src.size());
  return MakeBuffer();
}

bool NullWriter::WriteSlow(const absl::Cord& src) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), src.size())
      << "Failed precondition of Writer::WriteSlow(Cord): "
         "enough space available, use Write(Cord) instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  if (ABSL_PREDICT_FALSE(src.size() >
                         std::numeric_limits<Position>::max() - pos())) {
    return FailOverflow();
  }
  SyncBuffer();
  move_start_pos(src.size());
  return MakeBuffer();
}

bool NullWriter::WriteZerosSlow(Position length) {
  RIEGELI_ASSERT_LT(UnsignedMin(available(), kMaxBytesToCopy), length)
      << "Failed precondition of Writer::WriteZerosSlow(): "
         "enough space available, use WriteZeros() instead";
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  if (ABSL_PREDICT_FALSE(length >
                         std::numeric_limits<Position>::max() - pos())) {
    return FailOverflow();
  }
  SyncBuffer();
  move_start_pos(length);
  return MakeBuffer();
}

bool NullWriter::TruncateImpl(Position new_size) {
  if (ABSL_PREDICT_FALSE(!healthy())) return false;
  if (new_size >= start_pos()) {
    if (ABSL_PREDICT_FALSE(new_size > pos())) return false;
    set_cursor(start() + (new_size - start_pos()));
    return true;
  }
  set_start_pos(new_size);
  set_cursor(start());
  return true;
}

inline void NullWriter::SyncBuffer() {
  set_start_pos(pos());
  set_cursor(start());
}

inline bool NullWriter::MakeBuffer(size_t min_length) {
  if (ABSL_PREDICT_FALSE(min_length >
                         std::numeric_limits<Position>::max() - start_pos())) {
    return FailOverflow();
  }
  buffer_.Reset(UnsignedMax(kDefaultBufferSize, min_length));
  set_buffer(buffer_.data(),
             UnsignedMin(buffer_.capacity(),
                         std::numeric_limits<Position>::max() - start_pos()));
  return true;
}

}  // namespace riegeli
