# `swaynag_parse_options()` runs getopt past argv's NULL terminator for a trailing `-b<text>`

- Where: `swaynag/config.c:159-181`, the `-b/-B/-z/-Z` button cases.
- Cause: the "Missing action for button" check (`optind >= argc`) only runs when `swaynag != NULL`, but
  `optind++` runs unconditionally. In the bare first pass that `main()` makes (`swaynag == NULL`, to find
  `--config`/`--debug`), a button option whose text is attached (`-bx`, `-Zx`) and has no following
  action argument leaves `optind == argc + 1`, and the next `getopt_long()` call dereferences NULL.
- Reproduce: `./fuzz_swaynag_args-standalone mayhem/fuzz_swaynag_args/known-findings/button-attached-text-no-action/repro.bin`
  (arguments: `swaynag`, `-bx`): SEGV in `_getopt_internal_r`, called from `config.c:154`.
- Impact: `swaynag -bx` crashes instead of printing an error (a local denial of service of the notifier).
- Fix: apply the `optind >= argc` check in both passes, or only `optind++` when an action was consumed.
