// The state swaynag's main() builds before it parses options, and the cleanup that does not need
// a Wayland display (swaynag_destroy() tears down surfaces, so it is not used).
#ifndef MAYHEM_SWAYNAG_STATE_H
#define MAYHEM_SWAYNAG_STATE_H
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "swaynag/config.h"
#include "swaynag/swaynag.h"
#include "swaynag/types.h"

// swaynag_parse_options() only sets optind = 1. glibc's getopt_long() keeps the position inside the
// current argument ("nextchar", a pointer into the previous input's freed buffer) unless optind is
// 0, so an input that ends in the middle of an option cluster would leak into the next one.
// Setting optind to 0 makes the next call start from scratch; the empty call below does that.
static void nag_reset_getopt(void) {
	char *none[] = {"swaynag", NULL};
	optind = 0;
	getopt(1, none, "");
}

struct nag_state {
	struct swaynag nag;
	list_t *types;
};

static void nag_state_init(struct nag_state *s) {
	memset(s, 0, sizeof(*s));
	s->types = create_list();
	swaynag_types_add_default(s->types);

	s->nag.buttons = create_list();
	wl_list_init(&s->nag.outputs);
	wl_list_init(&s->nag.seats);

	struct swaynag_button *button_close = calloc(1, sizeof(struct swaynag_button));
	button_close->text = strdup("X");
	button_close->type = SWAYNAG_ACTION_DISMISS;
	list_add(s->nag.buttons, button_close);

	s->nag.details.details_text = strdup("Toggle details");
}

// main()'s "defaults, config, type, args" merge into the type that would be rendered.
static void nag_state_merge(struct nag_state *s) {
	if (!s->nag.type) {
		s->nag.type = swaynag_type_get(s->types, "error");
	}
	struct swaynag_type *type = swaynag_type_new(s->nag.type->name);
	swaynag_type_merge(type, swaynag_type_get(s->types, "<defaults>"));
	swaynag_type_merge(type, swaynag_type_get(s->types, "<config>"));
	swaynag_type_merge(type, s->nag.type);
	swaynag_type_merge(type, swaynag_type_get(s->types, "<args>"));
	swaynag_type_free(type);
}

static void nag_state_free(struct nag_state *s) {
	for (int i = 0; i < s->nag.buttons->length; i++) {
		struct swaynag_button *button = s->nag.buttons->items[i];
		free(button->text);
		free(button->action);
		free(button);
	}
	list_free(s->nag.buttons);
	free(s->nag.message);
	free(s->nag.details.message);
	free(s->nag.details.details_text);
	free(s->nag.details.button_up.text);
	free(s->nag.details.button_down.text);
	swaynag_types_free(s->types);
}
#endif
