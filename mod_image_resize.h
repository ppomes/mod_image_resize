/**
 * mod_image_resize.h
 *
 * Apache module for on-the-fly image resizing and compression.
 * Supports JPEG, PNG, GIF, and WebP formats with caching.
 */

#ifndef MOD_IMAGE_RESIZE_H
#define MOD_IMAGE_RESIZE_H

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <apr_strings.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_tables.h>
#include <apr_pools.h>
#include <apr_thread_mutex.h>
#include <util_filter.h>
#include <http_request.h>

// Module declaration
extern module AP_MODULE_DECLARE_DATA image_resize_module;

// Configuration structure
typedef struct
{
    const char *image_dir; // Source image directory
    const char *cache_dir; // Cache directory
    int jpeg_quality;      // JPEG quality (0-100)
    int png_quality_min;   // PNG min quality (0-100)
    int png_quality_max;   // PNG max quality (0-100)
    int cache_max_age;     // Cache lifetime in seconds
    int enable_debug;      // Enable debug logging
} image_resize_config;

// Request info structure
typedef struct
{
    char *filename; // Image filename (including path)
    int width;      // Target width
    int height;     // Target height
    char *format;   // "jpg", "png", "gif", "webp" or "unknown"
} image_request;

extern int imagemagick_initialized;
extern apr_thread_mutex_t *imagemagick_mutex;

// Image data structure
typedef struct
{
    unsigned char *data; // Image pixel data
    size_t size;         // Data size in bytes
    int width;           // Image width
    int height;          // Image height
    int channels;        // Number of color channels
} image_data;

#define DEBUG_LOG(r, fmt, ...)                                                                    \
    do                                                                                            \
    {                                                                                             \
        image_resize_config *cfg = ap_get_module_config(r->per_dir_config, &image_resize_module); \
        if (cfg && cfg->enable_debug)                                                             \
        {                                                                                         \
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,                                          \
                          "[image_resize] " fmt, ##__VA_ARGS__);                                  \
        }                                                                                         \
    } while (0)

#endif /* MOD_IMAGE_RESIZE_H */
