# Docker Excess SDK for C

A comprehensive, production-ready Docker API client library for C applications.

## Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
- [Examples](#examples)
- [Configuration](#configuration)
- [Error Handling](#error-handling)
- [Thread Safety](#thread-safety)
- [Building and Integration](#building-and-integration)

## Overview

Docker SDK for C provides a complete interface to the Docker Engine API, enabling C applications to:

- Manage containers (create, start, stop, remove, inspect)
- Handle images (pull, push, build, list, remove)
- Control networks and volumes
- Execute commands inside containers
- Stream logs and events
- Monitor container statistics
- Manage system resources

### Key Features

- **Header-only library** - Easy integration
- **Cross-platform** - Linux, macOS, Windows support
- **Thread-safe** - Optional thread safety for concurrent applications
- **Memory-managed** - Clear ownership model with cleanup functions
- **Type-safe** - Structured types and enums prevent common errors
- **Extensible** - Plugin architecture for logging and progress reporting
- **Production-ready** - Comprehensive error handling and timeout management

## Installation

### Option 1: Header-Only (Recommended)

```c
#define DOCKER_SDK_IMPLEMENTATION
#include "docker-sdk.h"
```

### Option 2: Compiled Library

```bash
# Compile as shared library
gcc -shared -fPIC -o libdocker-sdk.so docker-sdk.c -lcurl -ljson-c -lpthread

# Link in your project
gcc myapp.c -ldocker-sdk -lcurl -ljson-c -lpthread
```

### Dependencies

- **Required**: None (minimal implementation uses system calls)
- **Optional**: 
  - libcurl (HTTP transport)
  - json-c (JSON parsing)
  - pthread (thread safety)

## Quick Start

```c
#define DOCKER_SDK_IMPLEMENTATION
#include "docker-sdk.h"

int main() {
    // Initialize client
    docker_client_t *client = docker_client_create();
    if (!client) {
        fprintf(stderr, "Failed to create Docker client\n");
        return 1;
    }
    
    // Test connection
    if (docker_ping(client) != DOCKER_OK) {
        fprintf(stderr, "Cannot connect to Docker daemon\n");
        docker_client_destroy(client);
        return 1;
    }
    
    // Create and start a container
    docker_container_config_t *config = docker_container_config_create("nginx:latest");
    docker_container_config_add_port_mapping(config, 80, 8080, "tcp", NULL);
    
    char *container_id;
    docker_error_t result = docker_container_create(client, config, &container_id);
    
    if (result == DOCKER_OK) {
        printf("Created container: %s\n", container_id);
        docker_container_start(client, container_id);
        printf("Container started on port 8080\n");
        free(container_id);
    }
    
    // Cleanup
    docker_container_config_destroy(config);
    docker_client_destroy(client);
    return 0;
}
```

## API Reference

### Core Client

#### `docker_client_create()`
```c
docker_client_t* docker_client_create(void);
```
Creates a Docker client with default configuration (Unix socket connection).

**Returns**: Pointer to client or NULL on failure

#### `docker_client_create_with_config()`
```c
docker_client_t* docker_client_create_with_config(const docker_config_t *config);
```
Creates a Docker client with custom configuration.

**Parameters**:
- `config`: Client configuration

**Returns**: Pointer to client or NULL on failure

#### `docker_client_destroy()`
```c
void docker_client_destroy(docker_client_t *client);
```
Cleans up Docker client and frees resources.

#### `docker_ping()`
```c
docker_error_t docker_ping(docker_client_t *client);
```
Tests connection to Docker daemon.

**Returns**: `DOCKER_OK` if connected, error code otherwise

### Container Management

#### `docker_container_config_create()`
```c
docker_container_config_t* docker_container_config_create(const char *image);
```
Creates container configuration for specified image.

#### `docker_container_create()`
```c
docker_error_t docker_container_create(docker_client_t *client,
                                      const docker_container_config_t *config,
                                      char **container_id);
```
Creates a new container from configuration.

**Parameters**:
- `client`: Docker client
- `config`: Container configuration
- `container_id`: Output parameter for container ID (caller must free)

**Returns**: Error code

#### `docker_container_start()`
```c
docker_error_t docker_container_start(docker_client_t *client, const char *container_id);
```
Starts an existing container.

#### `docker_container_stop()`
```c
docker_error_t docker_container_stop(docker_client_t *client, const char *container_id, int timeout);
```
Stops a running container.

**Parameters**:
- `timeout`: Grace period in seconds before force kill

#### `docker_container_list()`
```c
docker_error_t docker_container_list(docker_client_t *client, bool all,
                                    docker_container_info_t ***containers, size_t *count);
```
Lists containers on the system.

**Parameters**:
- `all`: Include stopped containers
- `containers`: Output array (caller must free with `docker_container_info_free_array`)
- `count`: Number of containers returned

#### `docker_container_inspect()`
```c
docker_error_t docker_container_inspect(docker_client_t *client, const char *container_id,
                                       docker_container_info_t **info);
```
Gets detailed information about a container.

#### `docker_container_logs()`
```c
docker_error_t docker_container_logs(docker_client_t *client, const char *container_id,
                                    bool follow, bool stdout_flag, bool stderr_flag,
                                    int64_t since, int64_t until, int tail,
                                    docker_stream_callback_t callback, void *userdata);
```
Retrieves container logs with streaming support.

**Parameters**:
- `follow`: Stream logs continuously
- `stdout_flag`: Include stdout
- `stderr_flag`: Include stderr
- `since`: Unix timestamp to start from
- `until`: Unix timestamp to end at
- `tail`: Number of lines from end (0 = all)
- `callback`: Function called for each log chunk
- `userdata`: User data passed to callback

#### Callback Types

```c
typedef void (*docker_stream_callback_t)(const char *data, size_t size, void *userdata);
typedef void (*docker_progress_callback_t)(const char *status, int64_t current, int64_t total, void *userdata);
typedef void (*docker_log_callback_t)(docker_log_level_t level, const char *message, void *userdata);
```

### Container Configuration

#### `docker_container_config_add_env_var()`
```c
void docker_container_config_add_env_var(docker_container_config_t *config, 
                                        const char *name, const char *value);
```
Adds environment variable to container.

#### `docker_container_config_add_port_mapping()`
```c
void docker_container_config_add_port_mapping(docker_container_config_t *config,
                                             uint16_t container_port, uint16_t host_port,
                                             const char *protocol, const char *host_ip);
```
Maps container port to host port.

**Parameters**:
- `protocol`: "tcp" or "udp"
- `host_ip`: Host IP to bind to (NULL = all interfaces)

#### `docker_container_config_add_volume_mount()`
```c
void docker_container_config_add_volume_mount(docker_container_config_t *config,
                                             const char *source, const char *target,
                                             const char *type, bool read_only);
```
Adds volume mount to container.

**Parameters**:
- `source`: Host path or volume name
- `target`: Container path
- `type`: "bind", "volume", or "tmpfs"
- `read_only`: Mount as read-only

#### `docker_container_config_set_resource_limits()`
```c
void docker_container_config_set_resource_limits(docker_container_config_t *config,
                                                const docker_resource_limits_t *limits);
```
Sets resource limits for container.

### Execution

#### `docker_exec_create()`
```c
docker_error_t docker_exec_create(docker_client_t *client, const char *container_id,
                                 const docker_exec_config_t *config, char **exec_id);
```
Creates execution instance in running container.

#### `docker_exec_start()`
```c
docker_error_t docker_exec_start(docker_client_t *client, const char *exec_id,
                                bool detach, docker_stream_callback_t callback, void *userdata);
```
Starts execution instance.

**Parameters**:
- `detach`: Run in background
- `callback`: Function to handle output (NULL for detached)

### Image Management

#### `docker_image_pull()`
```c
docker_error_t docker_image_pull(docker_client_t *client, const char *image,
                                const char *tag, docker_progress_callback_t callback, void *userdata);
```
Pulls image from registry with progress reporting.

#### `docker_image_list()`
```c
docker_error_t docker_image_list(docker_client_t *client, bool all,
                                docker_image_info_t ***images, size_t *count);
```
Lists images on the system.

#### `docker_image_remove()`
```c
docker_error_t docker_image_remove(docker_client_t *client, const char *image,
                                  bool force, bool no_prune);
```
Removes image from system.

### File Operations

#### `docker_container_copy_to()`
```c
docker_error_t docker_container_copy_to(docker_client_t *client, const char *container_id,
                                       const char *container_path, const char *host_path);
```
Copies file from host to container.

#### `docker_container_copy_from()`
```c
docker_error_t docker_container_copy_from(docker_client_t *client, const char *container_id,
                                         const char *container_path, const char *host_path);
```
Copies file from container to host.

#### `docker_container_put_archive()`
```c
docker_error_t docker_container_put_archive(docker_client_t *client, const char *container_id,
                                           const char *container_path, const char *data, size_t size);
```
Uploads data directly to container from memory.

#### `docker_container_get_archive()`
```c
docker_error_t docker_container_get_archive(docker_client_t *client, const char *container_id,
                                           const char *container_path, char **data, size_t *size);
```
Downloads data directly from container to memory.

## Examples

### Creating a Web Server Container

```c
#include "docker-sdk.h"

void deploy_web_server() {
    docker_client_t *client = docker_client_create();
    
    // Create nginx container with custom configuration
    docker_container_config_t *config = docker_container_config_create("nginx:alpine");
    
    // Configure ports
    docker_container_config_add_port_mapping(config, 80, 8080, "tcp", NULL);
    docker_container_config_add_port_mapping(config, 443, 8443, "tcp", NULL);
    
    // Mount configuration
    docker_container_config_add_volume_mount(config, 
        "/host/nginx.conf", "/etc/nginx/nginx.conf", "bind", true);
    docker_container_config_add_volume_mount(config,
        "/host/www", "/var/www/html", "bind", false);
    
    // Set environment
    docker_container_config_add_env_var(config, "NGINX_HOST", "localhost");
    docker_container_config_add_env_var(config, "NGINX_PORT", "80");
    
    // Create and start container
    char *container_id;
    if (docker_container_create(client, config, &container_id) == DOCKER_OK) {
        printf("Created container: %s\n", container_id);
        
        if (docker_container_start(client, container_id) == DOCKER_OK) {
            printf("Web server running on ports 8080/8443\n");
        }
        free(container_id);
    }
    
    docker_container_config_destroy(config);
    docker_client_destroy(client);
}
```

### Monitoring Container Logs

```c
void log_callback(const char *data, size_t size, void *userdata) {
    printf("LOG: %.*s", (int)size, data);
}

void monitor_container(const char *container_id) {
    docker_client_t *client = docker_client_create();
    
    // Stream logs continuously
    docker_container_logs(client, container_id,
                         true,  // follow
                         true,  // stdout
                         true,  // stderr
                         0,     // since start
                         0,     // no end time
                         100,   // last 100 lines
                         log_callback, NULL);
    
    docker_client_destroy(client);
}
```

### Executing Commands in Container

```c
void run_maintenance(const char *container_id) {
    docker_client_t *client = docker_client_create();
    
    // Create exec configuration
    const char *cmd[] = {"/bin/sh", "-c", "apt update && apt upgrade -y"};
    docker_exec_config_t *exec_config = docker_exec_config_create(cmd, 3);
    
    char *exec_id;
    if (docker_exec_create(client, container_id, exec_config, &exec_id) == DOCKER_OK) {
        printf("Running maintenance...\n");
        docker_exec_start(client, exec_id, false, log_callback, NULL);
        free(exec_id);
    }
    
    docker_exec_config_destroy(exec_config);
    docker_client_destroy(client);
}
```

### Building Custom Images

```c
void build_custom_image() {
    docker_client_t *client = docker_client_create();
    
    docker_image_config_t *config = docker_image_config_create();
    config->dockerfile = 
        "FROM alpine:latest\n"
        "RUN apk add --no-cache curl\n"
        "COPY app /usr/local/bin/\n"
        "CMD [\"/usr/local/bin/app\"]\n";
    config->context_path = "./build-context";
    
    // Add build args
    config->build_args = malloc(2 * sizeof(char*));
    config->build_args[0] = "VERSION=1.0";
    config->build_args[1] = "DEBUG=false";
    config->build_args_count = 2;
    
    printf("Building image...\n");
    docker_image_build(client, config, NULL, NULL);
    
    docker_image_config_destroy(config);
    docker_client_destroy(client);
}
```

## Configuration

### Client Configuration

```c
docker_config_t *config = docker_config_create_default();

// Customize socket path
config->socket_path = "/custom/docker.sock";

// Set timeouts
docker_config_set_timeouts(config, 5000, 30000); // 5s connect, 30s request

// Enable logging
docker_config_set_log_callback(config, my_log_handler, NULL);

// Create client with custom config
docker_client_t *client = docker_client_create_with_config(config);
```

### Environment Configuration

```c
// Reads from DOCKER_HOST, DOCKER_TLS_VERIFY, etc.
docker_config_t *config = docker_config_create_from_env();
docker_client_t *client = docker_client_create_with_config(config);
```

### TCP with TLS

```c
docker_config_t *config = docker_config_create_default();
config->host = "tcp://docker.example.com";
config->port = 2376;
config->use_tls = true;
config->cert_path = "/path/to/cert.pem";
config->key_path = "/path/to/key.pem";
config->ca_path = "/path/to/ca.pem";
```

### Resource Limits

```c
docker_resource_limits_t limits = {0};
limits.memory_limit = 512 * 1024 * 1024;  // 512MB
limits.cpu_shares = 512;                   // Half CPU weight
limits.pids_limit = 100;                   // Max 100 processes
limits.cpuset_cpus = "0-1";               // CPUs 0 and 1 only

docker_container_config_set_resource_limits(config, &limits);
```

## Error Handling

All API functions return `docker_error_t` values:

```c
typedef enum {
    DOCKER_OK = 0,
    DOCKER_ERROR_INVALID_PARAM = -1,
    DOCKER_ERROR_MEMORY = -2,
    DOCKER_ERROR_NETWORK = -3,
    DOCKER_ERROR_HTTP = -4,
    DOCKER_ERROR_JSON = -5,
    DOCKER_ERROR_NOT_FOUND = -6,
    DOCKER_ERROR_UNAUTHORIZED = -7,
    DOCKER_ERROR_TIMEOUT = -8,
    DOCKER_ERROR_INTERNAL = -9
} docker_error_t;
```

### Error Checking

```c
docker_error_t result = docker_container_start(client, container_id);
if (result != DOCKER_OK) {
    fprintf(stderr, "Failed to start container: %s\n", 
            docker_error_string(result));
    
    // Get detailed error from client
    const char *error_msg = docker_client_get_last_error_message(client);
    if (error_msg) {
        fprintf(stderr, "Details: %s\n", error_msg);
    }
}
```

### Custom Error Handler

```c
void my_error_handler(docker_error_t error, const char *message, void *userdata) {
    syslog(LOG_ERR, "Docker error %d: %s", error, message);
}

docker_set_error_handler(my_error_handler, NULL);
```

## Thread Safety

Enable thread safety for multi-threaded applications:

```c
// Enable thread safety (requires pthread)
docker_enable_thread_safety(true);

// Now safe to use from multiple threads
docker_client_t *client = docker_client_create();
```

Thread safety protects:
- Client creation/destruction
- API calls on the same client
- Global configuration changes
- Error handling

Each thread should use its own client instance for best performance.

## Building and Integration

### Makefile Example

```makefile
CFLAGS = -Wall -Wextra -std=c99
LIBS = -lcurl -ljson-c -lpthread

# Header-only version
myapp: myapp.c docker-sdk.h
	gcc $(CFLAGS) -DDOCKER_SDK_IMPLEMENTATION myapp.c -o myapp $(LIBS)

# Compiled library version
libdocker-sdk.so: docker-sdk.c docker-sdk.h
	gcc $(CFLAGS) -shared -fPIC docker-sdk.c -o libdocker-sdk.so $(LIBS)

myapp_linked: myapp.c libdocker-sdk.so
	gcc $(CFLAGS) myapp.c -L. -ldocker-sdk -o myapp_linked
```

### CMake Integration

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(JSON_C REQUIRED json-c)

add_executable(myapp myapp.c)
target_compile_definitions(myapp PRIVATE DOCKER_SDK_IMPLEMENTATION)
target_link_libraries(myapp ${CURL_LIBRARIES} ${JSON_C_LIBRARIES} pthread)
target_include_directories(myapp PRIVATE ${CURL_INCLUDE_DIRS} ${JSON_C_INCLUDE_DIRS})
```

### Minimal Build (No Dependencies)

```c
// Disable optional features
#define DOCKER_SDK_NO_CURL
#define DOCKER_SDK_NO_JSON_C
#define DOCKER_SDK_NO_THREADS
#define DOCKER_SDK_IMPLEMENTATION
#include "docker-sdk.h"

// Uses basic HTTP client and minimal JSON parser
```

### Compile-Time Options

- `DOCKER_SDK_IMPLEMENTATION` - Include implementation
- `DOCKER_SDK_WITH_CURL` - Use libcurl for HTTP (recommended)
- `DOCKER_SDK_WITH_JSON_C` - Use json-c for JSON parsing
- `DOCKER_SDK_NO_THREADS` - Disable thread safety
- `DOCKER_SDK_STATIC` - Static linking
- `DOCKER_SDK_DEBUG` - Enable debug logging

### Performance Tuning

```c
// Increase buffer sizes for large responses
#define DOCKER_SDK_BUFFER_SIZE 16384
#define DOCKER_SDK_MAX_RESPONSE_SIZE (128 * 1024 * 1024)

// Reduce timeouts for faster failure detection
docker_config_set_timeouts(config, 1000, 5000); // 1s connect, 5s request
```

## License

This Docker SDK for C is released under the MIT License. See LICENSE file for details.

## Contributing

Contributions are welcome! Please see CONTRIBUTING.md for guidelines.

## Support

- GitHub Issues: Report bugs and request features
- Documentation: Full API reference and examples
- Docker API: Compatible with Docker Engine API v1.40+
