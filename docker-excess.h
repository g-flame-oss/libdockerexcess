#ifndef DOCKER_EXCESS_H
#define DOCKER_EXCESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standard includes */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#ifdef DOCKER_SDK_WITH_CURL
#include <curl/curl.h>
#endif

#ifdef DOCKER_SDK_WITH_JSON_C
#include <json-c/json.h>
#endif

/* ----------------- Configuration & Defaults ----------------- */

#ifndef DOCKER_SDK_API_VERSION
#define DOCKER_SDK_API_VERSION "1.41"
#endif

#ifndef DOCKER_SDK_DEFAULT_SOCKET
#ifdef _WIN32
#define DOCKER_SDK_DEFAULT_SOCKET "//./pipe/docker_engine"
#else
#define DOCKER_SDK_DEFAULT_SOCKET "/var/run/docker.sock"
#endif
#endif

#ifndef DOCKER_SDK_DEFAULT_TIMEOUT_S
#define DOCKER_SDK_DEFAULT_TIMEOUT_S 60L
#endif

#ifndef DOCKER_SDK_BUFFER_SIZE
#define DOCKER_SDK_BUFFER_SIZE 8192
#endif

#ifndef DOCKER_SDK_MAX_RESPONSE_SIZE
#define DOCKER_SDK_MAX_RESPONSE_SIZE (64 * 1024 * 1024)  // 64MB
#endif

/* ----------------- Types & Constants ----------------- */

/* Forward declarations */
typedef struct docker_client docker_client_t;
typedef struct docker_config docker_config_t;
typedef struct docker_container_config docker_container_config_t;
typedef struct docker_exec_config docker_exec_config_t;
typedef struct docker_image_config docker_image_config_t;
typedef struct docker_network_config docker_network_config_t;
typedef struct docker_volume_config docker_volume_config_t;

/* Error codes */
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

/* Container states */
typedef enum {
    DOCKER_CONTAINER_STATE_CREATED,
    DOCKER_CONTAINER_STATE_RESTARTING,
    DOCKER_CONTAINER_STATE_RUNNING,
    DOCKER_CONTAINER_STATE_REMOVING,
    DOCKER_CONTAINER_STATE_PAUSED,
    DOCKER_CONTAINER_STATE_EXITED,
    DOCKER_CONTAINER_STATE_DEAD
} docker_container_state_t;

/* Image pull/build status */
typedef enum {
    DOCKER_IMAGE_STATUS_DOWNLOADING,
    DOCKER_IMAGE_STATUS_EXTRACTING,
    DOCKER_IMAGE_STATUS_COMPLETE,
    DOCKER_IMAGE_STATUS_ERROR
} docker_image_status_t;

/* Log levels */
typedef enum {
    DOCKER_LOG_ERROR = 0,
    DOCKER_LOG_WARN = 1,
    DOCKER_LOG_INFO = 2,
    DOCKER_LOG_DEBUG = 3
} docker_log_level_t;

/* Buffer for responses */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} docker_buffer_t;

/* Resource limits */
typedef struct {
    int64_t memory_limit;          /* bytes, 0 = unlimited */
    int64_t memory_swap_limit;     /* bytes, -1 = no swap limit */
    int64_t cpu_shares;            /* relative weight, 0 = default */
    double cpu_quota;              /* CPU quota, 0.0 = unlimited */
    char *cpuset_cpus;             /* "0-3" or "0,1,2,3" */
    char *cpuset_mems;             /* NUMA memory nodes */
    int64_t blkio_weight;          /* block IO weight */
    int64_t pids_limit;            /* max processes */
    bool oom_kill_disable;         /* disable OOM killer */
} docker_resource_limits_t;

/* Port mapping */
typedef struct {
    char *host_ip;                 /* host IP, NULL = all interfaces */
    uint16_t host_port;            /* host port */
    uint16_t container_port;       /* container port */
    char *protocol;                /* "tcp" or "udp" */
} docker_port_mapping_t;

/* Volume mount */
typedef struct {
    char *source;                  /* host path or volume name */
    char *target;                  /* container path */
    char *type;                    /* "bind", "volume", "tmpfs" */
    bool read_only;                /* mount as read-only */
    char *options;                 /* mount options */
} docker_volume_mount_t;

/* Environment variable */
typedef struct {
    char *name;
    char *value;
} docker_env_var_t;

/* Container information */
typedef struct {
    char *id;                      /* full container ID */
    char *short_id;                /* first 12 chars */
    char *name;                    /* container name */
    char *image;                   /* image name */
    docker_container_state_t state;
    char *status;                  /* human readable status */
    int64_t created;               /* creation timestamp */
    int64_t started;               /* start timestamp */
    int64_t finished;              /* finish timestamp */
    int exit_code;                 /* exit code if stopped */
    docker_port_mapping_t *ports;  /* port mappings */
    size_t ports_count;
    docker_volume_mount_t *mounts; /* volume mounts */
    size_t mounts_count;
    docker_env_var_t *env_vars;    /* environment variables */
    size_t env_vars_count;
} docker_container_info_t;

/* Image information */
typedef struct {
    char *id;                      /* image ID */
    char *repo_digest;             /* repository digest */
    char **repo_tags;              /* repository tags */
    size_t repo_tags_count;
    int64_t created;               /* creation timestamp */
    int64_t size;                  /* image size in bytes */
    int64_t virtual_size;          /* virtual size in bytes */
    char *architecture;            /* architecture */
    char *os;                      /* operating system */
} docker_image_info_t;

/* Network information */
typedef struct {
    char *id;                      /* network ID */
    char *name;                    /* network name */
    char *driver;                  /* network driver */
    char *scope;                   /* network scope */
    bool internal;                 /* internal network */
    bool attachable;               /* attachable */
    bool ingress;                  /* ingress network */
    int64_t created;               /* creation timestamp */
} docker_network_info_t;

/* Volume information */
typedef struct {
    char *name;                    /* volume name */
    char *driver;                  /* volume driver */
    char *mountpoint;              /* mount point */
    char *scope;                   /* volume scope */
    int64_t created;               /* creation timestamp */
} docker_volume_info_t;

/* Callback types */
typedef void (*docker_log_callback_t)(docker_log_level_t level, const char *message, void *userdata);
typedef void (*docker_progress_callback_t)(const char *status, int64_t current, int64_t total, void *userdata);
typedef void (*docker_stream_callback_t)(const char *data, size_t size, void *userdata);
typedef void (*docker_event_callback_t)(const char *action, const char *type, const char *actor_id, 
                                       const char *actor_attributes, int64_t timestamp, void *userdata);

/* Configuration structure */
struct docker_config {
    char *api_version;             /* Docker API version */
    char *socket_path;             /* Docker socket path */
    char *host;                    /* Docker host (for TCP) */
    uint16_t port;                 /* Docker port (for TCP) */
    bool use_tls;                  /* Use TLS for TCP connections */
    char *cert_path;               /* TLS certificate path */
    char *key_path;                /* TLS key path */
    char *ca_path;                 /* TLS CA path */
    int32_t connect_timeout_ms;    /* Connection timeout */
    int32_t request_timeout_ms;    /* Request timeout */
    size_t max_response_size;      /* Maximum response size */
    docker_log_callback_t log_callback;
    void *log_userdata;
    docker_log_level_t log_level;  /* Minimum log level */
};

/* Container configuration */
struct docker_container_config {
    char *name;                    /* Container name */
    char *image;                   /* Image name */
    char **command;                /* Command to run */
    size_t command_count;
    char **entrypoint;             /* Entrypoint override */
    size_t entrypoint_count;
    char *working_dir;             /* Working directory */
    char *user;                    /* User to run as */
    docker_env_var_t *env_vars;    /* Environment variables */
    size_t env_vars_count;
    docker_port_mapping_t *ports;  /* Port mappings */
    size_t ports_count;
    docker_volume_mount_t *mounts; /* Volume mounts */
    size_t mounts_count;
    char **networks;               /* Networks to connect to */
    size_t networks_count;
    docker_resource_limits_t *limits; /* Resource limits */
    char **labels;                 /* Labels (key=value) */
    size_t labels_count;
    bool privileged;               /* Run in privileged mode */
    bool auto_remove;              /* Auto-remove when stopped */
    bool interactive;              /* Keep STDIN open */
    bool tty;                      /* Allocate pseudo-TTY */
    char *restart_policy;          /* Restart policy */
    char *log_driver;              /* Logging driver */
    char **log_options;            /* Logging options (key=value) */
    size_t log_options_count;
};

/* Exec configuration */
struct docker_exec_config {
    char **command;                /* Command to execute */
    size_t command_count;
    docker_env_var_t *env_vars;    /* Environment variables */
    size_t env_vars_count;
    char *working_dir;             /* Working directory */
    char *user;                    /* User to run as */
    bool attach_stdout;            /* Attach to stdout */
    bool attach_stderr;            /* Attach to stderr */
    bool attach_stdin;             /* Attach to stdin */
    bool tty;                      /* Allocate pseudo-TTY */
    bool privileged;               /* Run in privileged mode */
    char *detach_keys;             /* Detach key sequence */
};

/* Image configuration */
struct docker_image_config {
    char *from_image;              /* Base image name */
    char *tag;                     /* Image tag */
    char *dockerfile;              /* Dockerfile content */
    char *context_path;            /* Build context path */
    char **build_args;             /* Build arguments (key=value) */
    size_t build_args_count;
    char **labels;                 /* Labels (key=value) */
    size_t labels_count;
    char *target;                  /* Build target */
    bool no_cache;                 /* Disable build cache */
    bool force_rm;                 /* Always remove intermediate containers */
    bool pull;                     /* Always pull newer version of base image */
    char *platform;                /* Target platform */
};

/* Network configuration */
struct docker_network_config {
    char *name;                    /* Network name */
    char *driver;                  /* Network driver */
    bool check_duplicate;          /* Check for duplicate networks */
    bool internal;                 /* Internal network */
    bool attachable;               /* Attachable network */
    bool ingress;                  /* Ingress network */
    char *ipam_driver;             /* IPAM driver */
    char *subnet;                  /* Subnet */
    char *gateway;                 /* Gateway */
    char **labels;                 /* Labels (key=value) */
    size_t labels_count;
    char **options;                /* Driver options (key=value) */
    size_t options_count;
};

/* Volume configuration */
struct docker_volume_config {
    char *name;                    /* Volume name */
    char *driver;                  /* Volume driver */
    char **labels;                 /* Labels (key=value) */
    size_t labels_count;
    char **driver_opts;            /* Driver options (key=value) */
    size_t driver_opts_count;
};

/* ----------------- Core Client Functions ----------------- */

/* Initialize Docker client with default configuration */
docker_client_t* docker_client_create(void);

/* Initialize Docker client with custom configuration */
docker_client_t* docker_client_create_with_config(const docker_config_t *config);

/* Clean up Docker client */
void docker_client_destroy(docker_client_t *client);

/* Get last error from client */
docker_error_t docker_client_get_last_error(docker_client_t *client);

/* Get last error message */
const char* docker_client_get_last_error_message(docker_client_t *client);

/* Get Docker daemon version information */
docker_error_t docker_get_version(docker_client_t *client, char **version_json);

/* Get Docker daemon system information */
docker_error_t docker_get_system_info(docker_client_t *client, char **info_json);

/* Ping Docker daemon */
docker_error_t docker_ping(docker_client_t *client);

/* ----------------- Configuration Helpers ----------------- */

/* Create default configuration */
docker_config_t* docker_config_create_default(void);

/* Create configuration from environment */
docker_config_t* docker_config_create_from_env(void);

/* Clean up configuration */
void docker_config_destroy(docker_config_t *config);

/* Set log callback */
void docker_config_set_log_callback(docker_config_t *config, 
                                   docker_log_callback_t callback, void *userdata);

/* Set timeouts */
void docker_config_set_timeouts(docker_config_t *config, 
                               int32_t connect_timeout_ms, int32_t request_timeout_ms);

/* ----------------- Container Management ----------------- */

/* Create container configuration */
docker_container_config_t* docker_container_config_create(const char *image);

/* Clean up container configuration */
void docker_container_config_destroy(docker_container_config_t *config);

/* Set container command */
void docker_container_config_set_command(docker_container_config_t *config, 
                                        const char **command, size_t count);

/* Add environment variable */
void docker_container_config_add_env_var(docker_container_config_t *config, 
                                        const char *name, const char *value);

/* Add port mapping */
void docker_container_config_add_port_mapping(docker_container_config_t *config,
                                             uint16_t container_port, uint16_t host_port,
                                             const char *protocol, const char *host_ip);

/* Add volume mount */
void docker_container_config_add_volume_mount(docker_container_config_t *config,
                                             const char *source, const char *target,
                                             const char *type, bool read_only);

/* Set resource limits */
void docker_container_config_set_resource_limits(docker_container_config_t *config,
                                                const docker_resource_limits_t *limits);

/* Create container */
docker_error_t docker_container_create(docker_client_t *client,
                                      const docker_container_config_t *config,
                                      char **container_id);

/* Start container */
docker_error_t docker_container_start(docker_client_t *client, const char *container_id);

/* Stop container */
docker_error_t docker_container_stop(docker_client_t *client, const char *container_id, int timeout);

/* Restart container */
docker_error_t docker_container_restart(docker_client_t *client, const char *container_id, int timeout);

/* Pause container */
docker_error_t docker_container_pause(docker_client_t *client, const char *container_id);

/* Unpause container */
docker_error_t docker_container_unpause(docker_client_t *client, const char *container_id);

/* Kill container */
docker_error_t docker_container_kill(docker_client_t *client, const char *container_id, const char *signal);

/* Remove container */
docker_error_t docker_container_remove(docker_client_t *client, const char *container_id,
                                       bool force, bool remove_volumes, bool remove_links);

/* List containers */
docker_error_t docker_container_list(docker_client_t *client, bool all,
                                    docker_container_info_t ***containers, size_t *count);

/* Get container information */
docker_error_t docker_container_inspect(docker_client_t *client, const char *container_id,
                                       docker_container_info_t **info);

/* Get container logs */
docker_error_t docker_container_logs(docker_client_t *client, const char *container_id,
                                    bool follow, bool stdout_flag, bool stderr_flag,
                                    int64_t since, int64_t until, int tail,
                                    docker_stream_callback_t callback, void *userdata);

/* Get container stats */
docker_error_t docker_container_stats(docker_client_t *client, const char *container_id,
                                     bool stream, docker_stream_callback_t callback, void *userdata);

/* Update container resources */
docker_error_t docker_container_update(docker_client_t *client, const char *container_id,
                                      const docker_resource_limits_t *limits);

/* Rename container */
docker_error_t docker_container_rename(docker_client_t *client, const char *container_id,
                                      const char *new_name);

/* Wait for container */
docker_error_t docker_container_wait(docker_client_t *client, const char *container_id,
                                    const char *condition, int *exit_code);

/* ----------------- Container Execution ----------------- */

/* Create exec configuration */
docker_exec_config_t* docker_exec_config_create(const char **command, size_t count);

/* Clean up exec configuration */
void docker_exec_config_destroy(docker_exec_config_t *config);

/* Create exec instance */
docker_error_t docker_exec_create(docker_client_t *client, const char *container_id,
                                 const docker_exec_config_t *config, char **exec_id);

/* Start exec instance */
docker_error_t docker_exec_start(docker_client_t *client, const char *exec_id,
                                bool detach, docker_stream_callback_t callback, void *userdata);

/* Inspect exec instance */
docker_error_t docker_exec_inspect(docker_client_t *client, const char *exec_id, char **info_json);

/* Resize exec TTY */
docker_error_t docker_exec_resize(docker_client_t *client, const char *exec_id,
                                 int width, int height);

/* ----------------- Image Management ----------------- */

/* Create image configuration */
docker_image_config_t* docker_image_config_create(void);

/* Clean up image configuration */
void docker_image_config_destroy(docker_image_config_t *config);

/* List images */
docker_error_t docker_image_list(docker_client_t *client, bool all,
                                docker_image_info_t ***images, size_t *count);

/* Pull image */
docker_error_t docker_image_pull(docker_client_t *client, const char *image,
                                const char *tag, docker_progress_callback_t callback, void *userdata);

/* Push image */
docker_error_t docker_image_push(docker_client_t *client, const char *image,
                                const char *tag, docker_progress_callback_t callback, void *userdata);

/* Build image */
docker_error_t docker_image_build(docker_client_t *client, const docker_image_config_t *config,
                                 docker_progress_callback_t callback, void *userdata);

/* Remove image */
docker_error_t docker_image_remove(docker_client_t *client, const char *image,
                                  bool force, bool no_prune);

/* Inspect image */
docker_error_t docker_image_inspect(docker_client_t *client, const char *image,
                                   docker_image_info_t **info);

/* Get image history */
docker_error_t docker_image_history(docker_client_t *client, const char *image, char **history_json);

/* Tag image */
docker_error_t docker_image_tag(docker_client_t *client, const char *image,
                               const char *repo, const char *tag);

/* Search images */
docker_error_t docker_image_search(docker_client_t *client, const char *term,
                                  int limit, bool is_official, bool is_automated, char **results_json);

/* Prune unused images */
docker_error_t docker_image_prune(docker_client_t *client, bool dangling_only, char **results_json);

/* ----------------- Network Management ----------------- */

/* Create network configuration */
docker_network_config_t* docker_network_config_create(const char *name);

/* Clean up network configuration */
void docker_network_config_destroy(docker_network_config_t *config);

/* List networks */
docker_error_t docker_network_list(docker_client_t *client,
                                  docker_network_info_t ***networks, size_t *count);

/* Create network */
docker_error_t docker_network_create(docker_client_t *client, const docker_network_config_t *config,
                                    char **network_id);

/* Remove network */
docker_error_t docker_network_remove(docker_client_t *client, const char *network_id);

/* Inspect network */
docker_error_t docker_network_inspect(docker_client_t *client, const char *network_id,
                                     docker_network_info_t **info);

/* Connect container to network */
docker_error_t docker_network_connect(docker_client_t *client, const char *network_id,
                                     const char *container_id, const char *ip_address);

/* Disconnect container from network */
docker_error_t docker_network_disconnect(docker_client_t *client, const char *network_id,
                                        const char *container_id, bool force);

/* Prune unused networks */
docker_error_t docker_network_prune(docker_client_t *client, char **results_json);

/* ----------------- Volume Management ----------------- */

/* Create volume configuration */
docker_volume_config_t* docker_volume_config_create(const char *name);

/* Clean up volume configuration */
void docker_volume_config_destroy(docker_volume_config_t *config);

/* List volumes */
docker_error_t docker_volume_list(docker_client_t *client,
                                 docker_volume_info_t ***volumes, size_t *count);

/* Create volume */
docker_error_t docker_volume_create(docker_client_t *client, const docker_volume_config_t *config,
                                   docker_volume_info_t **info);

/* Remove volume */
docker_error_t docker_volume_remove(docker_client_t *client, const char *volume_name, bool force);

/* Inspect volume */
docker_error_t docker_volume_inspect(docker_client_t *client, const char *volume_name,
                                    docker_volume_info_t **info);

/* Prune unused volumes */
docker_error_t docker_volume_prune(docker_client_t *client, char **results_json);

/* ----------------- File Operations ----------------- */

/* Copy file to container */
docker_error_t docker_container_copy_to(docker_client_t *client, const char *container_id,
                                       const char *container_path, const char *host_path);

/* Copy file from container */
docker_error_t docker_container_copy_from(docker_client_t *client, const char *container_id,
                                         const char *container_path, const char *host_path);

/* Copy data to container (from memory) */
docker_error_t docker_container_put_archive(docker_client_t *client, const char *container_id,
                                           const char *container_path, const char *data, size_t size);

/* Copy data from container (to memory) */
docker_error_t docker_container_get_archive(docker_client_t *client, const char *container_id,
                                           const char *container_path, char **data, size_t *size);

/* ----------------- System Operations ----------------- */

/* Get system events */
docker_error_t docker_system_events(docker_client_t *client, int64_t since, int64_t until,
                                   const char *filters, docker_event_callback_t callback, void *userdata);

/* Get disk usage */
docker_error_t docker_system_df(docker_client_t *client, char **usage_json);

/* Prune system */
docker_error_t docker_system_prune(docker_client_t *client, bool prune_volumes, char **results_json);

/* ----------------- Utility Functions ----------------- */

/* Free container info structure */
void docker_container_info_free(docker_container_info_t *info);

/* Free container info array */
void docker_container_info_free_array(docker_container_info_t **infos, size_t count);

/* Free image info structure */
void docker_image_info_free(docker_image_info_t *info);

/* Free image info array */
void docker_image_info_free_array(docker_image_info_t **infos, size_t count);

/* Free network info structure */
void docker_network_info_free(docker_network_info_t *info);

/* Free network info array */
void docker_network_info_free_array(docker_network_info_t **infos, size_t count);

/* Free volume info structure */
void docker_volume_info_free(docker_volume_info_t *info);

/* Free volume info array */
void docker_volume_info_free_array(docker_volume_info_t **infos, size_t count);

/* Parse container ID from short form */
docker_error_t docker_resolve_container_id(docker_client_t *client, const char *name_or_id,
                                          char **full_id);

/* Parse image ID from name/tag */
docker_error_t docker_resolve_image_id(docker_client_t *client, const char *name_or_tag,
                                      char **full_id);

/* Format bytes to human readable string */
void docker_format_bytes(int64_t bytes, char *buffer, size_t buffer_size);

/* Format duration to human readable string */
void docker_format_duration(int64_t seconds, char *buffer, size_t buffer_size);

/* Parse environment variable string */
docker_error_t docker_parse_env_vars(const char *env_string, docker_env_var_t **env_vars, size_t *count);

/* Parse port mapping string */
docker_error_t docker_parse_port_mapping(const char *port_string, docker_port_mapping_t *mapping);

/* Parse volume mount string */
docker_error_t docker_parse_volume_mount(const char *volume_string, docker_volume_mount_t *mount);

/* ----------------- Error Handling ----------------- */

/* Get error string for error code */
const char* docker_error_string(docker_error_t error);

/* Set custom error handler */
typedef void (*docker_error_handler_t)(docker_error_t error, const char *message, void *userdata);
void docker_set_error_handler(docker_error_handler_t handler, void *userdata);

/* ----------------- Thread Safety ----------------- */

/* Enable/disable thread safety (requires pthread) */
docker_error_t docker_enable_thread_safety(bool enable);

/* Check if thread safety is enabled */
bool docker_is_thread_safe(void);

#ifdef __cplusplus
}
#endif

/* ----------------- Implementation Section ----------------- */
#ifdef DOCKER_EXCESS_SDK

/* Implementation would go here - this is a header-only library pattern */
/* Include all the implementation details, HTTP client, JSON parsing, etc. */

#endif /* DOCKER_EXCESS_SDK */

#endif /* DOCKER_EXCESS_H */
