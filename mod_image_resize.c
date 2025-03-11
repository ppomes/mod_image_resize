/**
 * mod_image_resize.c
 * 
 * Apache module for on-the-fly image resizing and compression.
 * Main module file implementing request handling and configuration.
 * Uses libvips for image processing, which is thread-safe and high-performance.
 */

#include "mod_image_resize.h"
#include <vips/vips.h>
#include <regex.h>
#include <libimagequant.h>
#include <png.h>

// Global mutex for cache operations
static apr_thread_mutex_t *cache_mutex = NULL;

// Track libvips initialization in child processes
static int libvips_initialized = 0;

// Function to parse URL and extract dimensions/filename
static image_request* parse_url(request_rec *r, const char *url) {
    regex_t regex;
    regmatch_t matches[4];
    image_request* req;
    
    DEBUG_LOG(r, "Parsing URL: %s", url);
    
    // Format: /WIDTHxHEIGHT/path/to/filename.ext
    int ret = regcomp(&regex, "/([0-9]+)x([0-9]+)/(.+\\.[^\\/]+)$", REG_EXTENDED);
    if (ret) {
        DEBUG_LOG(r, "Error compiling regex");
        return NULL;
    }
    
    ret = regexec(&regex, url, 4, matches, 0);
    if (ret != 0) {
        DEBUG_LOG(r, "URL does not match expected format");
        regfree(&regex);
        return NULL;
    }
    
    req = apr_pcalloc(r->pool, sizeof(image_request));
    if (!req) {
        DEBUG_LOG(r, "Memory allocation error for request");
        regfree(&regex);
        return NULL;
    }
    
    // Extract width
    char width_str[10] = {0};
    size_t width_len = matches[1].rm_eo - matches[1].rm_so;
    if (width_len >= sizeof(width_str)) width_len = sizeof(width_str) - 1;
    strncpy(width_str, url + matches[1].rm_so, width_len);
    req->width = atoi(width_str);
    
    // Extract height
    char height_str[10] = {0};
    size_t height_len = matches[2].rm_eo - matches[2].rm_so;
    if (height_len >= sizeof(height_str)) height_len = sizeof(height_str) - 1;
    strncpy(height_str, url + matches[2].rm_so, height_len);
    req->height = atoi(height_str);
    
    // Extract filename with path
    req->filename = apr_pcalloc(r->pool, matches[3].rm_eo - matches[3].rm_so + 1);
    if (!req->filename) {
        DEBUG_LOG(r, "Memory allocation error for filename");
        regfree(&regex);
        return NULL;
    }
    strncpy(req->filename, url + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);
    
    // Extract format from extension
    const char *ext = strrchr(req->filename, '.');
    if (ext) {
        ext++; // Skip the dot
        if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0)
            req->format = apr_pstrdup(r->pool, "jpg");
        else if (strcasecmp(ext, "png") == 0)
            req->format = apr_pstrdup(r->pool, "png");
        else if (strcasecmp(ext, "gif") == 0)
            req->format = apr_pstrdup(r->pool, "gif");
        else if (strcasecmp(ext, "webp") == 0)
            req->format = apr_pstrdup(r->pool, "webp");
        else
            req->format = apr_pstrdup(r->pool, "unknown");
    } else {
        req->format = apr_pstrdup(r->pool, "unknown");
    }
    
    DEBUG_LOG(r, "Parsed request: filename=%s, dimensions=%dx%d, format=%s", 
             req->filename, req->width, req->height, req->format);
    
    regfree(&regex);
    return req;
}

// Ensure a directory exists, creating it recursively if needed
static apr_status_t ensure_directory_exists(apr_pool_t *pool, const char *dir) {
    apr_status_t rv;
    apr_finfo_t finfo;
    
    // Check if directory exists
    rv = apr_stat(&finfo, dir, APR_FINFO_TYPE, pool);
    if (rv == APR_SUCCESS) {
        if (finfo.filetype == APR_DIR) {
            return APR_SUCCESS;
        }
        return APR_ENOTDIR;
    }
    
    // Create directory recursively
    rv = apr_dir_make_recursive(dir, APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE |
                                     APR_FPROT_GREAD | APR_FPROT_GEXECUTE |
                                     APR_FPROT_WREAD | APR_FPROT_WEXECUTE, pool);
    return rv;
}

// Ensure parent directory of a file exists
static int ensure_parent_directory_exists(request_rec *r, const char *file_path) {
    char dir_path[512];
    char *last_slash;
    
    // Copy file path
    strncpy(dir_path, file_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    
    // Find last slash to get parent directory
    last_slash = strrchr(dir_path, '/');
    if (!last_slash) {
        return 0; // No slash, no directory to create
    }
    
    // Terminate string at last slash to get just the directory path
    *last_slash = '\0';
    
    // Check if directory already exists
    apr_finfo_t finfo;
    if (apr_stat(&finfo, dir_path, APR_FINFO_TYPE, r->pool) == APR_SUCCESS) {
        if (finfo.filetype == APR_DIR) {
            return 0; // Directory already exists
        }
        DEBUG_LOG(r, "Path exists but is not a directory: %s", dir_path);
        return -1;
    }
    
    DEBUG_LOG(r, "Creating parent directory recursively: %s", dir_path);
    
    // Create directory recursively
    apr_status_t rv = apr_dir_make_recursive(dir_path, 
                                           APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE |
                                           APR_FPROT_GREAD | APR_FPROT_GEXECUTE |
                                           APR_FPROT_WREAD | APR_FPROT_WEXECUTE, 
                                           r->pool);
    
    if (rv != APR_SUCCESS) {
        DEBUG_LOG(r, "Failed to create parent directory: %s (code: %d)", 
                 dir_path, rv);
        return -1;
    }
    
    // Ensure permissions are correct for Apache
    if (chown(dir_path, geteuid(), getegid()) != 0) {
        DEBUG_LOG(r, "Warning: Unable to change directory owner: %s", 
                 dir_path);
    }
    
    return 0;
}

// Process an image with libvips - resize and compress according to format
static int process_image(request_rec *r, const image_resize_config *cfg, 
                       const image_request *req, const char *output_path) {
    char input_path[512];
    int ret = -1;
    VipsImage *in = NULL, *out = NULL;
    
    // Check if libvips is initialized
    if (!libvips_initialized) {
        DEBUG_LOG(r, "LibVips not initialized, cannot process image");
        return -1;
    }
    
    // Build path to source image
    apr_snprintf(input_path, sizeof(input_path), "%s/%s", cfg->image_dir, req->filename);
    DEBUG_LOG(r, "Source image path: %s", input_path);
    
    // Check if source image exists
    apr_finfo_t finfo;
    if (apr_stat(&finfo, input_path, APR_FINFO_TYPE, r->pool) != APR_SUCCESS) {
        DEBUG_LOG(r, "Source image not found");
        return -1; // File not found
    }
    
    DEBUG_LOG(r, "Output path: %s", output_path);
    
    // Ensure parent directory exists before processing
    if (ensure_parent_directory_exists(r, output_path) != 0) {
        DEBUG_LOG(r, "Failed to create parent directory for cache file");
        return -1;
    }
    
    // Load the image
    if (!(in = vips_image_new_from_file(input_path, NULL))) {
        DEBUG_LOG(r, "Failed to load image: %s", vips_error_buffer());
        vips_error_clear();
        return -1;
    }
    
    DEBUG_LOG(r, "Image loaded, original size: %dx%d", 
             vips_image_get_width(in), vips_image_get_height(in));
    
    // Calculate scale factors preserving aspect ratio
    double scale_x = (double)req->width / vips_image_get_width(in);
    double scale_y = (double)req->height / vips_image_get_height(in);
    double scale = scale_x < scale_y ? scale_x : scale_y;
    
    DEBUG_LOG(r, "Scaling factor: %f", scale);
    
    // Resize the image
    if (vips_resize(in, &out, scale, NULL)) {
        DEBUG_LOG(r, "Resize failed: %s", vips_error_buffer());
        vips_error_clear();
        g_object_unref(in);
        return -1;
    }
    
    DEBUG_LOG(r, "Image resized to: %dx%d", 
             vips_image_get_width(out), vips_image_get_height(out));
    
    // Save according to format with appropriate compression
    if (strcmp(req->format, "jpg") == 0) {
        // JPEG saving with mozjpeg (if available)
        if (vips_jpegsave(out, output_path, 
                         "Q", cfg->jpeg_quality, 
                         "optimize_coding", TRUE,
                         NULL)) {
            DEBUG_LOG(r, "JPEG save failed: %s", vips_error_buffer());
            vips_error_clear();
            g_object_unref(in);
            g_object_unref(out);
            return -1;
        }
        ret = 0;
    }
    else if (strcmp(req->format, "png") == 0) {
        // PNG saving with integrated libimagequant optimization
        if (vips_pngsave(out, output_path, 
                        "palette", TRUE,           // Use color palette
                        "Q", cfg->png_quality_min, // Quality
                        "compression", 9,          // Maximum compression
                        "interlace", TRUE,         // Progressive loading
                        NULL)) {
            DEBUG_LOG(r, "PNG save failed: %s", vips_error_buffer());
            vips_error_clear();
            g_object_unref(in);
            g_object_unref(out);
            return -1;
        }
        ret = 0;
    }
    else if (strcmp(req->format, "webp") == 0) {
        // WebP saving
        if (vips_webpsave(out, output_path, "Q", 75, NULL)) {
            DEBUG_LOG(r, "WebP save failed: %s", vips_error_buffer());
            vips_error_clear();
            g_object_unref(in);
            g_object_unref(out);
            return -1;
        }
        ret = 0;
    }
    else if (strcmp(req->format, "gif") == 0) {
        // GIF saving - check if supported in this libvips version
        #if (VIPS_MAJOR_VERSION > 8 || (VIPS_MAJOR_VERSION == 8 && VIPS_MINOR_VERSION >= 9))
            // GIF saving with libvips 8.9+
            if (vips_gifsave(out, output_path, NULL)) {
                DEBUG_LOG(r, "GIF save failed: %s", vips_error_buffer());
                vips_error_clear();
                g_object_unref(in);
                g_object_unref(out);
                return -1;
            }
        #else
            // Fallback for older libvips versions
            DEBUG_LOG(r, "GIF saving not supported in this libvips version, using PNG");
            if (vips_pngsave(out, output_path, NULL)) {
                DEBUG_LOG(r, "Fallback PNG save failed: %s", vips_error_buffer());
                vips_error_clear();
                g_object_unref(in);
                g_object_unref(out);
                return -1;
            }
        #endif
        ret = 0;
    }
    else {
        DEBUG_LOG(r, "Unsupported format: %s", req->format);
        // Default to JPEG
        if (vips_jpegsave(out, output_path, "Q", cfg->jpeg_quality, NULL)) {
            DEBUG_LOG(r, "Default JPEG save failed: %s", vips_error_buffer());
            vips_error_clear();
            g_object_unref(in);
            g_object_unref(out);
            return -1;
        }
        ret = 0;
    }
    
    // Clean up
    g_object_unref(in);
    g_object_unref(out);
    
    // Check output file size
    if (apr_stat(&finfo, output_path, APR_FINFO_SIZE, r->pool) == APR_SUCCESS) {
        DEBUG_LOG(r, "Output file size: %" APR_OFF_T_FMT " bytes", finfo.size);
    } else {
        DEBUG_LOG(r, "Unable to get output file size");
    }
    
    return ret;
}

// Handle cache checking, directory creation, and mutex locking
static int process_image_with_cache(request_rec *r, const image_resize_config *cfg, 
                                  const image_request *req, char *cache_path, size_t path_size) {
    int status = -1;
    
    // Build cache path preserving subdirectory structure
    apr_snprintf(cache_path, path_size, "%s/%dx%d_%s", 
                cfg->cache_dir, req->width, req->height, req->filename);
    
    DEBUG_LOG(r, "Cache check/write: %s", cache_path);
    
    // Lock for cache operations
    apr_thread_mutex_lock(cache_mutex);
    
    // Check if image exists in cache
    apr_finfo_t finfo;
    if (apr_stat(&finfo, cache_path, APR_FINFO_SIZE, r->pool) == APR_SUCCESS) {
        // Image already exists in cache
        DEBUG_LOG(r, "Image found in cache");
        status = 0; // Success
    } 
    else {
        DEBUG_LOG(r, "Image not found in cache, processing...");
        
        // Ensure cache base directory exists
        if (ensure_directory_exists(r->pool, cfg->cache_dir) != APR_SUCCESS) {
            DEBUG_LOG(r, "Failed to create cache directory: %s", cfg->cache_dir);
            apr_thread_mutex_unlock(cache_mutex);
            return -1;
        }
        
        // Process the image
        status = process_image(r, cfg, req, cache_path);
        
        if (status == 0) {
            DEBUG_LOG(r, "Image processed and cached successfully");
        } else {
            DEBUG_LOG(r, "Failed to process image for cache");
        }
    }
    
    // Unlock
    apr_thread_mutex_unlock(cache_mutex);
    
    return status;
}

// Module cleanup function - called when pools are cleaned up
static apr_status_t image_resize_cleanup(void *data) {
    server_rec *s = (server_rec *)data;
    
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                "mod_image_resize: module cleanup complete");
    
    return APR_SUCCESS;
}

// Child initialization function - called for each child process
static void child_init(apr_pool_t *p, server_rec *s) {
    // Initialize libvips in each child process
    if (VIPS_INIT("mod_image_resize")) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, 
                    "mod_image_resize: could not initialize libvips: %s", 
                    vips_error_buffer());
        vips_error_clear();
    } else {
        libvips_initialized = 1;
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                    "mod_image_resize: libvips initialized in child process (using libvips %s)", 
                    vips_version_string());
    }
}

// Module initialization function - called at server startup
static int image_resize_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    // Create cache mutex
    apr_status_t status = apr_thread_mutex_create(&cache_mutex, APR_THREAD_MUTEX_DEFAULT, p);
    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, s, 
                    "mod_image_resize: unable to create cache mutex");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    
    // Do NOT initialize libvips here - it will be done in child_init
    
    // Register cleanup function
    apr_pool_cleanup_register(p, s, image_resize_cleanup, apr_pool_cleanup_null);
    
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, 
                "mod_image_resize: module initialized");
    
    return OK;
}

// HTTP request handler
static int image_resize_handler(request_rec *r) {
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
    cfg = (image_resize_config*) ap_get_module_config(r->per_dir_config, &image_resize_module);
    
    DEBUG_LOG(r, "New request: %s", r->uri);
    
    // Parse URL
    req = parse_url(r, r->uri);
    if (!req) {
        DEBUG_LOG(r, "Invalid URL: %s", r->uri);
        return HTTP_BAD_REQUEST;
    }
    
    // Check cache and process image if needed
    if (process_image_with_cache(r, cfg, req, cache_path, sizeof(cache_path)) != 0) {
        DEBUG_LOG(r, "Error processing image");
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    
    // Open cached file
    DEBUG_LOG(r, "Opening cached file: %s", cache_path);
    if ((rv = apr_file_open(&fd, cache_path, APR_READ, APR_OS_DEFAULT, r->pool)) != APR_SUCCESS) {
        DEBUG_LOG(r, "Cannot open cached file: %s", 
                 apr_strerror(rv, cache_path, 100));
        return HTTP_NOT_FOUND;
    }
    
    // Get file size
    if ((rv = apr_file_info_get(&finfo, APR_FINFO_SIZE, fd)) != APR_SUCCESS) {
        DEBUG_LOG(r, "Cannot get file size: %s",
                 apr_strerror(rv, cache_path, 100));
        apr_file_close(fd);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    
    // Set response headers based on format
    if (strcmp(req->format, "jpg") == 0) {
        content_type = "image/jpeg";
    } else if (strcmp(req->format, "png") == 0) {
        content_type = "image/png";
    } else if (strcmp(req->format, "gif") == 0) {
        content_type = "image/gif";
    } else if (strcmp(req->format, "webp") == 0) {
        content_type = "image/webp";
    } else {
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
    if (r->header_only) {
        apr_file_close(fd);
        return OK;
    }
    
    rv = ap_send_fd(fd, r, 0, finfo.size, &finfo.size);
    
    // Close file
    apr_file_close(fd);
    
    return OK;
}

// Create per-directory configuration structure
static void *create_dir_config(apr_pool_t *p, char *arg) {
    image_resize_config *cfg = apr_pcalloc(p, sizeof(image_resize_config));
    
    if (cfg) {
        // Default values
        cfg->image_dir = "/var/www/images";     // Default directory
        cfg->cache_dir = "/var/cache/apache2/image_resize"; // Default cache
        cfg->jpeg_quality = 85;                 // Default JPEG quality
        cfg->png_quality_min = 65;              // Default PNG min quality
        cfg->png_quality_max = 80;              // Default PNG max quality
        cfg->cache_max_age = 86400;             // Default cache lifetime (1 day)
        cfg->enable_debug = 0;                  // Debug disabled by default
    }
    
    return cfg;
}

// Configuration command handlers
static const char *set_image_dir(cmd_parms *cmd, void *conf, const char *arg) {
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->image_dir = apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *set_cache_dir(cmd_parms *cmd, void *conf, const char *arg) {
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->cache_dir = apr_pstrdup(cmd->pool, arg);
    return NULL;
}

static const char *set_jpeg_quality(cmd_parms *cmd, void *conf, const char *arg) {
    image_resize_config *cfg = (image_resize_config *)conf;
    int val = atoi(arg);
    if (val < 0 || val > 100) {
        return "ImageResizeJpegQuality must be between 0 and 100";
    }
    cfg->jpeg_quality = val;
    return NULL;
}

static const char *set_png_quality(cmd_parms *cmd, void *conf, const char *arg1, const char *arg2) {
    image_resize_config *cfg = (image_resize_config *)conf;
    int min_val = atoi(arg1);
    int max_val = atoi(arg2);
    
    if (min_val < 0 || min_val > 100 || max_val < 0 || max_val > 100 || min_val > max_val) {
        return "ImageResizePngQuality must be between 0 and 100, and min <= max";
    }
    
    cfg->png_quality_min = min_val;
    cfg->png_quality_max = max_val;
    return NULL;
}

static const char *set_cache_max_age(cmd_parms *cmd, void *conf, const char *arg) {
    image_resize_config *cfg = (image_resize_config *)conf;
    int val = atoi(arg);
    if (val < 0) {
        return "ImageResizeCacheMaxAge must be a positive integer";
    }
    cfg->cache_max_age = val;
    return NULL;
}

static const char *set_enable_debug(cmd_parms *cmd, void *conf, int flag) {
    image_resize_config *cfg = (image_resize_config *)conf;
    cfg->enable_debug = flag;
    return NULL;
}

// Configuration commands table
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
    { NULL }
};

// Register hooks
static void register_hooks(apr_pool_t *p) {
    // Handler hook
    ap_hook_handler(image_resize_handler, NULL, NULL, APR_HOOK_MIDDLE);
    
    // Post-config hook - called at server startup
    ap_hook_post_config(image_resize_init, NULL, NULL, APR_HOOK_MIDDLE);
    
    // Child-init hook - called in each child process
    ap_hook_child_init(child_init, NULL, NULL, APR_HOOK_MIDDLE);
}

// Module declaration
module AP_MODULE_DECLARE_DATA image_resize_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,       /* create per-dir config structures */
    NULL,                    /* merge per-dir config structures */
    NULL,                    /* create per-server config structures */
    NULL,                    /* merge per-server config structures */
    image_resize_cmds,       /* table of config file commands */
    register_hooks           /* register hooks */
};
