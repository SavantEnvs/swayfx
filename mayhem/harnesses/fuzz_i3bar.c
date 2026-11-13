// swaybar status_command reader: the i3bar-protocol / plain-text status stream that
// swaybar reads from the (untrusted) status_command's stdout.
//
// The real swaybar polls the status pipe's read end (non-blocking) and calls
// status_handle_readable() each time it is readable. The harness builds the same state by hand
// (status_line_init() would fork `sh -c`): a non-blocking pipe preloaded with the input, with the
// WRITE end kept open so the read yields EAGAIN rather than EOF, exactly as with a live command.
//
// Input: byte 0 bit 0 selects whether the stream arrives in one chunk or two (cut in the middle);
// the remaining bytes are the status command's output.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <json.h>
#include "log.h"
#include "loop.h"
#include "swaybar/bar.h"
#include "swaybar/i3bar.h"
#include "swaybar/status_line.h"

// A single non-blocking write must fit the default 64 KiB pipe buffer.
#define MAX_STREAM 60000

static struct swaybar bar;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	sway_log_init(SWAY_SILENT, NULL);
	bar.eventloop = loop_create();
	return bar.eventloop ? 0 : 1;
}

static void feed(int fd, const uint8_t *data, size_t len) {
	if (len > 0 && write(fd, data, len) != (ssize_t)len) {
		abort();
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	// swaybar calls status_handle_readable() only when poll() reports data on the status pipe,
	// whose write end stays open: at least one stream byte follows the mode byte.
	if (size < 2 || size > MAX_STREAM) {
		return -1;
	}
	bool split = (data[0] & 1) && size > 2;
	data++;
	size--;

	int out[2], in[2];
	if (pipe(out) != 0 || pipe(in) != 0) {
		abort();
	}
	fcntl(out[0], F_SETFL, O_NONBLOCK);
	fcntl(in[1], F_SETFL, O_NONBLOCK);

	struct status_line *status = calloc(1, sizeof(*status));
	status->bar = &bar;
	status->stop_signal = 19;
	status->cont_signal = 18;
	status->buffer_size = 8192;
	status->buffer = malloc(status->buffer_size);
	status->read_fd = out[0];
	status->write_fd = in[1];
	// getline() in the text protocol reads through a FILE on the same open file description.
	status->read = fdopen(dup(out[0]), "r");

	size_t first = split ? size / 2 : size;
	feed(out[1], data, first);
	status_handle_readable(status);
	if (split) {
		feed(out[1], data + first, size - first);
		status_handle_readable(status);
	}

	if (status->tokener) {
		struct i3bar_block *block, *tmp;
		wl_list_for_each_safe(block, tmp, &status->blocks, link) {
			wl_list_remove(&block->link);
			i3bar_block_unref(block);
		}
		json_tokener_free(status->tokener);
	}
	if (status->read_fd != -1) {
		close(status->read_fd);
	}
	if (status->write_fd != -1) {
		close(status->write_fd);
	}
	fclose(status->read);
	free(status->buffer);
	free(status);
	close(out[1]);
	close(in[0]);
	return 0;
}
