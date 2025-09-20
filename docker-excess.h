#ifndef DOCKER_EXCESS_H
#define DOCKER_EXCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ----------------- Version Information ----------------- */
#define DOCKER_EXCESS_VERSION_MAJOR 2
#define DOCKER_EXCESS_VERSION_MINOR 0
#define DOCKER_EXCESS_VERSION_PATCH 0
#define DOCKER_EXCESS_VERSION "2.0.0"

/* ----------------- Core Types & Constants ----------------- */
#define DOCKER_EXCESS_DEFAULT_SOCKET "/var/run/docker.sock"
#define DOCKER_EXCESS_DEFAULT_TIMEOUT_S 30
#define DOCKER_EXCESS_API_VERSION "1.41"
#define DOCKER_EXCESS_MAX_ERROR_MSG 512
#define DOCKER_EXCESS_MAX_URL_LEN 2048

typedef struct docker_excess_t docker_excess_t;

/* Enhanced error codes */
typedef enum {
    DOCKER_EXCESS_OK = 0,
    DOCKER_EXCESS_ERR_INVALID_PARAM = -1,
    DOCKER_EXCESS_ERR_MEMORY = -2,
    DOCKER_EXCESS_ERR_NETWORK = -3,
    DOCKER_EXCESS_ERR_HTTP = -4,
    DOCKER_EXCESS_ERR_JSON = -5,
    DOCKER_EXCESS_ERR_NOT_FOUND = -6,
    DOCKER_EXCESS_ERR_TIMEOUT = -7,
    DOCKER_EXCESS_ERR_INTERNAL = -8,
    DOCKER_EXCESS_ERR_PERMISSION = -9,
    DOCKER_EXCESS_ERR_CONFLICT = -10,
    DOCKER_EXCESS_ERR_ALREADY_EXISTS = -11,
    DOCKER_EXCESS_ERR_CONTAINER_RUNNING = -12,
    DOCKER_EXCESS_ERR_CONTAINER_STOPPED = -13
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

/* Log levels */
typedef enum {
    DOCKER_EXCESS_LOG_ERROR,
    DOCKER_EXCESS_LOG_WARN,
    DOCKER_EXCESS_LOG_INFO,
    DOCKER_EXCESS_LOG_DEBUG
} docker_excess_log_level_t;

/* ----------------- Configuration Structure ----------------- */
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
    void (*log_callback)(docker_excess_log_level_t level, const char *message, void *userdata);
    void *log_userdata;             /* User data for log callback */
} docker_excess_config_t;

/* ----------------- Data Structures ----------------- */

/* Port mapping structure */
typedef struct {
    uint16_t host_port;
    uint16_t container_port;
    char *protocol;                 /* "tcp" or "udp" */
    char *host_ip;                  /* Host IP (optional) */
} docker_excess_port_mapping_t;

/* Volume mount structure */
typedef struct {
    char *source;                   /* Host path or volume name */
    char *target;                   /* Container path */
    char *type;                     /* "bind" or "volume" */
    bool read_only;
} docker_excess_mount_t;

/* Environment variable structure */
typedef struct {
    char *name;
    char *value;
} docker_excess_env_var_t;

/* Container information */
typedef struct {
    char *id;                       /* Full container ID */
    char *short_id;                 /* Short container ID (12 chars) */
    char *name;                     /* Container name */
    char *image;                    /* Image name */
    char *image_id;                 /* Image ID */
    char *status;                   /* Status string */
    docker_excess_container_state_t state;
    int64_t created;                /* Creation timestamp */
    int64_t started_at;             /* Start timestamp */
    int64_t finished_at;            /* Finish timestamp */
    int exit_code;                  /* Exit code (if exited) */
    docker_excess_port_mapping_t *ports;
    size_t ports_count;
    docker_excess_mount_t *mounts;
    size_t mounts_count;
    char **labels;                  /* Array of "key=value" strings */
    size_t labels_count;
} docker_excess_container_t;

/* Image information */
typedef struct {
    char *id;                       /* Full image ID */
    char *short_id;                 /* Short image ID (12 chars) */
    char **repo_tags;               /* Repository tags */
    size_t repo_tags_count;
    char **repo_digests;            /* Repository digests */
    size_t repo_digests_count;
    int64_t created;                /* Creation timestamp */
    int64_t size;                   /* Size in bytes */
    int64_t virtual_size;           /* Virtual size in bytes */
    char **labels;                  /* Array of "key=value" strings */
    size_t labels_count;
} docker_excess_image_t;

/* Network information */
typedef struct {
    char *id;                       /* Network ID */
    char *name;                     /* Network name */
    char *driver;                   /* Network driver */
    char *scope;                    /* Network scope */
    bool attachable;                /* Is attachable */
    bool ingress;                   /* Is ingress network */
    bool internal;                  /* Is internal */
    bool ipv6_enabled;              /* IPv6 enabled */
    int64_t created;                /* Creation timestamp */
    char **labels;                  /* Array of "key=value" strings */
    size_t labels_count;
} docker_excess_network_t;

/* Volume information */
typedef struct {
    char *name;                     /* Volume name */
    char *driver;                   /* Volume driver */
    char *mountpoint;               /* Mount point */
    char *scope;                    /* Volume scope */
    int64_t created;                /* Creation timestamp */
    char **labels;                  /* Array of "key=value" strings */
    size_t labels_count;
} docker_excess_volume_t;

/* File information */
typedef struct {
    char *name;                     /* File/directory name */
    char *full_path;                /* Full path */
    int64_t size;                   /* File size */
    time_t modified;                /* Last modified time */
    time_t accessed;                /* Last accessed time */
    bool is_dir;                    /* Is directory */
    bool is_link;                   /* Is symbolic link */
    uint32_t mode;                  /* File permissions */
    char *link_target;              /* Symlink target (if is_link) */
} docker_excess_file_t;

/* Enhanced container creation parameters */
typedef struct {
    /* Basic settings */
    char *name;                     /* Container name (optional) */
    char *image;                    /* Image name (required) */
    char **cmd;                     /* Command to run */
    size_t cmd_count;
    char **entrypoint;              /* Entrypoint override */
    size_t entrypoint_count;
    char *working_dir;              /* Working directory */
    char *user;                     /* User to run as */
    
    /* Environment and labels */
    docker_excess_env_var_t *env;   /* Environment variables */
    size_t env_count;
    char **labels;                  /* Labels (key=value) */
    size_t labels_count;
    
    /* Network and ports */
    docker_excess_port_mapping_t *ports;
    size_t ports_count;
    char **networks;                /* Networks to connect to */
    size_t networks_count;
    char *hostname;                 /* Container hostname */
    char *domain_name;              /* Container domain name */
    
    /* Storage */
    docker_excess_mount_t *mounts;
    size_t mounts_count;
    char **volumes_from;            /* Volumes from other containers */
    size_t volumes_from_count;
    
    /* Runtime options */
    int64_t memory_limit;           /* Memory limit in bytes (0 = unlimited) */
    int64_t memory_swap;            /* Memory + swap limit (-1 = unlimited) */
    double cpu_shares;              /* CPU shares (0 = default) */
    char *cpu_set;                  /* CPU set (e.g., "0-3,8-11") */
    bool privileged;                /* Run in privileged mode */
    char **cap_add;                 /* Linux capabilities to add */
    size_t cap_add_count;
    char **cap_drop;                /* Linux capabilities to drop */
    size_t cap_drop_count;
    
    /* Behavior */
    bool auto_remove;               /* Auto-remove when stopped */
    bool interactive;               /* Keep STDIN open */
    bool tty;                       /* Allocate pseudo-TTY */
    bool detach;                    /* Run detached */
    char *restart_policy;           /* Restart policy */
    int restart_max_retries;        /* Max restart retries */
    
    /* Health check */
    char **health_cmd;              /* Health check command */
    size_t health_cmd_count;
    int health_interval_s;          /* Health check interval */
    int health_timeout_s;           /* Health check timeout */
    int health_retries;             /* Health check retries */
    int health_start_period_s;      /* Health check start period */
} docker_excess_container_create_t;

/* Image build parameters */
typedef struct {
    char *dockerfile_path;          /* Path to Dockerfile */
    char *context_path;             /* Build context path */
    char *tag;                      /* Image tag */
    char **build_args;              /* Build arguments (key=value) */
    size_t build_args_count;
    char **labels;                  /* Build labels (key=value) */
    size_t labels_count;
    char *target;                   /* Build target */
    bool no_cache;                  /* Don't use cache */
    bool pull;                      /* Always pull base image */
    bool force_rm;                  /* Always remove intermediate containers */
    int64_t memory_limit;           /* Memory limit for build */
    char *network_mode;             /* Network mode for build */
} docker_excess_image_build_t;

/* Execution parameters */
typedef struct {
    char **cmd;                     /* Command to execute */
    size_t cmd_count;
    docker_excess_env_var_t *env;   /* Environment variables */
    size_t env_count;
    char *working_dir;              /* Working directory */
    char *user;                     /* User to run as */
    bool privileged;                /* Run privileged */
    bool tty;                       /* Allocate TTY */
    bool detach;                    /* Run detached */
} docker_excess_exec_params_t;

/* Log parameters */
typedef struct {
    bool follow;                    /* Follow log output */
    bool timestamps;                /* Show timestamps */
    bool details;                   /* Show extra details */
    int tail_lines;                 /* Number of lines from end (0 = all) */
    time_t since;                   /* Show logs since timestamp (0 = all) */
    time_t until;                   /* Show logs until timestamp (0 = all) */
} docker_excess_log_params_t;

/* ----------------- Callback Types ----------------- */
typedef void (*docker_excess_log_callback_t)(const char *line, bool is_stderr, time_t timestamp, void *userdata);
typedef void (*docker_excess_exec_callback_t)(const char *stdout_data, const char *stderr_data, void *userdata);
typedef void (*docker_excess_progress_callback_t)(const char *status, const char *progress, void *userdata);

/* ----------------- Core Functions ----------------- */

/* Get library version */
const char* docker_excess_get_version(void);

/* Create Docker client with default config */
docker_excess_error_t docker_excess_new(docker_excess_t **client);

/* Create Docker client with custom config */
docker_excess_error_t docker_excess_new_with_config(const docker_excess_config_t *config, docker_excess_t **client);

/* Free Docker client */
void docker_excess_free(docker_excess_t *client);

/* Get last error message (thread-safe) */
const char* docker_excess_get_error(docker_excess_t *client);

/* Clear last error */
void docker_excess_clear_error(docker_excess_t *client);

/* Test connection to Docker daemon */
docker_excess_error_t docker_excess_ping(docker_excess_t *client);

/* Get Docker version information */
docker_excess_error_t docker_excess_version(docker_excess_t *client, char **version_json);

/* Get Docker system information */
docker_excess_error_t docker_excess_system_info(docker_excess_t *client, char **info_json);

/* Get Docker daemon events (streaming) */
docker_excess_error_t docker_excess_events(docker_excess_t *client,
                                          docker_excess_log_callback_t callback, void *userdata);

/* ----------------- Container Management ----------------- */

/* List containers with filtering options */
docker_excess_error_t docker_excess_list_containers(docker_excess_t *client, bool all,
                                                   const char *filters, /* JSON filters */
                                                   docker_excess_container_t ***containers, size_t *count);

/* Get detailed container information */
docker_excess_error_t docker_excess_inspect_container(docker_excess_t *client, const char *container_id,
                                                     docker_excess_container_t **container);

/* Create container from parameters */
docker_excess_error_t docker_excess_create_container(docker_excess_t *client,
                                                    const docker_excess_container_create_t *params,
                                                    char **container_id);

/* Start container by ID or name */
docker_excess_error_t docker_excess_start_container(docker_excess_t *client, const char *container_id);

/* Stop container with optional timeout */
docker_excess_error_t docker_excess_stop_container(docker_excess_t *client, const char *container_id, int timeout_s);

/* Kill container with signal */
docker_excess_error_t docker_excess_kill_container(docker_excess_t *client, const char *container_id, const char *signal);

/* Restart container */
docker_excess_error_t docker_excess_restart_container(docker_excess_t *client, const char *container_id);

/* Remove container */
docker_excess_error_t docker_excess_remove_container(docker_excess_t *client, const char *container_id, bool force, bool remove_volumes);

/* Pause/unpause container */
docker_excess_error_t docker_excess_pause_container(docker_excess_t *client, const char *container_id);
docker_excess_error_t docker_excess_unpause_container(docker_excess_t *client, const char *container_id);

/* Wait for container to stop */
docker_excess_error_t docker_excess_wait_container(docker_excess_t *client, const char *container_id, int *exit_code);

/* Get container statistics (JSON) */
docker_excess_error_t docker_excess_stats_container(docker_excess_t *client, const char *container_id, bool stream,
                                                   char **stats_json);

/* Get container processes */
docker_excess_error_t docker_excess_top_container(docker_excess_t *client, const char *container_id,
                                                 const char *ps_args, char **processes_json);

/* Rename container */
docker_excess_error_t docker_excess_rename_container(docker_excess_t *client, const char *container_id, const char *new_name);

/* Update container resources */
docker_excess_error_t docker_excess_update_container(docker_excess_t *client, const char *container_id,
                                                    int64_t memory_limit, double cpu_shares);

/* ----------------- Container Logs and Execution ----------------- */

/* Get container logs with parameters */
docker_excess_error_t docker_excess_get_logs(docker_excess_t *client, const char *container_id,
                                            const docker_excess_log_params_t *params,
                                            docker_excess_log_callback_t callback, void *userdata);

/* Execute command in container */
docker_excess_error_t docker_excess_exec(docker_excess_t *client, const char *container_id,
                                        const docker_excess_exec_params_t *params,
                                        docker_excess_exec_callback_t callback, void *userdata);

/* Execute command and capture output (blocking) */
docker_excess_error_t docker_excess_exec_simple(docker_excess_t *client, const char *container_id,
                                               const char *command, char **stdout_out, char **stderr_out, int *exit_code);

/* ----------------- File Operations ----------------- */

/* List files/directories in container path with detailed info */
docker_excess_error_t docker_excess_list_files(docker_excess_t *client, const char *container_id,
                                              const char *path, bool recursive,
                                              docker_excess_file_t ***files, size_t *count);

/* Get file/directory information */
docker_excess_error_t docker_excess_stat_file(docker_excess_t *client, const char *container_id,
                                             const char *path, docker_excess_file_t **file_info);

/* Copy file/directory from container to host */
docker_excess_error_t docker_excess_copy_from_container(docker_excess_t *client, const char *container_id,
                                                       const char *container_path, const char *host_path);

/* Copy file/directory from host to container */
docker_excess_error_t docker_excess_copy_to_container(docker_excess_t *client, const char *container_id,
                                                     const char *host_path, const char *container_path);

/* Read file content from container */
docker_excess_error_t docker_excess_read_file(docker_excess_t *client, const char *container_id,
                                             const char *file_path, char **content, size_t *size);

/* Write content to file in container */
docker_excess_error_t docker_excess_write_file(docker_excess_t *client, const char *container_id,
                                              const char *file_path, const char *content, size_t size, uint32_t mode);

/* Create directory in container */
docker_excess_error_t docker_excess_mkdir(docker_excess_t *client, const char *container_id,
                                         const char *dir_path, uint32_t mode, bool parents);

/* Remove file/directory in container */
docker_excess_error_t docker_excess_remove_file(docker_excess_t *client, const char *container_id,
                                               const char *path, bool recursive);

/* ----------------- Image Management ----------------- */

/* List images with filtering */
docker_excess_error_t docker_excess_list_images(docker_excess_t *client, bool all,
                                               const char *filters, /* JSON filters */
                                               docker_excess_image_t ***images, size_t *count);

/* Get detailed image information */
docker_excess_error_t docker_excess_inspect_image(docker_excess_t *client, const char *image_name,
                                                 docker_excess_image_t **image);

/* Pull image with progress callback */
docker_excess_error_t docker_excess_pull_image(docker_excess_t *client, const char *image_name, const char *tag,
                                              docker_excess_progress_callback_t callback, void *userdata);

/* Push image with progress callback */
docker_excess_error_t docker_excess_push_image(docker_excess_t *client, const char *image_name, const char *tag,
                                              docker_excess_progress_callback_t callback, void *userdata);

/* Remove image */
docker_excess_error_t docker_excess_remove_image(docker_excess_t *client, const char *image_name, bool force, bool no_prune);

/* Build image from parameters */
docker_excess_error_t docker_excess_build_image(docker_excess_t *client, 
                                               const docker_excess_image_build_t *params,
                                               docker_excess_progress_callback_t callback, void *userdata);

/* Tag image */
docker_excess_error_t docker_excess_tag_image(docker_excess_t *client, const char *source_image, const char *target_image);

/* Get image history */
docker_excess_error_t docker_excess_image_history(docker_excess_t *client, const char *image_name, char **history_json);

/* Search images in registry */
docker_excess_error_t docker_excess_search_images(docker_excess_t *client, const char *term, int limit, char **results_json);

/* Prune unused images */
docker_excess_error_t docker_excess_prune_images(docker_excess_t *client, const char *filters, char **prune_result);

/* ----------------- Network Management ----------------- */

/* List networks with filtering */
docker_excess_error_t docker_excess_list_networks(docker_excess_t *client, const char *filters,
                                                 docker_excess_network_t ***networks, size_t *count);

/* Get detailed network information */
docker_excess_error_t docker_excess_inspect_network(docker_excess_t *client, const char *network_id,
                                                   docker_excess_network_t **network);

/* Create network with advanced options */
docker_excess_error_t docker_excess_create_network(docker_excess_t *client, const char *name,
                                                  const char *driver, const char *options, /* JSON options */
                                                  char **network_id);

/* Remove network */
docker_excess_error_t docker_excess_remove_network(docker_excess_t *client, const char *network_id);

/* Connect container to network */
docker_excess_error_t docker_excess_connect_network(docker_excess_t *client, const char *network_id,
                                                   const char *container_id, const char *config /* JSON config */);

/* Disconnect container from network */
docker_excess_error_t docker_excess_disconnect_network(docker_excess_t *client, const char *network_id,
                                                      const char *container_id, bool force);

/* Prune unused networks */
docker_excess_error_t docker_excess_prune_networks(docker_excess_t *client, const char *filters, char **prune_result);

/* ----------------- Volume Management ----------------- */

/* List volumes with filtering */
docker_excess_error_t docker_excess_list_volumes(docker_excess_t *client, const char *filters,
                                                docker_excess_volume_t ***volumes, size_t *count);

/* Get detailed volume information */
docker_excess_error_t docker_excess_inspect_volume(docker_excess_t *client, const char *volume_name,
                                                  docker_excess_volume_t **volume);

/* Create volume with options */
docker_excess_error_t docker_excess_create_volume(docker_excess_t *client, const char *name,
                                                 const char *driver, const char *options, /* JSON options */
                                                 char **volume_name);

/* Remove volume */
docker_excess_error_t docker_excess_remove_volume(docker_excess_t *client, const char *volume_name, bool force);

/* Prune unused volumes */
docker_excess_error_t docker_excess_prune_volumes(docker_excess_t *client, const char *filters, char **prune_result);

/* ----------------- Raw API Access ----------------- */

/* Make raw HTTP request to Docker API */
docker_excess_error_t docker_excess_raw_request(docker_excess_t *client, const char *method,
                                               const char *endpoint, const char *body,
                                               char **response, int *http_code);

/* Stream raw API response */
docker_excess_error_t docker_excess_raw_stream(docker_excess_t *client, const char *method,
                                              const char *endpoint, const char *body,
                                              docker_excess_log_callback_t callback, void *userdata);

/* ----------------- Utility Functions ----------------- */

/* Memory management */
void docker_excess_free_containers(docker_excess_container_t **containers, size_t count);
void docker_excess_free_images(docker_excess_image_t **images, size_t count);
void docker_excess_free_networks(docker_excess_network_t **networks, size_t count);
void docker_excess_free_volumes(docker_excess_volume_t **volumes, size_t count);
void docker_excess_free_files(docker_excess_file_t **files, size_t count);

/* Configuration helpers */
docker_excess_config_t docker_excess_default_config(void);
docker_excess_config_t docker_excess_config_from_env(void);
void docker_excess_free_config(docker_excess_config_t *config);

/* Parameter helpers */
docker_excess_container_create_t* docker_excess_container_create_new(const char *image);
void docker_excess_container_create_free(docker_excess_container_create_t *params);
docker_excess_image_build_t* docker_excess_image_build_new(const char *context_path);
void docker_excess_image_build_free(docker_excess_image_build_t *params);

/* String utilities */
const char* docker_excess_error_string(docker_excess_error_t error);
void docker_excess_format_bytes(int64_t bytes, char *buffer, size_t buffer_size);
char* docker_excess_format_time(time_t timestamp);
bool docker_excess_is_container_id(const char *str);
char* docker_excess_short_id(const char *full_id);

/* ID resolution */
docker_excess_error_t docker_excess_resolve_container_id(docker_excess_t *client, const char *name_or_id,
                                                        char **full_id);
docker_excess_error_t docker_excess_resolve_image_id(docker_excess_t *client, const char *name_or_id,
                                                    char **full_id);
docker_excess_error_t docker_excess_resolve_network_id(docker_excess_t *client, const char *name_or_id,
                                                      char **full_id);

/* Validation helpers */
bool docker_excess_validate_name(const char *name);
bool docker_excess_validate_tag(const char *tag);
bool docker_excess_validate_image_name(const char *name);

/* JSON utilities */
char* docker_excess_create_filters_json(const char **keys, const char **values, size_t count);
docker_excess_error_t docker_excess_parse_labels(const char *labels_json, char ***labels, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* DOCKER_EXCESS_H */
