# Docker-excess (C Docker SDK)

**Version:** `1.1.59 BETA`  
A robust, low-level C client for the Docker Engine API.  
Provides direct control over Docker via **Unix sockets** or **TCP**, using [`libcurl`](https://curl.se/libcurl/) and [`json-c`](https://github.com/json-c/json-c).

---

## 📋 Index

1. [License](#license)
2. [Build](#build)
3. [Quickstart](#quickstart)
4. [Configuration](#configuration)
5. [Containers](#containers)
6. [File Operations](#file-operations)
7. [Execution & Logs](#execution--logs)
8. [Images](#images)
9. [Networks](#networks)
10. [Volumes](#volumes)
11. [Raw API](#raw-api)
12. [Utility Functions](#utility-functions)
13. [API Coverage](#api-coverage)
14. [Error Handling](#error-handling)
15. [Thread Safety](#thread-safety)
16. [Memory Management](#memory-management)
17. [Notes](#notes)

---

## 🛠 Build

**Dependencies:**
- `libcurl` (>= 7.40.0)
- `json-c` (>= 0.12)
- `pthreads`

**Compile:**
```sh
gcc -o example example.c docker-excess.c -lcurl -ljson-c -lpthread
```

**CMake example:**
```cmake
target_link_libraries(your_target docker-excess curl json-c pthread)
```

---

## 🚀 Quickstart

```c
#include "docker-excess.h"
#include <stdio.h>

int main() {
    docker_excess_t *client;
    
    // Create client with default config (Unix socket)
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    // Test connection
    if (docker_excess_ping(client) != DOCKER_EXCESS_OK) {
        fprintf(stderr, "Docker not reachable: %s\n", docker_excess_get_error(client));
        docker_excess_free(client);
        return 1;
    }

    // Get Docker version
    char *version_json;
    if (docker_excess_version(client, &version_json) == DOCKER_EXCESS_OK) {
        printf("Docker version info: %s\n", version_json);
        free(version_json);
    }

    printf("✓ Connected to Docker daemon\n");
    
    docker_excess_free(client);
    return 0;
}
```

---

## ⚙️ Configuration

### Default Configuration (Unix Socket)
```c
docker_excess_t *client;
docker_excess_new(&client);  // Uses /var/run/docker.sock
```

### Custom Configuration
```c
docker_excess_config_t config = docker_excess_default_config();
config.host = strdup("192.168.1.100");
config.port = 2376;
config.use_tls = true;
config.timeout_s = 60;
config.debug = true;

docker_excess_t *client;
docker_excess_new_with_config(&config, &client);
```

### Environment-based Configuration
```c
// Automatically reads DOCKER_HOST, DOCKER_TLS_VERIFY, DOCKER_CERT_PATH
docker_excess_config_t config = docker_excess_config_from_env();
docker_excess_t *client;
docker_excess_new_with_config(&config, &client);
```

---

## 📦 Containers

### List Containers
```c
docker_excess_container_t **containers;
size_t count;

if (docker_excess_list_containers(client, true, &containers, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("ID: %.12s | Name: %s | Status: %s | Image: %s\n",
               containers[i]->id,
               containers[i]->name ? containers[i]->name : "<unnamed>",
               containers[i]->status,
               containers[i]->image);
    }
    docker_excess_free_containers(containers, count);
}
```

### Create Container
```c
docker_excess_container_create_t params = {0};
params.name = "my-test-container";
params.image = "alpine:latest";
params.cmd = (char*[]){"sleep", "30"};
params.cmd_count = 2;
params.env = (char*[]){"ENV_VAR=value", "DEBUG=1"};
params.env_count = 2;
params.ports = (char*[]){"8080:80", "9000:9000"};
params.ports_count = 2;
params.volumes = (char*[]){"/host/path:/container/path:ro"};
params.volumes_count = 1;
params.auto_remove = true;
params.interactive = false;
params.tty = false;

char *container_id;
if (docker_excess_create_container(client, &params, &container_id) == DOCKER_EXCESS_OK) {
    printf("Created container: %s\n", container_id);
    free(container_id);
}
```

### Container Lifecycle
```c
// Start
docker_excess_start_container(client, container_id);

// Stop with timeout
docker_excess_stop_container(client, container_id, 10);

// Restart
docker_excess_restart_container(client, container_id);

// Pause/Unpause
docker_excess_pause_container(client, container_id);
docker_excess_unpause_container(client, container_id);

// Remove (force if running)
docker_excess_remove_container(client, container_id, true);
```

### Wait for Container
```c
int exit_code;
if (docker_excess_wait_container(client, container_id, &exit_code) == DOCKER_EXCESS_OK) {
    printf("Container exited with code: %d\n", exit_code);
}
```

---

## 📁 File Operations

### Read File
```c
char *content;
size_t size;
if (docker_excess_read_file(client, container_id, "/etc/hostname", &content, &size) == DOCKER_EXCESS_OK) {
    printf("File content (%zu bytes): %.*s\n", size, (int)size, content);
    free(content);
}
```

### Write File (Binary-Safe)
```c
const char data[] = "Hello, Docker!\nThis is a test file.";
if (docker_excess_write_file(client, container_id, "/tmp/test.txt", data, sizeof(data)-1) == DOCKER_EXCESS_OK) {
    printf("File written successfully\n");
}
```

### List Directory
```c
docker_excess_file_t **files;
size_t count;
if (docker_excess_list_files(client, container_id, "/etc", &files, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("%c %8ld %s\n", 
               files[i]->is_dir ? 'd' : '-',
               files[i]->size,
               files[i]->name);
    }
    docker_excess_free_files(files, count);
}
```

### File Operations
```c
// Create directory
docker_excess_mkdir(client, container_id, "/tmp/new_dir", 0755);

// Remove file/directory
docker_excess_remove_file(client, container_id, "/tmp/test.txt", false);

// Remove directory recursively  
docker_excess_remove_file(client, container_id, "/tmp/old_dir", true);

// Copy from container to host
docker_excess_copy_from_container(client, container_id, "/etc/hosts", "./hosts");

// Copy from host to container
docker_excess_copy_to_container(client, container_id, "./config.txt", "/app/config.txt");
```

---

## ⚡ Execution & Logs

### Simple Command Execution
```c
char *stdout_data = NULL;
char *stderr_data = NULL;
int exit_code;

if (docker_excess_exec_simple(client, container_id, "ls -la /", &stdout_data, &stderr_data, &exit_code) == DOCKER_EXCESS_OK) {
    printf("Command output:\n%s\n", stdout_data);
    printf("Exit code: %d\n", exit_code);
    free(stdout_data);
    free(stderr_data);
}
```

### Callback-based Execution
```c
void exec_callback(const char *stdout_data, const char *stderr_data, void *userdata) {
    if (stdout_data) printf("STDOUT: %s\n", stdout_data);
    if (stderr_data) printf("STDERR: %s\n", stderr_data);
}

const char *cmd[] = {"ps", "aux"};
docker_excess_exec(client, container_id, cmd, 2, exec_callback, NULL);
```

### Container Logs
```c
void log_callback(const char *line, void *userdata) {
    printf("LOG: %s\n", line);
}

// Get last 100 lines, with timestamps, don't follow
docker_excess_get_logs(client, container_id, false, true, 100, log_callback, NULL);

// Follow logs in real-time
docker_excess_get_logs(client, container_id, true, false, 0, log_callback, NULL);
```

---

## 🖼 Images

### List Images
```c
docker_excess_image_t **images;
size_t count;

if (docker_excess_list_images(client, false, &images, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("Image ID: %.12s\n", images[i]->id);
        printf("  Created: %ld\n", images[i]->created);
        
        char size_str[64];
        docker_excess_format_bytes(images[i]->size, size_str, sizeof(size_str));
        printf("  Size: %s\n", size_str);
        
        if (images[i]->repo_tags) {
            printf("  Tags: ");
            for (size_t j = 0; j < images[i]->repo_tags_count; j++) {
                printf("%s ", images[i]->repo_tags[j]);
            }
            printf("\n");
        }
    }
    docker_excess_free_images(images, count);
}
```

### Pull Image
```c
// Pull latest
docker_excess_pull_image(client, "nginx", NULL);

// Pull specific tag
docker_excess_pull_image(client, "alpine", "3.18");
```

### Remove Image
```c
// Remove by name:tag
docker_excess_remove_image(client, "nginx:latest", false);

// Force remove (even if containers using it)
docker_excess_remove_image(client, "alpine:3.18", true);
```

---

## 🌐 Networks

### List Networks
```c
docker_excess_network_t **networks;
size_t count;

if (docker_excess_list_networks(client, &networks, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("Network: %s (ID: %.12s, Driver: %s, Scope: %s)\n",
               networks[i]->name,
               networks[i]->id,
               networks[i]->driver,
               networks[i]->scope);
    }
    docker_excess_free_networks(networks, count);
}
```

### Network Management
```c
// Create network
char *network_id;
docker_excess_create_network(client, "my-network", "bridge", &network_id);

// Connect container to network
docker_excess_connect_network(client, network_id, container_id);

// Disconnect container from network  
docker_excess_disconnect_network(client, network_id, container_id);

// Remove network
docker_excess_remove_network(client, network_id);

free(network_id);
```

---

## 💾 Volumes

### List Volumes
```c
docker_excess_volume_t **volumes;
size_t count;

if (docker_excess_list_volumes(client, &volumes, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("Volume: %s\n", volumes[i]->name);
        printf("  Driver: %s\n", volumes[i]->driver);
        printf("  Mountpoint: %s\n", volumes[i]->mountpoint);
    }
    docker_excess_free_volumes(volumes, count);
}
```

### Volume Management
```c
// Create volume
char *volume_name;
docker_excess_create_volume(client, "my-volume", "local", &volume_name);

// Remove volume
docker_excess_remove_volume(client, volume_name, false);

// Force remove (even if in use)
docker_excess_remove_volume(client, "old-volume", true);

free(volume_name);
```

---

## 🔧 Raw API

### Make Custom Requests
```c
char *response;
int http_code;

// GET request
if (docker_excess_raw_request(client, "GET", "/info", NULL, &response, &http_code) == DOCKER_EXCESS_OK) {
    printf("HTTP %d: %s\n", http_code, response);
    free(response);
}

// POST with body
const char *body = "{\"Image\":\"alpine\",\"Cmd\":[\"echo\",\"hello\"]}";
docker_excess_raw_request(client, "POST", "/containers/create", body, &response, &http_code);
```

---

## 🛠 Utility Functions

### Container ID Resolution
```c
// Resolve container name or partial ID to full ID
char *full_id;
if (docker_excess_resolve_container_id(client, "my-container", &full_id) == DOCKER_EXCESS_OK) {
    printf("Full container ID: %s\n", full_id);
    free(full_id);
}
```

### Byte Formatting
```c
char buffer[64];
docker_excess_format_bytes(1073741824, buffer, sizeof(buffer));
printf("Size: %s\n", buffer); // "1.0 GB"
```

### Error Information
```c
docker_excess_error_t err = docker_excess_ping(client);
if (err != DOCKER_EXCESS_OK) {
    printf("Error code: %d\n", err);
    printf("Error string: %s\n", docker_excess_error_string(err));
    printf("Detailed error: %s\n", docker_excess_get_error(client));
}
```

---

## 📊 API Coverage

| Category     | Function                                      | Status |
|------------- |-----------------------------------------------|--------|
| **Core**     | `docker_excess_new` / `docker_excess_free`  | ✅     |
|              | `docker_excess_ping` / `docker_excess_version` | ✅     |
| **Container** | `list` / `create` / `start` / `stop` / `restart` | ✅     |
|              | `remove` / `pause` / `unpause` / `wait`      | ✅     |
| **Execution** | `exec_simple` / `exec` / `get_logs`         | ✅     |
| **Files**    | `read_file` / `write_file` / `list_files`   | ✅     |
|              | `copy_from/to_container` / `mkdir` / `remove_file` | ✅     |
| **Images**   | `list_images` / `pull_image` / `remove_image` | ✅     |
|              | `build_image`                                | ⚠️*    |
| **Networks** | `list` / `create` / `remove` / `connect` / `disconnect` | ✅     |
| **Volumes**  | `list_volumes` / `create_volume` / `remove_volume` | ✅     |
| **Raw API**  | `raw_request`                                | ✅     |
| **Utility**  | `resolve_container_id` / `format_bytes` / `error_string` | ✅     |

**Legend:**  
✅ = Fully implemented  
⚠️* = Partial implementation (build_image needs tar archive creation)

---

## ❌ Error Handling

### Error Types
```c
typedef enum {
    DOCKER_EXCESS_OK = 0,
    DOCKER_EXCESS_ERR_INVALID_PARAM = -1,
    DOCKER_EXCESS_ERR_MEMORY = -2,
    DOCKER_EXCESS_ERR_NETWORK = -3,
    DOCKER_EXCESS_ERR_HTTP = -4,
    DOCKER_EXCESS_ERR_JSON = -5,
    DOCKER_EXCESS_ERR_NOT_FOUND = -6,
    DOCKER_EXCESS_ERR_TIMEOUT = -7,
    DOCKER_EXCESS_ERR_INTERNAL = -8
} docker_excess_error_t;
```

### Comprehensive Error Handling
```c
docker_excess_error_t err = docker_excess_start_container(client, "nonexistent");
switch (err) {
    case DOCKER_EXCESS_OK:
        printf("Success!\n");
        break;
    case DOCKER_EXCESS_ERR_NOT_FOUND:
        printf("Container not found\n");
        break;
    case DOCKER_EXCESS_ERR_NETWORK:
        printf("Network error: %s\n", docker_excess_get_error(client));
        break;
    case DOCKER_EXCESS_ERR_TIMEOUT:
        printf("Operation timed out\n");
        break;
    default:
        printf("Error: %s\n", docker_excess_error_string(err));
        printf("Details: %s\n", docker_excess_get_error(client));
}
```

---

## 🔒 Thread Safety

All public functions are **thread-safe**. Internal operations use mutex locking:

```c
// Safe to call from multiple threads
#pragma omp parallel for
for (int i = 0; i < 10; i++) {
    docker_excess_ping(client);  // Thread-safe
}
```

**Note:** Each thread should ideally have its own client instance for optimal performance.

---

## 🧠 Memory Management

### Critical Rules

1. **Always free returned arrays and strings:**
```c
// ✅ Correct
docker_excess_container_t **containers;
size_t count;
docker_excess_list_containers(client, true, &containers, &count);
docker_excess_free_containers(containers, count);  // Required!

// ❌ Wrong - memory leak
docker_excess_list_containers(client, true, &containers, &count);
// Missing docker_excess_free_containers() call
```

2. **Free single strings from functions:**
```c
// ✅ Correct  
char *version;
docker_excess_version(client, &version);
free(version);  // Required!
```

3. **Always free the client:**
```c
docker_excess_t *client;
docker_excess_new(&client);
// ... use client ...
docker_excess_free(client);  // Required!
```

### Memory Safety Checklist
- ✅ Use provided `docker_excess_free_*` functions for arrays
- ✅ Use standard `free()` for single returned strings
- ✅ Check return values before using output parameters
- ✅ Free client with `docker_excess_free()`
- ❌ Never free strings inside structures (handled by `docker_excess_free_*`)

---

## 📝 Notes

### Connection Details
- **Default socket:** `/var/run/docker.sock`
- **API Version:** `1.41` (Docker Engine >= 18.06)
- **Timeout:** 30 seconds (configurable)

### Platform Support
- **Linux:** Full support (Unix socket + TCP)
- **macOS:** Full support (Unix socket + TCP)  
- **Windows:** TCP only (no Unix socket support)

### Performance Tips
- Reuse client instances when possible
- Use `docker_excess_ping()` to verify connectivity before operations
- Set appropriate timeouts for long-running operations
- Consider separate client instances for concurrent operations

### Limitations
- `build_image` requires manual tar archive creation
- Log streaming doesn't handle all Docker multiplexed format edge cases
- No support for Docker Swarm-specific APIs
- Limited to Docker Engine API v1.41 features

### Debugging
```c
docker_excess_config_t config = docker_excess_default_config();
config.debug = true;  // Enable cURL verbose output
docker_excess_new_with_config(&config, &client);
```

---

## 📄 License

**MIT License** - See [LICENSE](./LICENSE) for details.

---

## 🤝 Contributing

Contributions welcome! Areas for improvement:
- Complete `build_image` tar archive creation
- Enhanced log parsing for edge cases
- Docker Compose API support
- Additional utility functions
- Performance optimizations

--