// sway's string helpers that parse command text and configuration values (common/stringop.c and
// common/util.c): argument splitting with quote/bracket handling, argsep, unescaping, quote and
// whitespace stripping, and the scalar value parsers. expand_path() is left out: it runs the
// string through wordexp(3), which executes command substitutions.
//
// Input: one string; every helper runs on its own fresh copy.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	sway_log_init(SWAY_SILENT, NULL);
	return 0;
}

static char *copy_of(const uint8_t *data, size_t size) {
	char *s = malloc(size + 1);
	memcpy(s, data, size);
	s[size] = '\0';
	return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	char *s = copy_of(data, size);

	int argc;
	char **argv = split_args(s, &argc);
	if (argc > 0) {
		char *joined = join_args(argv, argc);
		free(joined);
		struct movement_amount amount;
		parse_movement_amount(argc, argv, &amount);
	}
	free_argv(argc, argv);

	static const char *delims[] = {" ", ",;", "\n"};
	for (size_t i = 0; i < sizeof(delims) / sizeof(delims[0]); i++) {
		char *work = copy_of(data, size);
		char *rest = work;
		char matched;
		while (rest) {
			argsep(&rest, delims[i], &matched);
		}
		free(work);
		work = copy_of(data, size);
		rest = work;
		while (rest) {
			argsep(&rest, delims[i], NULL);
		}
		free(work);

		list_t *parts = split_string(s, delims[i]);
		list_free_items_and_destroy(parts);
	}

	char *work = copy_of(data, size);
	strip_whitespace(work);
	free(work);
	work = copy_of(data, size);
	strip_quotes(work);
	free(work);
	work = copy_of(data, size);
	unescape_string(work);
	free(work);

	uint32_t color;
	parse_color(s, &color);
	parse_boolean(s, false);
	parse_boolean(s, true);
	parse_float(s);
	parse_movement_unit(s);
	has_prefix(s, "output");
	free(s);
	return 0;
}
