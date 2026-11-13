// Known-answer tests for the swayfx code the fuzz targets cover (string helpers, value parsers,
// swaynag option and config parsing, the swaybar status stream, swaymsg's reply printer).
// Built by mayhem/build.sh with the project's normal flags; mayhem/test.sh runs it and reads the
// `TEST_RESULT` line it prints.
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main swaymsg_main
#include "../../swaymsg/main.c"
#undef main

#include "list.h"
#include "log.h"
#include "loop.h"
#include "stringop.h"
#include "util.h"
#include "swaybar/bar.h"
#include "swaybar/i3bar.h"
#include "swaybar/status_line.h"
#include "swaynag_state.h"

static int passed, failed;

#define CHECK(cond) do { \
	if (cond) { passed++; } else { failed++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)
#define CHECK_STR(got, want) do { \
	const char *g_ = (got); const char *w_ = (want); \
	if (g_ && strcmp(g_, w_) == 0) { passed++; } else { failed++; \
		fprintf(stderr, "FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, \
			g_ ? g_ : "(null)", w_); } \
} while (0)

static void test_values(void) {
	uint32_t c = 0;
	CHECK(parse_color("#ff8000", &c) && c == 0xff8000ffu);
	CHECK(parse_color("12345678", &c) && c == 0x12345678u);
	c = 7;
	CHECK(!parse_color("#12", &c) && c == 7);
	CHECK(!parse_color("zzzzzz", &c) && c == 7);

	CHECK(parse_boolean("Yes", false));
	CHECK(parse_boolean("enabled", false));
	CHECK(!parse_boolean("no", true));
	CHECK(parse_boolean("toggle", false) && !parse_boolean("toggle", true));

	CHECK(parse_float("1.5") == 1.5f);
	CHECK(isnan(parse_float("1.5x")));

	struct movement_amount m;
	char *a1[] = {"10px"};
	CHECK(parse_movement_amount(1, a1, &m) == 1 && m.amount == 10 && m.unit == MOVEMENT_UNIT_PX);
	char *a2[] = {"25", "ppt"};
	CHECK(parse_movement_amount(2, a2, &m) == 2 && m.amount == 25 && m.unit == MOVEMENT_UNIT_PPT);
	char *a3[] = {"5"};
	CHECK(parse_movement_amount(1, a3, &m) == 1 && m.amount == 5 && m.unit == MOVEMENT_UNIT_DEFAULT);
}

static void test_strings(void) {
	int argc;
	char **argv = split_args("foo \"bar baz\" 'x y' [a b]  tail", &argc);
	CHECK(argc == 5);
	if (argc == 5) {
		CHECK_STR(argv[0], "foo");
		CHECK_STR(argv[1], "\"bar baz\"");
		CHECK_STR(argv[2], "'x y'");
		CHECK_STR(argv[3], "[a b]");
		CHECK_STR(argv[4], "tail");
		char *joined = join_args(argv, argc);
		CHECK_STR(joined, "foo \"bar baz\" 'x y' [a b] tail");
		free(joined);
	}
	free_argv(argc, argv);

	char q[] = "\"a b\" 'c' d\\\"e";
	strip_quotes(q);
	CHECK_STR(q, "a b c d\\\"e");

	char w[] = "  hi there \n";
	strip_whitespace(w);
	CHECK_STR(w, "hi there");

	char u[] = "a\\tb\\x41";
	int n = unescape_string(u);
	CHECK(n == 4 && strcmp(u, "a\tbA") == 0);

	char *s = strdup("one,two three");
	char *rest = s, matched = 0;
	char *first = argsep(&rest, ",", &matched);
	CHECK_STR(first, "one");
	CHECK(matched == ',');
	CHECK_STR(rest, "two three");
	char *second = argsep(&rest, ",", &matched);
	CHECK_STR(second, "two three");
	CHECK(rest == NULL && matched == '\0');
	free(s);

	list_t *parts = split_string("a b  c", " ");
	CHECK(parts->length == 3);
	if (parts->length == 3) {
		CHECK_STR(parts->items[2], "c");
	}
	list_free_items_and_destroy(parts);
	CHECK(has_prefix("output HDMI-A-1", "output"));
	CHECK(!has_prefix("out", "output"));
}

static void test_swaynag_options(void) {
	struct nag_state s;
	nag_state_init(&s);
	struct swaynag_type *type = swaynag_type_new("<args>");
	list_add(s.types, type);
	char *argv[] = {"swaynag", "-m", "hello", "-B", "Yes", "true", "-z", "Close", "reboot",
		"--background", "ff0000", "--border-bottom-size", "9", "-e", "bottom", "-t", "warning", NULL};
	int rc = swaynag_parse_options(17, argv, &s.nag, s.types, type, NULL, NULL);
	CHECK(rc == 0);
	CHECK_STR(s.nag.message, "hello");
	CHECK(s.nag.buttons->length == 3);
	if (s.nag.buttons->length == 3) {
		struct swaynag_button *b = s.nag.buttons->items[1];
		CHECK_STR(b->text, "Yes");
		CHECK_STR(b->action, "true");
		CHECK(!b->terminal && !b->dismiss);
		b = s.nag.buttons->items[2];
		CHECK(b->dismiss && b->terminal == false);
	}
	CHECK(type->background == 0xff0000ffu);
	CHECK(type->bar_border_thickness == 9);
	CHECK(s.nag.type && strcmp(s.nag.type->name, "warning") == 0);
	CHECK(type->anchors != 0);
	nag_state_merge(&s);
	nag_state_free(&s);

	nag_state_init(&s);
	type = swaynag_type_new("<args>");
	list_add(s.types, type);
	char *bad[] = {"swaynag", "-e", "sideways", NULL};
	CHECK(swaynag_parse_options(3, bad, &s.nag, s.types, type, NULL, NULL) != 0);
	char *noaction[] = {"swaynag", "-b", "Only", NULL};
	CHECK(swaynag_parse_options(3, noaction, &s.nag, s.types, type, NULL, NULL) != 0);
	nag_state_free(&s);
}

static void test_swaynag_config(void) {
	char path[] = "/tmp/swayfx-selftest-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	if (fd < 0) {
		return;
	}
	const char *conf =
		"# a comment\n"
		"background=112233\n"
		"border-bottom-size=7\n"
		"\n"
		"[warning]\n"
		"text=00ff00\n"
		"edge=bottom\n";
	CHECK(write(fd, conf, strlen(conf)) == (ssize_t)strlen(conf));
	close(fd);

	struct nag_state s;
	nag_state_init(&s);
	CHECK(swaynag_load_config(path, &s.nag, s.types) == 0);
	struct swaynag_type *general = swaynag_type_get(s.types, "<config>");
	CHECK(general != NULL);
	if (general) {
		CHECK(general->background == 0x112233ffu);
		CHECK(general->bar_border_thickness == 7);
	}
	struct swaynag_type *warning = swaynag_type_get(s.types, "warning");
	CHECK(warning != NULL);
	if (warning) {
		CHECK(warning->text == 0x00ff00ffu);
	}
	nag_state_free(&s);

	fd = open(path, O_WRONLY | O_TRUNC);
	const char *broken = "[unterminated\n";
	CHECK(write(fd, broken, strlen(broken)) == (ssize_t)strlen(broken));
	close(fd);
	nag_state_init(&s);
	CHECK(swaynag_load_config(path, &s.nag, s.types) != 0);
	nag_state_free(&s);
	unlink(path);
}

static struct status_line *status_with(const char *stream, struct swaybar *bar, int *keep) {
	int out[2], in[2];
	CHECK(pipe(out) == 0 && pipe(in) == 0);
	fcntl(out[0], F_SETFL, O_NONBLOCK);
	fcntl(in[1], F_SETFL, O_NONBLOCK);
	struct status_line *st = calloc(1, sizeof(*st));
	st->bar = bar;
	st->buffer_size = 8192;
	st->buffer = malloc(st->buffer_size);
	st->read_fd = out[0];
	st->write_fd = in[1];
	st->read = fdopen(dup(out[0]), "r");
	CHECK(write(out[1], stream, strlen(stream)) == (ssize_t)strlen(stream));
	keep[0] = out[1];
	keep[1] = in[0];
	return st;
}

static void status_done(struct status_line *st, int *keep) {
	if (st->tokener) {
		struct i3bar_block *b, *t;
		wl_list_for_each_safe(b, t, &st->blocks, link) {
			wl_list_remove(&b->link);
			i3bar_block_unref(b);
		}
		json_tokener_free(st->tokener);
	}
	if (st->read_fd != -1) close(st->read_fd);
	if (st->write_fd != -1) close(st->write_fd);
	fclose(st->read);
	free(st->buffer);
	free(st);
	close(keep[0]);
	close(keep[1]);
}

static void test_status_stream(void) {
	struct swaybar bar = {0};
	bar.eventloop = loop_create();
	int keep[2];

	struct status_line *st = status_with(
		"{\"version\":1,\"click_events\":true}\n[\n"
		"[{\"full_text\":\"hello\",\"color\":\"#ff0000\",\"name\":\"a\"},"
		"{\"full_text\":\"world\",\"separator\":false,\"min_width\":30}],\n"
		"[{\"full_text\":\"later\"}],\n", &bar, keep);
	CHECK(status_handle_readable(st) == true);
	CHECK(st->protocol == PROTOCOL_I3BAR);
	CHECK(st->click_events);
	CHECK(wl_list_length(&st->blocks) == 1);
	struct i3bar_block *only = wl_container_of(st->blocks.next, only, link);
	CHECK_STR(only->full_text, "later");
	CHECK_STR(only->align, "left");
	CHECK(only->separator);
	status_done(st, keep);

	st = status_with(
		"{\"version\":1}\n[\n"
		"[{\"full_text\":\"hello\",\"color\":\"#ff0000\",\"name\":\"a\"},"
		"{\"full_text\":\"world\",\"separator\":false,\"min_width\":30}],\n", &bar, keep);
	CHECK(status_handle_readable(st) == true);
	CHECK(wl_list_length(&st->blocks) == 2);
	if (wl_list_length(&st->blocks) == 2) {
		// blocks are inserted at the head, so the last JSON element comes first
		struct i3bar_block *w = wl_container_of(st->blocks.next, w, link);
		struct i3bar_block *h = wl_container_of(st->blocks.prev, h, link);
		CHECK_STR(h->full_text, "hello");
		CHECK(h->color_set && h->color == 0xff0000ffu);
		CHECK_STR(h->name, "a");
		CHECK_STR(w->full_text, "world");
		CHECK(!w->separator && w->min_width == 30);
	}
	status_done(st, keep);

	st = status_with("{\"version\":1}\n[\n{\"oops\":", &bar, keep);
	CHECK(status_handle_readable(st) == false);
	CHECK(st->protocol == PROTOCOL_I3BAR && st->read_fd != -1);
	status_done(st, keep);

	st = status_with("{\"version\":1}\n[\nnot json,\n", &bar, keep);
	CHECK(status_handle_readable(st) == true);
	CHECK(st->protocol == PROTOCOL_ERROR);
	CHECK_STR(st->text, "[failed to parse i3bar json]");
	status_done(st, keep);

	st = status_with("plain status text\n", &bar, keep);
	CHECK(status_handle_readable(st) == true);
	CHECK(st->protocol == PROTOCOL_TEXT);
	CHECK_STR(st->text, "plain status text");
	status_done(st, keep);
	loop_destroy(bar.eventloop);
}

static char *capture_pretty(int type, const char *json) {
	fflush(stdout);
	char path[] = "/tmp/swayfx-selftest-out-XXXXXX";
	int fd = mkstemp(path);
	int saved = dup(STDOUT_FILENO);
	dup2(fd, STDOUT_FILENO);
	json_object *obj = json_tokener_parse(json);
	pretty_print(type, obj);
	json_object_put(obj);
	fflush(stdout);
	dup2(saved, STDOUT_FILENO);
	close(saved);
	char *buf = calloc(1, 4096);
	lseek(fd, 0, SEEK_SET);
	ssize_t n = read(fd, buf, 4095);
	if (n < 0) {
		n = 0;
	}
	buf[n] = '\0';
	close(fd);
	unlink(path);
	return buf;
}

static void test_swaymsg(void) {
	json_object *obj = json_tokener_parse("[{\"success\":true},{\"success\":false}]");
	CHECK(!success(obj, true));
	json_object_put(obj);
	obj = json_tokener_parse("[{\"success\":true}]");
	CHECK(success(obj, false));
	json_object_put(obj);
	obj = json_tokener_parse("{\"parse_error\":false}");
	CHECK(success(obj, false));
	json_object_put(obj);
	obj = json_tokener_parse("[]");
	CHECK(success(obj, false) == false && success(obj, true) == true);
	json_object_put(obj);

	char *out = capture_pretty(IPC_GET_VERSION, "{\"human_readable\":\"1.12.0-swayfx\"}");
	CHECK_STR(out, "sway version 1.12.0-swayfx\n");
	free(out);

	out = capture_pretty(IPC_GET_MARKS, "[\"a\",\"b\"]");
	CHECK(strstr(out, "\"a\"") && strstr(out, "\"b\"") && strchr(out, '\n'));
	free(out);

	out = capture_pretty(IPC_COMMAND, "[{\"success\":false,\"error\":\"nope\"}]");
	CHECK(strstr(out, "nope") != NULL);
	free(out);
}

int main(void) {
	sway_log_init(SWAY_SILENT, NULL);
	test_values();
	test_strings();
	test_swaynag_options();
	test_swaynag_config();
	test_status_stream();
	test_swaymsg();
	printf("TEST_RESULT passed=%d failed=%d\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
