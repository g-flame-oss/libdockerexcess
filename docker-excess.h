// docker.h

#ifndef DOCKER_H
#define DOCKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <curl/curl.h>
#include <json-c/json.h>

/* ----------------- Configurable defaults ----------------- */
#ifndef DOCKER_SDK_API_VERSION
#define DOCKER_SDK_API_VERSION "1.41"   /* default API version (use "1.41" style) */
#endif

#ifndef DOCKER_SDK_DEFAULT_SOCKET
#define DOCKER_SDK_DEFAULT_SOCKET "/var/run/docker.sock"
#endif

#ifndef DOCKER_SDK_DEFAULT_CONNECT_TIMEOUT_S
#define DOCKER_SDK_DEFAULT_CONNECT_TIMEOUT_S 10L
#endif

#ifndef DOCKER_SDK_DEFAULT_TIMEOUT_S
#define DOCKER_SDK_DEFAULT_TIMEOUT_S 60L
#endif

/* ----------------- Types & constants ----------------- */

/* Opaque client handle */
typedef struct docker_client DOCKER;

/* Maximum ID storage (64 hex + NUL) */
#define DOCKER_ID_LEN 65

/* Buffer returned by the SDK (raw HTTP body) */
typedef struct {
    char *data;   /* NUL-terminated (if >0) */
    size_t size;
} docker_buffer_t;

/* ----------------- Lifecycle & config ----------------- */
/*
 * docker_init:
 *   api_version: e.g. "1.41" or NULL -> uses DOCKER_SDK_API_VERSION
 *   socket_path: e.g. "/var/run/docker.sock" or NULL -> uses DOCKER_SDK_DEFAULT_SOCKET
 *
 * Returns NULL on error.
 */
DOCKER *docker_init(const char *api_version, const char *socket_path);
void    docker_destroy(DOCKER *cli);

/* Optionally change runtime settings */
void docker_set_timeouts(DOCKER *cli, long connect_timeout_s, long total_timeout_s);
void docker_set_user_agent(DOCKER *cli, const char *user_agent);
void docker_set_max_response_size(DOCKER *cli, size_t max_bytes); /* 0 = unlimited */

/* Access last raw response */
const char *docker_buffer(DOCKER *cli);
size_t      docker_buffer_size(DOCKER *cli);

/* Diagnostics */
long        docker_last_http_status(DOCKER *cli);
const char *docker_last_error(DOCKER *cli);

/* ----------------- Low-level HTTP helpers ----------------- */
/* Return libcurl status. If http_status != NULL this will be set to the HTTP status code. */
CURLcode docker_get(DOCKER *cli, const char *url, long *http_status);
CURLcode docker_post(DOCKER *cli, const char *url, const char *body, long *http_status);
CURLcode docker_delete(DOCKER *cli, const char *url, long *http_status);

/* ----------------- High-level API (helpers) ----------------- */
/* Images */
int docker_pull_image(DOCKER *cli, const char *image); /* returns 0 on success, -1 on failure */

/* Containers */
json_object *docker_build_container_config(const char *image,
                                           json_object *cmd_array,   /* array of strings or NULL */
                                           json_object *env_array,   /* array of "KEY=VAL" strings or NULL */
                                           json_object *volumes_array /* array of "host:container[:mode]" strings or NULL */
                                           );
/* create container; out_id must be at least DOCKER_ID_LEN bytes */
int docker_create_container(DOCKER *cli, const char *name, json_object *config, char out_id[DOCKER_ID_LEN]);
int docker_start_container (DOCKER *cli, const char *id);
int docker_stop_container  (DOCKER *cli, const char *id, int timeout_seconds);
int docker_remove_container(DOCKER *cli, const char *id, int remove_volumes, int force);

/* Inspect / list */
int docker_list_containers(DOCKER *cli, int all, json_object **out_json); /* out_json: caller must json_object_put() */
int docker_inspect_container(DOCKER *cli, const char *id, json_object **out_json);

/* Exec inside container */
/* docker_exec_create: returns 0 and sets out_exec_id on success */
int docker_exec_create(DOCKER *cli, const char *container_id, json_object *cmd_array,
                       json_object *env_array, const char *workdir, char out_exec_id[DOCKER_ID_LEN]);
/* docker_exec_start: starts exec and returns the raw output (multiplexed) via out_output (caller must free) */
int docker_exec_start(DOCKER *cli, const char *exec_id, char **out_output);

/* ----------------- Utility helpers ----------------- */
/* Parse response JSON {"Id":"abcd..."} style to extract Id into out (>= DOCKER_ID_LEN) */
int docker_parse_id_from_response(const char *response_body, char *out_id, size_t out_size);

#ifdef __cplusplus
}
#endif

/* ----------------- Implementation ----------------- */
#ifdef DOCKER_EXCESS

#include <errno.h>
#include <stdarg.h>

/* Private client structure */
struct docker_client {
    CURL *curl;
    char *api_version;    /* e.g. "1.41" */
    char *socket_path;
    docker_buffer_t buf;
    char *user_agent;
    long connect_timeout_s;
    long total_timeout_s;
    size_t max_response;  /* 0 = unlimited */
    long last_http_status;
    char last_error[256];
};

/* ---------- internal helpers ---------- */

static void docker_buf_init(docker_buffer_t *b) {
    if (!b) return;
    b->data = NULL;
    b->size = 0;
}
static void docker_buf_free(docker_buffer_t *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

/* Expand buffer; return 0 on success, -1 on OOM */
static int docker_buf_append(docker_buffer_t *b, const void *data, size_t len, size_t maxcap) {
    if (!b) return -1;
    if (len == 0) return 0;
    /* Enforce cap if requested */
    if (maxcap && b->size + len + 1 > maxcap) {
        /* trim to fit */
        if (maxcap <= b->size + 1) return -1;
        len = maxcap - (b->size + 1);
    }
    char *p = realloc(b->data, b->size + len + 1);
    if (!p) return -1;
    b->data = p;
    memcpy(b->data + b->size, data, len);
    b->size += len;
    b->data[b->size] = '\0';
    return 0;
}

/* curl write callback */
static size_t docker_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    DOCKER *c = (DOCKER*)userdata;
    size_t realsz = size * nmemb;
    if (!c) return 0;
    if (docker_buf_append(&c->buf, ptr, realsz, c->max_response) != 0) {
        /* returning 0 signals libcurl to abort with CURLE_WRITE_ERROR */
        return 0;
    }
    return realsz;
}

static void docker_set_error(DOCKER *c, const char *fmt, ...) {
    if (!c) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->last_error, sizeof(c->last_error), fmt, ap);
    va_end(ap);
}

/* ensure curl_global_init is called once */
static int docker_ensure_curl_global(void) {
    static int s_init = 0;
    if (s_init) return 0;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return -1;
    s_init = 1;
    return 0;
}

/* build base URL: "http://localhost/v<api_version><tail>".
   tail may start with '/' or not. Returns malloc'd string or NULL.
*/
static char *docker_build_url(const DOCKER *c, const char *tail) {
    if (!c || !tail) return NULL;
    const char *prefix = "http://localhost/v";
    size_t need = strlen(prefix) + strlen(c->api_version) + 1 + strlen(tail) + 1;
    char *out = (char*)malloc(need);
    if (!out) return NULL;
    if (tail[0] == '/')
        snprintf(out, need, "%s%s%s", prefix, c->api_version, tail);
    else
        snprintf(out, need, "%s%s/%s", prefix, c->api_version, tail);
    return out;
}

/* percent-escape using curl_easy_escape, returns malloc'd copy or NULL */
static char *docker_escape(CURL *curl, const char *s) {
    if (!curl || !s) return NULL;
    char *esc = curl_easy_escape(curl, s, 0);
    if (!esc) return NULL;
    char *dup = strdup(esc);
    curl_free(esc);
    return dup;
}

/* core HTTP performer */
static CURLcode docker_perform_http(DOCKER *c, const char *url, const char *method, const char *body, struct curl_slist *extra_headers, long *http_status_out) {
    if (!c || !c->curl || !url) return CURLE_BAD_FUNCTION_ARGUMENT;
    /* reset buffer and last status */
    docker_buf_free(&c->buf);
    docker_buf_init(&c->buf);
    c->last_http_status = 0;
    c->last_error[0] = '\0';

    curl_easy_reset(c->curl);
    curl_easy_setopt(c->curl, CURLOPT_UNIX_SOCKET_PATH, c->socket_path);
    curl_easy_setopt(c->curl, CURLOPT_URL, url);
    curl_easy_setopt(c->curl, CURLOPT_WRITEFUNCTION, docker_write_cb);
    curl_easy_setopt(c->curl, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(c->curl, CURLOPT_CONNECTTIMEOUT, c->connect_timeout_s);
    curl_easy_setopt(c->curl, CURLOPT_TIMEOUT, c->total_timeout_s);
    curl_easy_setopt(c->curl, CURLOPT_USERAGENT, c->user_agent ? c->user_agent : "docker-h-sdk/1.0");
    curl_easy_setopt(c->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(c->curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(c->curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(c->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        } else {
            curl_easy_setopt(c->curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    } else if (method && strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(c->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(c->curl, CURLOPT_HTTPGET, 1L);
    }

    /* ensure Host header for unix socket */
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Host: localhost");
    /* copy extra headers */
    struct curl_slist *hcur = extra_headers;
    while (hcur) {
        hdrs = curl_slist_append(hdrs, hcur->data);
        hcur = hcur->next;
    }
    curl_easy_setopt(c->curl, CURLOPT_HTTPHEADER, hdrs);

    CURLcode res = curl_easy_perform(c->curl);
    curl_easy_getinfo(c->curl, CURLINFO_RESPONSE_CODE, &c->last_http_status);
    if (http_status_out) *http_status_out = c->last_http_status;

    if (res != CURLE_OK) {
        docker_set_error(c, "curl: %s", curl_easy_strerror(res));
    } else if (c->max_response && c->buf.size >= c->max_response) {
        docker_set_error(c, "response truncated (max cap reached)");
    }

    curl_slist_free_all(hdrs);
    return res;
}

/* ----------------- Public implementation ----------------- */

DOCKER *docker_init(const char *api_version, const char *socket_path) {
    if (docker_ensure_curl_global() != 0) return NULL;
    DOCKER *c = (DOCKER*)calloc(1, sizeof(DOCKER));
    if (!c) return NULL;

    c->curl = curl_easy_init();
    if (!c->curl) { free(c); return NULL; }

    /* api version */
    const char *ver = api_version ? api_version : DOCKER_SDK_API_VERSION;
    c->api_version = strdup(ver);
    if (!c->api_version) { curl_easy_cleanup(c->curl); free(c); return NULL; }

    c->socket_path = strdup(socket_path ? socket_path : DOCKER_SDK_DEFAULT_SOCKET);
    if (!c->socket_path) { free(c->api_version); curl_easy_cleanup(c->curl); free(c); return NULL; }

    docker_buf_init(&c->buf);
    c->user_agent = strdup("docker-h-sdk/1.0");
    c->connect_timeout_s = DOCKER_SDK_DEFAULT_CONNECT_TIMEOUT_S;
    c->total_timeout_s = DOCKER_SDK_DEFAULT_TIMEOUT_S;
    c->max_response = 16 * 1024 * 1024; /* 16 MiB default */
    c->last_http_status = 0;
    c->last_error[0] = '\0';
    return c;
}

void docker_destroy(DOCKER *cli) {
    if (!cli) return;
    if (cli->curl) curl_easy_cleanup(cli->curl);
    free(cli->api_version);
    free(cli->socket_path);
    free(cli->user_agent);
    docker_buf_free(&cli->buf);
    free(cli);
}

void docker_set_timeouts(DOCKER *cli, long connect_timeout_s, long total_timeout_s) {
    if (!cli) return;
    if (connect_timeout_s > 0) cli->connect_timeout_s = connect_timeout_s;
    if (total_timeout_s > 0) cli->total_timeout_s = total_timeout_s;
}
void docker_set_user_agent(DOCKER *cli, const char *user_agent) {
    if (!cli || !user_agent) return;
    char *dup = strdup(user_agent);
    if (!dup) return;
    free(cli->user_agent);
    cli->user_agent = dup;
}
void docker_set_max_response_size(DOCKER *cli, size_t max_bytes) {
    if (!cli) return;
    cli->max_response = max_bytes;
}

const char *docker_buffer(DOCKER *cli) {
    return cli ? cli->buf.data : NULL;
}
size_t docker_buffer_size(DOCKER *cli) {
    return cli ? cli->buf.size : 0;
}

long docker_last_http_status(DOCKER *cli) {
    return cli ? cli->last_http_status : 0;
}
const char *docker_last_error(DOCKER *cli) {
    return (cli && cli->last_error[0]) ? cli->last_error : NULL;
}

/* Low-level wrappers */
CURLcode docker_get(DOCKER *cli, const char *url, long *http_status) {
    return docker_perform_http(cli, url, "GET", NULL, NULL, http_status);
}
CURLcode docker_post(DOCKER *cli, const char *url, const char *body, long *http_status) {
    struct curl_slist *hdrs = NULL;
    if (body) hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    return docker_perform_http(cli, url, "POST", body, hdrs, http_status);
}
CURLcode docker_delete(DOCKER *cli, const char *url, long *http_status) {
    return docker_perform_http(cli, url, "DELETE", NULL, NULL, http_status);
}

/* ---------- High-level helpers ---------- */

int docker_pull_image(DOCKER *cli, const char *image) {
    if (!cli || !image) return -1;
    char *imgenc = docker_escape(cli->curl, image);
    if (!imgenc) return -1;
    size_t plen = strlen("/images/create?fromImage=") + strlen(imgenc) + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(imgenc); return -1; }
    snprintf(path, plen, "/images/create?fromImage=%s", imgenc);
    free(imgenc);

    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) return -1;

    long status = 0;
    CURLcode r = docker_post(cli, url, NULL, &status);
    free(url);
    if (r != CURLE_OK) return -1;
    if (status == 200 || status == 201) return 0;
    return -1;
}

/* Build a container JSON configuration.
   - image is required
   - cmd_array and env_array are json-c arrays (or NULL)
   - volumes_array is json-c array of strings like "/host/path:/container/path:rw" (or NULL)
   Caller must json_object_put() the returned object.
*/
json_object *docker_build_container_config(const char *image,
                                           json_object *cmd_array,
                                           json_object *env_array,
                                           json_object *volumes_array) {
    if (!image) return NULL;
    json_object *cfg = json_object_new_object();
    if (!cfg) return NULL;
    json_object_object_add(cfg, "Image", json_object_new_string(image));
    if (cmd_array) json_object_object_add(cfg, "Cmd", json_object_get(cmd_array));
    if (env_array) json_object_object_add(cfg, "Env", json_object_get(env_array));

    /* Build HostConfig with Binds if volumes provided */
    json_object *hostcfg = json_object_new_object();
    if (!hostcfg) { json_object_put(cfg); return NULL; }

    if (volumes_array && json_object_get_type(volumes_array) == json_type_array) {
        /* Binds: host:container:mode array */
        json_object_object_add(hostcfg, "Binds", json_object_get(volumes_array));
    }
    /* Add a minimal RestartPolicy: empty object by default */
    json_object_object_add(hostcfg, "RestartPolicy", json_object_new_object());
    json_object_object_add(cfg, "HostConfig", hostcfg);

    return cfg;
}

/* Create container: posts config; optionally name provided (NULL for none).
   out_id must point to buffer of length DOCKER_ID_LEN.
*/
int docker_create_container(DOCKER *cli, const char *name, json_object *config, char out_id[DOCKER_ID_LEN]) {
    if (!cli || !config || !out_id) return -1;
    const char *body = json_object_to_json_string(config);
    if (!body) return -1;

    char *url = NULL;
    if (name && name[0]) {
        char *nenc = docker_escape(cli->curl, name);
        if (!nenc) return -1;
        size_t plen = strlen("/containers/create?name=") + strlen(nenc) + 1;
        char *path = (char*)malloc(plen);
        if (!path) { free(nenc); return -1; }
        snprintf(path, plen, "/containers/create?name=%s", nenc);
        free(nenc);
        url = docker_build_url(cli, path);
        free(path);
    } else {
        url = docker_build_url(cli, "/containers/create");
    }
    if (!url) return -1;

    long status = 0;
    CURLcode r = docker_post(cli, url, body, &status);
    free(url);
    if (r != CURLE_OK) return -1;
    if (!(status == 201 || status == 200)) return -1;

    if (docker_parse_id_from_response(cli->buf.data, out_id, DOCKER_ID_LEN) != 0) return -1;
    return 0;
}

int docker_start_container(DOCKER *cli, const char *id) {
    if (!cli || !id) return -1;
    char *idenc = docker_escape(cli->curl, id);
    if (!idenc) return -1;
    size_t plen = strlen("/containers/") + strlen(idenc) + strlen("/start") + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); return -1; }
    snprintf(path, plen, "/containers/%s/start", idenc);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) return -1;
    long status = 0;
    CURLcode r = docker_post(cli, url, "", &status);
    free(url);
    if (r != CURLE_OK) return -1;
    /* Docker typically returns 204 on success */
    if (status == 204 || status == 200) return 0;
    return -1;
}

int docker_stop_container(DOCKER *cli, const char *id, int timeout_seconds) {
    if (!cli || !id) return -1;
    char *idenc = docker_escape(cli->curl, id);
    if (!idenc) return -1;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%d", timeout_seconds);
    size_t plen = strlen("/containers/") + strlen(idenc) + strlen("/stop?t=") + strlen(tbuf) + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); return -1; }
    snprintf(path, plen, "/containers/%s/stop?t=%s", idenc, tbuf);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) return -1;
    long status = 0;
    CURLcode r = docker_post(cli, url, "", &status);
    free(url);
    if (r != CURLE_OK) return -1;
    if (status == 204 || status == 200) return 0;
    return -1;
}

int docker_remove_container(DOCKER *cli, const char *id, int remove_volumes, int force) {
    if (!cli || !id) return -1;
    char *idenc = docker_escape(cli->curl, id);
    if (!idenc) return -1;
    char vb[8], fb[8];
    snprintf(vb, sizeof(vb), "%d", remove_volumes ? 1 : 0);
    snprintf(fb, sizeof(fb), "%d", force ? 1 : 0);
    size_t plen = strlen("/containers/") + strlen(idenc) + strlen("?v=&force=") + strlen(vb) + strlen(fb) + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); return -1; }
    snprintf(path, plen, "/containers/%s?v=%s&force=%s", idenc, vb, fb);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) return -1;
    long status = 0;
    CURLcode r = docker_delete(cli, url, &status);
    free(url);
    if (r != CURLE_OK) return -1;
    if (status == 204 || status == 200) return 0;
    return -1;
}

int docker_list_containers(DOCKER *cli, int all, json_object **out_json) {
    if (!cli || !out_json) return -1;
    char path[128];
    snprintf(path, sizeof(path), "/containers/json?all=%d", all ? 1 : 0);
    char *url = docker_build_url(cli, path);
    if (!url) return -1;
    long status = 0;
    CURLcode r = docker_get(cli, url, &status);
    free(url);
    if (r != CURLE_OK || status != 200) return -1;
    json_object *j = json_tokener_parse(cli->buf.data ? cli->buf.data : "{}");
    if (!j) return -1;
    *out_json = j;
    return 0;
}

int docker_inspect_container(DOCKER *cli, const char *id, json_object **out_json) {
    if (!cli || !id || !out_json) return -1;
    char *idenc = docker_escape(cli->curl, id);
    if (!idenc) return -1;
    size_t plen = strlen("/containers/") + strlen(idenc) + strlen("/json") + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); return -1; }
    snprintf(path, plen, "/containers/%s/json", idenc);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) return -1;
    long status = 0;
    CURLcode r = docker_get(cli, url, &status);
    free(url);
    if (r != CURLE_OK || status != 200) return -1;
    json_object *j = json_tokener_parse(cli->buf.data ? cli->buf.data : "{}");
    if (!j) return -1;
    *out_json = j;
    return 0;
}

/* Exec create */
int docker_exec_create(DOCKER *cli, const char *container_id, json_object *cmd_array,
                       json_object *env_array, const char *workdir, char out_exec_id[DOCKER_ID_LEN]) {
    if (!cli || !container_id || !out_exec_id) return -1;
    json_object *body = json_object_new_object();
    if (!body) return -1;
    json_object_object_add(body, "AttachStdin", json_object_new_boolean(0));
    json_object_object_add(body, "AttachStdout", json_object_new_boolean(1));
    json_object_object_add(body, "AttachStderr", json_object_new_boolean(1));
    json_object_object_add(body, "Tty", json_object_new_boolean(0));
    if (cmd_array) json_object_object_add(body, "Cmd", json_object_get(cmd_array));
    if (env_array) json_object_object_add(body, "Env", json_object_get(env_array));
    if (workdir) json_object_object_add(body, "WorkingDir", json_object_new_string(workdir));

    const char *body_str = json_object_to_json_string(body);
    char *idenc = docker_escape(cli->curl, container_id);
    if (!idenc) { json_object_put(body); return -1; }
    size_t plen = strlen("/containers/") + strlen(idenc) + strlen("/exec") + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); json_object_put(body); return -1; }
    snprintf(path, plen, "/containers/%s/exec", idenc);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) { json_object_put(body); return -1; }

    long status = 0;
    CURLcode r = docker_post(cli, url, body_str, &status);
    json_object_put(body);
    free(url);
    if (r != CURLE_OK) return -1;
    if (!(status == 201 || status == 200)) return -1;

    if (docker_parse_id_from_response(cli->buf.data, out_exec_id, DOCKER_ID_LEN) != 0) return -1;
    return 0;
}

/* Exec start: returns raw multiplexed output in *out_output (malloc'd string) */
int docker_exec_start(DOCKER *cli, const char *exec_id, char **out_output) {
    if (!cli || !exec_id) return -1;
    json_object *body = json_object_new_object();
    if (!body) return -1;
    json_object_object_add(body, "Detach", json_object_new_boolean(0));
    json_object_object_add(body, "Tty", json_object_new_boolean(0));
    const char *body_str = json_object_to_json_string(body);

    char *idenc = docker_escape(cli->curl, exec_id);
    if (!idenc) { json_object_put(body); return -1; }
    size_t plen = strlen("/exec/") + strlen(idenc) + strlen("/start") + 1;
    char *path = (char*)malloc(plen);
    if (!path) { free(idenc); json_object_put(body); return -1; }
    snprintf(path, plen, "/exec/%s/start", idenc);
    free(idenc);
    char *url = docker_build_url(cli, path);
    free(path);
    if (!url) { json_object_put(body); return -1; }

    long status = 0;
    CURLcode r = docker_post(cli, url, body_str, &status);
    json_object_put(body);
    free(url);
    if (r != CURLE_OK) return -1;
    if (!(status == 200 || status == 204)) return -1;

    /* Return a copy of the buffer (caller must free) */
    if (out_output) {
        if (cli->buf.data) {
            *out_output = strdup(cli->buf.data);
            if (!*out_output) return -1;
        } else {
            *out_output = strdup("");
            if (!*out_output) return -1;
        }
    }
    return 0;
}

/* parse {"Id":"..."} style response */
int docker_parse_id_from_response(const char *response_body, char *out_id, size_t out_size) {
    if (!response_body || !out_id || out_size == 0) return -1;
    json_object *j = json_tokener_parse(response_body);
    if (!j) return -1;
    json_object *idobj = NULL;
    if (!json_object_object_get_ex(j, "Id", &idobj)) {
        json_object_put(j);
        return -1;
    }
    const char *id = json_object_get_string(idobj);
    if (!id) { json_object_put(j); return -1; }
    strncpy(out_id, id, out_size - 1);
    out_id[out_size - 1] = '\0';
    json_object_put(j);
    return 0;
}

#endif /* DOCKER_EXCESS */

#endif /* DOCKER_EXCESS_H */
