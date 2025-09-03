# Lib Docker Excess

A **single-header C SDK** for interacting with the Docker Engine API over a Unix socket.
Provides both **low-level HTTP primitives** and **high-level helpers** for working with images, containers, and execs.

* Single-header (`docker-excess.h`)
* Dependencies: [libcurl](https://curl.se/libcurl/) (with Unix socket support) and [json-c](https://github.com/json-c/json-c)
* Style: simple API, safe(ish) memory handling
* Target: Linux/Unix with `/var/run/docker.sock`

---

## Quick Start

### Add the header

```c
#define DOCKER_EXCESS
#include "docker-excess.h"
```

### Build

```sh
gcc myprog.c -o myprog -lcurl -ljson-c
```

---

## API Overview

### Lifecycle

```c
DOCKER *docker_init(const char *api_version, const char *socket_path);
void    docker_destroy(DOCKER *cli);
```

* `api_version`: `"1.41"` or `NULL` for default
* `socket_path`: usually `"/var/run/docker.sock"` or `NULL` for default

---

### Config

```c
void docker_set_timeouts(DOCKER *cli, long connect_timeout_s, long total_timeout_s);
void docker_set_user_agent(DOCKER *cli, const char *user_agent);
void docker_set_max_response_size(DOCKER *cli, size_t max_bytes);
```

---

### Diagnostics

```c
const char *docker_buffer(DOCKER *cli);
size_t      docker_buffer_size(DOCKER *cli);

long        docker_last_http_status(DOCKER *cli);
const char *docker_last_error(DOCKER *cli);
```

---

### Low-Level HTTP

```c
CURLcode docker_get   (DOCKER *cli, const char *url, long *http_status);
CURLcode docker_post  (DOCKER *cli, const char *url, const char *body, long *http_status);
CURLcode docker_delete(DOCKER *cli, const char *url, long *http_status);
```

---

### High-Level Helpers

**Images**

```c
int docker_pull_image(DOCKER *cli, const char *image);
```

**Containers**

```c
json_object *docker_build_container_config(
    const char *image,
    json_object *cmd_array,
    json_object *env_array,
    json_object *volumes_array
);

int docker_create_container(DOCKER *cli, const char *name,
                            json_object *config, char out_id[DOCKER_ID_LEN]);
int docker_start_container (DOCKER *cli, const char *id);
int docker_stop_container  (DOCKER *cli, const char *id, int timeout_seconds);
int docker_remove_container(DOCKER *cli, const char *id,
                            int remove_volumes, int force);

int docker_list_containers   (DOCKER *cli, int all, json_object **out_json);
int docker_inspect_container (DOCKER *cli, const char *id, json_object **out_json);
```

**Exec**

```c
int docker_exec_create(DOCKER *cli, const char *container_id,
                       json_object *cmd_array,
                       json_object *env_array,
                       const char *workdir,
                       char out_exec_id[DOCKER_ID_LEN]);

int docker_exec_start(DOCKER *cli, const char *exec_id, char **out_output);
```

**Utilities**

```c
int docker_parse_id_from_response(const char *response_body,
                                  char *out_id, size_t out_size);
```

---

## Example

Run `alpine` and print output:

```c
#define DOCKER_EXCESS
#include "docker-excess.h"

int main(void) {
    DOCKER *cli = docker_init(NULL, NULL);
    if (!cli) return 1;

    docker_pull_image(cli, "alpine:latest");

    json_object *cmd = json_object_new_array();
    json_object_array_add(cmd, json_object_new_string("echo"));
    json_object_array_add(cmd, json_object_new_string("hello world"));

    json_object *cfg = docker_build_container_config("alpine:latest", cmd, NULL, NULL);

    char cid[DOCKER_ID_LEN];
    if (docker_create_container(cli, NULL, cfg, cid) == 0) {
        docker_start_container(cli, cid);

        char *out = NULL;
        char eid[DOCKER_ID_LEN];
        json_object *exec_cmd = json_object_new_array();
        json_object_array_add(exec_cmd, json_object_new_string("echo"));
        json_object_array_add(exec_cmd, json_object_new_string("inside container"));

        docker_exec_create(cli, cid, exec_cmd, NULL, NULL, eid);
        docker_exec_start(cli, eid, &out);
        printf("Exec output: %s\n", out);
        free(out);

        docker_stop_container(cli, cid, 5);
        docker_remove_container(cli, cid, 1, 1);
    }

    json_object_put(cfg);
    json_object_put(cmd);
    docker_destroy(cli);
}
```

---

## Notes

* Tested against Docker Engine API **v1.41**
* Requires `libcurl` with Unix socket support
* Default max response size: **16 MB**
* Caller must manage `json_object` lifetimes (`json_object_put`)
* `docker_exec_start` returns raw multiplexed output (stdout/stderr combined)
