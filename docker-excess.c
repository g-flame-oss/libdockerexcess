#include "docker-excess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>
#include <ctype.h>
#include <netinet/in.h>
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
    bool curl_initialized;
};

/* ----------------- Static Helper Functions ----------------- */

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

static char* url_encode(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    if (!encoded) return NULL;
    
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded[pos++] = c;
        } else {
            sprintf(&encoded[pos], "%%%02X", (unsigned char)c);
            pos += 3;
        }
    }
    encoded[pos] = '\0';
    
    char *result = realloc(encoded, pos + 1);
    return result ? result : encoded;
}

static docker_excess_error_t make_request(docker_excess_t *client, const char *method, 
                                         const char *endpoint, const char *body,
                                         char **response, int *http_code) {
    if (!client || !method || !endpoint) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&client->mutex);
    
    response_buffer_t buffer = {0};
    char url[1024];
    
    // Build URL
    if (client->config.host) {
        snprintf(url, sizeof(url), "%s://%s:%d/v%s%s",
                client->config.use_tls ? "https" : "http",
                client->config.host, client->config.port, 
                DOCKER_EXCESS_API_VERSION, endpoint);
    } else {
        snprintf(url, sizeof(url), "http://localhost/v%s%s", 
                DOCKER_EXCESS_API_VERSION, endpoint);
    }
    
    // Configure cURL
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, (long)client->config.timeout_s);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 5L);
    
    if (client->config.debug) {
        curl_easy_setopt(client->curl, CURLOPT_VERBOSE, 1L);
    }
    
    // Unix socket configuration
    if (!client->config.host) {
        curl_easy_setopt(client->curl, CURLOPT_UNIX_SOCKET_PATH, client->config.socket_path);
    }
    
    // TLS configuration
    if (client->config.use_tls) {
        curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        if (client->config.ca_path) {
            curl_easy_setopt(client->curl, CURLOPT_CAINFO, client->config.ca_path);
        }
        if (client->config.cert_path) {
            curl_easy_setopt(client->curl, CURLOPT_SSLCERT, client->config.cert_path);
        }
        if (client->config.key_path) {
            curl_easy_setopt(client->curl, CURLOPT_SSLKEY, client->config.key_path);
        }
    }
    
    // Headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: docker-excess/1.0");
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    
    // Request body
    if (body && strlen(body) > 0) {
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(client->curl);
    long response_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    pthread_mutex_unlock(&client->mutex);
    
    if (http_code) *http_code = (int)response_code;
    
    if (res != CURLE_OK) {
        set_error(client, "cURL error: %s", curl_easy_strerror(res));
        free(buffer.data);
        if (res == CURLE_OPERATION_TIMEDOUT) return DOCKER_EXCESS_ERR_TIMEOUT;
        return DOCKER_EXCESS_ERR_NETWORK;
    }
    
    if (response) {
        *response = buffer.data;
    } else {
        free(buffer.data);
    }
    
    if (response_code >= 400) {
        if (response_code == 404) return DOCKER_EXCESS_ERR_NOT_FOUND;
        if (response_code >= 500) return DOCKER_EXCESS_ERR_INTERNAL;
        return DOCKER_EXCESS_ERR_HTTP;
    }
    
    return DOCKER_EXCESS_OK;
}

/* JSON Helper Functions */
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

static bool get_json_bool(json_object *obj, const char *key) {
    json_object *field = get_json_object(obj, key);
    if (field && json_object_get_type(field) == json_type_boolean) {
        return json_object_get_boolean(field);
    }
    return false;
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

static char** parse_json_string_array(json_object *array, size_t *count) {
    if (!array || json_object_get_type(array) != json_type_array) {
        *count = 0;
        return NULL;
    }
    
    size_t len = json_object_array_length(array);
    if (len == 0) {
        *count = 0;
        return NULL;
    }
    
    char **result = calloc(len, sizeof(char*));
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(array, i);
        const char *str = json_object_get_string(item);
        if (str) {
            result[i] = strdup(str);
        }
    }
    
    *count = len;
    return result;
}

/* Base64 encoding for binary-safe file operations */
static char* base64_encode(const char *input, size_t length) {
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t output_length = ((length + 2) / 3) * 4;
    char *encoded = malloc(output_length + 1);
    if (!encoded) return NULL;
    
    size_t i, j;
    for (i = 0, j = 0; i < length;) {
        uint32_t octet_a = i < length ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < length ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < length ? (unsigned char)input[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    for (i = 0; i < (3 - length % 3) % 3; i++) {
        encoded[output_length - 1 - i] = '=';
    }
    
    encoded[output_length] = '\0';
    return encoded;
}

/* Log streaming callback structure */
struct log_stream_data {
    docker_excess_log_callback_t callback;
    void *userdata;
};

static size_t log_stream_callback(void *contents, size_t size, size_t nmemb, struct log_stream_data *data) {
    size_t total_size = size * nmemb;
    char *content = (char*)contents;
    
    // Docker log format: [8 byte header][payload]
    // Header: [stream type (1 byte)][reserved (3 bytes)][size (4 bytes)]
    size_t processed = 0;
    
    while (processed < total_size) {
        if (total_size - processed < 8) break; // Need at least header
        
        uint32_t payload_size = ntohl(*(uint32_t*)(content + processed + 4));
        
        if (processed + 8 + payload_size > total_size) break;
        
        char *line = malloc(payload_size + 1);
        if (line) {
            memcpy(line, content + processed + 8, payload_size);
            line[payload_size] = '\0';
            
            // Remove trailing newlines
            while (payload_size > 0 && (line[payload_size-1] == '\n' || line[payload_size-1] == '\r')) {
                line[--payload_size] = '\0';
            }
            
            if (payload_size > 0) {
                data->callback(line, data->userdata);
            }
            free(line);
        }
        
        processed += 8 + payload_size;
    }
    
    return total_size;
}

/* ----------------- Core API Implementation ----------------- */

docker_excess_error_t docker_excess_new(docker_excess_t **client) {
    docker_excess_config_t config = docker_excess_default_config();
    return docker_excess_new_with_config(&config, client);
}

docker_excess_error_t docker_excess_new_with_config(const docker_excess_config_t *config, docker_excess_t **client) {
    if (!config || !client) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    docker_excess_t *c = calloc(1, sizeof(docker_excess_t));
    if (!c) return DOCKER_EXCESS_ERR_MEMORY;
    
    // Initialize config with deep copy
    memset(&c->config, 0, sizeof(docker_excess_config_t));
    c->config.socket_path = config->socket_path ? strdup(config->socket_path) : NULL;
    c->config.host = config->host ? strdup(config->host) : NULL;
    c->config.port = config->port;
    c->config.use_tls = config->use_tls;
    c->config.cert_path = config->cert_path ? strdup(config->cert_path) : NULL;
    c->config.key_path = config->key_path ? strdup(config->key_path) : NULL;
    c->config.ca_path = config->ca_path ? strdup(config->ca_path) : NULL;
    c->config.timeout_s = config->timeout_s;
    c->config.debug = config->debug;
    
    if (pthread_mutex_init(&c->mutex, NULL) != 0) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    // Initialize cURL
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    c->curl = curl_easy_init();
    if (!c->curl) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    c->curl_initialized = true;
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
    
    if (client->curl_initialized) {
        curl_global_cleanup();
    }
    
    pthread_mutex_destroy(&client->mutex);
    free(client);
}

const char* docker_excess_get_error(docker_excess_t *client) {
    if (!client) return "Invalid client";
    
    pthread_mutex_lock(&client->mutex);
    const char *msg = client->error_msg[0] ? client->error_msg : "No error";
    pthread_mutex_unlock(&client->mutex);
    
    return msg;
}

docker_excess_error_t docker_excess_ping(docker_excess_t *client) {
    char *response = NULL;
    int http_code;
    docker_excess_error_t err = make_request(client, "GET", "/_ping", NULL, &response, &http_code);
    
    free(response);
}

docker_excess_error_t docker_excess_connect_network(docker_excess_t *client, const char *network_id,
                                                   const char *container_id) {
    if (!client || !network_id || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    json_object *config = json_object_new_object();
    json_object_object_add(config, "Container", json_object_new_string(container_id));
    
    const char *json_string = json_object_to_json_string(config);
    
    char *encoded_id = url_encode(network_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/networks/%s/connect", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(config);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_disconnect_network(docker_excess_t *client, const char *network_id,
                                                      const char *container_id) {
    if (!client || !network_id || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    json_object *config = json_object_new_object();
    json_object_object_add(config, "Container", json_object_new_string(container_id));
    
    const char *json_string = json_object_to_json_string(config);
    
    char *encoded_id = url_encode(network_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/networks/%s/disconnect", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(config);
    free(response);
    
    return err;
}

/* ----------------- Volume Management Implementation ----------------- */

docker_excess_error_t docker_excess_list_volumes(docker_excess_t *client,
                                                docker_excess_volume_t ***volumes, size_t *count) {
    if (!client || !volumes || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", "/volumes", NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json) {
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    json_object *volumes_obj = get_json_object(json, "Volumes");
    if (!volumes_obj || json_object_get_type(volumes_obj) != json_type_array) {
        json_object_put(json);
        *volumes = NULL;
        *count = 0;
        return DOCKER_EXCESS_OK;
    }
    
    size_t array_len = json_object_array_length(volumes_obj);
    docker_excess_volume_t **result = NULL;
    
    if (array_len > 0) {
        result = calloc(array_len, sizeof(docker_excess_volume_t*));
        if (!result) {
            json_object_put(json);
            return DOCKER_EXCESS_ERR_MEMORY;
        }
        
        for (size_t i = 0; i < array_len; i++) {
            json_object *vol_obj = json_object_array_get_idx(volumes_obj, i);
            if (!vol_obj) continue;
            
            docker_excess_volume_t *volume = calloc(1, sizeof(docker_excess_volume_t));
            if (!volume) continue;
            
            const char *name = get_json_string(vol_obj, "Name");
            if (name) volume->name = strdup(name);
            
            const char *driver = get_json_string(vol_obj, "Driver");
            if (driver) volume->driver = strdup(driver);
            
            const char *mountpoint = get_json_string(vol_obj, "Mountpoint");
            if (mountpoint) volume->mountpoint = strdup(mountpoint);
            
            // Parse creation time from CreatedAt field
            const char *created_at = get_json_string(vol_obj, "CreatedAt");
            if (created_at) {
                // Simple timestamp parsing - in production you'd use strptime
                volume->created = time(NULL); // Fallback to current time
            }
            
            result[i] = volume;
        }
    }
    
    json_object_put(json);
    
    *volumes = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_create_volume(docker_excess_t *client, const char *name,
                                                 const char *driver, char **volume_name) {
    if (!client || !name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    json_object *config = json_object_new_object();
    json_object_object_add(config, "Name", json_object_new_string(name));
    
    if (driver) {
        json_object_object_add(config, "Driver", json_object_new_string(driver));
    }
    
    const char *json_string = json_object_to_json_string(config);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", "/volumes/create", json_string, &response, NULL);
    json_object_put(config);
    
    if (err == DOCKER_EXCESS_OK && volume_name && response) {
        json_object *resp_obj = json_tokener_parse(response);
        if (resp_obj) {
            const char *vol_name = get_json_string(resp_obj, "Name");
            if (vol_name) *volume_name = strdup(vol_name);
            json_object_put(resp_obj);
        }
    }
    
    free(response);
    return err;
}

docker_excess_error_t docker_excess_remove_volume(docker_excess_t *client, const char *volume_name, bool force) {
    if (!client || !volume_name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_name = url_encode(volume_name);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/volumes/%s?force=%s", encoded_name, force ? "true" : "false");
    free(encoded_name);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

/* ----------------- Raw API Access ----------------- */

docker_excess_error_t docker_excess_raw_request(docker_excess_t *client, const char *method,
                                               const char *endpoint, const char *body,
                                               char **response, int *http_code) {
    return make_request(client, method, endpoint, body, response, http_code);
}

/* ----------------- Utility Functions Implementation ----------------- */

void docker_excess_free_containers(docker_excess_container_t **containers, size_t count) {
    if (!containers) return;
    
    for (size_t i = 0; i < count; i++) {
        if (containers[i]) {
            free(containers[i]->id);
            free(containers[i]->name);
            free(containers[i]->image);
            free(containers[i]->status);
            
            if (containers[i]->ports) {
                for (size_t j = 0; j < containers[i]->ports_count; j++) {
                    free(containers[i]->ports[j]);
                }
                free(containers[i]->ports);
            }
            
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
            
            if (images[i]->repo_tags) {
                for (size_t j = 0; j < images[i]->repo_tags_count; j++) {
                    free(images[i]->repo_tags[j]);
                }
                free(images[i]->repo_tags);
            }
            
            free(images[i]);
        }
    }
    free(images);
}

void docker_excess_free_networks(docker_excess_network_t **networks, size_t count) {
    if (!networks) return;
    
    for (size_t i = 0; i < count; i++) {
        if (networks[i]) {
            free(networks[i]->id);
            free(networks[i]->name);
            free(networks[i]->driver);
            free(networks[i]->scope);
            free(networks[i]);
        }
    }
    free(networks);
}

void docker_excess_free_volumes(docker_excess_volume_t **volumes, size_t count) {
    if (!volumes) return;
    
    for (size_t i = 0; i < count; i++) {
        if (volumes[i]) {
            free(volumes[i]->name);
            free(volumes[i]->driver);
            free(volumes[i]->mountpoint);
            free(volumes[i]);
        }
    }
    free(volumes);
}

void docker_excess_free_files(docker_excess_file_t **files, size_t count) {
    if (!files) return;
    
    for (size_t i = 0; i < count; i++) {
        if (files[i]) {
            free(files[i]->name);
            free(files[i]);
        }
    }
    free(files);
}

docker_excess_config_t docker_excess_default_config(void) {
    docker_excess_config_t config = {0};
    config.socket_path = strdup(DOCKER_EXCESS_DEFAULT_SOCKET);
    config.timeout_s = DOCKER_EXCESS_DEFAULT_TIMEOUT_S;
    config.port = 2376;
    config.use_tls = false;
    config.debug = false;
    return config;
}

docker_excess_config_t docker_excess_config_from_env(void) {
    docker_excess_config_t config = docker_excess_default_config();
    
    const char *docker_host = getenv("DOCKER_HOST");
    if (docker_host) {
        if (strncmp(docker_host, "tcp://", 6) == 0) {
            // Parse TCP host
            const char *host_port = docker_host + 6;
            char *host_copy = strdup(host_port);
            char *colon = strchr(host_copy, ':');
            if (colon) {
                *colon = '\0';
                free(config.socket_path);
                config.socket_path = NULL;
                config.host = strdup(host_copy);
                config.port = (uint16_t)atoi(colon + 1);
            }
            free(host_copy);
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
        char cert_file[512], key_file[512], ca_file[512];
        snprintf(cert_file, sizeof(cert_file), "%s/cert.pem", cert_path);
        snprintf(key_file, sizeof(key_file), "%s/key.pem", cert_path);
        snprintf(ca_file, sizeof(ca_file), "%s/ca.pem", cert_path);
        
        if (access(cert_file, R_OK) == 0) config.cert_path = strdup(cert_file);
        if (access(key_file, R_OK) == 0) config.key_path = strdup(key_file);
        if (access(ca_file, R_OK) == 0) config.ca_path = strdup(ca_file);
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
    if (!buffer || buffer_size == 0) return;
    
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = (double)bytes;
    int unit_index = 0;
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buffer, buffer_size, "%ld %s", bytes, units[unit_index]);
    } else {
        snprintf(buffer, buffer_size, "%.1f %s", size, units[unit_index]);
    }
}

docker_excess_error_t docker_excess_resolve_container_id(docker_excess_t *client, const char *name_or_id,
                                                        char **full_id) {
    if (!client || !name_or_id || !full_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // First try to get container info directly
    char *encoded_name = url_encode(name_or_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/json", encoded_name);
    free(encoded_name);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    
    if (err == DOCKER_EXCESS_OK && response) {
        json_object *obj = json_tokener_parse(response);
        if (obj) {
            const char *id = get_json_string(obj, "Id");
            if (id) {
                *full_id = strdup(id);
                json_object_put(obj);
                free(response);
                return DOCKER_EXCESS_OK;
            }
            json_object_put(obj);
        }
    }
    
    free(response);
    
    // If direct lookup failed, search through all containers
    docker_excess_container_t **containers = NULL;
    size_t count = 0;
    
    err = docker_excess_list_containers(client, true, &containers, &count);
    if (err != DOCKER_EXCESS_OK) return err;
    
    for (size_t i = 0; i < count; i++) {
        if (containers[i] && containers[i]->id && containers[i]->name) {
            // Check if name matches
            if (strcmp(containers[i]->name, name_or_id) == 0) {
                *full_id = strdup(containers[i]->id);
                docker_excess_free_containers(containers, count);
                return DOCKER_EXCESS_OK;
            }
            
            // Check if it's a partial ID match
            if (strncmp(containers[i]->id, name_or_id, strlen(name_or_id)) == 0) {
                *full_id = strdup(containers[i]->id);
                docker_excess_free_containers(containers, count);
                return DOCKER_EXCESS_OK;
            }
        }
    }
    
    docker_excess_free_containers(containers, count);
    set_error(client, "Container not found: %s", name_or_id);
    return DOCKER_EXCESS_ERR_NOT_FOUND;
}
}

docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json) {
    return make_request(client, "GET", "/version", NULL, version_json, NULL);
}

/* ----------------- Container Management Implementation ----------------- */

docker_excess_error_t docker_excess_list_containers(docker_excess_t *client, bool all,
                                                   docker_excess_container_t ***containers, size_t *count) {
    if (!client || !containers || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/containers/json?all=%s", all ? "true" : "false");
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_pause_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/pause", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_unpause_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/unpause", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_wait_container(docker_excess_t *client, const char *container_id, int *exit_code) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/wait", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
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

/* ----------------- Log and Exec Implementation ----------------- */

docker_excess_error_t docker_excess_get_logs(docker_excess_t *client, const char *container_id,
                                            bool follow, bool timestamps, int tail_lines,
                                            docker_excess_log_callback_t callback, void *userdata) {
    if (!client || !container_id || !callback) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char url[1024];
    snprintf(url, sizeof(url), 
             "http://localhost/v%s/containers/%s/logs?stdout=true&stderr=true&follow=%s&timestamps=%s&tail=%d",
             DOCKER_EXCESS_API_VERSION, encoded_id, follow ? "true" : "false", 
             timestamps ? "true" : "false", tail_lines);
    free(encoded_id);
    
    pthread_mutex_lock(&client->mutex);
    
    struct log_stream_data stream_data = { callback, userdata };
    
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, log_stream_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &stream_data);
    
    if (!client->config.host) {
        curl_easy_setopt(client->curl, CURLOPT_UNIX_SOCKET_PATH, client->config.socket_path);
    }
    
    CURLcode res = curl_easy_perform(client->curl);
    pthread_mutex_unlock(&client->mutex);
    
    if (res != CURLE_OK) {
        set_error(client, "cURL error in logs: %s", curl_easy_strerror(res));
        return DOCKER_EXCESS_ERR_NETWORK;
    }
    
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_exec_simple(docker_excess_t *client, const char *container_id,
                                               const char *command, char **stdout_out, char **stderr_out, int *exit_code) {
    if (!client || !container_id || !command) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // Create exec instance
    json_object *exec_config = json_object_new_object();
    json_object_object_add(exec_config, "AttachStdout", json_object_new_boolean(true));
    json_object_object_add(exec_config, "AttachStderr", json_object_new_boolean(true));
    json_object_object_add(exec_config, "Tty", json_object_new_boolean(false));
    
    json_object *cmd_array = json_object_new_array();
    json_object_array_add(cmd_array, json_object_new_string("/bin/sh"));
    json_object_array_add(cmd_array, json_object_new_string("-c"));
    json_object_array_add(cmd_array, json_object_new_string(command));
    json_object_object_add(exec_config, "Cmd", cmd_array);
    
    const char *json_string = json_object_to_json_string(exec_config);
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/exec", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(exec_config);
    
    if (err != DOCKER_EXCESS_OK) {
        free(response);
        return err;
    }
    
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
        response = NULL;
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
        *stdout_out = strdup(response);
    }
    
    // Get exit code
    if (exit_code) {
        snprintf(endpoint, sizeof(endpoint), "/exec/%s/json", exec_id);
        char *inspect_response = NULL;
        if (make_request(client, "GET", endpoint, NULL, &inspect_response, NULL) == DOCKER_EXCESS_OK && inspect_response) {
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
    if (!client || !container_id || !command || cmd_count == 0) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // Build command string
    size_t total_len = 0;
    for (size_t i = 0; i < cmd_count; i++) {
        total_len += strlen(command[i]) + (i > 0 ? 1 : 0); // +1 for space
    }
    
    char *full_cmd = malloc(total_len + 1);
    if (!full_cmd) return DOCKER_EXCESS_ERR_MEMORY;
    
    strcpy(full_cmd, command[0]);
    for (size_t i = 1; i < cmd_count; i++) {
        strcat(full_cmd, " ");
        strcat(full_cmd, command[i]);
    }
    
    char *stdout_data = NULL, *stderr_data = NULL;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, full_cmd, &stdout_data, &stderr_data, NULL);
    
    if (err == DOCKER_EXCESS_OK && callback) {
        callback(stdout_data, stderr_data, userdata);
    }
    
    free(full_cmd);
    free(stdout_data);
    free(stderr_data);
    return err;
}

/* ----------------- File Operations Implementation ----------------- */

docker_excess_error_t docker_excess_list_files(docker_excess_t *client, const char *container_id,
                                              const char *path, docker_excess_file_t ***files, size_t *count) {
    if (!client || !container_id || !path || !files || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    if (asprintf(&cmd, "find '%s' -maxdepth 1 -printf '%%y\\t%%s\\t%%T@\\t%%m\\t%%P\\n' 2>/dev/null | head -1000", path) < 0) {
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *output = NULL;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, NULL);
    free(cmd);
    
    if (err != DOCKER_EXCESS_OK) {
        free(output);
        return err;
    }
    
    if (!output || strlen(output) == 0) {
        *files = NULL;
        *count = 0;
        free(output);
        return DOCKER_EXCESS_OK;
    }
    
    // Count lines
    size_t line_count = 0;
    char *line_ptr = output;
    while ((line_ptr = strchr(line_ptr, '\n')) != NULL) {
        line_count++;
        line_ptr++;
    }
    
    if (line_count == 0) {
        *files = NULL;
        *count = 0;
        free(output);
        return DOCKER_EXCESS_OK;
    }
    
    docker_excess_file_t **result = calloc(line_count, sizeof(docker_excess_file_t*));
    if (!result) {
        free(output);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *line = strtok(output, "\n");
    size_t file_count = 0;
    
    while (line && file_count < line_count) {
        char *type = strtok(line, "\t");
        char *size_str = strtok(NULL, "\t");
        char *mtime_str = strtok(NULL, "\t");
        char *mode_str = strtok(NULL, "\t");
        char *name = strtok(NULL, "\t");
        
        if (type && size_str && mtime_str && mode_str && name && strlen(name) > 0) {
            docker_excess_file_t *file = calloc(1, sizeof(docker_excess_file_t));
            if (file) {
                file->name = strdup(name);
                file->size = strtoll(size_str, NULL, 10);
                file->modified = (int64_t)(strtod(mtime_str, NULL));
                file->is_dir = (type[0] == 'd');
                file->mode = (uint32_t)strtoul(mode_str, NULL, 8);
                
                result[file_count++] = file;
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(output);
    
    *files = result;
    *count = file_count;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_read_file(docker_excess_t *client, const char *container_id,
                                             const char *file_path, char **content, size_t *size) {
    if (!client || !container_id || !file_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    if (asprintf(&cmd, "if [ -f '%s' ]; then cat '%s' 2>/dev/null; else echo 'File not found' >&2; exit 1; fi", file_path, file_path) < 0) {
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *output = NULL;
    int exit_code = 0;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, &exit_code);
    
    free(cmd);
    
    if (err != DOCKER_EXCESS_OK) {
        free(output);
        return err;
    }
    
    if (exit_code != 0) {
        free(output);
        set_error(client, "File not found or not accessible: %s", file_path);
        return DOCKER_EXCESS_ERR_NOT_FOUND;
    }
    
    if (content) {
        *content = output;
        if (size) *size = output ? strlen(output) : 0;
    } else {
        free(output);
    }
    
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_write_file(docker_excess_t *client, const char *container_id,
                                              const char *file_path, const char *content, size_t size) {
    if (!client || !container_id || !file_path || !content) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // Use base64 encoding to handle binary content safely
    char *b64_content = base64_encode(content, size);
    if (!b64_content) return DOCKER_EXCESS_ERR_MEMORY;
    
    char *cmd = NULL;
    if (asprintf(&cmd, "echo '%s' | base64 -d > '%s'", b64_content, file_path) < 0) {
        free(b64_content);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *output = NULL;
    int exit_code = 0;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, &exit_code);
    
    free(cmd);
    free(b64_content);
    free(output);
    
    if (err != DOCKER_EXCESS_OK) return err;
    if (exit_code != 0) return DOCKER_EXCESS_ERR_INTERNAL;
    
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_copy_from_container(docker_excess_t *client, const char *container_id,
                                                       const char *container_path, const char *host_path) {
    if (!client || !container_id || !container_path || !host_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char *encoded_path = url_encode(container_path);
    char endpoint[1024];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/archive?path=%s", encoded_id, encoded_path);
    free(encoded_id);
    free(encoded_path);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    
    if (err == DOCKER_EXCESS_OK && response) {
        FILE *file = fopen(host_path, "wb");
        if (file) {
            fwrite(response, 1, strlen(response), file);
            fclose(file);
        } else {
            err = DOCKER_EXCESS_ERR_INTERNAL;
            set_error(client, "Could not write to host file: %s", host_path);
        }
    }
    
    free(response);
    return err;
}

docker_excess_error_t docker_excess_copy_to_container(docker_excess_t *client, const char *container_id,
                                                     const char *host_path, const char *container_path) {
    if (!client || !container_id || !host_path || !container_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    FILE *file = fopen(host_path, "rb");
    if (!file) {
        set_error(client, "Could not read host file: %s", host_path);
        return DOCKER_EXCESS_ERR_INVALID_PARAM;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *file_content = malloc(file_size);
    if (!file_content) {
        fclose(file);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    fread(file_content, 1, file_size, file);
    fclose(file);
    
    char *encoded_id = url_encode(container_id);
    char *encoded_path = url_encode(container_path);
    char endpoint[1024];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/archive?path=%s", encoded_id, encoded_path);
    free(encoded_id);
    free(encoded_path);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "PUT", endpoint, file_content, &response, NULL);
    
    free(file_content);
    free(response);
    return err;
}

docker_excess_error_t docker_excess_mkdir(docker_excess_t *client, const char *container_id,
                                         const char *dir_path, uint32_t mode) {
    if (!client || !container_id || !dir_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    if (asprintf(&cmd, "mkdir -p '%s' && chmod %o '%s'", dir_path, mode, dir_path) < 0) {
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *output = NULL;
    int exit_code = 0;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, &exit_code);
    
    free(cmd);
    free(output);
    
    if (err != DOCKER_EXCESS_OK) return err;
    if (exit_code != 0) return DOCKER_EXCESS_ERR_INTERNAL;
    
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_remove_file(docker_excess_t *client, const char *container_id,
                                               const char *path, bool recursive) {
    if (!client || !container_id || !path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *cmd = NULL;
    if (recursive) {
        if (asprintf(&cmd, "rm -rf '%s'", path) < 0) return DOCKER_EXCESS_ERR_MEMORY;
    } else {
        if (asprintf(&cmd, "rm -f '%s'", path) < 0) return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    char *output = NULL;
    int exit_code = 0;
    docker_excess_error_t err = docker_excess_exec_simple(client, container_id, cmd, &output, NULL, &exit_code);
    
    free(cmd);
    free(output);
    
    if (err != DOCKER_EXCESS_OK) return err;
    if (exit_code != 0) return DOCKER_EXCESS_ERR_INTERNAL;
    
    return DOCKER_EXCESS_OK;
}

/* ----------------- Image Management Implementation ----------------- */

docker_excess_error_t docker_excess_list_images(docker_excess_t *client, bool all,
                                               docker_excess_image_t ***images, size_t *count) {
    if (!client || !images || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/images/json?all=%s", all ? "true" : "false");
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json || json_object_get_type(json) != json_type_array) {
        if (json) json_object_put(json);
        set_error(client, "Invalid JSON response for image list");
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    size_t array_len = json_object_array_length(json);
    docker_excess_image_t **result = NULL;
    
    if (array_len > 0) {
        result = calloc(array_len, sizeof(docker_excess_image_t*));
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
            if (repo_tags) {
                image->repo_tags = parse_json_string_array(repo_tags, &image->repo_tags_count);
            }
            
            result[i] = image;
        }
    }
    
    json_object_put(json);
    
    *images = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_pull_image(docker_excess_t *client, const char *image_name, const char *tag) {
    if (!client || !image_name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_name = url_encode(image_name);
    char endpoint[1024];
    
    if (tag) {
        char *encoded_tag = url_encode(tag);
        snprintf(endpoint, sizeof(endpoint), "/images/create?fromImage=%s&tag=%s", encoded_name, encoded_tag);
        free(encoded_tag);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/images/create?fromImage=%s", encoded_name);
    }
    
    free(encoded_name);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_remove_image(docker_excess_t *client, const char *image_name, bool force) {
    if (!client || !image_name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_name = url_encode(image_name);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/images/%s?force=%s", encoded_name, force ? "true" : "false");
    free(encoded_name);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_build_image(docker_excess_t *client, const char *dockerfile_path,
                                               const char *context_path, const char *tag) {
    if (!client || !dockerfile_path || !context_path) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    // This is a simplified implementation - full implementation would need tar creation
    char endpoint[512];
    if (tag) {
        char *encoded_tag = url_encode(tag);
        snprintf(endpoint, sizeof(endpoint), "/build?t=%s", encoded_tag);
        free(encoded_tag);
    } else {
        strcpy(endpoint, "/build");
    }
    
    // TODO: Implement tar archive creation from build context
    set_error(client, "Image building requires tar archive creation - not fully implemented");
    return DOCKER_EXCESS_ERR_INTERNAL;
}

/* ----------------- Network Management Implementation ----------------- */

docker_excess_error_t docker_excess_list_networks(docker_excess_t *client,
                                                 docker_excess_network_t ***networks, size_t *count) {
    if (!client || !networks || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", "/networks", NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json || json_object_get_type(json) != json_type_array) {
        if (json) json_object_put(json);
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    size_t array_len = json_object_array_length(json);
    docker_excess_network_t **result = NULL;
    
    if (array_len > 0) {
        result = calloc(array_len, sizeof(docker_excess_network_t*));
        if (!result) {
            json_object_put(json);
            return DOCKER_EXCESS_ERR_MEMORY;
        }
        
        for (size_t i = 0; i < array_len; i++) {
            json_object *net_obj = json_object_array_get_idx(json, i);
            if (!net_obj) continue;
            
            docker_excess_network_t *network = calloc(1, sizeof(docker_excess_network_t));
            if (!network) continue;
            
            const char *id = get_json_string(net_obj, "Id");
            if (id) network->id = strdup(id);
            
            const char *name = get_json_string(net_obj, "Name");
            if (name) network->name = strdup(name);
            
            const char *driver = get_json_string(net_obj, "Driver");
            if (driver) network->driver = strdup(driver);
            
            const char *scope = get_json_string(net_obj, "Scope");
            if (scope) network->scope = strdup(scope);
            
            network->created = get_json_int(net_obj, "Created");
            
            result[i] = network;
        }
    }
    
    json_object_put(json);
    
    *networks = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_create_network(docker_excess_t *client, const char *name,
                                                  const char *driver, char **network_id) {
    if (!client || !name) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    json_object *config = json_object_new_object();
    json_object_object_add(config, "Name", json_object_new_string(name));
    
    if (driver) {
        json_object_object_add(config, "Driver", json_object_new_string(driver));
    }
    
    const char *json_string = json_object_to_json_string(config);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", "/networks/create", json_string, &response, NULL);
    json_object_put(config);
    
    if (err == DOCKER_EXCESS_OK && network_id && response) {
        json_object *resp_obj = json_tokener_parse(response);
        if (resp_obj) {
            const char *id = get_json_string(resp_obj, "Id");
            if (id) *network_id = strdup(id);
            json_object_put(resp_obj);
        }
    }
    
    free(response);
    return err;
}

docker_excess_error_t docker_excess_remove_network(docker_excess_t *client, const char *network_id) {
    if (!client || !network_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(network_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/networks/%s", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "DELETE", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    free(response);
    
    if (!json || json_object_get_type(json) != json_type_array) {
        if (json) json_object_put(json);
        set_error(client, "Invalid JSON response for container list");
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    size_t array_len = json_object_array_length(json);
    docker_excess_container_t **result = NULL;
    
    if (array_len > 0) {
        result = calloc(array_len, sizeof(docker_excess_container_t*));
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
            
            // Parse ports
            json_object *ports_obj = get_json_object(container_obj, "Ports");
            if (ports_obj) {
                container->ports = parse_json_string_array(ports_obj, &container->ports_count);
            }
            
            result[i] = container;
        }
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
    json_object_object_add(config, "Image", json_object_new_string(params->image));
    
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
    
    // Host configuration
    json_object *host_config = json_object_new_object();
    
    if (params->auto_remove) {
        json_object_object_add(host_config, "AutoRemove", json_object_new_boolean(true));
    }
    
    // Port bindings
    if (params->ports && params->ports_count > 0) {
        json_object *port_bindings = json_object_new_object();
        json_object *exposed_ports = json_object_new_object();
        
        for (size_t i = 0; i < params->ports_count; i++) {
            char *port_mapping = strdup(params->ports[i]);
            char *colon = strchr(port_mapping, ':');
            if (colon) {
                *colon = '\0';
                const char *host_port = port_mapping;
                const char *container_port = colon + 1;
                
                char container_port_key[64];
                snprintf(container_port_key, sizeof(container_port_key), "%s/tcp", container_port);
                
                json_object *binding_array = json_object_new_array();
                json_object *binding = json_object_new_object();
                json_object_object_add(binding, "HostPort", json_object_new_string(host_port));
                json_object_array_add(binding_array, binding);
                
                json_object_object_add(port_bindings, container_port_key, binding_array);
                json_object_object_add(exposed_ports, container_port_key, json_object_new_object());
            }
            free(port_mapping);
        }
        
        json_object_object_add(host_config, "PortBindings", port_bindings);
        json_object_object_add(config, "ExposedPorts", exposed_ports);
    }
    
    // Volume bindings
    if (params->volumes && params->volumes_count > 0) {
        json_object *binds = json_object_new_array();
        for (size_t i = 0; i < params->volumes_count; i++) {
            json_object_array_add(binds, json_object_new_string(params->volumes[i]));
        }
        json_object_object_add(host_config, "Binds", binds);
    }
    
    // Create final container spec
    json_object *create_obj = json_object_new_object();
    json_object_object_add(create_obj, "Config", config);
    json_object_object_add(create_obj, "HostConfig", host_config);
    
    const char *json_string = json_object_to_json_string(create_obj);
    
    char endpoint[512];
    if (params->name) {
        char *encoded_name = url_encode(params->name);
        snprintf(endpoint, sizeof(endpoint), "/containers/create?name=%s", encoded_name);
        free(encoded_name);
    } else {
        strcpy(endpoint, "/containers/create");
    }
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, json_string, &response, NULL);
    json_object_put(create_obj);
    
    if (err != DOCKER_EXCESS_OK) {
        free(response);
        return err;
    }
    
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
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/start", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_stop_container(docker_excess_t *client, const char *container_id, int timeout_s) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    if (timeout_s > 0) {
        snprintf(endpoint, sizeof(endpoint), "/containers/%s/stop?t=%d", encoded_id, timeout_s);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/containers/%s/stop", encoded_id);
    }
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_restart_container(docker_excess_t *client, const char *container_id) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/restart", encoded_id);
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "POST", endpoint, NULL, &response, NULL);
    free(response);
    
    return err;
}

docker_excess_error_t docker_excess_remove_container(docker_excess_t *client, const char *container_id, bool force) {
    if (!client || !container_id) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s?force=%s", encoded_id, force ? "true" : "false");
    free(encoded_id);
    
    char *response = NULL;
    docker_excess_error