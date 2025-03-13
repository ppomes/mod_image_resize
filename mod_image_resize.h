/**
 * mod_image_resize.h
 * 
 * Apache module for on-the-fly image resizing and compression.
 * Uses libvips for image processing, mozjpeg for JPEG compression,
 * and libimagequant for PNG optimization.
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
typedef struct {
    const char *image_dir;       // Source image directory
    const char *cache_dir;       // Cache directory
    int quality;                 // Universal image quality (0-100)
    int cache_max_age;           // Cache lifetime in seconds
    int enable_mutex;            // Enable cache mutex (0/1)
    int check_source_mtime;      // Check if source image is newer than cached image (0/1)
} image_resize_config;

// Request info structure
typedef struct {
    char* filename;              // Image filename (including path)
    int width;                   // Target width
    int height;                  // Target height
    char* format;                // "jpg", "png", "gif", "webp" or "unknown"
} image_request;

// Logging macros pour différents niveaux de sévérité

// Error logging macro - toujours visible si LogLevel est error ou supérieur
#define ERROR_LOG(r, fmt, ...) \
    do { \
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, \
                     "[image_resize] ERROR: " fmt, ##__VA_ARGS__); \
    } while (0)

// Warning logging macro - visible si LogLevel est warn ou supérieur
#define WARNING_LOG(r, fmt, ...) \
    do { \
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, \
                     "[image_resize] WARNING: " fmt, ##__VA_ARGS__); \
    } while (0)

// Info logging macro - visible si LogLevel est info ou supérieur
#define INFO_LOG(r, fmt, ...) \
    do { \
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, \
                     "[image_resize] INFO: " fmt, ##__VA_ARGS__); \
    } while (0)

// Debug logging macro - visible si LogLevel est debug ou supérieur
#define DEBUG_LOG(r, fmt, ...) \
    do { \
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, \
                     "[image_resize] DEBUG: " fmt, ##__VA_ARGS__); \
    } while (0)

#endif /* MOD_IMAGE_RESIZE_H */