# `unescape_string()` reads past the end of the string on a trailing backslash

- Where: `common/stringop.c:158`, `switch (string[++i])` in `unescape_string()`.
- Cause: when the last character is `\`, `++i` steps onto the terminating NUL and the loop's
  `for (...; string[i]; ++i)` then increments past it, reading beyond the allocation.
- Reproduce: `./fuzz_stringop-standalone mayhem/fuzz_stringop/known-findings/unescape-string-trailing-backslash/repro.bin`
  (one byte, `\`): ASan heap-buffer-overflow READ of size 1, `unescape_string` at `stringop.c:156`.
- Impact: out-of-bounds read of one byte for any caller that unescapes user-controlled text
  (e.g. `swaynag --message`, `bindsym` strings).
- Fix: stop at the NUL: `if (string[i] == '\\' && string[i + 1]) { ... }`, or break when `string[++i]` is `\0`.
