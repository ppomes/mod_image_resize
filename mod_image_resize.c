/**
 * mod_image_resize.c
 *
 * Apache module for on-the-fly image resizing and compression.
 * Main module file containing configuration and request handling.
 */

#include "mod_image_resize.h"
#include <MagickWand/MagickWand.h>

// External function declarations
extern image_request *parse_url(request_rec *r, const char *url);
extern int detect_image_type(request_rec *r, const char *path, char *format_buffer, size_t buffer_size);
extern apr_status_t ensure_directory_exists(apr_pool_t *pool, const char *dir);
extern int ensure_parent_directory_exists(request_rec *r, const char *file_path);
extern int process_image(request_rec *r, const image_resize_config *cfg,
                         const image_request *req, const char *output_path);

// Global mutex for cache operations
static apr_thread_mutex_t *cache_mutex = NULL;

// Global mutex and variable for ImageMagick init and processing
int imagemagick_initialized = 0;
apr_thread_mutex_t *imagemagick_mutex = NULL;

/**
 * Module cleanup function - called when pools are cleaned up
 */
static apr_status_t image_resize_cleanup(void *data)
{
    // Release ImageMagick resources
    if (imagemagick_initialized)
    {
        MagickWandTerminus();
        imagemagick_initialized = 0;
    }

    server_rec *s = (server_rec *)data;
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s,
                 "mod_image_resize: module cleanup complete");

    return APR_SUCCESS;
}

/**
 * Module initialization function - called at server startup
 */
static int image_resize_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
    // Create ImageMagick Mutex
    apr_thread_mutex_create(&imagemagick_mutex, APR_THREAD_MUTEX_DEFAULT, p);

    // Initialize ImageMagick once at startup
    MagickWandGenesis();
    imagemagick_initialized = 1;

    // Create cache mutex
    apr_status_t status = apr_thread_mutex_create(&cache_mutex, APR_THREAD_MUTEX_DEFAULT, p);
    if (status != APR_SUCCESS)
    {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
                     "mod_image_resize: unable to create cache mutex");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Register cleanup function
    apr_pool_cleanup_register(p, s, image_resize_cleanup, apr_pool_cleanup_null);

    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s,
                 "mod_image_resize: module initialized");

    return OK;
}

/**
 * Process image with cache handling.
 * This function handles cache checking, directory creation, and mutex locking.
 */
static int process_image_with_cache(request_rec *r, const image_resize_config *cfg,
                                    const image_request *req, char *cache_path, size_t path_size)
{
    int status = -1;

    // Build cache path preserving subdirectory structure
    apr_snprintf(cache_path, path_size, "%s/%dx%d_%s",
                 cfg->cache_dir, req->width, req->height, req->filename);

    DEBUG_LOG(r, "Cache check/write: %s", cache_path);

    // Lock for cache operations
    apr_thread_mutex_lock(cache_mutex);

    // Check if image exists in cache
    apr_finfo_t finfo;
    if (apr_stat(&finfo, cache_path, APR_FINFO_SIZE, r->pool) == APR_SUCCESS)
    {
        // Image already exists in cache
        DEBUG_LOG(r, "Image found in cache");
        status = 0; // Success
    }
    else
    {
        DEBUG_LOG(r, "Image not found in cache, processing...");

        // Ensure cache base directory exists
        if (ensure_directory_exists(r->pool, cfg->cache_dir) != APR_SUCCESS)
        {
            DEBUG_LOG(r, "Failed to create cache directory: %s", cfg->cache_dir);
            apr_thread_mutex_unlock(cache_mutex);
            return -1;
        }

        // Process the image
        status = process_image(r, cfg, req, cache_path);

        if (status == 0)
        {
            DEBUG_LOG(r, "Image processed and cached successfully");
        }
        else
        {
            DEBUG_LOG(r, "Failed to process image for cache");
        }
    }

    // Unlock
    apr_thread_mutex_unlock(cache_mutex);

    return status;
}

/**
 * HTTP request handler - handles image resize requests
 */
static int image_resize_handler(request_rec *r)
{
    image_resize_config *cfg;
    image_request *req;
    char cache_path[512];
    apr_file_t *fd;
    apr_finfo_t finfo;
    apr_status_t rv;
    char date_str[APR_RFC822_DATE_LEN];
    const char *content_type;

    // Check if this is our handler
    if (!r->handler || strcmp(r->handler, "image-resize") != 0)
        return DECLINED;

    // Only handle GET requests
    if (r->method_number != M_GET)
        return HTTP_METHOD_NOT_ALLOWED;

    // Get module configuration
    cfg = (image_resize_config *)ap_get_module_config(r->per_dir_config, &image_resize_module);

    DEBUG_LOG(r, "New request: %s", r->uri);

    // Parse URL
    req = parse_url(r, r->uri);
    if (!req)
    {
        DEBUG_LOG(r, "Invalid URL: %s", r->uri);
        return HTTP_BAD_REQUEST;
    }

    // Check cache and process image if needed
    if (process_image_with_cache(r, cfg, req, cache_path, sizeof(cache_path)) != 0)
    {
        DEBUG_LOG(r, "Error processing image");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Open cached file
    DEBUG_LOG(r, "Opening cached file: %s", cache_path);
    if ((rv = apr_file_open(&fd, cache_path, APR_READ, APR_OS_DEFAULT, r->pool)) != APR_SUCCESS)
    {
        DEBUG_LOG(r, "Cannot open cached file: %s",
                  apr_strerror(rv, cache_path, 100));
        return HTTP_NOT_FOUND;
    }

    // Get file size
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fd)) != APR_SUCCESS)
    {
        DEBUG_LOG(r, "Cannot get file size: %s",
                  apr_strerror(rv, cache_path, 100));
        apr_file_close(fd);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Set response headers based on actual image format
    const char *format = req->format;
    char format_buffer[10] = {0};
    if (strcmp(format, "unknown") == 0)
    {
        // Determine type based on content detection
        detect_image_type(r, cache_path, format_buffer, sizeof(format_buffer));
    }

    if (strcmp(format_buffer, "jpg") == 0)
    {
        content_type = "image/jpeg";
    }
    else if (strcmp(format_buffer, "png") == 0)
    {
        content_type = "image/png";
    }
    else if (strcmp(format_buffer, "gif") == 0)
    {
        content_type = "image/gif";
    }
    else if (strcmp(format_buffer, "webp") == 0)
    {
        content_type = "image/webp";
    }
    else
    {
        content_type = "image/jpeg"; // default
    }

    ap_set_content_type(r, content_type);
    DEBUG_LOG(r, "Content-Type set: %s", content_type);

    // Set Cache-Control
    char cache_control[100];
    apr_snprintf(cache_control, sizeof(cache_control), "max-age=%d", cfg->cache_max_age);
    apr_table_set(r->headers_out, "Cache-Control", cache_control);

    // Set expiration date
    apr_time_t expires = apr_time_now() + (apr_time_t)cfg->cache_max_age * APR_USEC_PER_SEC;
    apr_rfc822_date(date_str, expires);
    apr_table_set(r->headers_out, "Expires", date_str);

    // Set content length
    ap_set_content_length(r, finfo.size);

    // Send response body
    if (r->header_only)
    {
        apr_file_close(fd);
        return OK;
    }

    rv = ap_send_fd(fd, r, 0, finfo.size, &finfo.size);

    // Close file
    apr_file_close(fd);

    return OK;
}

/**
 * Create per-directory configuration structure
 */
static void *create_dir_config(apr_pool_t *p, char *arg)
{
    image_resize_config *cfg = apr_pcalloc(p, sizeof(image_resize_config));

    if (cfg)
    {
        // Default values
        cfg->image_dir = "/var/www/images";                 // Default directory
        cfg->cache_dir = "/var/cache/apache2/image_resize"; // Default cache
        cfg->jpeg_quality = 85;                             // Default JPEG quality
        cfg->png_quality_min = 65;                          // Default PNG min quality
        cfg->png_quality_max = 80;                          // Default PNG max quality
        cfg->cache_max_age = 86400;                         // Default cache lifetime (1 day)
        cfg->enable_debug = 0;                              // Debug disabled by default
    }

    return cfg;
}

/**
 * Configuration command handlers
 */
static const char *set_image_dir(cmd_parms *cmd, void *conf, const char *arg)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->image_dir = apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *set_cache_dir(cmd_parms *cmd, void *conf, const char *arg)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->cache_dir = apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *set_jpeg_quality(cmd_parms *cmd, void *conf, const char *arg)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    int val = atoi(arg);
    if (val < 0 || val > 100)
    {
        return "ImageResizeJpegQuality must be between 0 and 100";
    }
    cfg->jpeg_quality = val;
    return NULL;
}

static const char *set_png_quality(cmd_parms *cmd, void *conf, const char *arg1, const char *arg2)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    int min_val = atoi(arg1);
    int max_val = atoi(arg2);

    if (min_val < 0 || min_val > 100 || max_val < 0 || max_val > 100 || min_val > max_val)
    {
        return "ImageResizePngQuality must be between 0 and 100, and min <= max";
    }

    cfg->png_quality_min = min_val;
    cfg->png_quality_max = max_val;
    return NULL;
}

static const char *set_cache_max_age(cmd_parms *cmd, void *conf, const char *arg)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    int val = atoi(arg);
    if (val < 0)
    {
        return "ImageResizeCacheMaxAge must be a positive integer";
    }
    cfg->cache_max_age = val;
    return NULL;
}

static const char *set_enable_debug(cmd_parms *cmd, void *conf, int flag)
{
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->enable_debug = flag;
    return NULL;
}

/**
 * Configuration commands table
 */
static const command_rec image_resize_cmds[] = {
    AP_INIT_TAKE1("ImageResizeSourceDir", set_image_dir, NULL, ACCESS_CONF,
                  "Directory containing source images"),
    AP_INIT_TAKE1("ImageResizeCacheDir", set_cache_dir, NULL, ACCESS_CONF,
                  "Directory for storing resized images"),
    AP_INIT_TAKE1("ImageResizeJpegQuality", set_jpeg_quality, NULL, ACCESS_CONF,
                  "JPEG compression quality (0-100)"),
    AP_INIT_TAKE2("ImageResizePngQuality", set_png_quality, NULL, ACCESS_CONF,
                  "PNG compression quality (min max, 0-100)"),
    AP_INIT_TAKE1("ImageResizeCacheMaxAge", set_cache_max_age, NULL, ACCESS_CONF,
                  "Cache lifetime in seconds"),
    AP_INIT_FLAG("ImageResizeDebug", set_enable_debug, NULL, ACCESS_CONF,
                 "Enable debug logging"),
    {NULL}};

/**
 * Register hooks
 */
static void register_hooks(apr_pool_t *p)
{
    // Handler hook
    ap_hook_handler(image_resize_handler, NULL, NULL, APR_HOOK_MIDDLE);

    // Initialization hook - called at server startup
    ap_hook_post_config(image_resize_init, NULL, NULL, APR_HOOK_MIDDLE);
}

/**
 * Module declaration
 */
module AP_MODULE_DECLARE_DATA image_resize_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config, /* create per-dir config structures */
    NULL,              /* merge per-dir config structures */
    NULL,              /* create per-server config structures */
    NULL,              /* merge per-server config structures */
    image_resize_cmds, /* table of config file commands */
    register_hooks     /* register hooks */
};
