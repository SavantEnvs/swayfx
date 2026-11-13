# `swaymsg -t get_inputs` passes NULL to `strcmp` when an input has no `type`

- Where: `swaymsg/main.c`, `pretty_type_name()` (line 107 in this tree), reached from
  `pretty_print_input()`: `json_object_get_string()` of the missing `type` key is NULL and is handed
  to `strcmp(type_names[i].a, name)`.
- Cause: the lookup result of `json_object_object_get_ex()` is used without checking that the key exists
  (same missing-key root cause as `seat-without-devices`, a different call site). glibc's `strcmp` reads
  address 0.
- Reproduce: `./fuzz_swaymsg_pretty-standalone mayhem/fuzz_swaymsg_pretty/known-findings/input-without-type/repro.bin`
  (byte 0x06 selects `IPC_GET_INPUTS`; payload `[{"identifier":"x","name":"y"}]`): UBSan reports
  `null pointer passed as argument 2, which is declared to never be null` at `main.c:107`.
- Impact: swaymsg crashes on a malformed or version-skewed sway reply.
- Fix: print the raw type (or skip it) when `type` is absent.
