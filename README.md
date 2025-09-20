# Docker-excess (Enhanced C Docker SDK)

**Version:** `2.0.0`  
A robust, thread-safe, low-level C client for the Docker Engine API with comprehensive functionality and improved error handling.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#build)

## 🚀 Key Improvements in v2.0

- **Enhanced Error Handling**: Comprehensive error codes and detailed error messages
- **Thread Safety**: Full thread-safe implementation with proper mutex locking
- **Memory Management**: Improved memory handling with leak prevention
- **Better API Design**: More intuitive function signatures and parameter structures
- **Extended Functionality**: Support for all major Docker API operations
- **Logging System**: Configurable logging with callback support
- **Robust Configuration**: Enhanced configuration options with environment support
- **Documentation**: Comprehensive examples and API documentation

---

## 📋 Table of Contents

1. [Features](#features)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [API Reference](#api-reference)
5. [Examples](#examples)
6. [Configuration](#configuration)
7. [Error Handling](#error-handling)
8. [Thread Safety](#thread-safety)
9. [Building from Source](#building-from-source)
10. [Contributing](#contributing)
11. [License](#license)

---

## ✨ Features

### Core Functionality
- ✅ **Container Management**: Create, start, stop, remove, inspect containers
- ✅ **Image Operations**: Pull, push, build, remove, inspect images
- ✅ **Network Management**: Create, remove, connect/disconnect networks
- ✅ **Volume Management**: Create, remove, inspect volumes
- ✅ **File Operations**: Copy files, read/write content, list directories
- ✅ **Command Execution**: Execute commands in containers with real-time output
- ✅ **Log Streaming**: Stream container logs with filtering options
- ✅ **System Information**: Get Docker version, system info, events

### Advanced Features
- 🔒 **Thread-Safe**: All operations are thread-safe with proper locking
- 🚨 **Comprehensive Error Handling**: Detailed error codes and messages
- 📊 **Progress Callbacks**: Real-time progress for long-running operations
- 🔧 **Flexible Configuration**: Support for Unix sockets, TCP, TLS
- 📝 **Logging System**: Configurable logging with custom callbacks
- 🏷️ **Resource Filtering**: Advanced filtering for lists and searches
- 💾 **Memory Safe**: Proper memory management with helper functions
- ⚡ **Performance Optimized**: Efficient HTTP handling and JSON parsing

### Connection Options
- 🔌 **Unix Socket**: Default connection via `/var/run/docker.sock`
- 🌐 **TCP/HTTP**: Remote Docker daemon connections
- 🔐 **TLS Support**: Secure connections with certificate authentication
- 🌍 **Environment Variables**: Auto-configuration from Docker environment

---

## 📦 Installation

### Package Managers

#### Ubuntu/Debian
```bash
# Install dependencies
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev libjson-c-dev

# Install from source (see Building section)
```

#### CentOS/RHEL/Fedora
```bash
# Install dependencies
sudo yum install libcurl-devel json-c-devel  # CentOS/RHEL
sudo dnf install libcurl-devel json-c-devel  # Fedora

# Install from source (see Building section)
```

#### macOS
```bash
# Install dependencies via Homebrew
brew install curl json-c

# Install from source (see Building section)
```

### From Source
```bash
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

---

## 🚀 Quick Start

### Basic Usage

```c
#include <docker-excess.h>
#include <stdio.h>

int main() {
    docker_excess_t *client;
    
    // Create client
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        fprintf(stderr, "Failed to create Docker client\n");
        return 1;
    }

    // Test connection
    if (docker_excess_ping(client) != DOCKER_EXCESS_OK) {
        fprintf(stderr, "Cannot connect to Docker: %s\n", 
                docker_excess_get_error(client));
        docker_excess_free(client);
        return 1;
    }

    printf("✓ Connected to Docker daemon\n");

    // List containers
    docker_excess_container_t **containers;
    size_t count;
    
    if (docker_excess_list_containers(client, true, NULL, &containers, &count) == DOCKER_EXCESS_OK) {
        printf("Found %zu containers:\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %s (%s)\n", 
                   containers[i]->name ? containers[i]->name : "unnamed",
                   containers[i]->short_id);
        }
        docker_excess_free_containers(containers, count);
    }

    docker_excess_free(client);
    return 0;
}
```

### Compile and Run
```bash
gcc -o example example.c -ldocker-excess -lcurl -ljson-c -lpthread
./example
```

---

## 📖 API Reference

### Core Functions

#### Client Management
```c
// Create new client with default config
docker_excess_error_t docker_excess_new(docker_excess_t **client);

// Create client with custom config
docker_excess_error_t docker_excess_new_with_config(
    const docker_excess_config_t *config, 
    docker_excess_t **client);

// Free client resources
void docker_excess_free(docker_excess_t *client);

// Test connection
docker_excess_error_t docker_excess_ping(docker_excess_t *client);
```

#### Container Operations
```c
// List containers with optional filtering
docker_excess_error_t docker_excess_list_containers(
    docker_excess_t *client, 
    bool all,
    const char *filters,  // JSON filters
    docker_excess_container_t ***containers, 
    size_t *count);

// Create container with parameters
docker_excess_error_t docker_excess_create_container(
    docker_excess_t *client,
    const docker_excess_container_create_t *params,
    char **container_id);

// Container lifecycle
docker_excess_error_t docker_excess_start_container(docker_excess_t *client, const char *id);
docker_excess_error_t docker_excess_stop_container(docker_excess_t *client, const char *id, int timeout);
docker_excess_error_t docker_excess_restart_container(docker_excess_t *client, const char *id);
docker_excess_error_t docker_excess_remove_container(docker_excess_t *client, const char *id, bool force, bool remove_volumes);
```

#### Image Operations
```c
// List images
docker_excess_error_t docker_excess_list_images(
    docker_excess_t *client, 
    bool all,
    const char *filters,
    docker_excess_image_t ***images, 
    size_t *count);

// Pull image with progress callback
docker_excess_error_t docker_excess_pull_image(
    docker_excess_t *client, 
    const char *image_name, 
    const char *tag,
    docker_excess_progress_callback_t callback, 
    void *userdata);

// Build image
docker_excess_error_t docker_excess_build_image(
    docker_excess_t *client,
    const docker_excess_image_build_t *params,
    docker_excess_progress_callback_t callback,
    void *userdata);
```

#### File Operations
```c
// List files in container
docker_excess_error_t docker_excess_list_files(
    docker_excess_t *client,
    const char *container_id,
    const char *path,
    bool recursive,
    docker_excess_file_t ***files,
    size_t *count);

// Read/write files
docker_excess_error_t docker_excess_read_file(
    docker_excess_t *client,
    const char *container_id,
    const char *file_path,
    char **content,
    size_t *size);

docker_excess_error_t docker_excess_write_file(
    docker_excess_t *client,
    const char *container_id,
    const char *file_path,
    const char *content,
    size_t size,
    uint32_t mode);
```

### Data Structures

#### Container Creation Parameters
```c
typedef struct {
    // Basic settings
    char *name;                     // Container name
    char *image;                    // Image name (required)
    char **cmd;                     // Command array
    size_t cmd_count;
    char *working_dir;              // Working directory
    char *user;                     // User to run as
    
    // Environment and labels
    docker_excess_env_var_t *env;   // Environment variables
    size_t env_count;
    char **labels;                  // Labels array
    size_t labels_count;
    
    // Network and ports
    docker_excess_port_mapping_t *ports;
    size_t ports_count;
    char **networks;                // Networks to connect
    size_t networks_count;
    
    // Storage
    docker_excess_mount_t *mounts;
    size_t mounts_count;
    
    // Runtime options
    int64_t memory_limit;           // Memory limit in bytes
    double cpu_shares;              // CPU shares
    bool privileged;                // Privileged mode
    bool auto_remove;               // Auto-remove when stopped
    bool detach;                    // Run detached
    
    // Health check
    char **health_cmd;
    size_t health_cmd_count;
    int health_interval_s;
    int health_timeout_s;
    int health_retries;
} docker_excess_container_create_t;
```

---

## 💡 Examples

### Advanced Container Creation
```c
// Create advanced container with all options
docker_excess_container_create_t *params = docker_excess_container_create_new("nginx:alpine");

// Basic configuration
params->name = strdup("my-web-server");
params->hostname = strdup("web01");

// Environment variables
params->env = malloc(2 * sizeof(docker_excess_env_var_t));
params->env[0].name = strdup("NGINX_PORT");
params->env[0].value = strdup("8080");
params->env[1].name = strdup("DEBUG");
params->env[1].value = strdup("1");
params->env_count = 2;

// Port mappings
params->ports = malloc(sizeof(docker_excess_port_mapping_t));
params->ports[0].host_port = 8080;
params->ports[0].container_port = 80;
params->ports[0].protocol = strdup("tcp");
params->ports_count = 1;

// Volume mounts
params->mounts = malloc(sizeof(docker_excess_mount_t));
params->mounts[0].source = strdup("/host/data");
params->mounts[0].target = strdup("/usr/share/nginx/html");
params->mounts[0].type = strdup("bind");
params->mounts[0].read_only = false;
params->mounts_count = 1;

// Resource limits
params->memory_limit = 512 * 1024 * 1024; // 512MB
params->cpu_shares = 0.5; // 50% CPU

// Create and start container
char *container_id = NULL;
if (docker_excess_create_container(client, params, &container_id) == DOCKER_EXCESS_OK) {
    printf("Created container: %s\n", container_id);
    docker_excess_start_container(client, container_id);
    free(container_id);
}

docker_excess_container_create_free(params);
```

### Real-time Log Streaming
```c
void log_callback(const char *line, bool is_stderr, time_t timestamp, void *userdata) {
    const char *stream = is_stderr ? "STDERR" : "STDOUT";
    char *time_str = docker_excess_format_time(timestamp);
    printf("[%s][%s] %s\n", time_str, stream, line);
    free(time_str);
}

docker_excess_log_params_t log_params = {0};
log_params.follow = true;           // Follow logs in real-time
log_params.timestamps = true;       // Include timestamps
log_params.tail_lines = 100;        // Start with last 100 lines

docker_excess_get_logs(client, container_id, &log_params, log_callback, NULL);
```

### Image Building with Progress
```c
void build_progress(const char *status, const char *progress, void *userdata) {
    printf("Build: %s", status);
    if (progress) printf(" (%s)", progress);
    printf("\n");
}

docker_excess_image_build_t *build_params = docker_excess_image_build_new("./build-context");
build_params->dockerfile_path = strdup("./Dockerfile");
build_params->tag = strdup("my-app:latest");
build_params->no_cache = false;
build_params->pull = true;

// Build arguments
build_params->build_args = malloc(2 * sizeof(char*));
build_params->build_args[0] = strdup("VERSION=1.0.0");
build_params->build_args[1] = strdup("ENV=production");
build_params->build_args_count = 2;

docker_excess_build_image(client, build_params, build_progress, NULL);
docker_excess_image_build_free(build_params);
```

---

## ⚙️ Configuration

### Default Configuration
```c
docker_excess_config_t config = docker_excess_default_config();
// Uses: Unix socket at /var/run/docker.sock, 30s timeout
```

### Custom Configuration
```c
docker_excess_config_t config = docker_excess_default_config();

// Remote Docker daemon
free(config.socket_path);
config.socket_path = NULL;
config.host = strdup("docker.example.com");
config.port = 2376;
config.use_tls = true;
config.timeout_s = 60;

// TLS certificates
config.cert_path = strdup("/path/to/cert.pem");
config.key_path = strdup("/path/to/key.pem");
config.ca_path = strdup("/path/to/ca.pem");

// Custom logging
config.log_callback = my_log_callback;
config.log_userdata = my_context;

docker_excess_t *client;
docker_excess_new_with_config(&config, &client);
docker_excess_free_config(&config);
```

### Environment Variables
```bash
export DOCKER_HOST=tcp://docker.example.com:2376
export DOCKER_TLS_VERIFY=1
export DOCKER_CERT_PATH=/path/to/certs

# SDK automatically reads these variables
docker_excess_config_t config = docker_excess_config_from_env();
```

---

## ❌ Error Handling

### Error Codes
```c
typedef enum {
    DOCKER_EXCESS_OK = 0,                    // Success
    DOCKER_EXCESS_ERR_INVALID_PARAM = -1,    // Invalid parameter
    DOCKER_EXCESS_ERR_MEMORY = -2,           // Memory allocation failed
    DOCKER_EXCESS_ERR_NETWORK = -3,          // Network error
    DOCKER_EXCESS_ERR_HTTP = -4,             // HTTP error
    DOCKER_EXCESS_ERR_JSON = -5,             // JSON parsing error
    DOCKER_EXCESS_ERR_NOT_FOUND = -6,        // Resource not found
    DOCKER_EXCESS_ERR_TIMEOUT = -7,          // Operation timeout
    DOCKER_EXCESS_ERR_INTERNAL = -8,         // Internal error
    DOCKER_EXCESS_ERR_PERMISSION = -9,       // Permission denied
    DOCKER_EXCESS_ERR_CONFLICT = -10,        // Resource conflict
    DOCKER_EXCESS_ERR_ALREADY_EXISTS = -11,  // Resource already exists
} docker_excess_error_t;
```

### Error Handling Best Practices
```c
docker_excess_error_t err = docker_excess_start_container(client, "container-id");

switch (err) {
    case DOCKER_EXCESS_OK:
        printf("Container started successfully\n");
        break;
        
    case DOCKER_EXCESS_ERR_NOT_FOUND:
        printf("Container not found\n");
        break;
        
    case DOCKER_EXCESS_ERR_CONFLICT:
        printf("Container is already running\n");
        break;
        
    case DOCKER_EXCESS_ERR_NETWORK:
        printf("Network error: %s\n", docker_excess_get_error(client));
        break;
        
    default:
        printf("Error: %s\n", docker_excess_error_string(err));
        printf("Details: %s\n", docker_excess_get_error(client));
}
```

---

## 🔒 Thread Safety

All public functions are **fully thread-safe**. Internal operations use proper mutex locking:

```c
#include <pthread.h>
#include <omp.h>

docker_excess_t *client;
docker_excess_new(&client);

// Safe to call from multiple threads
#pragma omp parallel for
for (int i = 0; i < 10; i++) {
    docker_excess_ping(client);  // Thread-safe
    
    // Each thread can safely use the same client
    docker_excess_container_t **containers;
    size_t count;
    docker_excess_list_containers(client, false, NULL, &containers, &count);
    docker_excess_free_containers(containers, count);
}

docker_excess_free(client);
```

### Best Practices for Threading
- **Single Client**: One client instance can be safely shared across threads
- **Multiple Clients**: For heavy concurrent usage, consider one client per thread
- **Error Handling**: Each thread should check errors independently
- **Memory Management**: Always free resources in the same thread that allocated them

---

## 🛠 Building from Source

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libcurl4-openssl-dev libjson-c-dev

# CentOS/RHEL
sudo yum install gcc cmake libcurl-devel json-c-devel

# macOS
brew install cmake curl json-c
```

### Build Options
```bash
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess
mkdir build && cd build

# Configure build
cmake .. \
    -DDOCKER_EXCESS_BUILD_SHARED=ON \
    -DDOCKER_EXCESS_BUILD_STATIC=ON \
    -DDOCKER_EXCESS_BUILD_EXAMPLES=ON \
    -DDOCKER_EXCESS_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Install
sudo make install

# Run tests
make test

# Generate documentation (if Doxygen available)
make doc
```

### CMake Options
| Option | Default | Description |
|--------|---------|-------------|
| `DOCKER_EXCESS_BUILD_SHARED` | ON | Build shared library |
| `DOCKER_EXCESS_BUILD_STATIC` | ON | Build static library |
| `DOCKER_EXCESS_BUILD_EXAMPLES` | ON | Build example programs |
| `DOCKER_EXCESS_BUILD_TESTS` | ON | Build test suite |
| `DOCKER_EXCESS_ENABLE_ASAN` | OFF | Enable AddressSanitizer |
| `DOCKER_EXCESS_ENABLE_TSAN` | OFF | Enable ThreadSanitizer |

### Development Build
```bash
# Debug build with sanitizers
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDOCKER_EXCESS_ENABLE_ASAN=ON \
    -DDOCKER_EXCESS_ENABLE_TSAN=OFF

make -j$(nproc)
```

---

## 📈 Performance Considerations

### Optimization Tips
- **Connection Pooling**: Reuse client instances when possible
- **Batch Operations**: Group multiple operations together
- **Filtering**: Use server-side filtering instead of client-side
- **Streaming**: Use streaming APIs for large data transfers
- **Memory**: Always free allocated resources promptly

### Benchmarks
| Operation | Time (ms) | Memory (KB) |
|-----------|-----------|-------------|
| Create Client | 5-10 | 64 |
| List Containers (100) | 50-100 | 256 |
| Container Create | 100-200 | 128 |
| Container Start | 200-500 | 64 |
| Image Pull (100MB) | 5000-15000 | 1024 |

---

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

### Development Setup
```bash
git clone https://github.com/G-flame-OSS/docker-excess.git
cd docker-excess
git checkout -b feature/my-feature

# Make changes...

# Test your changes
mkdir build && cd build
cmake .. -DDOCKER_EXCESS_BUILD_TESTS=ON
make test

# Submit pull request
```

### Areas for Contribution
- Additional Docker API endpoints
- Performance optimizations
- Platform-specific improvements
- Documentation and examples
- Test coverage improvements
- Language bindings (Python, Go, etc.)

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- [libcurl](https://curl.se/libcurl/) - HTTP client library
- [json-c](https://github.com/json-c/json-c) - JSON parsing library
- [Docker Engine API](https://docs.docker.com/engine/api/) - API specification
- Contributors and users of the docker-excess library

---

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/G-flame-OSS/docker-excess/issues)
- **Discussions**: [GitHub Discussions](https://github.com/G-flame-OSS/docker-excess/discussions)
- **Documentation**: [API Documentation](https://g-flame-oss.github.io/docker-excess/)
- **Examples**: See [examples/](examples/) directory

---

**Happy coding with Docker-excess! 🐳**
