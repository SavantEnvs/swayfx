// swaynag config file: swaynag_load_config() over a config file, then the type merge.
// The loader takes a path, so each input is written to one scratch file that is rewritten per
// input (the state of a run never depends on a previous input).
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "swaynag_state.h"

static char path[4096];

static void remove_scratch(void) {
	unlink(path);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	sway_log_init(SWAY_SILENT, NULL);
	if (!freopen("/dev/null", "r", stdin) || !freopen("/dev/null", "w", stdout)) {
		return 1;
	}
	const char *tmp = getenv("TMPDIR");
	snprintf(path, sizeof(path), "%s/swaynag-config-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	int fd = mkstemp(path);
	if (fd < 0) {
		return 1;
	}
	close(fd);
	atexit(remove_scratch);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	int fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0) {
		abort();
	}
	size_t off = 0;
	while (off < size) {
		ssize_t n = write(fd, data + off, size - off);
		if (n <= 0) {
			abort();
		}
		off += n;
	}
	close(fd);

	struct nag_state s;
	nag_state_init(&s);
	nag_reset_getopt();
	if (swaynag_load_config(path, &s.nag, s.types) == 0) {
		nag_state_merge(&s);
	}
	nag_state_free(&s);
	return 0;
}
