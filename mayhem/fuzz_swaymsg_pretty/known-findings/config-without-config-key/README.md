# `swaymsg -t get_config` prints a NULL string when the reply has no `config` key

- Where: `swaymsg/main.c`, `pretty_print_config()` (line 321 in this tree): `printf("%s\n", ...)` of
  `json_object_get_string()` on the missing `config` key, which is NULL (the compiler turns it into
  `puts(NULL)`).
- Cause: same missing-key root cause as `seat-without-devices`: the result of
  `json_object_object_get_ex()` is not checked.
- Reproduce: `./fuzz_swaymsg_pretty-standalone mayhem/fuzz_swaymsg_pretty/known-findings/config-without-config-key/repro.bin`
  (byte 0x05 selects `IPC_GET_CONFIG`; payload `{}`): SEGV in `puts` called from `main.c:321`.
- Impact: swaymsg crashes on a malformed or version-skewed sway reply.
- Fix: check that `config` exists (and is a string) before printing it.
