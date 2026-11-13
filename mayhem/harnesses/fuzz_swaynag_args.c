// swaynag command line: swaynag_parse_options() run over an argv, in the two passes main() makes
// (a bare pass for --config/--debug, then one that fills the swaynag state), then the type merge.
//
// Input: the arguments, one per line (argv[0] is "swaynag").
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "swaynag_state.h"

#define MAX_ARGS 64

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	sway_log_init(SWAY_SILENT, NULL);
	// --detailed-message reads the message from stdin; keep it empty and deterministic.
	if (!freopen("/dev/null", "r", stdin) || !freopen("/dev/null", "w", stdout)) {
		return 1;
	}
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	char *text = malloc(size + 1);
	memcpy(text, data, size);
	text[size] = '\0';

	char *args[MAX_ARGS + 2];
	int argc = 0;
	args[argc++] = "swaynag";
	for (char *p = text; argc < MAX_ARGS + 1;) {
		args[argc++] = p;
		char *nl = strchr(p, '\n');
		if (!nl) {
			break;
		}
		*nl = '\0';
		p = nl + 1;
	}
	args[argc] = NULL;

	struct nag_state s;
	nag_state_init(&s);

	char *config_path = NULL;
	bool debug = false;
	nag_reset_getopt();
	int status = swaynag_parse_options(argc, args, NULL, NULL, NULL, &config_path, &debug);
	if (status == 0 && argc > 1) {
		struct swaynag_type *type_args = swaynag_type_new("<args>");
		list_add(s.types, type_args);
		nag_reset_getopt();
		status = swaynag_parse_options(argc, args, &s.nag, s.types, type_args, NULL, NULL);
		if (status == 0) {
			nag_state_merge(&s);
		}
	}

	free(config_path);
	nag_state_free(&s);
	free(text);
	return 0;
}
