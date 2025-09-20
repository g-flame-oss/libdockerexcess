# libdockerexcess

A modern, thread-safe C library for the Docker Engine API with comprehensive container, image, and network management capabilities.
### **Documentation**: [API Docs](https://g-flame-oss.github.io/docker-excess/)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)](#)


## Quick Start

### Installation

**Ubuntu/Debian:**
```bash
sudo apt-get install libcurl4-openssl-dev libjson-c-dev cmake build-essential
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
```

**CentOS/RHEL/Fedora:**
```bash
sudo dnf install libcurl-devel json-c-devel cmake gcc  # Fedora
# sudo yum install libcurl-devel json-c-devel cmake gcc  # CentOS/RHEL
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess && mkdir build && cd build
cmake .. && make -j$(nproc) && sudo make install
```

### Hello Docker

```c
#include <docker-excess.h>

int main() {
    docker_excess_t *client;
    
    // Connect to Docker
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        printf("Failed to create client\n");
        return 1;
    }
    
    // Test connection
    if (docker_excess_ping(client) != DOCKER_EXCESS_OK) {
        printf("Docker not available: %s\n", docker_excess_get_error(client));
        docker_excess_free(client);
        return 1;
    }
    
    printf("✓ Connected to Docker\n");
    docker_excess_free(client);
    return 0;
}
```

Compile with: `gcc -o hello hello.c -ldocker-excess`

## Core Concepts

### Client Management

```c
// Default client (uses /var/run/docker.sock)
docker_excess_t *client;
docker_excess_new(&client);

// Custom configuration
docker_excess_config_t config = docker_excess_default_config();
config.host = strdup("192.168.1.100");
config.port = 2376;
config.timeout_s = 60;
docker_excess_new_with_config(&config, &client);

// Always cleanup
docker_excess_free(client);
```

### Error Handling

Every function returns a `docker_excess_error_t`. Always check it:

```c
docker_excess_error_t err = docker_excess_start_container(client, "my-container");
if (err != DOCKER_EXCESS_OK) {
    printf("Error: %s\n", docker_excess_error_string(err));
    printf("Details: %s\n", docker_excess_get_error(client));
}
```

## Common Operations

### List and Manage Containers

```c
// List all containers (running + stopped)
docker_excess_container_t **containers;
size_t count;

if (docker_excess_list_containers(client, true, NULL, &containers, &count) == DOCKER_EXCESS_OK) {
    for (size_t i = 0; i < count; i++) {
        printf("Container: %s (%s) - %s\n", 
               containers[i]->name, 
               containers[i]->short_id,
               containers[i]->status);
    }
    docker_excess_free_containers(containers, count);
}

// Container lifecycle
docker_excess_start_container(client, "container-id");
docker_excess_stop_container(client, "container-id", 10); // 10s timeout
docker_excess_remove_container(client, "container-id", false, false);
```

### Create and Run Containers

```c
// Simple container creation
docker_excess_container_create_t *params = docker_excess_container_create_new("nginx:alpine");
params->name = strdup("my-nginx");

char *container_id;
if (docker_excess_create_container(client, params, &container_id) == DOCKER_EXCESS_OK) {
    docker_excess_start_container(client, container_id);
    printf("Started container: %s\n", container_id);
    free(container_id);
}
docker_excess_container_create_free(params);
```

### Advanced Container Creation

```c
docker_excess_container_create_t *params = docker_excess_container_create_new("postgres:13");
params->name = strdup("my-postgres");

// Environment variables
params->env = malloc(2 * sizeof(docker_excess_env_var_t));
params->env[0] = (docker_excess_env_var_t){"POSTGRES_PASSWORD", "secret123"};
params->env[1] = (docker_excess_env_var_t){"POSTGRES_DB", "myapp"};
params->env_count = 2;

// Port mapping
params->ports = malloc(sizeof(docker_excess_port_mapping_t));
params->ports[0] = (docker_excess_port_mapping_t){5432, 5432, "tcp", NULL};
params->ports_count = 1;

// Volume mount
params->mounts = malloc(sizeof(docker_excess_mount_t));
params->mounts[0] = (docker_excess_mount_t){
    .source = "/host/postgres-data",
    .target = "/var/lib/postgresql/data", 
    .type = "bind",
    .read_only = false
};
params->mounts_count = 1;

// Resource limits
params->memory_limit = 512 * 1024 * 1024; // 512MB
params->cpu_shares = 0.5; // 50% CPU

char *container_id;
docker_excess_create_container(client, params, &container_id);
docker_excess_container_create_free(params);
```

### Execute Commands

```c
// Simple command execution
char *stdout_out, *stderr_out;
int exit_code;

docker_excess_exec_simple(client, container_id, "ls -la /app", 
                         &stdout_out, &stderr_out, &exit_code);
printf("Exit code: %d\n", exit_code);
printf("Output:\n%s\n", stdout_out);
free(stdout_out);
free(stderr_out);
```

### File Operations

```c
// Read file from container
char *content;
size_t size;
if (docker_excess_read_file(client, container_id, "/app/config.txt", &content, &size) == DOCKER_EXCESS_OK) {
    printf("File content:\n%.*s\n", (int)size, content);
    free(content);
}

// Write file to container
const char *new_content = "key=value\nother=setting\n";
docker_excess_write_file(client, container_id, "/app/config.txt", 
                        new_content, strlen(new_content), 0644);

// List files in container
docker_excess_file_t **files;
size_t file_count;
docker_excess_list_files(client, container_id, "/app", false, &files, &file_count);
for (size_t i = 0; i < file_count; i++) {
    printf("%s %s %ld bytes\n", 
           files[i]->is_dir ? "DIR" : "FILE",
           files[i]->name, 
           files[i]->size);
}
docker_excess_free_files(files, file_count);
```

### Stream Logs

```c
void log_handler(const char *line, bool is_stderr, time_t timestamp, void *userdata) {
    printf("[%s] %s\n", is_stderr ? "ERR" : "OUT", line);
}

docker_excess_log_params_t log_params = {
    .follow = true,        // Stream logs in real-time
    .timestamps = true,    // Include timestamps  
    .tail_lines = 100      // Start with last 100 lines
};

docker_excess_get_logs(client, container_id, &log_params, log_handler, NULL);
```

### Image Management

```c
// List images
docker_excess_image_t **images;
size_t image_count;
docker_excess_list_images(client, false, NULL, &images, &image_count);

// Pull image with progress
void pull_progress(const char *status, const char *progress, void *userdata) {
    printf("Pull: %s %s\n", status, progress ? progress : "");
}
docker_excess_pull_image(client, "ubuntu", "20.04", pull_progress, NULL);

// Build image
docker_excess_image_build_t *build_params = docker_excess_image_build_new("./my-app");
build_params->dockerfile_path = strdup("Dockerfile");
build_params->tag = strdup("my-app:latest");
build_params->no_cache = false;

docker_excess_build_image(client, build_params, pull_progress, NULL);
docker_excess_image_build_free(build_params);
```

## Configuration Options

### Connection Types

```c
// Unix socket (default)
docker_excess_config_t config = docker_excess_default_config();
// Uses /var/run/docker.sock

// TCP connection
config.host = strdup("docker-host.example.com");
config.port = 2376;

// TLS-secured connection
config.use_tls = true;
config.cert_path = strdup("/path/to/cert.pem");
config.key_path = strdup("/path/to/key.pem");
config.ca_path = strdup("/path/to/ca.pem");
```

### Environment Variables

The library automatically reads Docker environment variables:

```bash
export DOCKER_HOST=tcp://docker.example.com:2376
export DOCKER_TLS_VERIFY=1
export DOCKER_CERT_PATH=/path/to/certs
```

```c
docker_excess_config_t config = docker_excess_config_from_env();
docker_excess_t *client;
docker_excess_new_with_config(&config, &client);
```

### Logging

```c
void my_logger(docker_excess_log_level_t level, const char *message, void *userdata) {
    const char *level_str[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    printf("[%s] %s\n", level_str[level], message);
}

docker_excess_config_t config = docker_excess_default_config();
config.log_callback = my_logger;
config.debug = true; // Enable verbose cURL output
```

## Thread Safety

All functions are thread-safe. You can share a single client across threads:

```c
#include <pthread.h>

void* worker_thread(void *arg) {
    docker_excess_t *client = (docker_excess_t*)arg;
    
    // Safe to use client from multiple threads
    docker_excess_container_t **containers;
    size_t count;
    docker_excess_list_containers(client, false, NULL, &containers, &count);
    docker_excess_free_containers(containers, count);
    
    return NULL;
}

int main() {
    docker_excess_t *client;
    docker_excess_new(&client);
    
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker_thread, client);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    docker_excess_free(client);
    return 0;
}
```

## Error Reference

| Error Code | Description |
|------------|-------------|
| `DOCKER_EXCESS_OK` | Operation successful |
| `DOCKER_EXCESS_ERR_INVALID_PARAM` | Invalid parameter passed |
| `DOCKER_EXCESS_ERR_MEMORY` | Memory allocation failed |
| `DOCKER_EXCESS_ERR_NETWORK` | Network/connection error |
| `DOCKER_EXCESS_ERR_NOT_FOUND` | Container/image/resource not found |
| `DOCKER_EXCESS_ERR_CONFLICT` | Resource conflict (e.g., container already running) |
| `DOCKER_EXCESS_ERR_PERMISSION` | Permission denied |
| `DOCKER_EXCESS_ERR_TIMEOUT` | Operation timed out |

Always use `docker_excess_get_error(client)` for detailed error messages.

## Real-World Examples

### Container Health Monitor

```c
#include <docker-excess.h>
#include <unistd.h>

int monitor_container(const char *container_name) {
    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) return 1;
    
    while (1) {
        docker_excess_container_t *container;
        docker_excess_error_t err = docker_excess_inspect_container(client, container_name, &container);
        
        if (err == DOCKER_EXCESS_OK) {
            printf("Container %s: %s\n", container_name, 
                   container->state == DOCKER_EXCESS_STATE_RUNNING ? "HEALTHY" : "UNHEALTHY");
            
            if (container->state == DOCKER_EXCESS_STATE_EXITED) {
                printf("Container exited with code: %d\n", container->exit_code);
                docker_excess_start_container(client, container_name);
            }
            
            docker_excess_free_containers(&container, 1);
        } else {
            printf("Failed to inspect container: %s\n", docker_excess_get_error(client));
        }
        
        sleep(30);
    }
    
    docker_excess_free(client);
    return 0;
}
```

### Batch Container Operations

```c
int stop_all_containers(docker_excess_t *client) {
    docker_excess_container_t **containers;
    size_t count;
    
    // Get only running containers
    const char *filters = "{\"status\":[\"running\"]}";
    if (docker_excess_list_containers(client, false, filters, &containers, &count) != DOCKER_EXCESS_OK) {
        return -1;
    }
    
    printf("Stopping %zu containers...\n", count);
    
    for (size_t i = 0; i < count; i++) {
        printf("Stopping %s...", containers[i]->name);
        if (docker_excess_stop_container(client, containers[i]->id, 10) == DOCKER_EXCESS_OK) {
            printf(" OK\n");
        } else {
            printf(" FAILED: %s\n", docker_excess_get_error(client));
        }
    }
    
    docker_excess_free_containers(containers, count);
    return 0;
}
```

## Build Options

```bash
# Standard build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Development build with debugging
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDOCKER_EXCESS_BUILD_TESTS=ON \
  -DDOCKER_EXCESS_ENABLE_ASAN=ON

# Custom installation
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
```

### CMake Integration

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(DOCKER_EXCESS REQUIRED docker-excess)

target_link_libraries(your_target ${DOCKER_EXCESS_LIBRARIES})
target_include_directories(your_target PRIVATE ${DOCKER_EXCESS_INCLUDE_DIRS})
```

## Performance Tips

1. **Reuse clients** - Creating clients is expensive, reuse them
2. **Batch operations** - Group multiple container operations together  
3. **Use filters** - Filter on the Docker daemon rather than client-side
4. **Stream when possible** - Use streaming APIs for logs and events
5. **Free resources** - Always use the provided `_free` functions

## Debugging

Enable debug logging to see HTTP requests:

```c
docker_excess_config_t config = docker_excess_default_config();
config.debug = true;
config.log_callback = my_logger;
```

Use tools like `strace` to debug socket communication:
```bash
strace -e trace=connect,read,write ./your_program
```

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Run tests: `make test`
4. Submit a pull request

### Development Setup

```bash
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess
mkdir build && cd build
cmake .. -DDOCKER_EXCESS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
make test
```

## License

MIT License - see [LICENSE](LICENSE) file.

## Support

- **Issues**: [GitHub Issues](https://github.com/G-flame-OSS/docker-excess/issues)
---

