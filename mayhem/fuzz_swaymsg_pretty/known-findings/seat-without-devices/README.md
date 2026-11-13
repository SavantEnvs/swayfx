# `swaymsg -t get_seats` aborts when a seat object has no `devices` array

- Where: `swaymsg/main.c`, `pretty_print_seat()` (line 166 in this tree): `json_object_array_length()` is
  called on the result of `json_object_object_get_ex()` without checking that the key exists.
- Cause: json-c asserts `json_object_get_type(jso) == json_type_array`; with the key missing (or not an
  array) the object is NULL (or another type), so the assertion fails and the process aborts (SIGABRT).
  The same pattern exists in the other `pretty_print_*` helpers for nested arrays.
- Reproduce: `./fuzz_swaymsg_pretty-standalone mayhem/fuzz_swaymsg_pretty/known-findings/seat-without-devices/repro.bin`
  (byte 0x07 selects `IPC_GET_SEATS`; payload `[{}]`): `json_object_array_length: Assertion ... failed`.
- Impact: swaymsg aborts on a malformed or version-skewed sway reply instead of printing what it can.
- Fix: check the return value of `json_object_object_get_ex()` / the object's type before calling
  `json_object_array_length()`.
