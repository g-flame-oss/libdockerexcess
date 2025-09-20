# Docker-excess API Documentation

Complete reference for the docker-excess C library with working examples.

## Table of Contents

- [Core Functions](#core-functions)
- [Container Management](#container-management)
- [Image Management](#image-management)
- [File Operations](#file-operations)
- [Network Management](#network-management)
- [Volume Management](#volume-management)
- [Execution and Logs](#execution-and-logs)
- [Configuration](#configuration)
- [Utility Functions](#utility-functions)
- [Data Structures](#data-structures)

---

## Core Functions

### docker_excess_new()

Create a new Docker client with default configuration.

```c
docker_excess_error_t docker_excess_new(docker_excess_t **client);
```

**Parameters:**
- `client`: Pointer to store the created client

**Returns:** `DOCKER_EXCESS_OK` on success, error code on failure

**Example:**
```c
#include <docker-excess.h>

int main() {
    docker_excess_t *client;
    docker_excess_error_t err = docker_excess_new(&client);
    
    if (err != DOCKER_EXCESS_OK) {
        printf("Failed to create client: %s\n", docker_excess_error_string(err));
        return 1;
    }
    
    // Use client...
    
    docker_excess_free(client);
    return 0;
}
```

### docker_excess_new_with_config()

Create a Docker client with custom configuration.

```c
docker_excess_error_t docker_excess_new_with_config(
    const docker_excess_config_t *config, 
    docker_excess_t **client
);
```

**Example:**
```c
docker_excess_config_t config = docker_excess_default_config();
config.host = strdup("192.168.1.100");
config.port = 2376;
config.use_tls = true;
config.timeout_s = 60;

docker_excess_t *client;
if (docker_excess_new_with_config(&config, &client) == DOCKER_EXCESS_OK) {
    // Client is ready
    docker_excess_free(client);
}
docker_excess_free_config(&config);
```

### docker_excess_ping()

Test connection to Docker daemon.

```c
docker_excess_error_t docker_excess_ping(docker_excess_t *client);
```

**Example:**
```c
if (docker_excess_ping(client) == DOCKER_EXCESS_OK) {
    printf("Docker daemon is accessible\n");
} else {
    printf("Cannot reach Docker: %s\n", docker_excess_get_error(client));
}
```

### docker_excess_version()

Get Docker daemon version information.

```c
docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json);
```

**Example:**
```c
char *version_info;
if (docker_excess_version(client, &version_info) == DOCKER_EXCESS_OK) {
    printf("Docker version info:\n%s\n", version_info);
    free(version_info);
}
```

---

## Container Management

### docker_excess_list_containers()

List containers with optional filtering.

```c
docker_excess_error_t docker_excess_list_containers(
    docker_excess_t *client, 
    bool all,
    const char *filters,
    docker_excess_container_t ***containers, 
    size_t *count
);
```

**Parameters:**
- `all`: Include stopped containers
- `filters`: JSON filter string (optional)
- `containers`: Array of container pointers (output)
- `count`: Number of containers (output)

**Example:**
```c
// List all containers
docker_excess_container_t **containers;
size_t count;

if (docker_excess_list_containers(client, true, NULL, &containers, &count) == DOCKER_EXCESS_OK) {
    printf("Found %zu containers:\n", count);
    
    for (size_t i = 0; i < count; i++) {
        printf("  ID: %s\n", containers[i]->short_id);
        printf("  Name: %s\n", containers[i]->name ? containers[i]->name : "<none>");
        printf("  Image: %s\n", containers[i]->image);
        printf("  State: %d\n", containers[i]->state);
        printf("  Status: %s\n\n", containers[i]->status);
    }
    
    docker_excess_free_containers(containers, count);
}

// List only running containers with filter
const char *filters = "{\"status\":[\"running\"]}";
if (docker_excess_list_containers(client, false, filters, &containers, &count) == DOCKER_EXCESS_OK) {
    printf("Running containers: %zu\n", count);
    docker_excess_free_containers(containers, count);
}
```

### docker_excess_create_container()

Create a new container from parameters.

```c
docker_excess_error_t docker_excess_create_container(
    docker_excess_t *client,
    const docker_excess_container_create_t *params,
    char **container_id
);
```

**Example:**
```c
// Simple container creation
docker_excess_container_create_t *params = docker_excess_container_create_new("nginx:alpine");
params->name = strdup("my-nginx-server");

// Set port mapping
params->ports = malloc(sizeof(docker_excess_port_mapping_t));
params->ports[0] = (docker_excess_port_mapping_t){
    .host_port = 8080,
    .container_port = 80,
    .protocol = strdup("tcp"),
    .host_ip = NULL
};
params->ports_count = 1;

// Environment variables
params->env = malloc(2 * sizeof(docker_excess_env_var_t));
params->env[0] = (docker_excess_env_var_t){"NGINX_PORT", "80"};
params->env[1] = (docker_excess_env_var_t){"ENVIRONMENT", "production"};
params->env_count = 2;

char *container_id;
if (docker_excess_create_container(client, params, &container_id) == DOCKER_EXCESS_OK) {
    printf("Created container: %s\n", container_id);
    free(container_id);
} else {
    printf("Failed to create container: %s\n", docker_excess_get_error(client));
}

docker_excess_container_create_free(params);
```

### docker_excess_start_container()

Start a container by ID or name.

```c
docker_excess_error_t docker_excess_start_container(docker_excess_t *client, const char *container_id);
```

**Example:**
```c
if (docker_excess_start_container(client, "my-nginx-server") == DOCKER_EXCESS_OK) {
    printf("Container started successfully\n");
} else {
    printf("Failed to start container: %s\n", docker_excess_get_error(client));
}
```

### docker_excess_stop_container()

Stop a running container with timeout.

```c
docker_excess_error_t docker_excess_stop_container(
    docker_excess_t *client, 
    const char *container_id, 
    int timeout_s
);
```

**Example:**
```c
// Stop container with 10 second timeout
docker_excess_error_t err = docker_excess_stop_container(client, "my-nginx-server", 10);

switch (err) {
    case DOCKER_EXCESS_OK:
        printf("Container stopped successfully\n");
        break;
    case DOCKER_EXCESS_ERR_NOT_FOUND:
        printf("Container not found\n");
        break;
    case DOCKER_EXCESS_ERR_TIMEOUT:
        printf("Container stop timed out\n");
        break;
    default:
        printf("Error stopping container: %s\n", docker_excess_get_error(client));
}
```

### docker_excess_remove_container()

Remove a container.

```c
docker_excess_error_t docker_excess_remove_container(
    docker_excess_t *client, 
    const char *container_id, 
    bool force, 
    bool remove_volumes
);
```

**Example:**
```c
// Force remove container and its volumes
if (docker_excess_remove_container(client, "my-nginx-server", true, true) == DOCKER_EXCESS_OK) {
    printf("Container removed\n");
}
```

### docker_excess_inspect_container()

Get detailed information about a container.

```c
docker_excess_error_t docker_excess_inspect_container(
    docker_excess_t *client, 
    const char *container_id,
    docker_excess_container_t **container
);
```

**Example:**
```c
docker_excess_container_t *container;
if (docker_excess_inspect_container(client, "my-nginx-server", &container) == DOCKER_EXCESS_OK) {
    printf("Container Details:\n");
    printf("  ID: %s\n", container->id);
    printf("  Name: %s\n", container->name);
    printf("  Image: %s\n", container->image);
    printf("  State: %s\n", container->state == DOCKER_EXCESS_STATE_RUNNING ? "Running" : "Stopped");
    printf("  Created: %ld\n", container->created);
    
    if (container->labels_count > 0) {
        printf("  Labels:\n");
        for (size_t i = 0; i < container->labels_count; i++) {
            printf("    %s\n", container->labels[i]);
        }
    }
    
    docker_excess_free_containers(&container, 1);
}
```

---

## Image Management

### docker_excess_list_images()

List Docker images with filtering.

```c
docker_excess_error_t docker_excess_list_images(
    docker_excess_t *client, 
    bool all,
    const char *filters,
    docker_excess_image_t ***images, 
    size_t *count
);
```

**Example:**
```c
docker_excess_image_t **images;
size_t count;

if (docker_excess_list_images(client, false, NULL, &images, &count) == DOCKER_EXCESS_OK) {
    printf("Found %zu images:\n", count);
    
    for (size_t i = 0; i < count; i++) {
        printf("Image %zu:\n", i + 1);
        printf("  ID: %s\n", images[i]->short_id);
        printf("  Size: %ld bytes\n", images[i]->size);
        printf("  Created: %ld\n", images[i]->created);
        
        if (images[i]->repo_tags_count > 0) {
            printf("  Tags:\n");
            for (size_t j = 0; j < images[i]->repo_tags_count; j++) {
                printf("    %s\n", images[i]->repo_tags[j]);
            }
        }
        printf("\n");
    }
    
    docker_excess_free_images(images, count);
}
```

### docker_excess_pull_image()

Pull an image from registry with progress tracking.

```c
docker_excess_error_t docker_excess_pull_image(
    docker_excess_t *client, 
    const char *image_name, 
    const char *tag,
    docker_excess_progress_callback_t callback, 
    void *userdata
);
```

**Example:**
```c
void pull_progress(const char *status, const char *progress, void *userdata) {
    printf("Pull status: %s", status);
    if (progress) {
        printf(" - %s", progress);
    }
    printf("\n");
}

// Pull Ubuntu 20.04 image
if (docker_excess_pull_image(client, "ubuntu", "20.04", pull_progress, NULL) == DOCKER_EXCESS_OK) {
    printf("Image pulled successfully\n");
} else {
    printf("Failed to pull image: %s\n", docker_excess_get_error(client));
}
```

### docker_excess_build_image()

Build an image from Dockerfile.

```c
docker_excess_error_t docker_excess_build_image(
    docker_excess_t *client, 
    const docker_excess_image_build_t *params,
    docker_excess_progress_callback_t callback, 
    void *userdata
);
```

**Example:**
```c
void build_progress(const char *status, const char *progress, void *userdata) {
    printf("Build: %s\n", status);
}

docker_excess_image_build_t *build_params = docker_excess_image_build_new("./my-app");
build_params->dockerfile_path = strdup("Dockerfile");
build_params->tag = strdup("my-app:v1.0");
build_params->no_cache = false;
build_params->pull = true;

// Add build arguments
build_params->build_args = malloc(2 * sizeof(char*));
build_params->build_args[0] = strdup("VERSION=1.0.0");
build_params->build_args[1] = strdup("ENV=production");
build_params->build_args_count = 2;

if (docker_excess_build_image(client, build_params, build_progress, NULL) == DOCKER_EXCESS_OK) {
    printf("Image built successfully\n");
}

docker_excess_image_build_free(build_params);
```

### docker_excess_remove_image()

Remove an image from local storage.

```c
docker_excess_error_t docker_excess_remove_image(
    docker_excess_t *client, 
    const char *image_name, 
    bool force, 
    bool no_prune
);
```

**Example:**
```c
// Force remove image without pruning
if (docker_excess_remove_image(client, "my-app:v1.0", true, true) == DOCKER_EXCESS_OK) {
    printf("Image removed\n");
}
```

---

## File Operations

### docker_excess_list_files()

List files in a container directory.

```c
docker_excess_error_t docker_excess_list_files(
    docker_excess_t *client, 
    const char *container_id,
    const char *path, 
    bool recursive,
    docker_excess_file_t ***files, 
    size_t *count
);
```

**Example:**
```c
docker_excess_file_t **files;
size_t count;

if (docker_excess_list_files(client, "my-container", "/app", false, &files, &count) == DOCKER_EXCESS_OK) {
    printf("Files in /app:\n");
    
    for (size_t i = 0; i < count; i++) {
        char *type = files[i]->is_dir ? "DIR" : "FILE";
        printf("  %s: %s (%ld bytes)\n", type, files[i]->name, files[i]->size);
        
        if (files[i]->is_link) {
            printf("    -> %s\n", files[i]->link_target);
        }
    }
    
    docker_excess_free_files(files, count);
}
```

### docker_excess_read_file()

Read file content from container.

```c
docker_excess_error_t docker_excess_read_file(
    docker_excess_t *client, 
    const char *container_id,
    const char *file_path, 
    char **content, 
    size_t *size
);
```

**Example:**
```c
char *content;
size_t size;

if (docker_excess_read_file(client, "my-container", "/app/config.json", &content, &size) == DOCKER_EXCESS_OK) {
    printf("File content (%zu bytes):\n", size);
    printf("%.*s\n", (int)size, content);
    free(content);
} else {
    printf("Failed to read file: %s\n", docker_excess_get_error(client));
}
```

### docker_excess_write_file()

Write content to file in container.

```c
docker_excess_error_t docker_excess_write_file(
    docker_excess_t *client, 
    const char *container_id,
    const char *file_path, 
    const char *content, 
    size_t size, 
    uint32_t mode
);
```

**Example:**
```c
const char *config = "{\n  \"debug\": true,\n  \"port\": 3000\n}";

if (docker_excess_write_file(client, "my-container", "/app/config.json", 
                            config, strlen(config), 0644) == DOCKER_EXCESS_OK) {
    printf("File written successfully\n");
}
```

### docker_excess_copy_from_container()

Copy files from container to host.

```c
docker_excess_error_t docker_excess_copy_from_container(
    docker_excess_t *client, 
    const char *container_id,
    const char *container_path, 
    const char *host_path
);
```

**Example:**
```c
// Copy logs from container to host
if (docker_excess_copy_from_container(client, "my-app", "/var/log/app.log", "./app.log") == DOCKER_EXCESS_OK) {
    printf("Log file copied to host\n");
}
```

### docker_excess_copy_to_container()

Copy files from host to container.

```c
docker_excess_error_t docker_excess_copy_to_container(
    docker_excess_t *client, 
    const char *container_id,
    const char *host_path, 
    const char *container_path
);
```

**Example:**
```c
// Copy configuration file to container
if (docker_excess_copy_to_container(client, "my-app", "./config.yml", "/app/config.yml") == DOCKER_EXCESS_OK) {
    printf("Configuration uploaded\n");
}
```

---

## Execution and Logs

### docker_excess_exec_simple()

Execute command in container and capture output.

```c
docker_excess_error_t docker_excess_exec_simple(
    docker_excess_t *client, 
    const char *container_id,
    const char *command, 
    char **stdout_out, 
    char **stderr_out, 
    int *exit_code
);
```

**Example:**
```c
char *stdout_out, *stderr_out;
int exit_code;

if (docker_excess_exec_simple(client, "my-container", "ls -la /app", 
                             &stdout_out, &stderr_out, &exit_code) == DOCKER_EXCESS_OK) {
    printf("Command exit code: %d\n", exit_code);
    printf("STDOUT:\n%s\n", stdout_out);
    
    if (stderr_out && strlen(stderr_out) > 0) {
        printf("STDERR:\n%s\n", stderr_out);
    }
    
    free(stdout_out);
    free(stderr_out);
}
```

### docker_excess_exec()

Execute command with advanced parameters and streaming output.

```c
docker_excess_error_t docker_excess_exec(
    docker_excess_t *client, 
    const char *container_id,
    const docker_excess_exec_params_t *params,
    docker_excess_exec_callback_t callback, 
    void *userdata
);
```

**Example:**
```c
void exec_callback(const char *stdout_data, const char *stderr_data, void *userdata) {
    if (stdout_data) printf("OUT: %s", stdout_data);
    if (stderr_data) printf("ERR: %s", stderr_data);
}

docker_excess_exec_params_t exec_params = {0};
exec_params.cmd = malloc(3 * sizeof(char*));
exec_params.cmd[0] = "sh";
exec_params.cmd[1] = "-c";
exec_params.cmd[2] = "echo 'Hello from container' && sleep 1 && echo 'Done'";
exec_params.cmd_count = 3;
exec_params.tty = true;

docker_excess_exec(client, "my-container", &exec_params, exec_callback, NULL);
free(exec_params.cmd);
```

### docker_excess_get_logs()

Stream container logs with filtering.

```c
docker_excess_error_t docker_excess_get_logs(
    docker_excess_t *client, 
    const char *container_id,
    const docker_excess_log_params_t *params,
    docker_excess_log_callback_t callback, 
    void *userdata
);
```

**Example:**
```c
void log_callback(const char *line, bool is_stderr, time_t timestamp, void *userdata) {
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));
    
    printf("[%s][%s] %s\n", 
           time_str, 
           is_stderr ? "ERR" : "OUT", 
           line);
}

docker_excess_log_params_t log_params = {
    .follow = true,        // Follow logs in real-time
    .timestamps = true,    // Include timestamps
    .tail_lines = 50,      // Start with last 50 lines
    .since = time(NULL) - 3600  // Last hour only
};

// This will stream logs until interrupted
docker_excess_get_logs(client, "my-container", &log_params, log_callback, NULL);
```

---

## Network Management

### docker_excess_list_networks()

List Docker networks.

```c
docker_excess_error_t docker_excess_list_networks(
    docker_excess_t *client, 
    const char *filters,
    docker_excess_network_t ***networks, 
    size_t *count
);
```

**Example:**
```c
docker_excess_network_t **networks;
size_t count;

if (docker_excess_list_networks(client, NULL, &networks, &count) == DOCKER_EXCESS_OK) {
    printf("Networks:\n");
    
    for (size_t i = 0; i < count; i++) {
        printf("  %s (%s) - Driver: %s\n", 
               networks[i]->name, 
               networks[i]->id, 
               networks[i]->driver);
    }
    
    docker_excess_free_networks(networks, count);
}
```

### docker_excess_create_network()

Create a new Docker network.

```c
docker_excess_error_t docker_excess_create_network(
    docker_excess_t *client, 
    const char *name,
    const char *driver, 
    const char *options,
    char **network_id
);
```

**Example:**
```c
const char *network_options = "{"
    "\"IPAM\": {"
        "\"Config\": [{"
            "\"Subnet\": \"172.20.0.0/16\""
        "}]"
    "}"
"}";

char *network_id;
if (docker_excess_create_network(client, "my-app-network", "bridge", 
                                network_options, &network_id) == DOCKER_EXCESS_OK) {
    printf("Created network: %s\n", network_id);
    free(network_id);
}
```

### docker_excess_connect_network()

Connect container to network.

```c
docker_excess_error_t docker_excess_connect_network(
    docker_excess_t *client, 
    const char *network_id,
    const char *container_id, 
    const char *config
);
```

**Example:**
```c
const char *connect_config = "{"
    "\"IPAMConfig\": {"
        "\"IPv4Address\": \"172.20.0.10\""
    "}"
"}";

if (docker_excess_connect_network(client, "my-app-network", "my-container", 
                                 connect_config) == DOCKER_EXCESS_OK) {
    printf("Container connected to network\n");
}
```

---

## Volume Management

### docker_excess_list_volumes()

List Docker volumes.

```c
docker_excess_error_t docker_excess_list_volumes(
    docker_excess_t *client, 
    const char *filters,
    docker_excess_volume_t ***volumes, 
    size_t *count
);
```

**Example:**
```c
docker_excess_volume_t **volumes;
size_t count;

if (docker_excess_list_volumes(client, NULL, &volumes, &count) == DOCKER_EXCESS_OK) {
    printf("Volumes:\n");
    
    for (size_t i = 0; i < count; i++) {
        printf("  %s - Driver: %s, Mountpoint: %s\n", 
               volumes[i]->name, 
               volumes[i]->driver,
               volumes[i]->mountpoint);
    }
    
    docker_excess_free_volumes(volumes, count);
}
```

### docker_excess_create_volume()

Create a new Docker volume.

```c
docker_excess_error_t docker_excess_create_volume(
    docker_excess_t *client, 
    const char *name,
    const char *driver, 
    const char *options,
    char **volume_name
);
```

**Example:**
```c
const char *volume_options = "{"
    "\"Labels\": {"
        "\"purpose\": \"database\","
        "\"app\": \"myapp\""
    "}"
"}";

char *volume_name;
if (docker_excess_create_volume(client, "myapp-db-data", "local", 
                               volume_options, &volume_name) == DOCKER_EXCESS_OK) {
    printf("Created volume: %s\n", volume_name);
    free(volume_name);
}
```

---

## Configuration

### docker_excess_default_config()

Get default configuration structure.

```c
docker_excess_config_t docker_excess_default_config(void);
```

**Example:**
```c
docker_excess_config_t config = docker_excess_default_config();

// Customize configuration
config.timeout_s = 120;  // 2 minute timeout
config.debug = true;     // Enable debug output

// Create client with custom config
docker_excess_t *client;
docker_excess_new_with_config(&config, &client);

// Don't forget to cleanup
docker_excess_free_config(&config);
docker_excess_free(client);
```

### docker_excess_config_from_env()

Create configuration from environment variables.

```c
docker_excess_config_t docker_excess_config_from_env(void);
```

**Example:**
```c
// Set environment variables
setenv("DOCKER_HOST", "tcp://192.168.1.100:2376", 1);
setenv("DOCKER_TLS_VERIFY", "1", 1);
setenv("DOCKER_CERT_PATH", "/home/user/.docker", 1);

// Create config from environment
docker_excess_config_t config = docker_excess_config_from_env();

docker_excess_t *client;
docker_excess_new_with_config(&config, &client);

docker_excess_free_config(&config);
docker_excess_free(client);
```

### Custom Logging

```c
void my_logger(docker_excess_log_level_t level, const char *message, void *userdata) {
    const char *level_names[] = {"ERROR", "WARN", "INFO", "DEBUG"};
    FILE *logfile = (FILE*)userdata;
    
    fprintf(logfile, "[%s] %s\n", level_names[level], message);
    fflush(logfile);
}

int main() {
    FILE *logfile = fopen("/var/log/myapp.log", "a");
    
    docker_excess_config_t config = docker_excess_default_config();
    config.log_callback = my_logger;
    config.log_userdata = logfile;
    config.debug = true;
    
    docker_excess_t *client;
    docker_excess_new_with_config(&config, &client);
    
    // All docker operations will be logged to file
    docker_excess_ping(client);
    
    docker_excess_free(client);
    docker_excess_free_config(&config);
    fclose(logfile);
    return 0;
}
```

---

## Utility Functions

### Error Handling

```c
const char* docker_excess_error_string(docker_excess_error_t error);
const char* docker_excess_get_error(docker_excess_t *client);
void docker_excess_clear_error(docker_excess_t *client);
```

**Example:**
```c
docker_excess_error_t err = docker_excess_start_container(client, "nonexistent");

if (err != DOCKER_EXCESS_OK) {
    printf("Error code: %s\n", docker_excess_error_string(err));
    printf("Detailed message: %s\n", docker_excess_get_error(client));
    
    // Clear error for next operation
    docker_excess_clear_error(client);
}
```

### ID Utilities

```c
char* docker_excess_short_id(const char *full_id);
bool docker_excess_is_container_id(const char *str);
docker_excess_error_t docker_excess_resolve_container_id(
    docker_excess_t *client, 
    const char *name_or_id,
    char **full_id
);
```

**Example:**
```c
// Convert full ID to short ID
char *short_id = docker_excess_short_id("1234567890abcdef1234567890abcdef12345678");
printf("Short ID: %s\n", short_id);  // Prints: 1234567890ab
free(short_id);

// Check if string looks like container ID
if (docker_excess_is_container_id("1234567890ab")) {
    printf("This looks like a container ID\n");
}

// Resolve name to full ID
char *full_id;
if (docker_excess_resolve_container_id(client, "my-container", &full_id) == DOCKER_EXCESS_OK) {
    printf("Full container ID: %s\n", full_id);
    free(full_id);
}
```

### Formatting Utilities

```c
void docker_excess_format_bytes(int64_t bytes, char *buffer, size_t buffer_size);
char* docker_excess_format_time(time_t timestamp);
```

**Example:**
```c
// Format file sizes
char size_str[32];
docker_excess_format_bytes(1536000, size_str, sizeof(size_str));
printf("Size: %s\n", size_str);  // Prints: 1.5 MB

// Format timestamps
time_t now = time(NULL);
char *time_str = docker_excess_format_time(now);
printf("Current time: %s\n", time_str);
free(time_str);
```

### Memory Management

Always use the provided free functions:

```c
// Free container arrays
docker_excess_free_containers(containers, count);

// Free image arrays  
docker_excess_free_images(images, count);

// Free network arrays
docker_excess_free_networks(networks, count);

// Free volume arrays
docker_excess_free_volumes(volumes, count);

// Free file arrays
docker_excess_free_files(files, count);

// Free configuration
docker_excess_free_config(&config);

// Free parameter structures
docker_excess_container_create_free(create_params);
docker_excess_image_build_free(build_params);
```

---

## Data Structures

### docker_excess_container_t

Container information structure.

```c
typedef struct {
    char *id;                       // Full container ID
    char *short_id;                 // Short container ID (12 chars)
    char *name;                     // Container name
    char *image;                    // Image name
    char *image_id;                 // Image ID
    char *status;                   // Status string
    docker_excess_container_state_t state;  // Container state enum
    int64_t created;                // Creation timestamp
    int64_t started_at;             // Start timestamp
    int64_t finished_at;            // Finish timestamp
    int exit_code;                  // Exit code (if exited)
    docker_excess_port_mapping_t *ports;    // Port mappings
    size_t ports_count;
    docker_excess_mount_t *mounts;  // Volume mounts
    size_t mounts_count;
    char **labels;                  // Labels array
    size_t labels_count;
} docker_excess_container_t;
```

### docker_excess_container_create_t

Parameters for creating containers.

```c
typedef struct {
    char *name;                     // Container name (optional)
    char *image;                    // Image name (required)
    char **cmd;                     // Command array
    size_t cmd_count;
    char **entrypoint;              // Entrypoint override
    size_t entrypoint_count;
    char *working_dir;              // Working directory
    char *user;                     // User to run as
    
    // Environment and labels
    docker_excess_env_var_t *env;   // Environment variables
    size_t env_count;
    char **labels;                  // Labels (key=value)
    size_t labels_count;
    
    // Network and ports
    docker_excess_port_mapping_t *ports;
    size_t ports_count;
    char **networks;                // Networks to connect
    size_t networks_count;
    char *hostname;                 // Container hostname
    char *domain_name;              // Container domain name
    
    // Storage
    docker_excess_mount_t *mounts;
    size_t mounts_count;
    char **volumes_from;            // Volumes from other containers
    size_t volumes_from_count;
    
    // Runtime options
    int64_t memory_limit;           // Memory limit in bytes
    int64_t memory_swap;            // Memory + swap limit
    double cpu_shares;              // CPU shares
    char *cpu_set;                  // CPU set
    bool privileged;                // Privileged mode
    char **cap_add;                 // Linux capabilities to add
    size_t cap_add_count;
    char **cap_drop;                // Linux capabilities to drop
    size_t cap_drop_count;
    
    // Behavior
    bool auto_remove;               // Auto-remove when stopped
    bool interactive;               // Keep STDIN open
    bool tty;                       // Allocate pseudo-TTY
    bool detach;                    // Run detached
    char *restart_policy;           // Restart policy
    int restart_max_retries;        // Max restart retries
    
    // Health check
    char **health_cmd;              // Health check command
    size_t health_cmd_count;
    int health_interval_s;          // Health check interval
    int health_timeout_s;           // Health check timeout
    int health_retries;             // Health check retries
    int health_start_period_s;      // Health check start period
} docker_excess_container_create_t;
```

**Example:**
```c
docker_excess_container_create_t *params = docker_excess_container_create_new("postgres:13");
params->name = strdup("my-database");

// Set environment variables
params->env = malloc(3 * sizeof(docker_excess_env_var_t));
params->env[0] = (docker_excess_env_var_t){"POSTGRES_PASSWORD", "secretpassword"};
params->env[1] = (docker_excess_env_var_t){"POSTGRES_DB", "myappdb"};
params->env[2] = (docker_excess_env_var_t){"POSTGRES_USER", "appuser"};
params->env_count = 3;

// Configure ports
params->ports = malloc(sizeof(docker_excess_port_mapping_t));
params->ports[0] = (docker_excess_port_mapping_t){5432, 5432, "tcp", "127.0.0.1"};
params->ports_count = 1;

// Add volume mount
params->mounts = malloc(sizeof(docker_excess_mount_t));
params->mounts[0] = (docker_excess_mount_t){
    .source = "postgres-data",
    .target = "/var/lib/postgresql/data",
    .type = "volume",
    .read_only = false
};
params->mounts_count = 1;

// Resource limits
params->memory_limit = 512 * 1024 * 1024;  // 512MB
params->cpu_shares = 1.0;  // 100% CPU

// Health check
params->health_cmd = malloc(4 * sizeof(char*));
params->health_cmd[0] = "pg_isready";
params->health_cmd[1] = "-U";
params->health_cmd[2] = "appuser";
params->health_cmd[3] = "-d";
params->health_cmd[4] = "myappdb";
params->health_cmd_count = 5;
params->health_interval_s = 30;
params->health_timeout_s = 10;
params->health_retries = 3;

// Create container
char *container_id;
docker_excess_create_container(client, params, &container_id);
docker_excess_container_create_free(params);
```

### docker_excess_image_t

Image information structure.

```c
typedef struct {
    char *id;                       // Full image ID
    char *short_id;                 // Short image ID
    char **repo_tags;               // Repository tags
    size_t repo_tags_count;
    char **repo_digests;            // Repository digests
    size_t repo_digests_count;
    int64_t created;                // Creation timestamp
    int64_t size;                   // Size in bytes
    int64_t virtual_size;           // Virtual size in bytes
    char **labels;                  // Labels array
    size_t labels_count;
} docker_excess_image_t;
```

### docker_excess_config_t

Client configuration structure.

```c
typedef struct {
    char *socket_path;              // Docker socket path
    char *host;                     // Docker host for TCP
    uint16_t port;                  // Docker port for TCP
    bool use_tls;                   // Use TLS for remote connections
    char *cert_path;                // TLS certificate path
    char *key_path;                 // TLS key path
    char *ca_path;                  // TLS CA path
    int timeout_s;                  // Request timeout in seconds
    bool debug;                     // Enable debug logging
    void (*log_callback)(docker_excess_log_level_t level, const char *message, void *userdata);
    void *log_userdata;             // User data for log callback
} docker_excess_config_t;
```

---

## Complete Examples

### Container Lifecycle Management

```c
#include <docker-excess.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        printf("Failed to create Docker client\n");
        return 1;
    }

    // Test connection
    if (docker_excess_ping(client) != DOCKER_EXCESS_OK) {
        printf("Cannot connect to Docker: %s\n", docker_excess_get_error(client));
        docker_excess_free(client);
        return 1;
    }

    printf("✓ Connected to Docker daemon\n");

    // Create a new container
    docker_excess_container_create_t *params = docker_excess_container_create_new("nginx:alpine");
    params->name = strdup("test-nginx");
    
    // Configure port mapping
    params->ports = malloc(sizeof(docker_excess_port_mapping_t));
    params->ports[0] = (docker_excess_port_mapping_t){8080, 80, "tcp", NULL};
    params->ports_count = 1;

    char *container_id;
    if (docker_excess_create_container(client, params, &container_id) == DOCKER_EXCESS_OK) {
        printf("✓ Created container: %s\n", container_id);

        // Start the container
        if (docker_excess_start_container(client, container_id) == DOCKER_EXCESS_OK) {
            printf("✓ Started container\n");

            // Wait a moment then stop it
            sleep(2);
            
            if (docker_excess_stop_container(client, container_id, 10) == DOCKER_EXCESS_OK) {
                printf("✓ Stopped container\n");

                // Remove the container
                if (docker_excess_remove_container(client, container_id, false, false) == DOCKER_EXCESS_OK) {
                    printf("✓ Removed container\n");
                }
            }
        }

        free(container_id);
    } else {
        printf("Failed to create container: %s\n", docker_excess_get_error(client));
    }

    docker_excess_container_create_free(params);
    docker_excess_free(client);
    return 0;
}
```

### Log Monitoring Service

```c
#include <docker-excess.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

static volatile bool running = true;

void signal_handler(int sig) {
    running = false;
}

void log_handler(const char *line, bool is_stderr, time_t timestamp, void *userdata) {
    const char *stream = is_stderr ? "STDERR" : "STDOUT";
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&timestamp));
    
    printf("[%s][%s] %s\n", time_str, stream, line);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <container_name_or_id>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        printf("Failed to create Docker client\n");
        return 1;
    }

    printf("Monitoring logs for container: %s\n", argv[1]);
    printf("Press Ctrl+C to stop...\n\n");

    docker_excess_log_params_t log_params = {
        .follow = true,
        .timestamps = true,
        .tail_lines = 10
    };

    // This will block and stream logs until interrupted
    docker_excess_get_logs(client, argv[1], &log_params, log_handler, NULL);

    docker_excess_free(client);
    return 0;
}
```

### Container Health Checker

```c
#include <docker-excess.h>
#include <stdio.h>
#include <unistd.h>

void check_container_health(docker_excess_t *client, const char *container_name) {
    docker_excess_container_t *container;
    
    if (docker_excess_inspect_container(client, container_name, &container) == DOCKER_EXCESS_OK) {
        printf("Container: %s\n", container->name);
        printf("  Status: %s\n", container->status);
        
        switch (container->state) {
            case DOCKER_EXCESS_STATE_RUNNING:
                printf("  Health: HEALTHY\n");
                break;
            case DOCKER_EXCESS_STATE_EXITED:
                printf("  Health: EXITED (code: %d)\n", container->exit_code);
                // Restart if needed
                docker_excess_start_container(client, container_name);
                break;
            case DOCKER_EXCESS_STATE_DEAD:
                printf("  Health: DEAD - Manual intervention required\n");
                break;
            default:
                printf("  Health: %d\n", container->state);
        }
        
        docker_excess_free_containers(&container, 1);
    } else {
        printf("Failed to inspect container %s: %s\n", 
               container_name, docker_excess_get_error(client));
    }
}

int main() {
    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        return 1;
    }

    const char *containers[] = {"web-server", "database", "cache"};
    const size_t container_count = sizeof(containers) / sizeof(containers[0]);

    while (1) {
        printf("=== Health Check ===\n");
        
        for (size_t i = 0; i < container_count; i++) {
            check_container_health(client, containers[i]);
        }
        
        printf("\n");
        sleep(60);  // Check every minute
    }

    docker_excess_free(client);
    return 0;
}
```

### Image Build and Deploy

```c
#include <docker-excess.h>
#include <stdio.h>

void build_progress(const char *status, const char *progress, void *userdata) {
    printf("Build: %s", status);
    if (progress && strlen(progress) > 0) {
        printf(" (%s)", progress);
    }
    printf("\n");
}

int deploy_application(docker_excess_t *client, const char *app_name, const char *version) {
    printf("Building application: %s:%s\n", app_name, version);

    // Build image
    docker_excess_image_build_t *build_params = docker_excess_image_build_new("./app");
    build_params->dockerfile_path = strdup("Dockerfile");
    
    char tag[256];
    snprintf(tag, sizeof(tag), "%s:%s", app_name, version);
    build_params->tag = strdup(tag);
    
    // Add build args
    build_params->build_args = malloc(2 * sizeof(char*));
    build_params->build_args[0] = strdup("VERSION=" + version);
    build_params->build_args[1] = strdup("BUILD_DATE=" + time(NULL));
    build_params->build_args_count = 2;

    if (docker_excess_build_image(client, build_params, build_progress, NULL) != DOCKER_EXCESS_OK) {
        printf("Build failed: %s\n", docker_excess_get_error(client));
        docker_excess_image_build_free(build_params);
        return 1;
    }

    printf("✓ Build completed\n");

    // Stop existing container
    docker_excess_stop_container(client, app_name, 10);
    docker_excess_remove_container(client, app_name, true, false);

    // Deploy new container
    docker_excess_container_create_t *deploy_params = docker_excess_container_create_new(tag);
    deploy_params->name = strdup(app_name);
    deploy_params->auto_remove = false;
    deploy_params->detach = true;

    // Port mapping
    deploy_params->ports = malloc(sizeof(docker_excess_port_mapping_t));
    deploy_params->ports[0] = (docker_excess_port_mapping_t){80, 8080, "tcp", NULL};
    deploy_params->ports_count = 1;

    char *container_id;
    if (docker_excess_create_container(client, deploy_params, &container_id) == DOCKER_EXCESS_OK) {
        if (docker_excess_start_container(client, container_id) == DOCKER_EXCESS_OK) {
            printf("✓ Application deployed successfully\n");
        }
        free(container_id);
    }

    docker_excess_container_create_free(deploy_params);
    docker_excess_image_build_free(build_params);
    return 0;
}

int main() {
    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        return 1;
    }

    deploy_application(client, "myapp", "v1.2.3");

    docker_excess_free(client);
    return 0;
}
```

### Batch File Operations

```c
#include <docker-excess.h>
#include <stdio.h>
#include <string.h>

int backup_container_files(docker_excess_t *client, const char *container_id, const char *backup_dir) {
    // Create backup directory
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", backup_dir);
    system(mkdir_cmd);

    // List important directories to backup
    const char *backup_paths[] = {"/etc", "/var/log", "/opt/app/config"};
    const size_t path_count = sizeof(backup_paths) / sizeof(backup_paths[0]);

    for (size_t i = 0; i < path_count; i++) {
        printf("Backing up %s...\n", backup_paths[i]);

        docker_excess_file_t **files;
        size_t count;
        
        if (docker_excess_list_files(client, container_id, backup_paths[i], true, &files, &count) == DOCKER_EXCESS_OK) {
            printf("  Found %zu files\n", count);

            for (size_t j = 0; j < count; j++) {
                if (!files[j]->is_dir) {
                    // Create host path
                    char host_path[1024];
                    snprintf(host_path, sizeof(host_path), "%s%s", backup_dir, files[j]->full_path);

                    // Copy file from container
                    if (docker_excess_copy_from_container(client, container_id, 
                                                         files[j]->full_path, host_path) == DOCKER_EXCESS_OK) {
                        printf("    ✓ %s\n", files[j]->name);
                    } else {
                        printf("    ✗ Failed to backup %s\n", files[j]->name);
                    }
                }
            }

            docker_excess_free_files(files, count);
        } else {
            printf("  Failed to list files in %s\n", backup_paths[i]);
        }
    }

    printf("Backup completed to %s\n", backup_dir);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <container_id> <backup_directory>\n", argv[0]);
        return 1;
    }

    docker_excess_t *client;
    if (docker_excess_new(&client) != DOCKER_EXCESS_OK) {
        return 1;
    }

    backup_container_files(client, argv[1], argv[2]);

    docker_excess_free(client);
    return 0;
}
```

## Error Handling Best Practices

### Comprehensive Error Checking

```c
docker_excess_error_t handle_container_operation(docker_excess_t *client, const char *container_name) {
    docker_excess_error_t err;
    
    // Try to start container
    err = docker_excess_start_container(client, container_name);
    
    switch (err) {
        case DOCKER_EXCESS_OK:
            printf("Container started successfully\n");
            return DOCKER_EXCESS_OK;
            
        case DOCKER_EXCESS_ERR_NOT_FOUND:
            printf("Container '%s' not found, creating it...\n", container_name);
            
            // Create container
            docker_excess_container_create_t *params = docker_excess_container_create_new("nginx:alpine");
            params->name = strdup(container_name);
            
            char *container_id;
            err = docker_excess_create_container(client, params, &container_id);
            docker_excess_container_create_free(params);
            
            if (err == DOCKER_EXCESS_OK) {
                err = docker_excess_start_container(client, container_id);
                free(container_id);
            }
            break;
            
        case DOCKER_EXCESS_ERR_CONFLICT:
            printf("Container is already running\n");
            return DOCKER_EXCESS_OK;
            
        case DOCKER_EXCESS_ERR_PERMISSION:
            printf("Permission denied. Are you in the docker group?\n");
            break;
            
        case DOCKER_EXCESS_ERR_NETWORK:
            printf("Network error: %s\n", docker_excess_get_error(client));
            break;
            
        case DOCKER_EXCESS_ERR_TIMEOUT:
            printf("Operation timed out\n");
            break;
            
        default:
            printf("Unexpected error (%d): %s\n", err, docker_excess_get_error(client));
    }
    
    return err;
}
```

### Retry Logic

```c
docker_excess_error_t retry_operation(docker_excess_t *client, 
                                     docker_excess_error_t (*operation)(docker_excess_t*),
                                     int max_retries) {
    docker_excess_error_t err;
    int attempts = 0;
    
    do {
        err = operation(client);
        
        if (err == DOCKER_EXCESS_OK) {
            return DOCKER_EXCESS_OK;
        }
        
        // Don't retry certain errors
        if (err == DOCKER_EXCESS_ERR_INVALID_PARAM || 
            err == DOCKER_EXCESS_ERR_PERMISSION ||
            err == DOCKER_EXCESS_ERR_NOT_FOUND) {
            break;
        }
        
        attempts++;
        if (attempts < max_retries) {
            printf("Operation failed, retrying (%d/%d)...\n", attempts, max_retries);
            sleep(1);
        }
        
    } while (attempts < max_retries);
    
    return err;
}
```
