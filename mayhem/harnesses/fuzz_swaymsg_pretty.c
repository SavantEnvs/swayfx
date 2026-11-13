// swaymsg: parse and pretty-print the JSON reply of the sway IPC socket, the way swaymsg's
// main() does after ipc_single_command() returns. pretty_print() and success() are static, so
// swaymsg/main.c is included into this translation unit (with main renamed).
//
// Input: byte 0 selects the IPC message type the reply is for; the rest is the reply payload.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main swaymsg_main
#include "../../swaymsg/main.c"
#undef main

static const int types[] = {
	IPC_COMMAND, IPC_GET_WORKSPACES, IPC_GET_OUTPUTS, IPC_GET_TREE, IPC_GET_VERSION,
	IPC_GET_CONFIG, IPC_GET_INPUTS, IPC_GET_SEATS, IPC_GET_MARKS, IPC_GET_BAR_CONFIG,
	IPC_GET_BINDING_MODES, IPC_SEND_TICK, IPC_SYNC, IPC_GET_BINDING_STATE, IPC_SUBSCRIBE,
};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
	sway_log_init(SWAY_SILENT, NULL);
	if (!freopen("/dev/null", "w", stdout)) {
		return 1;
	}
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size < 2) {
		return -1;
	}
	int type = types[data[0] % (sizeof(types) / sizeof(types[0]))];
	char *resp = malloc(size);
	memcpy(resp, data + 1, size - 1);
	resp[size - 1] = '\0';

	json_tokener *tok = json_tokener_new_ex(JSON_MAX_DEPTH);
	json_object *obj = json_tokener_parse_ex(tok, resp, -1);
	enum json_tokener_error err = json_tokener_get_error(tok);
	json_tokener_free(tok);
	if (obj != NULL && err == json_tokener_success) {
		success(obj, true);
		pretty_print(type, obj);
		json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
		json_object_put(obj);
	}
	free(resp);
	return 0;
}
