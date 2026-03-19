// overflow_test.cpp — Runtime-level tests for explicit overflow operations.
//
// These tests call the C runtime hooks directly to verify actual runtime
// behavior (wrapping semantics, saturating clamping, boundary values).
// The compiler-level tests only verify IR declarations; these verify
// that the implementations produce correct results.
//
// Authority: docs/contracts/CONTRACT_NUMERIC_SEMANTICS.md section 4.2

#include "dao_abi.h"

#include <boost/ut.hpp>
#include <cstdint>

using namespace boost::ut;

// NOLINTBEGIN(readability-magic-numbers)

// ---------------------------------------------------------------------------
// i8 wrapping
// ---------------------------------------------------------------------------

suite<"wrapping_i8"> wrapping_i8 = [] {
  "wrapping_add_i8 identity"_test = [] {
    expect(eq(__dao_wrapping_add_i8(10, 20), int8_t{30}));
  };

  "wrapping_add_i8 overflow wraps"_test = [] {
    // 127 + 1 wraps to -128
    expect(eq(__dao_wrapping_add_i8(INT8_MAX, 1), INT8_MIN));
  };

  "wrapping_sub_i8 underflow wraps"_test = [] {
    // -128 - 1 wraps to 127
    expect(eq(__dao_wrapping_sub_i8(INT8_MIN, 1), INT8_MAX));
  };

  "wrapping_mul_i8 overflow wraps"_test = [] {
    // 127 * 2 = 254 -> wraps to -2 (254 as int8_t)
    expect(eq(__dao_wrapping_mul_i8(INT8_MAX, 2), int8_t{-2}));
  };
};

// ---------------------------------------------------------------------------
// i16 wrapping
// ---------------------------------------------------------------------------

suite<"wrapping_i16"> wrapping_i16 = [] {
  "wrapping_add_i16 identity"_test = [] {
    expect(eq(__dao_wrapping_add_i16(100, 200), int16_t{300}));
  };

  "wrapping_add_i16 overflow wraps"_test = [] {
    expect(eq(__dao_wrapping_add_i16(INT16_MAX, 1), INT16_MIN));
  };

  "wrapping_sub_i16 underflow wraps"_test = [] {
    expect(eq(__dao_wrapping_sub_i16(INT16_MIN, 1), INT16_MAX));
  };

  "wrapping_mul_i16 overflow wraps"_test = [] {
    // 32767 * 2 = 65534 -> wraps to -2 (65534 as int16_t)
    expect(eq(__dao_wrapping_mul_i16(INT16_MAX, 2), int16_t{-2}));
  };

  "wrapping_mul_i16 extreme values"_test = [] {
    // This is the case that triggered the integer-promotion UB:
    // (uint16_t)32767 * (uint16_t)32767 = 1073676289, which overflows
    // signed int on 32-bit-int targets when promoted from uint16_t.
    // Expected: (32767 * 32767) mod 65536 = 1073676289 mod 65536 = 1
    expect(eq(__dao_wrapping_mul_i16(INT16_MAX, INT16_MAX), int16_t{1}));
  };

  "wrapping_mul_i16 near-max negative"_test = [] {
    // -32768 * -1 = 32768, wraps to -32768 (mod 65536)
    expect(eq(__dao_wrapping_mul_i16(INT16_MIN, -1), INT16_MIN));
  };
};

// ---------------------------------------------------------------------------
// i32 wrapping
// ---------------------------------------------------------------------------

suite<"wrapping_i32"> wrapping_i32 = [] {
  "wrapping_add_i32 overflow wraps"_test = [] {
    expect(eq(__dao_wrapping_add_i32(INT32_MAX, 1), INT32_MIN));
  };

  "wrapping_sub_i32 underflow wraps"_test = [] {
    expect(eq(__dao_wrapping_sub_i32(INT32_MIN, 1), INT32_MAX));
  };

  "wrapping_mul_i32 overflow wraps"_test = [] {
    expect(eq(__dao_wrapping_mul_i32(INT32_MAX, 2), int32_t{-2}));
  };
};

// ---------------------------------------------------------------------------
// i64 wrapping
// ---------------------------------------------------------------------------

suite<"wrapping_i64"> wrapping_i64 = [] {
  "wrapping_add_i64 overflow wraps"_test = [] {
    expect(eq(__dao_wrapping_add_i64(INT64_MAX, 1), INT64_MIN));
  };

  "wrapping_mul_i64 overflow wraps"_test = [] {
    expect(eq(__dao_wrapping_mul_i64(INT64_MAX, 2), int64_t{-2}));
  };
};

// ---------------------------------------------------------------------------
// i8 saturating
// ---------------------------------------------------------------------------

suite<"saturating_i8"> saturating_i8 = [] {
  "saturating_add_i8 clamps to max"_test = [] {
    expect(eq(__dao_saturating_add_i8(INT8_MAX, 1), INT8_MAX));
  };

  "saturating_sub_i8 clamps to min"_test = [] {
    expect(eq(__dao_saturating_sub_i8(INT8_MIN, 1), INT8_MIN));
  };

  "saturating_mul_i8 clamps to max"_test = [] {
    expect(eq(__dao_saturating_mul_i8(INT8_MAX, 2), INT8_MAX));
  };

  "saturating_mul_i8 clamps to min"_test = [] {
    expect(eq(__dao_saturating_mul_i8(INT8_MIN, 2), INT8_MIN));
  };
};

// ---------------------------------------------------------------------------
// i16 saturating
// ---------------------------------------------------------------------------

suite<"saturating_i16"> saturating_i16 = [] {
  "saturating_add_i16 clamps to max"_test = [] {
    expect(eq(__dao_saturating_add_i16(INT16_MAX, 1), INT16_MAX));
  };

  "saturating_sub_i16 clamps to min"_test = [] {
    expect(eq(__dao_saturating_sub_i16(INT16_MIN, 1), INT16_MIN));
  };

  "saturating_mul_i16 clamps to max"_test = [] {
    expect(eq(__dao_saturating_mul_i16(INT16_MAX, 2), INT16_MAX));
  };

  "saturating_mul_i16 clamps to min"_test = [] {
    expect(eq(__dao_saturating_mul_i16(INT16_MIN, 2), INT16_MIN));
  };
};

// ---------------------------------------------------------------------------
// i32 saturating
// ---------------------------------------------------------------------------

suite<"saturating_i32"> saturating_i32 = [] {
  "saturating_add_i32 clamps to max"_test = [] {
    expect(eq(__dao_saturating_add_i32(INT32_MAX, 1), INT32_MAX));
  };

  "saturating_sub_i32 clamps to min"_test = [] {
    expect(eq(__dao_saturating_sub_i32(INT32_MIN, 1), INT32_MIN));
  };

  "saturating_mul_i32 clamps to max"_test = [] {
    expect(eq(__dao_saturating_mul_i32(INT32_MAX, 2), INT32_MAX));
  };
};

// ---------------------------------------------------------------------------
// i64 saturating
// ---------------------------------------------------------------------------

suite<"saturating_i64"> saturating_i64 = [] {
  "saturating_add_i64 clamps to max"_test = [] {
    expect(eq(__dao_saturating_add_i64(INT64_MAX, 1), INT64_MAX));
  };

  "saturating_sub_i64 clamps to min"_test = [] {
    expect(eq(__dao_saturating_sub_i64(INT64_MIN, 1), INT64_MIN));
  };

  "saturating_mul_i64 clamps to max"_test = [] {
    expect(eq(__dao_saturating_mul_i64(INT64_MAX, 2), INT64_MAX));
  };

  "saturating_mul_i64 clamps to min"_test = [] {
    expect(eq(__dao_saturating_mul_i64(INT64_MIN, 2), INT64_MIN));
  };
};

// NOLINTEND(readability-magic-numbers)

auto main() -> int {} // NOLINT(readability-named-parameter)
