#include "docker-excess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <curl/curl.h>
#include <json-c/json.h>

/* ----------------- Internal Structures ----------------- */

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

struct docker_excess_t {
    docker_excess_config_t config;
    CURL *curl;
    char error_msg[512];
    pthread_mutex_t mutex;
};

/* ----------------- Static Functions ----------------- */

static size_t write_response_callback(void *contents, size_t size, size_t nmemb, response_buffer_t *buffer) {
    size_t total_size = size * nmemb;
    
    if (buffer->size + total_size >= buffer->capacity) {
        size_t new_capacity = (buffer->capacity == 0) ? 8192 : buffer->capacity * 2;
        while (new_capacity < buffer->size + total_size + 1) {
            new_capacity *= 2;
        }
        
        char *new_data = realloc(buffer->data, new_capacity);
        if (!new_data) return 0;
        
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    
    memcpy(buffer->data + buffer->size, contents, total_size);
    buffer->size += total_size;
    buffer->data[buffer->size] = '\0';
    
    return total_size;
}

static void set_error(docker_excess_t *client, const char *format, ...) {
    if (!client) return;
    
    pthread_mutex_lock(&client->mutex);
    va_list args;
    va_start(args, format);
    vsnprintf(client->error_msg, sizeof(client->error_msg), format, args);
    va_end(args);
    pthread_mutex_unlock(&client->mutex);
}

static docker_excess_error_t make_request(docker_excess_t *client, const char *method, 
                                         const char *endpoint, const char *body,
                                         char **response, int *http_code) {
    if (!client || !method || !endpoint) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&client->mutex);
    
    response_buffer_t buffer = {0};
    char url[1024];
    
    if (client->config.host) {
        snprintf(url, sizeof(url), "%s://%s:%d%s",
                client->config.use_tls ? "https" : "http",
                client->config.host, client->config.port, endpoint);
    } else {
        snprintf(url, sizeof(url), "http://localhost%s", endpoint);
    }
    
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, client->config.timeout_s);
    
    if (!client->config.host) {
        curl_easy_setopt(client->curl, CURLOPT_UNIX_SOCKET_PATH, client->config.socket_path);
    }
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    
    if (body && strlen(body) > 0) {
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
    }
    
    CURLcode res = curl_easy_perform(client->curl);
    long response_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    pthread_mutex_unlock(&client->mutex);
    
    if (http_code) *http_code = (int)response_code;
    
    if (res != CURLE_OK) {
        set_error(client, "cURL error: %s", curl_easy_strerror(res));
        free(buffer.data);
        return DOCKER_EXCESS_ERR_NETWORK;
    }
    
    if (response) {
        *response = buffer.data;
    } else {
        free(buffer.data);
    }
    
    if (response_code >= 400) {
        if (response_code == 404) return DOCKER_EXCESS_ERR_NOT_FOUND;
        return DOCKER_EXCESS_ERR_HTTP;
    }
    
    return DOCKER_EXCESS_OK;
}

static json_object* get_json_object(json_object *obj, const char *key) {
    json_object *result;
    if (json_object_object_get_ex(obj, key, &result)) {
        return result;
    }
    return NULL;
}

static const char* get_json_string(json_object *obj, const char *key) {
    json_object *field = get_json_object(obj, key);
    if (field && json_object_get_type(field) == json_type_string) {
        return json_object_get_string(field);
    }
    return NULL;
}

static int64_t get_json_int(json_object *obj, const char *key) {
    json_object *field = get_json_object(obj, key);
    if (field && json_object_get_type(field) == json_type_int) {
        return json_object_get_int64(field);
    }
    return 0;
}

static docker_excess_container_state_t parse_container_state(const char *state_str) {
    if (!state_str) return DOCKER_EXCESS_STATE_CREATED;
    
    if (strcmp(state_str, "created") == 0) return DOCKER_EXCESS_STATE_CREATED;
    if (strcmp(state_str, "restarting") == 0) return DOCKER_EXCESS_STATE_RESTARTING;
    if (strcmp(state_str, "running") == 0) return DOCKER_EXCESS_STATE_RUNNING;
    if (strcmp(state_str, "removing") == 0) return DOCKER_EXCESS_STATE_REMOVING;
    if (strcmp(state_str, "paused") == 0) return DOCKER_EXCESS_STATE_PAUSED;
    if (strcmp(state_str, "exited") == 0) return DOCKER_EXCESS_STATE_EXITED;
    if (strcmp(state_str, "dead") == 0) return DOCKER_EXCESS_STATE_DEAD;
    
    return DOCKER_EXCESS_STATE_CREATED;
}

/* ----------------- Public API Implementation ----------------- */

docker_excess_error_t docker_excess_new(docker_excess_t **client) {
    docker_excess_config_t config = docker_excess_default_config();
    return docker_excess_new_with_config(&config, client);
}

docker_excess_error_t docker_excess_new_with_config(const docker_excess_config_t *config, docker_excess_t **client) {
    if (!config || !client) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    docker_excess_t *c = calloc(1, sizeof(docker_excess_t));
    if (!c) return DOCKER_EXCESS_ERR_MEMORY;
    
    memcpy(&c->config, config, sizeof(docker_excess_config_t));
    
    // Copy strings
    if (config->socket_path) {
        c->config.socket_path = strdup(config->socket_path);
    }
    if (config->host) {
        c->config.host = strdup(config->host);
    }
    if (config->cert_path) {
        c->config.cert_path = strdup(config->cert_path);
    }
    if (config->key_path) {
        c->config.key_path = strdup(config->key_path);
    }
    if (config->ca_path) {
        c->config.ca_path = strdup(config->ca_path);
    }
    
    pthread_mutex_init(&c->mutex, NULL);
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    c->curl = curl_easy_init();
    if (!c->curl) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    *client = c;
    return DOCKER_EXCESS_OK;
}

void docker_excess_free(docker_excess_t *client) {
    if (!client) return;
    
    free(client->config.socket_path);
    free(client->config.host);
    free(client->config.cert_path);
    free(client->config.key_path);
    free(client->config.ca_path);
    
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    
    pthread_mutex_destroy(&client->mutex);
    free(client);
    curl_global_cleanup();
}

const char* docker_excess_get_error(docker_excess_t *client) {
    if (!client) return "Invalid client";
    
    pthread_mutex_lock(&client->mutex);
    const char *msg = client->error_msg;
    pthread_mutex_unlock(&client->mutex);
    
    return msg;
}

docker_excess_error_t docker_excess_ping(docker_excess_t *client) {
    char *response;
    int http_code;
    docker_excess_error_t err = make_request(client, "GET", "/_ping", NULL, &response, &http_code);
    
    if (response) {
        free(response);
    }
    
    return err;
}

docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json) {
    return make_request(client, "GET", "/version", NULL, version_json, NULL);
}

docker_excess_error_t docker_excess_list_containers(docker_excess_t *client, bool all,
                                                   docker_excess_container_t ***containers, size_t *count) {
    if (!client || !containers || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/containers/json?all=%s", all ? "true" : "false");
    
    char *response;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json || json_object_get_type(json) != json_type_array) {
        if (json) json_object_put(json);
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    size_t array_len = json_object_array_length(json);
    docker_excess_container_t **result = calloc(array_len, sizeof(docker_excess_container_t*));
    if (!result) {
        json_object_put(json);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    for (size_t i = 0; i < array_len; i++) {
        json_object *container_obj = json_object_array_get_idx(json, i);
        if (!container_obj) continue;
        
        docker_excess_container_t *container = calloc(1, sizeof(docker_excess_container_t));
        if (!container) continue;
        
        const char *id = get_json_string(container_obj, "Id");
        if (id) container->id = strdup(id);
        
        const char *image = get_json_string(container_obj, "Image");
        if (image) container->image = strdup(image);
        
        const char *status = get_json_string(container_obj, "Status");
        if (status) container->status = strdup(status);
        
        const char *state = get_json_string(container_obj, "State");
        container->state = parse_container_state(state);
        
        container->created = get_json_int(container_obj, "Created");
        
        // Parse names array
        json_object *names_obj = get_json_object(container_obj, "Names");
        if (names_obj && json_object_get_type(names_obj) == json_type_array) {
            if (json_object_array_length(names_obj) > 0) {
                json_object *name_obj = json_object_array_get_idx(names_obj, 0);
                const char *name = json_object_get_string(name_obj);
                if (name && name[0] == '/') {
                    container->name = strdup(name + 1); // Skip leading '/'
                }
            }
        }
        
        result[i] = container;
    }
    
    json_object_put(json);
    
    *containers = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_create_container(docker_excess_t *client,
                                                    const docker_excess_container_create_t *params,
                                                    char **container_id) {
    if (!client || !params || !params->image) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    json_object *config = json_object_new_object();
    json_object *image_obj = json_object_new_string(params->image);
    json_object_object_add(config, "Image", image_obj);
    
    if (params->cmd && params->cmd_count > 0) {
        json_object *cmd_array = json_object_new_array();
        for (size_t i = 0; i < params->cmd_count; i++) {
            json_object_array_add(cmd_array, json_object_new_string(params->cmd[i]));
        }
        json_object_object_add(config, "Cmd", cmd_array);
    }
    
    if (params->env && params->env_count > 0) {
        json_object *env_array = json_object_new_array();
        for (size_t i = 0; i < params->env_count; i++) {
            json_object_array_add(env_array, json_object_new_string(params->env[i]));
        }
        json_object_object_add(config, "Env", env_array);
    }
    
    if (params->working_dir) {
        json_object_object_add(config, "WorkingDir", json_object_new_string(params->working_dir));
    }
    
    if (params->interactive) {
        json_object_object_add(config, "OpenStdin", json_object_new_boolean(true));
        json_object_object_add(config, "StdinOnce", json_object_new_boolean(true));
    }
    
    if (params->tty) {
        json_object_object_add(config, "Tty", json_object_new_boolean(true));
    }
    
    json_object *host_config = json_object_new_object();
    
    if (params->auto_remove) {
        json_object_object_add(host_config, "AutoRemove", json_object_new_boolean(true));
    }
    
    json_object *create_obj = json_object_new_object();
    json_object_object_add(create_obj, "Config", config);
    json_object_object_add(create_obj, "HostConfig", host_config);
    
    if (params->name) {
        json_object_object_add(create_obj, "Name", json_object_new_string(params->name));
    }
    
    const char *json_string = json_object_to_json_string(create_obj);
    
    char endpoint[256];
    if (params->name) {
        snprintf(endpoint, sizeof(endpoint), "/containers/create?name=%s", params->name);
    } else {
        strcpy(endpoint, "/containers/create");
    }
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(create_obj);
    
    if (err != DOCKER_EXCESS_OK) return err;
    
    if (container_id && response) {
        json_object *resp_obj = json_tokener_parse(response);
        if (resp_obj) {
            const char *id = get_json_string(resp_obj, "Id");
            if (id) {
                *container_id = strdup(id);
            }
            json_object_put(resp_obj);
        }
    }
    
    free(response);
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_start_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/start", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_stop_container(docker_excess_t *client, const char *container_id, int timeout_s) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    if (timeout_s > 0) {
        snprintf(endpoint, sizeof(endpoint), "/containers/%s/stop?t=%d", container_id, timeout_s);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/containers/%s/stop", container_id);
    }
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_restart_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/restart", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_remove_container(docker_excess_t *client, const char *container_id, bool force) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s?force=%s", container_id, force ? "true" : "false");
    
    char *response;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_get_logs(docker_excess_t *client, const char *container_id,
                                            bool follow, bool timestamps, int tail_lines,
                                            docker_excess_log_callback_t callback, void *userdata) {
    if (!client || !container_id || !callback) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[1024];
    snprintf(endpoint, sizeof(endpoint), 
             "/containers/%s/logs?stdout=true&stderr=true&follow=%s&timestamps=%s&tail=%d",
             container_id, follow ? "true" : "false", timestamps ? "true" : "false", tail_lines);
    
    char *response;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    
    if (err == DOCKER_EXCESS_OK && response) {
        // Simple line-by-line callback (Docker multiplexed format not handled here)
        char *line = strtok(response, "\n");
        while (line) {
            callback(line, userdata);
            line = strtok(NULL, "\n");
        }
    }
    
    free(response);
    return err;
}

docker_excess_error_t docker_excess_exec_simple(docker_excess_t *client, const char *container_id,
                                               const char *command, char **stdout_out, char **stderr_out, int *exit_code) {
    if (!client || !container_id || !command) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // Create exec instance
    json_object *exec_config = json_object_new_object();
    json_object_object_add(exec_config, "AttachStdout", json_object_new_boolean(true));
    json_object_object_add(exec_config, "AttachStderr", json_object_new_boolean(true));
    
    json_object *cmd_array = json_object_new_array();
    json_object_array_add(cmd_array, json_object_new_string("/bin/sh"));
    json_object_array_add(cmd_array, json_object_new_string("-c"));
    json_object_array_add(cmd_array, json_object_new_string(command));
    json_object_object_add(exec_config, "Cmd", cmd_array);
    
    const char *json_string = json_object_to_json_string(exec_config);
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/exec", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(exec_config);
    
    if (err != DOCKER_EXCESS_OK) return err;
    
    // Parse exec ID
    char *exec_id = NULL;
    if (response) {
        json_object *resp_obj = json_tokener_parse(response);
        if (resp_obj) {
            const char *id = get_json_string(resp_obj, "Id");
            if (id) exec_id = strdup(id);
            json_object_put(resp_obj);
        }
        free(response);
    }
    
    if (!exec_id) return DOCKER_EXCESS_ERR_INTERNAL;
    
    // Start exec
    json_object *start_config = json_object_new_object();
    json_object_object_add(start_config, "Detach", json_object_new_boolean(false));
    json_object_object_add(start_config, "Tty", json_object_new_boolean(false));
    
    json_string = json_object_to_json_string(start_config);
    snprintf(endpoint, sizeof(endpoint), "/exec/%s/start", exec_id);
    
    err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(start_config);
    
    if (err == DOCKER_EXCESS_OK && stdout_out && response) {
        *stdout_out = response;
        response = NULL; // Don't free it
    }
    
    // Get exit code
    if (exit_code) {
        snprintf(endpoint, sizeof(endpoint), "/exec/%s/json", exec_id);
        char *inspect_response;
        if (make_request(client, "GET", endpoint, NULL, &inspect_response, NULL) == DOCKER_EXCESS_OK) {
            json_object *inspect_obj = json_tokener_parse(inspect_response);
            if (inspect_obj) {
                *exit_code = (int)get_json_int(inspect_obj, "ExitCode");
                json_object_put(inspect_obj);
            }
            free(inspect_response);
        }
    }
    
    free(exec_id);
    free(response);
    return err;
}

docker_excess_error_t docker_excess_exec(docker_excess_t *client, const char *container_id,
                                        const char **command, size_t cmd_count,
                                        docker_excess_exec_callback_t callback, void *userdata) {
    // Simplified implementation - concatenate command and use exec_simple
    if (!client || !container_id || !command || cmd_count == 0) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    size_t total_len = 0;
    for (size_t i = 0; i < cmd_count; i++) {
        total_len += strlen(command[i]) + 1;
    }
    
    char *full_cmd = malloc(total_len + 1);
    if (!full_cmd) return DOCKER_EXCESS_ERR_MEMORY;
    
    strcpy(full_cmd, command[0]);
    for (size_t i = 1; i < cmd_count; i++) {
        strcat(full_cmd, " ");
        strcat(full_cmd, command[i]);
    }
    
    char *stdout_data = NULL;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, full_cmd, &stdout_data, NULL, NULL);
    
    if (err == DOCKER_EXCESS_OK && callback && stdout_data) {
        callback(stdout_data, NULL, userdata);
    }
    
    free(full_cmd);
    free(stdout_data);
    return err;
}

docker_excess_error_t docker_excess_pause_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/pause", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_unpause_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/unpause", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_wait_container(docker_excess_t *client, const char *container_id, int *exit_code) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/wait", container_id);
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    
    if (err == DOCKER_EXCESS_OK && exit_code && response) {
        json_object *obj = json_tokener_parse(response);
        if (obj) {
            *exit_code = (int)get_json_int(obj, "StatusCode");
            json_object_put(obj);
        }
    }
    
    free(response);
    return err;
}

// File operations (simplified implementations)
docker_excess_error_t docker_excess_read_file(docker_excess_t *client, const char *container_id,
                                             const char *file_path, char **content, size_t *size) {
    if (!client || !container_id || !file_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    asprintf(&cmd, "cat %s", file_path);
    if (!cmd) return DOCKER_EXCESS_ERR_MEMORY;
    
    char *output = NULL;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, NULL);
    
    if (err == DOCKER_EXCESS_OK && content) {
        *content = output;
        if (size) *size = output ? strlen(output) : 0;
    } else {
        free(output);
    }
    
    free(cmd);
    return err;
}

docker_excess_error_t docker_excess_write_file(docker_excess_t *client, const char *container_id,
                                              const char *file_path, const char *content, size_t size) {
    if (!client || !container_id || !file_path || !content) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    asprintf(&cmd, "echo '%.*s' > %s", (int)size, content, file_path);
    if (!cmd) return DOCKER_EXCESS_ERR_MEMORY;
    
    char *output = NULL;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, NULL);
    
    free(cmd);
    free(output);
    return err;
}

docker_excess_error_t docker_excess_list_images(docker_excess_t *client, bool all,
                                               docker_excess_image_t ***images, size_t *count) {
    if (!client || !images || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/images/json?all=%s", all ? "true" : "false");
    
    char *response;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json || json_object_get_type(json) != json_type_array) {
        if (json) json_object_put(json);
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    size_t array_len = json_object_array_length(json);
    docker_excess_image_t **result = calloc(array_len, sizeof(docker_excess_image_t*));
    if (!result) {
        json_object_put(json);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    for (size_t i = 0; i < array_len; i++) {
        json_object *image_obj = json_object_array_get_idx(json, i);
        if (!image_obj) continue;
        
        docker_excess_image_t *image = calloc(1, sizeof(docker_excess_image_t));
        if (!image) continue;
        
        const char *id = get_json_string(image_obj, "Id");
        if (id) image->id = strdup(id);
        
        image->created = get_json_int(image_obj, "Created");
        image->size = get_json_int(image_obj, "Size");
        
        json_object *repo_tags = get_json_object(image_obj, "RepoTags");
        if (repo_tags && json_object_get_type(repo_tags) == json_type_array) {
            size_t tags_len = json_object_array_length(repo_tags);
            image->repo_tags = calloc(tags_len, sizeof(char*));
            image->repo_tags_count = tags_len;
            
            for (size_t j = 0; j < tags_len; j++) {
                json_object *tag_obj = json_object_array_get_idx(repo_tags, j);
                const char *tag = json_object_get_string(tag_obj);
                if (tag) image->repo_tags[j] = strdup(tag);
            }
        }
        
        result[i] = image;
    }
    
    json_object_put(json);
    
    *images = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_pull_image(docker_excess_t *client, const char *image_name, const char *tag) {
    if (!client || !image_name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    if (tag) {
        snprintf(endpoint, sizeof(endpoint), "/images/create?fromImage=%s&tag=%s", image_name, tag);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/images/create?fromImage=%s", image_name);
    }
    
    char *response;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_remove_image(docker_excess_t *client, const char *image_name, bool force) {
    if (!client || !image_name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/images/%s?force=%s", image_name, force ? "true" : "false");
    
    char *response;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_raw_request(docker_excess_t *client, const char *method,
                                               const char *endpoint, const char *body,
                                               char **response, int *http_code) {
    return make_request(client, method, endpoint, body, response, http_code);
}

// Utility functions
void docker_excess_free_containers(docker_excess_container_t **containers, size_t count) {
    if (!containers) return;
    
    for (size_t i = 0; i < count; i++) {
        if (containers[i]) {
            free(containers[i]->id);
            free(containers[i]->name);
            free(containers[i]->image);
            free(containers[i]->status);
            
            for (size_t j = 0; j < containers[i]->ports_count; j++) {
                free(containers[i]->ports[j]);
            }
            free(containers[i]->ports);
            
            free(containers[i]);
        }
    }
    free(containers);
}

void docker_excess_free_images(docker_excess_image_t **images, size_t count) {
    if (!images) return;
    
    for (size_t i = 0; i < count; i++) {
        if (images[i]) {
            free(images[i]->id);
            
            for (size_t j = 0; j < images[i]->repo_tags_count; j++) {
                free(images[i]->repo_tags[j]);
            }
            free(images[i]->repo_tags);
            
            free(images[i]);
        }
    }
    free(images);
}

docker_excess_config_t docker_excess_default_config(void) {
    docker_excess_config_t config = {0};
    config.socket_path = strdup(DOCKER_EXCESS_DEFAULT_SOCKET);
    config.timeout_s = DOCKER_EXCESS_DEFAULT_TIMEOUT_S;
    config.port = 2376;
    return config;
}

docker_excess_config_t docker_excess_config_from_env(void) {
    docker_excess_config_t config = docker_excess_default_config();
    
    const char *docker_host = getenv("DOCKER_HOST");
    if (docker_host) {
        // Parse docker_host (tcp://host:port or unix:///path/to/socket)
        if (strncmp(docker_host, "tcp://", 6) == 0) {
            config.use_tls = false;
            // Parse host and port from docker_host + 6
        } else if (strncmp(docker_host, "unix://", 7) == 0) {
            free(config.socket_path);
            config.socket_path = strdup(docker_host + 7);
        }
    }
    
    if (getenv("DOCKER_TLS_VERIFY")) {
        config.use_tls = true;
    }
    
    const char *cert_path = getenv("DOCKER_CERT_PATH");
    if (cert_path) {
        config.cert_path = strdup(cert_path);
    }
    
    return config;
}

const char* docker_excess_error_string(docker_excess_error_t error) {
    switch (error) {
        case DOCKER_EXCESS_OK: return "Success";
        case DOCKER_EXCESS_ERR_INVALID_PARAM: return "Invalid parameter";
        case DOCKER_EXCESS_ERR_MEMORY: return "Memory allocation failed";
        case DOCKER_EXCESS_ERR_NETWORK: return "Network error";
        case DOCKER_EXCESS_ERR_HTTP: return "HTTP error";
        case DOCKER_EXCESS_ERR_JSON: return "JSON parsing error";
        case DOCKER_EXCESS_ERR_NOT_FOUND: return "Resource not found";
        case DOCKER_EXCESS_ERR_TIMEOUT: return "Operation timeout";
        case DOCKER_EXCESS_ERR_INTERNAL: return "Internal error";
        default: return "Unknown error";
    }
}

void docker_excess_format_bytes(int64_t bytes, char *buffer, size_t buffer_size) {
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%ld B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

// Stub implementations for remaining functions
docker_excess_error_t docker_excess_list_files(docker_excess_t *client, const char *container_id,
                                              const char *path, docker_excess_file_t ***files, size_t *count) {
    // Simplified implementation using ls command
    return DOCKER_EXCESS_ERR_INTERNAL; // Not implemented
}

docker_excess_error_t docker_excess_copy_from_container(docker_excess_t *client, const char *container_id,
                                                       const char *container_path, const char *host_path) {
    return DOCKER_EXCESS_ERR_INTERNAL; // Not implemented - would use /containers/{id}/archive
}

docker_excess_error_t docker_excess_copy_to_container(docker_excess_t *client, const char *container_id,
                                                     const char *host_path, const char *container_path) {
    return DOCKER_EXCESS_ERR_INTERNAL; // Not implemented - would use PUT /containers/{id}/archive
}

// Add empty implementations for remaining functions to avoid linker errors
docker_excess_error_t docker_excess_mkdir(docker_excess_t *client, const char *container_id, const char *dir_path, uint32_t mode) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_remove_file(docker_excess_t *client, const char *container_id, const char *path, bool recursive) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_build_image(docker_excess_t *client, const char *dockerfile_path, const char *context_path, const char *tag) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_list_networks(docker_excess_t *client, docker_excess_network_t ***networks, size_t *count) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_create_network(docker_excess_t *client, const char *name, const char *driver, char **network_id) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_remove_network(docker_excess_t *client, const char *network_id) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_connect_network(docker_excess_t *client, const char *network_id, const char *container_id) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_disconnect_network(docker_excess_t *client, const char *network_id, const char *container_id) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_list_volumes(docker_excess_t *client, docker_excess_volume_t ***volumes, size_t *count) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_create_volume(docker_excess_t *client, const char *name, const char *driver, char **volume_name) { return DOCKER_EXCESS_ERR_INTERNAL; }
docker_excess_error_t docker_excess_remove_volume(docker_excess_t *client, const char *volume_name, bool force) { return DOCKER_EXCESS_ERR_INTERNAL; }
void docker_excess_free_networks(docker_excess_network_t **networks, size_t count) {}
void docker_excess_free_volumes(docker_excess_volume_t **volumes, size_t count) {}
void docker_excess_free_files(docker_excess_file_t **files, size_t count) {}
docker_excess_error_t docker_excess_resolve_container_id(docker_excess_t *client, const char *name_or_id, char **full_id) { return DOCKER_EXCESS_ERR_INTERNAL; }
