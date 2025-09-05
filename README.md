# Docker-excess  (C Docker SDK)

**Version:** `1.23.0`
A lightweight C client for the Docker Engine API.
Provides a straightforward C interface over **Unix sockets** or **TCP**, using [`libcurl`](https://curl.se/libcurl/) and [`json-c`](https://github.com/json-c/json-c).

---

##  Index

1. [License](#license)
2. [Build](#build)
3. [Quickstart](#quickstart)
4. [Containers](#containers)
5. [Files](#files)
6. [Images](#images)
7. [Networks](#networks)
8. [Volumes](#volumes)
9. [Raw API](#raw-api)
10. [Utility](#utility)
11. [API Coverage](#api-coverage)
12. [Error Handling](#error-handling)
13. [Notes](#notes)

---

##  Build

**Dependencies:**

* `libcurl`
* `json-c`
* `pthreads`

Compile an example:

```sh
gcc -o example example.c docker-excess.c -lcurl -ljson-c -lpthread
```

---

##  Quickstart

```c
#include "docker-excess.h"
#include <stdio.h>

int main() {
    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) return 1;

    if (docker_excess_ping(client) != DOCKER_EXCESS_OK) {
        fprintf(stderr, "docker not reachable: %s\n", docker_excess_get_error(client));
        docker_excess_free(client);
        return 1;
    }

    char *version;
    if (docker_excess_version(client, &version) == DOCKER_EXCESS_OK) {
        printf("Docker version: %s\n", version);
        free(version);
    }

    docker_excess_free(client);
    return 0;
}
```

---

##  Containers

### List

```c
docker_excess_container_t **containers;
size_t count;
if (docker_excess_list_containers(client, true, &containers, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("%s %s (%s)\n", containers[i]->id, containers[i]->name, containers[i]->status);
    }
    docker_excess_free_containers(containers, count);
}
```

### Create + Start

```c
docker_excess_container_create_t params = {0};
params.image = "alpine";
params.cmd = (char*[]){"sleep", "10"};
params.cmd_count = 2;
params.auto_remove = true;

char *cid = NULL;
if (docker_excess_create_container(client, &params, &cid) == DOCKER_EXCESS_OK) {
    docker_excess_start_container(client, cid);
    free(cid);
}
```

### Control

```c
docker_excess_stop_container(client, cid, 5);
docker_excess_restart_container(client, cid);
docker_excess_remove_container(client, cid, true);
```

### Exec

```c
char *out = NULL;
int exit_code = 0;
if (docker_excess_exec_simple(client, cid, "echo hello", &out, NULL, &exit_code) == DOCKER_EXCESS_OK) {
    printf("stdout: %s\nexit=%d\n", out, exit_code);
    free(out);
}
```

### Logs

```c
void log_callback(const char *line, void *u) {
    printf("log: %s\n", line);
}
docker_excess_get_logs(client, cid, false, false, 100, log_callback, NULL);
```

### Wait

```c
int code;
docker_excess_wait_container(client, cid, &code);
printf("exit code=%d\n", code);
```

---

##  Files

```c
// read
char *content;
size_t size;
if (docker_excess_read_file(client, cid, "/etc/hostname", &content, &size) == DOCKER_EXCESS_OK) {
    printf("%.*s\n", (int)size, content);
    free(content);
}

// write
docker_excess_write_file(client, cid, "/tmp/test.txt", "hello world", 11);
```

**In progress:** list, copy, mkdir, remove.

---

##  Images

```c
// pull
docker_excess_pull_image(client, "alpine", "latest");

// list
docker_excess_image_t **images;
size_t count;
if (docker_excess_list_images(client, false, &images, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("%s\n", images[i]->id);
    }
    docker_excess_free_images(images, count);
}

// remove
docker_excess_remove_image(client, "alpine:latest", true);
```

**In progress:** build.

---

##  Networks

Declared but not yet complete (return `DOCKER_EXCESS_ERR_INTERNAL`):

* `docker_excess_list_networks`
* `docker_excess_create_network`
* `docker_excess_remove_network`
* `docker_excess_connect_network`
* `docker_excess_disconnect_network`

---

##  Volumes

Declared but not yet complete (return `DOCKER_EXCESS_ERR_INTERNAL`):

* `docker_excess_list_volumes`
* `docker_excess_create_volume`
* `docker_excess_remove_volume`

---

##  Raw API

```c
char *resp;
int code;
if (docker_excess_raw_request(client, "GET", "/info", NULL, &resp, &code) == DOCKER_EXCESS_OK) {
    printf("HTTP %d: %s\n", code, resp);
    free(resp);
}
```

---

##  Utility

```c
char buf[64];
docker_excess_format_bytes(1024*1024, buf, sizeof buf);
printf("%s\n", buf); // "1.0 MB"
```

---

##  API Coverage

| Area       | Function                              | Status        |
| ---------- | ------------------------------------- | ------------- |
| Core       | new/free/ping/version                 | ✔️            |
| Containers | list/create/start/stop/restart/remove | ✔️            |
|            | exec/logs/wait/pause/unpause          | ✔️            |
| Files      | read/write                            | ✔️            |
|            | list/copy/mkdir/remove                | ⏳ in progress |
| Images     | list/pull/remove                      | ✔️            |
|            | build                                 | ⏳ in progress |
| Networks   | list/create/remove/connect/disconnect | ⏳ in progress |
| Volumes    | list/create/remove                    | ⏳ in progress |
| Raw API    | request                               | ✔️            |
| Utils      | free helpers, error strings, format   | ✔️            |

✔️ = implemented
⏳ = declared, not yet complete

---

## Error Handling

```c
docker_excess_error_t err = docker_excess_ping(client);
if (err != DOCKER_EXCESS_OK) {
    fprintf(stderr, "error: %s\n", docker_excess_error_string(err));
}
```

---

##  Notes

* Default socket: `/var/run/docker.sock`
* Always free arrays and strings with the provided `free` functions
* Stubbed features will be completed in upcoming releases

---

##  License

[MIT](./LICENSE) © [g-flame-oss](https://github.com/g-flame-oss)

