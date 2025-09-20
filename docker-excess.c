#include "docker-excess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
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
    char error_msg[DOCKER_EXCESS_MAX_ERROR_MSG];
    pthread_mutex_t mutex;
    bool curl_initialized;
    bool is_connected;
    time_t last_ping;
};

/* Global initialization counter for libcurl */
static int g_curl_init_count = 0;
static pthread_mutex_t g_curl_init_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ----------------- Static Helper Functions ----------------- */

static void docker_excess_log(docker_excess_t *client, docker_excess_log_level_t level, const char *format, ...) {
    if (!client || !client->config.log_callback) return;
    
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    client->config.log_callback(level, message, client->config.log_userdata);
}

static size_t write_response_callback(void *contents, size_t size, size_t nmemb, response_buffer_t *buffer) {
    size_t total_size = size * nmemb;
    
    if (buffer->size + total_size >= buffer->capacity) {
        size_t new_capacity = (buffer->capacity == 0) ? 8192 : buffer->capacity * 2;
        while (new_capacity < buffer->size + total_size + 1) {
            new_capacity *= 2;
        }
        
        char *new_data = realloc(buffer->data, new_capacity);
        if (!new_data) {
            return 0; /* Signal error to curl */
        }
        
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
    
    docker_excess_log(client, DOCKER_EXCESS_LOG_ERROR, "%s", client->error_msg);
}

static char* safe_strdup(const char *str) {
    return str ? strdup(str) : NULL;
}

static void safe_free(void *ptr) {
    if (ptr) free(ptr);
}

static char* url_encode(const char *str) {
    if (!str) return NULL;
    
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    char *encoded = curl_easy_escape(curl, str, 0);
    char *result = encoded ? strdup(encoded) : NULL;
    
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

static bool is_success_status(int http_code) {
    return http_code >= 200 && http_code < 300;
}

static docker_excess_error_t map_http_error(int http_code) {
    switch (http_code) {
        case 400: return DOCKER_EXCESS_ERR_INVALID_PARAM;
        case 401: case 403: return DOCKER_EXCESS_ERR_PERMISSION;
        case 404: return DOCKER_EXCESS_ERR_NOT_FOUND;
        case 409: return DOCKER_EXCESS_ERR_CONFLICT;
        case 500: case 502: case 503: return DOCKER_EXCESS_ERR_INTERNAL;
        default: return DOCKER_EXCESS_ERR_HTTP;
    }
}

static docker_excess_error_t make_request(docker_excess_t *client, const char *method, 
                                         const char *endpoint, const char *body,
                                         char **response, int *http_code) {
    if (!client || !method || !endpoint) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&client->mutex);
    
    response_buffer_t buffer = {0};
    char url[DOCKER_EXCESS_MAX_URL_LEN];
    
    /* Build URL */
    if (client->config.host) {
        snprintf(url, sizeof(url), "%s://%s:%d/v%s%s",
                client->config.use_tls ? "https" : "http",
                client->config.host, client->config.port, 
                DOCKER_EXCESS_API_VERSION, endpoint);
    } else {
        snprintf(url, sizeof(url), "http://localhost/v%s%s", 
                DOCKER_EXCESS_API_VERSION, endpoint);
    }
    
    docker_excess_log(client, DOCKER_EXCESS_LOG_DEBUG, "Making %s request to %s", method, url);
    
    /* Configure cURL */
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, (long)client->config.timeout_s);
    curl_easy_setopt(client->curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L); /* Thread safety */
    
    if (client->config.debug) {
        curl_easy_setopt(client->curl, CURLOPT_VERBOSE, 1L);
    }
    
    /* Unix socket configuration */
    if (!client->config.host) {
        curl_easy_setopt(client->curl, CURLOPT_UNIX_SOCKET_PATH, client->config.socket_path);
    }
    
    /* TLS configuration */
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
    
    /* Headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: docker-excess/2.0");
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    
    /* Request body */
    if (body && strlen(body) > 0) {
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(client->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    
    /* Perform request */
    CURLcode res = curl_easy_perform(client->curl);
    long response_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    pthread_mutex_unlock(&client->mutex);
    
    if (http_code) *http_code = (int)response_code;
    
    docker_excess_log(client, DOCKER_EXCESS_LOG_DEBUG, "Request completed with HTTP %ld", response_code);
    
    if (res != CURLE_OK) {
        set_error(client, "cURL error: %s", curl_easy_strerror(res));
        safe_free(buffer.data);
        
        switch (res) {
            case CURLE_OPERATION_TIMEDOUT:
                return DOCKER_EXCESS_ERR_TIMEOUT;
            case CURLE_COULDNT_CONNECT:
            case CURLE_COULDNT_RESOLVE_HOST:
                return DOCKER_EXCESS_ERR_NETWORK;
            default:
                return DOCKER_EXCESS_ERR_INTERNAL;
        }
    }
    
    if (response) {
        *response = buffer.data;
    } else {
        safe_free(buffer.data);
    }
    
    if (!is_success_status(response_code)) {
        return map_http_error(response_code);
    }
    
    return DOCKER_EXCESS_OK;
}

/* ----------------- JSON Helper Functions ----------------- */

static json_object* get_json_object(json_object *obj, const char *key) {
    if (!obj || !key) return NULL;
    json_object *result;
    return json_object_object_get_ex(obj, key, &result) ? result : NULL;
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
        result[i] = safe_strdup(str);
    }
    
    *count = len;
    return result;
}

static docker_excess_container_state_t parse_container_state(const char *state_str) {
    if (!state_str) return DOCKER_EXCESS_STATE_CREATED;
    
    static const struct {
        const char *name;
        docker_excess_container_state_t state;
    } state_map[] = {
        {"created", DOCKER_EXCESS_STATE_CREATED},
        {"restarting", DOCKER_EXCESS_STATE_RESTARTING},
        {"running", DOCKER_EXCESS_STATE_RUNNING},
        {"removing", DOCKER_EXCESS_STATE_REMOVING},
        {"paused", DOCKER_EXCESS_STATE_PAUSED},
        {"exited", DOCKER_EXCESS_STATE_EXITED},
        {"dead", DOCKER_EXCESS_STATE_DEAD}
    };
    
    for (size_t i = 0; i < sizeof(state_map) / sizeof(state_map[0]); i++) {
        if (strcmp(state_str, state_map[i].name) == 0) {
            return state_map[i].state;
        }
    }
    
    return DOCKER_EXCESS_STATE_CREATED;
}

/* ----------------- Core API Implementation ----------------- */

const char* docker_excess_get_version(void) {
    return DOCKER_EXCESS_VERSION;
}

docker_excess_error_t docker_excess_new(docker_excess_t **client) {
    docker_excess_config_t config = docker_excess_default_config();
    docker_excess_error_t err = docker_excess_new_with_config(&config, client);
    docker_excess_free_config(&config);
    return err;
}

docker_excess_error_t docker_excess_new_with_config(const docker_excess_config_t *config, docker_excess_t **client) {
    if (!config || !client) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    docker_excess_t *c = calloc(1, sizeof(docker_excess_t));
    if (!c) return DOCKER_EXCESS_ERR_MEMORY;
    
    /* Initialize config with deep copy */
    memset(&c->config, 0, sizeof(docker_excess_config_t));
    c->config.socket_path = safe_strdup(config->socket_path);
    c->config.host = safe_strdup(config->host);
    c->config.port = config->port;
    c->config.use_tls = config->use_tls;
    c->config.cert_path = safe_strdup(config->cert_path);
    c->config.key_path = safe_strdup(config->key_path);
    c->config.ca_path = safe_strdup(config->ca_path);
    c->config.timeout_s = config->timeout_s;
    c->config.debug = config->debug;
    c->config.log_callback = config->log_callback;
    c->config.log_userdata = config->log_userdata;
    
    if (pthread_mutex_init(&c->mutex, NULL) != 0) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    /* Initialize cURL (thread-safe) */
    pthread_mutex_lock(&g_curl_init_mutex);
    if (g_curl_init_count == 0) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            pthread_mutex_unlock(&g_curl_init_mutex);
            docker_excess_free(c);
            return DOCKER_EXCESS_ERR_INTERNAL;
        }
    }
    g_curl_init_count++;
    pthread_mutex_unlock(&g_curl_init_mutex);
    
    c->curl = curl_easy_init();
    if (!c->curl) {
        docker_excess_free(c);
        return DOCKER_EXCESS_ERR_INTERNAL;
    }
    
    c->curl_initialized = true;
    c->is_connected = false;
    c->last_ping = 0;
    
    docker_excess_log(c, DOCKER_EXCESS_LOG_INFO, "Docker client created successfully");
    
    *client = c;
    return DOCKER_EXCESS_OK;
}

void docker_excess_free(docker_excess_t *client) {
    if (!client) return;
    
    docker_excess_log(client, DOCKER_EXCESS_LOG_DEBUG, "Freeing Docker client");
    
    /* Free configuration */
    safe_free(client->config.socket_path);
    safe_free(client->config.host);
    safe_free(client->config.cert_path);
    safe_free(client->config.key_path);
    safe_free(client->config.ca_path);
    
    /* Cleanup cURL */
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    
    if (client->curl_initialized) {
        pthread_mutex_lock(&g_curl_init_mutex);
        g_curl_init_count--;
        if (g_curl_init_count == 0) {
            curl_global_cleanup();
        }
        pthread_mutex_unlock(&g_curl_init_mutex);
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

void docker_excess_clear_error(docker_excess_t *client) {
    if (!client) return;
    
    pthread_mutex_lock(&client->mutex);
    client->error_msg[0] = '\0';
    pthread_mutex_unlock(&client->mutex);
}

docker_excess_error_t docker_excess_ping(docker_excess_t *client) {
    if (!client) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *response = NULL;
    int http_code;
    docker_excess_error_t err = make_request(client, "GET", "/_ping", NULL, &response, &http_code);
    
    if (err == DOCKER_EXCESS_OK) {
        client->is_connected = true;
        client->last_ping = time(NULL);
        docker_excess_log(client, DOCKER_EXCESS_LOG_DEBUG, "Ping successful");
    } else {
        client->is_connected = false;
        docker_excess_log(client, DOCKER_EXCESS_LOG_WARN, "Ping failed");
    }
    
    safe_free(response);
    return err;
}

docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json) {
    return make_request(client, "GET", "/version", NULL, version_json, NULL);
}

docker_excess_error_t docker_excess_system_info(docker_excess_t *client, char **info_json) {
    return make_request(client, "GET", "/info", NULL, info_json, NULL);
}

/* ----------------- Container Management Implementation ----------------- */

docker_excess_error_t docker_excess_list_containers(docker_excess_t *client, bool all,
                                                   const char *filters,
                                                   docker_excess_container_t ***containers, size_t *count) {
    if (!client || !containers || !count) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char endpoint[512];
    if (filters) {
        char *encoded_filters = url_encode(filters);
        snprintf(endpoint, sizeof(endpoint), "/containers/json?all=%s&filters=%s", 
                all ? "true" : "false", encoded_filters);
        safe_free(encoded_filters);
    } else {
        snprintf(endpoint, sizeof(endpoint), "/containers/json?all=%s", all ? "true" : "false");
    }
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    safe_free(response);
    
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
            if (id) {
                container->id = safe_strdup(id);
                container->short_id = docker_excess_short_id(id);
            }
            
            const char *image = get_json_string(container_obj, "Image");
            container->image = safe_strdup(image);
            
            const char *image_id = get_json_string(container_obj, "ImageID");
            container->image_id = safe_strdup(image_id);
            
            const char *status = get_json_string(container_obj, "Status");
            container->status = safe_strdup(status);
            
            const char *state = get_json_string(container_obj, "State");
            container->state = parse_container_state(state);
            
            container->created = get_json_int(container_obj, "Created");
            
            /* Parse names array */
            json_object *names_obj = get_json_object(container_obj, "Names");
            if (names_obj && json_object_get_type(names_obj) == json_type_array) {
                if (json_object_array_length(names_obj) > 0) {
                    json_object *name_obj = json_object_array_get_idx(names_obj, 0);
                    const char *name = json_object_get_string(name_obj);
                    if (name && name[0] == '/') {
                        container->name = safe_strdup(name + 1); /* Skip leading '/' */
                    }
                }
            }
            
            /* Parse labels */
            json_object *labels_obj = get_json_object(container_obj, "Labels");
            if (labels_obj) {
                docker_excess_parse_labels(json_object_to_json_string(labels_obj), 
                                         &container->labels, &container->labels_count);
            }
            
            result[i] = container;
        }
    }
    
    json_object_put(json);
    
    *containers = result;
    *count = array_len;
    return DOCKER_EXCESS_OK;
}

docker_excess_error_t docker_excess_inspect_container(docker_excess_t *client, const char *container_id,
                                                     docker_excess_container_t **container) {
    if (!client || !container_id || !container) return DOCKER_EXCESS_ERR_INVALID_PARAM;
    
    char *encoded_id = url_encode(container_id);
    if (!encoded_id) return DOCKER_EXCESS_ERR_MEMORY;
    
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint), "/containers/%s/json", encoded_id);
    safe_free(encoded_id);
    
    char *response = NULL;
    docker_excess_error_t err = make_request(client, "GET", endpoint, NULL, &response, NULL);
    if (err != DOCKER_EXCESS_OK) return err;
    
    json_object *json = json_tokener_parse(response);
    safe_free(response);
    
    if (!json) {
        return DOCKER_EXCESS_ERR_JSON;
    }
    
    docker_excess_container_t *result = calloc(1, sizeof(docker_excess_container_t));
    if (!result) {
        json_object_put(json);
        return DOCKER_EXCESS_ERR_MEMORY;
    }
    
    /* Parse detailed container information */
    const char *id = get_json_string(json, "Id");
    if (id) {
        result->id = safe_strdup(id);
        result->short_id = docker_excess_short_id(id);
    }
    
    const char *name = get_json_string(json, "Name");
    if (name && name[0] == '/') {
        result->name = safe_strdup(name + 1);
    }
    
    /* Parse Config section */
    json_object *config_obj = get_json_object(json, "Config");
    if (config_obj) {
        const char *image = get_json_string(config_obj, "Image");
        result->image = safe_strdup(image);
        
        json_object *labels_obj = get_json_object(config_obj, "Labels");
        if (labels_obj) {
            docker_excess_parse_labels(json_object_to_json_string(labels_obj),
                                     &result->labels, &result->labels_count);
        }
    }
    
    /* Parse State section */
    json_object *state_obj = get_json_object(json, "State");
    if (state_obj) {
        const char *status = get_json_string(state_obj, "Status");
        result->state = parse_container_state(status);
        
        result->exit_code = (int)get_json_int(state_obj, "ExitCode");
        
        const char *started_at = get_json_string(state_obj, "StartedAt");
        const char *finished_at = get_json_string(state_obj, "FinishedAt");
        /* TODO: Parse ISO 8601 timestamps */
    }
    
    result->created = get_json_int(json, "Created");
    
    json_object_put(json);
    *container = result;
    return DOCKER_EXCESS_OK;
}
