/**
 * @file /magma/check/magma/servers/camel/camel_check_network.c
 *
 * @brief Functions used to test the Camelface.
 *
 */

#include "magma_check.h"

/**
 * @brief	Reads lines from the client until the HTTP response status code is found, which it checks.
 *
 * @param	client	A client_t* to read lines from. An HTTP request should have been submitted
 * 			from the client before this function is called.
 *
 * @return	True if the HTTP status code of the response begins with a '2', false otherwise.
 */
bool_t check_camel_status(client_t *client) {

	while (st_cmp_cs_starts(&(client->line), NULLER("HTTP/1.1"))) {
		if (client_read_line(client) <= 2) return false;
	}

	return ((*(pl_char_get(client->line) + 9) == '2') ? true : false);
}

client_t * check_camel_connect(bool_t secure) {

	client_t *client = NULL;
	server_t *server = NULL;

	if (!(server = servers_get_by_protocol(HTTP, secure))) {
		return NULL;
	}
	else if (!(client = client_connect("localhost", server->network.port))) {
		return NULL;
	}
	else if (secure && client_secure(client) != 0) {
		client_close(client);
		return NULL;
	}

	return client;
}

// Combine submit and read, because we now need to handle the connection being closed between requests.

/**
 * @brief	Reads lines from the client until the end of the HTTP response is reached.
 *
 * @param	client	A client_t* to read lines from. An HTTP request should have been submitted
 * 			from the client before this function is called.
 *
 * @return	True if the end of the HTTP response was reached, false if client_read_line reads
 * 			a 0 length line before the last line is reached.
 */
stringer_t * check_camel_json_read(client_t *client, size_t length) {

	stringer_t *json = NULL;
	uint32_t content_read = 0;

	while (st_cmp_cs_eq(&(client->line), PLACER("\r\n", 2))) {
		if (client_read_line(client) <= 0) return NULL;
	}

	while (content_read < length) {
		content_read += client_read(client);
		json = st_append_opts(8192, json, client->buffer);
	}

	if (st_empty(json)) {
		st_free(json);
		return NULL;
	}

	return json;
}

bool_t check_camel_json_write(client_t *client, stringer_t *json, stringer_t *cookie, bool_t keep_alive) {

	chr_t *message = "POST /portal/camel HTTP/1.1\r\nHost: localhost:10000\r\nAccept: */*\r\nContent-Length: %u\r\n" \
		"Content-Type: application/x-www-form-urlencoded\r\nCookie: portal=%.*s;\r\nConnection: %s\r\n\r\n%.*s\r\n\r\n";

	if (client_print(client, message, st_length_get(json), st_length_int(cookie), (cookie ? st_char_get(cookie) : ""),
		(keep_alive ? "keep-alive" : "close"), st_length_int(json), st_char_get(json)) != (ns_length_get(message) - 12 +
		st_length_get(json) + uint32_digits(st_length_int(json)) + (cookie ? st_length_get(cookie) : 0) + (keep_alive ? 10 : 5))) {

		return false;
	}

	return true;
}

/**
 * @brief	Submits an auth request to /portal/camel, setting *cookie to the session cookie in the response.
 *
 * @param	client	should be connected to an HTTP server.
 * @param	id		the value to place in the "id" field of the json request.
 * @param	user	the username of the account to issue the auth request for.
 * @param	pass	the password of the account to issue the auth request for.
 * @param	cookie	if not NULL, will be set to the value of Set-Cookie in the response.
 *
 * @return	True if the request was successful, false otherwise.
 */
bool_t check_camel_login(client_t *client, uint32_t id, stringer_t *user, stringer_t *pass, stringer_t *cookie) {

	json_error_t json_err;
	size_t content_length = 0;
	json_t *json_root = NULL, *json_result = NULL, *json_key = NULL;
	uint32_t length = 62 + ns_length_get(user) + ns_length_get(pass) + uint32_digits(id);
	stringer_t *json = NULL, *message = NULLER("POST /portal/camel HTTP/1.1\r\nHost: localhost:10000\r\nAccept: */*\r\n" \
		"Content-Length: %u\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n{\"id\":%u,\"method\":\"auth\"," \
		"\"params\":{\"username\":\"%.*s\",\"password\":\"%.*s\"}}\r\n\r\n");

	if (client_print(client, st_char_get(message), length, id, st_length_int(user), st_char_get(user), st_length_int(pass),
		st_char_get(pass)) != ((st_length_get(message) - 12) + uint32_digits(length) + uint32_digits(id) + st_length_get(user) +
		st_length_get(pass)) || !check_camel_status(client) || !(content_length = check_http_content_length_get(client)) ||
		!(json = check_camel_json_read(client, content_length))) {

		return false;
	}
	else if (!(json_root = json_loads_d(st_char_get(json), 0, &json_err)) || !(json_result = json_object_get_d(json_root, "result")) ||
		!(json_key = json_object_get_d(json_result, "session"))) {

		st_free(json);
		mm_cleanup(json_root, json_result, json_key);

		return false;
	}
	else if (cookie && st_sprint(cookie, "%s", json_string_value_d(json_key)) == -1) {

		st_free(json);
		mm_cleanup(json_root, json_result, json_key);
		return false;
	}

	st_free(json);
	mm_cleanup(json_root, json_result, json_key);

	return true;
}

// LOW: Test the four different ways of preserving a session token: Cookie, URL param, JSON param, Form post.
bool_t check_camel_auth_sthread(bool_t secure, stringer_t *errmsg) {

	client_t *client = NULL;
	stringer_t *cookie = MANAGEDBUF(1024);

	if (!(client = check_camel_connect(secure))) {
		st_sprint(errmsg, "There were no HTTP servers available for %s connections.", (secure ? "TLS" : "TCP"));
		return false;
	}
	else if (!check_camel_login(client, 1, PLACER("princess", 8), PLACER("password", 8), cookie)) {

		st_sprint(errmsg, "Failed to return successful state after auth request.");
		return false;
	}

	client_close(client);
	return true;
}

bool_t check_camel_basic_sthread(bool_t secure, stringer_t *errmsg) {

	json_error_t json_err;
	client_t *client = NULL;
	const chr_t *json_value = NULL;
	json_t *json_objs[4] = { [ 0 ... 3] NULL };
	uint32_t content_length = 0;//, folderids[2] = { 0, 0 };
	chr_t *choices = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	stringer_t *cookie = MANAGEDBUF(1024), *command = MANAGEDBUF(8192), *rand_strs[2] = { MANAGEDBUF(64), MANAGEDBUF(64) }, *json = NULL;
	chr_t *commands[] = {
		"{\"id\":2,\"method\":\"config.edit\",\"params\":{\"%.*s\":\"%.*s\"}}",
		"{\"id\":3,\"method\":\"config.load\"}",
		"{\"id\":4,\"method\":\"config.edit\",\"params\":{\"%.*s\":null}}",
		"{\"id\":5,\"method\":\"config.load\"}",
		"{\"id\":6,\"method\":\"config.edit\",\"params\":{\"%.*s\":\"%.*s\"}}",
		"{\"id\":7,\"method\":\"folders.add\",\"params\":{\"context\":\"contacts\",\"name\":\"%.*s\"}}",
		"{\"id\":8,\"method\":\"folders.list\",\"params\":{\"context\":\"contacts\"}}",
		"{\"id\":9,\"method\":\"contacts.add\",\"params\":{\"folderID\":%u, \"contact\":{\"name\":\"%.*s\", \"email\":\"%.*s\"}}}",
		"{\"id\":10,\"method\":\"contacts.copy\",\"params\":{\"sourceFolderID\":%u, \"targetFolderID\":%u, \"contactID\":%u}}",
		"{\"id\":11,\"method\":\"contacts.list\",\"params\":{\"folderID\":%u}}",
		"{\"id\":12,\"method\":\"contacts.edit\",\"params\":{\"folderID\":%u, \"contactID\":%u, \"contact\":{\"name\":\"%.*s\", \"email\":\"%.*s\"}}}",
		"{\"id\":13,\"method\":\"contacts.load\",\"params\":{\"folderID\":%u, \"contactID\":%u }}",
		"{\"id\":14,\"method\":\"contacts.edit\",\"params\":{\"folderID\":%u, \"contactID\":%u, \"contact\":{\"name\":\"%.*s\", \"email\":\"%.*s\", \"phone\":\"%u\", \"notes\":\"%.*s\"}}}",
		"{\"id\":15,\"method\":\"contacts.load\",\"params\":{\"folderID\":%u, \"contactID\":%u}}",
		"{\"id\":16,\"method\":\"folders.add\",\"params\":{\"context\":\"contacts\",\"name\":\"%.*s\"}}",
		"{\"id\":17,\"method\":\"contacts.move\",\"params\":{ \"contactID\":%u, \"sourceFolderID\":%u, \"targetFolderID\":%u}}",
		"{\"id\":18,\"method\":\"contacts.list\",\"params\":{\"folderID\":%u}}",
		"{\"id\":19,\"method\":\"contacts.list\",\"params\":{\"folderID\":%u}}",
		"{\"id\":20,\"method\":\"contacts.remove\",\"params\":{\"folderID\":%u, \"contactID\":%u}}",
		"{\"id\":21,\"method\":\"contacts.remove\",\"params\":{\"folderID\":%u, \"contactID\":%u}}",
		"{\"id\":22,\"method\":\"contacts.list\",\"params\":{\"folderID\":%u}}",
		"{\"id\":23,\"method\":\"folders.remove\",\"params\":{\"context\":\"contacts\",\"folderID\":%u}}",
		"{\"id\":24,\"method\":\"folders.remove\",\"params\":{\"context\":\"contacts\",\"folderID\":%u}}",
		"{\"id\":25,\"method\":\"cookies\"}",
		"{\"id\":26,\"method\":\"alert.list\"}",
		"{\"id\":27,\"method\":\"alert.acknowledge\",\"params\":[1,7,13]}",
		"{\"id\":28,\"method\":\"alert.list\"}",
		"{\"id\":29,\"method\":\"folders.list\",\"params\":{\"context\":\"mail\"}}",
		"{\"id\":30,\"method\":\"folders.list\",\"params\":{\"context\":\"settings\"}}",
		"{\"id\":31,\"method\":\"folders.list\",\"params\":{\"context\":\"help\"}}",
		"{\"id\":32,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"name\":\"%.*s\"}}",
		"{\"id\":33,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"parentID\":,\"name\":\"%.*s\"}}",
		"{\"id\":34,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"parentID\":,\"name\":\"%.*s\"}}",
		"{\"id\":35,\"method\":\"folders.rename\",\"params\":{\"context\":\"mail\",\"folderID\":,\"name\":\"%.*s\"}}",
		"{\"id\":36,\"method\":\"folders.rename\",\"params\":{\"context\":\"mail\",\"folderID\":,\"name\":\"%.*s\"}}",
		"{\"id\":37,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":38,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":39,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":40,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":41,\"method\":\"aliases\"}",
		"{\"id\":42,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"name\":\"%.*s\"}}",
		"{\"id\":43,\"method\":\"messages.copy\",\"params\":{\"messageIDs\":[%u], \"sourceFolderID\":%u, \"targetFolderID\":%u}}",
		"{\"id\":44,\"method\":\"messages.copy\",\"params\":{\"messageIDs\":[%u], \"sourceFolderID\":%u, \"targetFolderID\":%u}}",
		"{\"id\":45,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":46,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"name\":\"%.*s\"}}",
		"{\"id\":47,\"method\":\"messages.load\",\"params\":{\"messageID\":%u, \"folderID\":%u, \"sections\": [\"meta\", \"source\", \"security\", \"server\", \"header\", \"body\", \"attachments\"]}}",
		"{\"id\":48,\"method\":\"messages.copy\",\"params\":{\"messageIDs\":[%u], \"sourceFolderID\":%u, \"targetFolderID\":%u}}",
		"{\"id\":49,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":50,\"method\":\"messages.flag\",\"params\":{\"action\":\"add\", \"flags\":[\"flagged\"], \"messageIDs\": [%u], \"folderID\":%u}}",
		"{\"id\":51,\"method\":\"messages.tags\",\"params\":{\"action\":\"add\", \"tags\":[\"girlie\",\"girlie-6169\"], \"messageIDs\": [%u], \"folderID\":%u}}",
		"{\"id\":52,\"method\":\"messages.flag\",\"params\":{\"action\":\"list\", \"messageIDs\":[%u], \"folderID\":%u}}",
		"{\"id\":53,\"method\":\"messages.tags\",\"params\":{\"action\":\"list\", \"messageIDs\":[%u], \"folderID\":%u}}",
		"{\"id\":54,\"method\":\"messages.list\",\"params\":{\"folderID\":%u}}",
		"{\"id\":55,\"method\":\"folders.tags\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":56,\"method\":\"folders.add\",\"params\":{\"context\":\"mail\",\"name\":\"Mover\"}}",
		"{\"id\":57,\"method\":\"messages.move\",\"params\":{\"messageIDs\":[%u], \"sourceFolderID\":%u, \"targetFolderID\":%u}}",
		"{\"id\":58,\"method\":\"messages.remove\",\"params\":{\"folderID\":%u,\"messageIDs\":[%u]}}",
		"{\"id\":59,\"method\":\"folders.remove\",\"params\":{\"context\":\"mail\",\"folderID\":%u}}",
		"{\"id\":60,\"method\":\"logout\"}"
	};

	if (!(client = check_camel_connect(secure))) {
		st_sprint(errmsg, "There were no HTTP servers available for %s connections.", (secure ? "TLS" : "TCP"));
	}
	else if (!check_camel_login(client, 1, PLACER("princess", 8), PLACER("password", 8), cookie)) {

		st_sprint(errmsg, "Failed to return successful response after auth request.");
		client_close(client);
		return false;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Test config.edit 	: commands[0]
	// JSON Command			: {"id":2,"method":"config.edit","params":{<rand_strs[0]>:<rand_strs[1]>}}"
	// Expected Response 	: {"jsonrpc":"2.0","result":{"config.edit":"success"},"id":2}
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Generate the random inputs for "key" and "value".
	if (!rand_choices(choices, 64, rand_strs[0]) || !rand_choices(choices, 64, rand_strs[1])) {

		st_sprint(errmsg, "Failed to create random inputs. { command = \"%.*s\" }", ns_length_int(commands[0]), commands[0]);
		client_close(client);
		return false;
	}

	// Construct the command string.
	else if (!(st_sprint(command, commands[0], st_length_int(rand_strs[0]), st_char_get(rand_strs[0]), st_length_int(rand_strs[1]),
		st_char_get(rand_strs[1])))) {

		st_sprint(errmsg, "Failed to create command string. { command = \"%.*s\" }", st_length_int(command), st_char_get(command));
		client_close(client);
		return false;
	}

	// Submit the command and check the status of the response.
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, command, cookie, true) ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) ||
		!(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return a successful HTTP response. { command = \"%.*s\" }", st_length_int(command), st_char_get(command));
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Parse the returned JSON.
	else if (!(json_objs[0] = json_loads_d(st_char_get(json), 0, &json_err)) || !(json_objs[1] = json_object_get_d(json_objs[0], "result")) ||
		!(json_objs[2] = json_object_get_d(json_objs[1], "config.edit")) || !(json_value = json_string_value_d(json_objs[2]))) {

		st_sprint(errmsg, "Failed parsing the returned JSON. { command = \"%.*s\", json = \"%.*s\" }", st_length_int(command), st_char_get(command),
			st_length_int(json), st_char_get(json));
		mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Check if the returned JSON is correct.
	else if (st_cmp_cs_eq(PLACER((chr_t*)json_value, ns_length_get(json_value)), PLACER("success", 7))) {

		st_sprint(errmsg, "Failed parsing the returned JSON. { command = \"%.*s\", json = \"%.*s\" }", st_length_int(command), st_char_get(command),
			st_length_int(json), st_char_get(json));
		mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Clean up before the next check.
	st_wipe(command);
	st_cleanup(json);
	mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);

	json = NULL;
	command = NULL;

	for (size_t i = 0; i < sizeof(json_objs)/sizeof(json_t *); i++) {
		json_objs[i] = NULL;
	}

	client_close(client);
	client = NULL;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Test config.edit 	: commands[1]
	// JSON Command			: {"id":3,"method":"config.load"}
	// Expected Response 	: {"jsonrpc":"2.0","result":{<rand_strs[0]>:{"value":<rand_strs[1]>, "flags":[]}, ...}, "id":3}
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Submit the command and check the status of the response.
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, PLACER(commands[1], ns_length_get(commands[1])), cookie, true) ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) ||
		!(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return a successful HTTP response. { command = \"%.*s\" }", ns_length_int(commands[1]), commands[1]);
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Parse the returned JSON.
	else if (!(json_objs[0] = json_loads_d(st_char_get(json), 0, &json_err)) || !(json_objs[1] = json_object_get_d(json_objs[0], "result")) ||
		!(json_objs[2] = json_object_get_d(json_objs[1], st_char_get(rand_strs[0]))) ||
		!(json_objs[3] = json_object_get_d(json_objs[2], "value")) ||
		!(json_value = json_string_value_d(json_objs[3]))) {

		st_sprint(errmsg, "Failed parsing the returned JSON. { command = \"%.*s\", json = \"%.*s\" }", ns_length_int(commands[0]), commands[0],
			st_length_int(json), st_char_get(json));
		mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Check if the returned JSON is correct.
	else if (st_cmp_cs_eq(PLACER((chr_t*)json_value, ns_length_get(json_value)), rand_strs[1])) {

		st_sprint(errmsg, "Failed to return a successful JSON response. { command = \"%.*s\", json = \"%.*s\" }", ns_length_int(commands[0]), commands[0],
			st_length_int(json), st_char_get(json));
		mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);
		client_close(client);
		st_cleanup(json);
		return false;
	}

	// Clean up before the next check.
	st_cleanup(json);
	st_wipe(command);
	st_wipe(rand_strs[1]);
	mm_cleanup(json_objs[0], json_objs[1], json_objs[2], json_objs[3]);

	json = NULL;

	for (size_t i = 0; i < sizeof(json_objs)/sizeof(json_t *); i++) {
		json_objs[i] = NULL;
	}

	client_close(client);
	client = NULL;

	//
	return true;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Test config.load (commands[1] "{\"id\":3,\"method\":\"config.load\"}"
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/*
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[0], cookie, true) ||
		client_status(client) != 1 || !check_camel_status(client) || !(content_length = check_http_content_length_get(client)) ||
		!(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return a successful response. { command = 0 }");
		st_cleanup(rand_strs[0], rand_strs[1]);
		client_close(client);
		return false;
	}
	*/

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/*
	// Test config.load
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[1], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) ||
		!(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return successful response after config.load.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test config.edit { key: "key", value: null }
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[2], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return successful response after config.edit.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test config.load
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[3], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return successful response after config.load.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test config.edit { key: "key.3943", value: "18346" }
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[4], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return successful response after config.edit.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test folders.add { context: "contacts", name: "Flight Crew" }
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[5], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length)) ||
		json_unpack_d(json, "{s:{s:i}}", "result", "folderID", &folderid)) {

		st_sprint(errmsg, "Failed to return successful response after folders.add.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test folders.list { context: "contacts" }
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[6], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length)) ||
		json_unpack_d(json, "{s:[{s:i}]}", "result", "folderID", &folderid_buff) || folderid != folderid_buff) {

		st_sprint(errmsg, "Failed to return successful response after folders.list.");
		client_close(client);
		return false;
	}

	st_free(json);

	// Test contacts.add { folderID: %u, contact: { name: "Jenna", email: "jenna@jameson.com" }}
	if (!(client = check_camel_connect(secure)) || !check_camel_json_write(client, commands[7], cookie, true) || client_status(client) != 1 ||
		!check_camel_status(client) || !(content_length = check_http_content_length_get(client)) || !(json = check_camel_json_read(client, content_length))) {

		st_sprint(errmsg, "Failed to return successful response after contacts.add.");
		client_close(client);
		return false;
	}

	st_free(json);
	*/
}
