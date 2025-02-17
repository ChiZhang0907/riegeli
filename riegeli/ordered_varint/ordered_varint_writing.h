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

#ifndef RIEGELI_ORDERED_VARINT_ORDERED_VARINT_WRITING_H_
#define RIEGELI_ORDERED_VARINT_ORDERED_VARINT_WRITING_H_

#include <stddef.h>
#include <stdint.h>

#include "absl/base/optimization.h"
#include "riegeli/base/base.h"
#include "riegeli/base/port.h"
#include "riegeli/bytes/writer.h"
#include "riegeli/ordered_varint/ordered_varint.h"

namespace riegeli {

// An ordered varint represents an unsigned integer in a variable number of
// bytes, such that smaller values are represented by lexicographically smaller
// strings, and also smaller values tend to be represented by shorter strings.
//
// Encoding a 64-bit value X:
//
// If X == 0, then let L = 0. Otherwise let L = floor(log2(X)). L is in the
// range [0..63].
//
// If L == 63, then let N = 9. Otherwise let N = L / 7 + 1. N is in the range
// [1..9]. X will be encoded into N bytes.
//
// The first byte of the encoding consists of the following bits, from highest
// to lowest:
//  * N - 1 one bits
//  * 1 zero bit, if N < 9
//  * 8 - N bits representing X >> (8 * (N - 1)), if N < 8
//
// The remaining N - 1 bytes represent lower order bytes of X in big endian.

// Writes an ordered varint.
//
// Returns `false` on failure.
bool WriteOrderedVarint32(uint32_t data, Writer& dest);
bool WriteOrderedVarint64(uint64_t data, Writer& dest);

// Returns the length needed to write a given value as an ordered varint, which
// is at most `kMaxLengthOrderedVarint{32,64}`.
size_t LengthOrderedVarint32(uint32_t data);
size_t LengthOrderedVarint64(uint64_t data);

// Implementation details follow.

namespace internal {

bool WriteOrderedVarint32Slow(uint32_t data, riegeli::Writer& dest);
bool WriteOrderedVarint64Slow(uint64_t data, riegeli::Writer& dest);

}  // namespace internal

inline bool WriteOrderedVarint32(uint32_t data, Writer& dest) {
  if (ABSL_PREDICT_TRUE(data < 0x80)) {
    return dest.WriteByte(IntCast<uint8_t>(data));
  }
  return internal::WriteOrderedVarint32Slow(data, dest);
}

inline bool WriteOrderedVarint64(uint64_t data, Writer& dest) {
  if (ABSL_PREDICT_TRUE(data < 0x80)) {
    return dest.WriteByte(IntCast<uint8_t>(data));
  }
  return internal::WriteOrderedVarint64Slow(data, dest);
}

inline size_t LengthOrderedVarint32(uint32_t data) {
#if RIEGELI_INTERNAL_HAS_BUILTIN(__builtin_clz) || \
    RIEGELI_INTERNAL_IS_GCC_VERSION(3, 4)
  const size_t floor_log2 = IntCast<size_t>(
      sizeof(unsigned) >= 4 ? __builtin_clz(data | 1) ^ __builtin_clz(1)
                            : __builtin_clzl(data | 1) ^ __builtin_clzl(1));
  // This is the same as `floor_log2 / 7 + 1` for `floor_log2` in [0..31],
  // but performs division by a power of 2.
  return (floor_log2 * 9 + 72) / 64;
#else
  size_t length = 1;
  while (data >= 0x80) {
    ++length;
    data >>= 7;
  }
  return length;
#endif
}

inline size_t LengthOrderedVarint64(uint64_t data) {
#if RIEGELI_INTERNAL_HAS_BUILTIN(__builtin_clzll) || \
    RIEGELI_INTERNAL_IS_GCC_VERSION(3, 4)
  const size_t floor_log2 =
      IntCast<size_t>(__builtin_clzll(data | 1) ^ __builtin_clzll(1));
  // This is the same as `floor_log2 == 63 ? 9 : floor_log2 / 7 + 1`
  // for `floor_log2` in [0..63], but performs division by a power of 2
  // and does not need a special case for 63.
  return (floor_log2 * 9 + 72) / 64;
#else
  size_t length = 1;
  while (data >= 0x80) {
    ++length;
    data >>= 7;
  }
  return UnsignedMin(length, size_t{9});
#endif
}

}  // namespace riegeli

#endif  // RIEGELI_ORDERED_VARINT_ORDERED_VARINT_WRITING_H_
