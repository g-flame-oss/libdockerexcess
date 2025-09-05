#ifndef DOCKER_EXCESS_H
#define DOCKER_EXCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ----------------- Core Types & Constants ----------------- */

#define DOCKER_EXCESS_DEFAULT_SOCKET "/var/run/docker.sock"
#define DOCKER_EXCESS_DEFAULT_TIMEOUT_S 30
#define DOCKER_EXCESS_API_VERSION "1.41"

typedef struct docker_excess_t docker_excess_t;

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

/* Container states */
typedef enum {
    DOCKER_EXCESS_STATE_CREATED,
    DOCKER_EXCESS_STATE_RESTARTING,
    DOCKER_EXCESS_STATE_RUNNING,
    DOCKER_EXCESS_STATE_REMOVING,
    DOCKER_EXCESS_STATE_PAUSED,
    DOCKER_EXCESS_STATE_EXITED,
    DOCKER_EXCESS_STATE_DEAD
} docker_excess_container_state_t;

/* Configuration structure */
typedef struct {
    char *socket_path;              /* Docker socket path (default: /var/run/docker.sock) */
    char *host;                     /* Docker host for TCP (optional) */
    uint16_t port;                  /* Docker port for TCP (default: 2376) */
    bool use_tls;                   /* Use TLS for remote connections */
    char *cert_path;                /* TLS certificate path */
    char *key_path;                 /* TLS key path */
    char *ca_path;                  /* TLS CA path */
    int timeout_s;                  /* Request timeout in seconds */
    bool debug;                     /* Enable debug logging */
} docker_excess_config_t;

/* Container information */
typedef struct {
    char *id;                       /* Container ID */
    char *name;                     /* Container name */
    char *image;                    /* Image name */
    char *status;                   /* Status string */
    docker_excess_container_state_t state; /* Parsed state */
    int64_t created;                /* Creation timestamp */
    char **ports;                   /* Port mappings (array of strings) */
    size_t ports_count;
} docker_excess_container_t;

/* Image information */
typedef struct {
    char *id;                       /* Image ID */
    char **repo_tags;               /* Repository tags */
    size_t repo_tags_count;
    int64_t created;                /* Creation timestamp */
    int64_t size;                   /* Size in bytes */
} docker_excess_image_t;

/* Network information */
typedef struct {
    char *id;                       /* Network ID */
    char *name;                     /* Network name */
    char *driver;                   /* Network driver */
    char *scope;                    /* Network scope */
    int64_t created;                /* Creation timestamp */
} docker_excess_network_t;

/* Volume information */
typedef struct {
    char *name;                     /* Volume name */
    char *driver;                   /* Volume driver */
    char *mountpoint;               /* Mount point */
    int64_t created;                /* Creation timestamp */
} docker_excess_volume_t;

/* File information */
typedef struct {
    char *name;                     /* File/directory name */
    int64_t size;                   /* File size */
    int64_t modified;               /* Last modified timestamp */
    bool is_dir;                    /* Is directory */
    uint32_t mode;                  /* File permissions */
} docker_excess_file_t;

/* Container creation parameters */
typedef struct {
    char *name;                     /* Container name (optional) */
    char *image;                    /* Image name (required) */
    char **cmd;                     /* Command to run */
    size_t cmd_count;
    char **env;                     /* Environment variables (KEY=VALUE) */
    size_t env_count;
    char **ports;                   /* Port mappings (HOST:CONTAINER) */
    size_t ports_count;
    char **volumes;                 /* Volume mounts (HOST:CONTAINER) */
    size_t volumes_count;
    char *working_dir;              /* Working directory */
    bool auto_remove;               /* Auto-remove when stopped */
    bool interactive;               /* Keep STDIN open */
    bool tty;                       /* Allocate pseudo-TTY */
} docker_excess_container_create_t;

/* Callback types */
typedef void (*docker_excess_log_callback_t)(const char *line, void *userdata);
typedef void (*docker_excess_exec_callback_t)(const char *stdout_data, const char *stderr_data, void *userdata);

/* ----------------- Core Functions ----------------- */

/* Create Docker client with default config */
docker_excess_error_t docker_excess_new(docker_excess_t **client);

/* Create Docker client with custom config */
docker_excess_error_t docker_excess_new_with_config(const docker_excess_config_t *config, docker_excess_t **client);

/* Free Docker client */
void docker_excess_free(docker_excess_t *client);

/* Get last error message (thread-safe) */
const char* docker_excess_get_error(docker_excess_t *client);

/* Test connection to Docker daemon */
docker_excess_error_t docker_excess_ping(docker_excess_t *client);

/* Get Docker version information (returns JSON string - caller must free) */
docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json);

/* ----------------- Container Management ----------------- */

/* List all containers (if all=true) or just running ones */
docker_excess_error_t docker_excess_list_containers(docker_excess_t *client, bool all,
                                                   docker_excess_container_t ***containers, size_t *count);

/* Create container from parameters */
docker_excess_error_t docker_excess_create_container(docker_excess_t *client,
                                                    const docker_excess_container_create_t *params,
                                                    char **container_id);

/* Start container by ID or name */
docker_excess_error_t docker_excess_start_container(docker_excess_t *client, const char *container_id);

/* Stop container with optional timeout */
docker_excess_error_t docker_excess_stop_container(docker_excess_t *client, const char *container_id, int timeout_s);

/* Restart container */
docker_excess_error_t docker_excess_restart_container(docker_excess_t *client, const char *container_id);

/* Remove container (force=true to remove running containers) */
docker_excess_error_t docker_excess_remove_container(docker_excess_t *client, const char *container_id, bool force);

/* Get container logs (callback called for each line) */
docker_excess_error_t docker_excess_get_logs(docker_excess_t *client, const char *container_id,
                                            bool follow, bool timestamps, int tail_lines,
                                            docker_excess_log_callback_t callback, void *userdata);

/* Execute command in container */
docker_excess_error_t docker_excess_exec(docker_excess_t *client, const char *container_id,
                                        const char **command, size_t cmd_count,
                                        docker_excess_exec_callback_t callback, void *userdata);

/* Execute command and capture output (simpler version) */
docker_excess_error_t docker_excess_exec_simple(docker_excess_t *client, const char *container_id,
                                               const char *command, char **stdout_out, char **stderr_out, int *exit_code);

/* Pause/unpause container */
docker_excess_error_t docker_excess_pause_container(docker_excess_t *client, const char *container_id);
docker_excess_error_t docker_excess_unpause_container(docker_excess_t *client, const char *container_id);

/* Wait for container to stop (returns exit code) */
docker_excess_error_t docker_excess_wait_container(docker_excess_t *client, const char *container_id, int *exit_code);

/* ----------------- File Operations ----------------- */

/* List files/directories in container path */
docker_excess_error_t docker_excess_list_files(docker_excess_t *client, const char *container_id,
                                              const char *path, docker_excess_file_t ***files, size_t *count);

/* Copy file from container to host */
docker_excess_error_t docker_excess_copy_from_container(docker_excess_t *client, const char *container_id,
                                                       const char *container_path, const char *host_path);

/* Copy file from host to container */
docker_excess_error_t docker_excess_copy_to_container(docker_excess_t *client, const char *container_id,
                                                     const char *host_path, const char *container_path);

/* Read file content from container (returns allocated buffer - caller must free) */
docker_excess_error_t docker_excess_read_file(docker_excess_t *client, const char *container_id,
                                             const char *file_path, char **content, size_t *size);

/* Write content to file in container */
docker_excess_error_t docker_excess_write_file(docker_excess_t *client, const char *container_id,
                                              const char *file_path, const char *content, size_t size);

/* Create directory in container */
docker_excess_error_t docker_excess_mkdir(docker_excess_t *client, const char *container_id,
                                         const char *dir_path, uint32_t mode);

/* Remove file/directory in container */
docker_excess_error_t docker_excess_remove_file(docker_excess_t *client, const char *container_id,
                                               const char *path, bool recursive);

/* ----------------- Image Management ----------------- */

/* List images */
docker_excess_error_t docker_excess_list_images(docker_excess_t *client, bool all,
                                               docker_excess_image_t ***images, size_t *count);

/* Pull image (blocking call) */
docker_excess_error_t docker_excess_pull_image(docker_excess_t *client, const char *image_name, const char *tag);

/* Remove image */
docker_excess_error_t docker_excess_remove_image(docker_excess_t *client, const char *image_name, bool force);

/* Build image from Dockerfile (basic version) */
docker_excess_error_t docker_excess_build_image(docker_excess_t *client, const char *dockerfile_path,
                                               const char *context_path, const char *tag);

/* ----------------- Network Management ----------------- */

/* List networks */
docker_excess_error_t docker_excess_list_networks(docker_excess_t *client,
                                                 docker_excess_network_t ***networks, size_t *count);

/* Create network */
docker_excess_error_t docker_excess_create_network(docker_excess_t *client, const char *name,
                                                  const char *driver, char **network_id);

/* Remove network */
docker_excess_error_t docker_excess_remove_network(docker_excess_t *client, const char *network_id);

/* Connect container to network */
docker_excess_error_t docker_excess_connect_network(docker_excess_t *client, const char *network_id,
                                                   const char *container_id);

/* Disconnect container from network */
docker_excess_error_t docker_excess_disconnect_network(docker_excess_t *client, const char *network_id,
                                                      const char *container_id);

/* ----------------- Volume Management ----------------- */

/* List volumes */
docker_excess_error_t docker_excess_list_volumes(docker_excess_t *client,
                                                docker_excess_volume_t ***volumes, size_t *count);

/* Create volume */
docker_excess_error_t docker_excess_create_volume(docker_excess_t *client, const char *name,
                                                 const char *driver, char **volume_name);

/* Remove volume */
docker_excess_error_t docker_excess_remove_volume(docker_excess_t *client, const char *volume_name, bool force);

/* ----------------- Raw API Access ----------------- */

/* Make raw HTTP request to Docker API 
 * method: GET, POST, PUT, DELETE
 * endpoint: e.g., "/containers/json", "/images/create"
 * body: request body (can be NULL)
 * response: allocated response body (caller must free)
 */
docker_excess_error_t docker_excess_raw_request(docker_excess_t *client, const char *method,
                                               const char *endpoint, const char *body,
                                               char **response, int *http_code);

/* ----------------- Utility Functions ----------------- */

/* Free container info array */
void docker_excess_free_containers(docker_excess_container_t **containers, size_t count);

/* Free image info array */
void docker_excess_free_images(docker_excess_image_t **images, size_t count);

/* Free network info array */
void docker_excess_free_networks(docker_excess_network_t **networks, size_t count);

/* Free volume info array */
void docker_excess_free_volumes(docker_excess_volume_t **volumes, size_t count);

/* Free file info array */
void docker_excess_free_files(docker_excess_file_t **files, size_t count);

/* Create default config */
docker_excess_config_t docker_excess_default_config(void);

/* Create config from environment variables */
docker_excess_config_t docker_excess_config_from_env(void);

/* Get error string for error code */
const char* docker_excess_error_string(docker_excess_error_t error);

/* Format bytes to human readable string */
void docker_excess_format_bytes(int64_t bytes, char *buffer, size_t buffer_size);

/* Parse container ID from name (resolves short IDs and names to full IDs) */
docker_excess_error_t docker_excess_resolve_container_id(docker_excess_t *client, const char *name_or_id,
                                                        char **full_id);

#ifdef __cplusplus
}
#endif

#endif /* DOCKER_EXCESS_H */
