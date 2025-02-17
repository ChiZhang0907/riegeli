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

#ifndef RIEGELI_VARINT_VARINT_WRITING_H_
#define RIEGELI_VARINT_VARINT_WRITING_H_

#include <stddef.h>
#include <stdint.h>

#include "absl/base/optimization.h"
#include "riegeli/base/base.h"
#include "riegeli/base/port.h"
#include "riegeli/bytes/backward_writer.h"
#include "riegeli/bytes/writer.h"
#include "riegeli/varint/varint.h"

namespace riegeli {

// Writes a varint.
//
// Returns `false` on failure.
bool WriteVarint32(uint32_t data, Writer& dest);
bool WriteVarint64(uint64_t data, Writer& dest);
bool WriteVarint32(uint32_t data, BackwardWriter& dest);
bool WriteVarint64(uint64_t data, BackwardWriter& dest);

// Returns the length needed to write a given value as a varint, which is at
// most `kMaxLengthVarint{32,64}`.
size_t LengthVarint32(uint32_t data);
size_t LengthVarint64(uint64_t data);

// Writes a varint.
//
// Writes `LengthVarint{32,64}(data)` bytes to `dest[]`.
//
// Returns the updated `dest` after the written value.
char* WriteVarint32(uint32_t data, char* dest);
char* WriteVarint64(uint64_t data, char* dest);

// Implementation details follow.

inline bool WriteVarint32(uint32_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(RIEGELI_IS_CONSTANT(data) ||
                                            RIEGELI_IS_CONSTANT(data < 0x80)
                                        ? LengthVarint32(data)
                                        : kMaxLengthVarint32))) {
    return false;
  }
  dest.set_cursor(WriteVarint32(data, dest.cursor()));
  return true;
}

inline bool WriteVarint64(uint64_t data, Writer& dest) {
  if (ABSL_PREDICT_FALSE(!dest.Push(RIEGELI_IS_CONSTANT(data) ||
                                            RIEGELI_IS_CONSTANT(data < 0x80)
                                        ? LengthVarint64(data)
                                        : kMaxLengthVarint64))) {
    return false;
  }
  dest.set_cursor(WriteVarint64(data, dest.cursor()));
  return true;
}

inline bool WriteVarint32(uint32_t data, BackwardWriter& dest) {
  const size_t length = LengthVarint32(data);
  if (ABSL_PREDICT_FALSE(!dest.Push(length))) return false;
  dest.move_cursor(length);
  WriteVarint32(data, dest.cursor());
  return true;
}

inline bool WriteVarint64(uint64_t data, BackwardWriter& dest) {
  const size_t length = LengthVarint64(data);
  if (ABSL_PREDICT_FALSE(!dest.Push(length))) return false;
  dest.move_cursor(length);
  WriteVarint64(data, dest.cursor());
  return true;
}

inline size_t LengthVarint32(uint32_t data) {
#if RIEGELI_INTERNAL_HAS_BUILTIN(__builtin_clz) || \
    RIEGELI_INTERNAL_IS_GCC_VERSION(3, 4)
  const size_t floor_log2 = IntCast<size_t>(
      sizeof(unsigned) >= 4 ? __builtin_clz(data | 1) ^ __builtin_clz(1)
                            : __builtin_clzl(data | 1) ^ __builtin_clzl(1));
  // This is the same as `floor_log2 / 7 + 1` for `floor_log2` in [0..31]
  // but performs division by a power of 2.
  return (floor_log2 * 9 + 73) / 64;
#else
  size_t length = 1;
  while (data >= 0x80) {
    ++length;
    data >>= 7;
  }
  return length;
#endif
}

inline size_t LengthVarint64(uint64_t data) {
#if RIEGELI_INTERNAL_HAS_BUILTIN(__builtin_clzll) || \
    RIEGELI_INTERNAL_IS_GCC_VERSION(3, 4)
  const size_t floor_log2 =
      IntCast<size_t>(__builtin_clzll(data | 1) ^ __builtin_clzll(1));
  // This is the same as `floor_log2 / 7 + 1` for `floor_log2` in [0..63]
  // but performs division by a power of 2.
  return (floor_log2 * 9 + 73) / 64;
#else
  size_t length = 1;
  while (data >= 0x80) {
    ++length;
    data >>= 7;
  }
  return length;
#endif
}

inline char* WriteVarint32(uint32_t data, char* dest) {
  if (data < 0x80) {
    *dest++ = static_cast<char>(data);
    return dest;
  }
  do {
    *dest++ = static_cast<char>(data | 0x80);
    data >>= 7;
  } while (data >= 0x80);
  *dest++ = static_cast<char>(data);
  return dest;
}

inline char* WriteVarint64(uint64_t data, char* dest) {
  if (data < 0x80) {
    *dest++ = static_cast<char>(data);
    return dest;
  }
  do {
    *dest++ = static_cast<char>(data | 0x80);
    data >>= 7;
  } while (data >= 0x80);
  *dest++ = static_cast<char>(data);
  return dest;
}

}  // namespace riegeli

#endif  // RIEGELI_VARINT_VARINT_WRITING_H_
