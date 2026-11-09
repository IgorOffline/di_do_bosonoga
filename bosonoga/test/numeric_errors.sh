#!/bin/sh
# The normal .bosonoga suite checks successful programs; these must fail.
set -eu
cd "$(dirname "$0")/../.."

test_dir=$(mktemp -d)
trap 'rm -f "$test_dir/case.bosonoga"; rmdir "$test_dir"' EXIT HUP INT TERM

check_error() {
  expected=$1
  statement=$2
  printf '%s\nbosonoga_main\ndi\n  %s\n  return exit_success\ndo\n' \
    "${3:-}" "$statement" > "$test_dir/case.bosonoga"
  if output=$(G_MESSAGES_DEBUG=all bosonoga/build/test "$test_dir/case.bosonoga" 2>&1); then
    printf 'Expected failure for: %s\n' "$statement" >&2
    exit 1
  fi
  case "$output" in
    *"$expected"*) ;;
    *) printf 'Expected error containing: %s\n%s\n' "$expected" "$output" >&2; exit 1 ;;
  esac
}

check_error 'to_abs result is outside the i32 range' 'var value is to_abs -2147483648_i32'
for operation in to_abs to_min to_max; do
  check_error "$operation needs i32 or f32" "var value is $operation shift_0"
  check_error "$operation needs i32 or f32" "var value is $operation \"text\"_str"
done
for operation in to_min to_max; do
  check_error "$operation needs i32" "var value is $operation 1_i32 2_f32"
  check_error "$operation needs f32" "var value is $operation 1_f32 2_i32"
  check_error 'expected a value' "var value is $operation 1_i32"
  check_error 'expected a statement' "var value is $operation 1_i32 2_i32 3_i32"
done
for operation in to_floor to_ceil to_round; do
  check_error "$operation needs f32" "var value is $operation 1_i32"
done
check_error "'value' is i32, but the conversion gives f32" 'var value is 1_i32
  set value to_abs -1_f32'
check_error "'value' is f32, but the conversion gives i32" 'var value is 1_f32
  set value to_min 1_i32 2_i32'
check_error 'expected a variable name' 'var to_round is 1_i32'

# Both precisions enforce domains and finite results.
for precision in f32 f64; do
  for operation in to_log to_log10; do
    check_error 'logarithms need a positive value' "var value is $operation 0_$precision"
    check_error 'logarithms need a positive value' "var value is $operation -1_$precision"
  done
  for operation in to_asin to_acos; do
    check_error 'needs a value from -1 to 1' "var value is $operation 1d01_$precision"
    check_error 'needs a value from -1 to 1' "var value is $operation -1d01_$precision"
  done
  check_error "outside the $precision range" "var value is to_exp 1000_$precision"
  check_error 'division by zero' "var value is 0_$precision
    set value to_divide 1_$precision 0_$precision"
  check_error 'negative number' "var value is to_sqrt -1_$precision"
  check_error 'fractional power' "var value is to_pow -2_$precision 0d5_$precision"
done
check_error 'needs f64' 'var value is to_pow 2_f64 3_f32'
check_error 'cannot compare f32 with f64' 'if 1_f32 eq_6 1_f64
  di
    nihil
  do'
check_error 'outside the i32 range' 'var value is to_i32 2147483648_f64'
check_error 'outside the f32 range' 'var value is to_pow 10_f64 100_f64
  var narrow is to_f32 value'
check_error 'outside the f64 range' 'var value is to_pow 10_f64 308_f64
  set value to_multiply value 10_f64'
check_error 'cannot be set' 'set pi to_add 4_f64' 'const pi is 3d14_f64'
check_error 'constant with this name already exists' 'var pi is 1_f64' 'const pi is 3d14_f64'
check_error 'constant with this name already exists' 'nihil' 'const pi is 3d14_f64
const pi is 3_f64'
check_error 'already a constant' 'nihil' 'const table is 1_i32
array table 0_i32
di
  row 1_i32
do'
check_error 'literal or an earlier constant' 'nihil' 'bosonoga_global
di
  var mutable is 1_i32
do
const frozen is mutable'
check_error 'unknown variable' 'nihil' 'const first is later
const later is 1_f64'

check_error 'f64 has no exact eq' 'if 1_f64 eq 1_f64
  di
    nihil
  do'
check_error 'needs f64' 'var value is to_min 1_f64 2_f32'
check_error 'conversion gives f64' 'var value is 1_f32
  set value to_abs -1_f64'
check_error 'outside the i32 range' 'var value is to_i32 -2147483649_f64'
check_error 'constant with this name already exists' 'nihil' 'const argument_f64 is 1_f64
def example_fn argument_f64 ret 0_f64
di
  return argument_f64
do'
check_error 'expected a value' 'nihil' 'const calculated is to_sqrt 2_f64'
check_error 'real number does not fit in 64 bits' 'var value is 9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999_f64'

for precision in f32 f64; do
  check_error 'to_log1p needs a value greater than -1' "var value is to_log1p -1_$precision"
  check_error 'to_log1p needs a value greater than -1' "var value is to_log1p -2_$precision"
  check_error "outside the $precision range" "var value is to_expm1 1000_$precision"
done
for operation in to_log1p to_expm1 to_hypot; do
  check_error "$operation needs f32 or f64" "var value is $operation 1_i32"
  check_error 'expected a variable name' "var $operation is 1_i32"
done
check_error 'to_hypot needs f64' 'var value is to_hypot 3_f64 4_f32'
check_error 'expected a value' 'var value is to_hypot 3_f64'
check_error 'expected a statement' 'var value is to_hypot 3_f64 4_f64 5_f64'
check_error 'outside the f32 range' 'var value is to_pow 10_f32 38d5_f32
  set value to_hypot value value'
check_error 'outside the f64 range' 'var value is to_pow 10_f64 308_f64
  set value to_multiply value 1d7_f64
  set value to_hypot value value'
