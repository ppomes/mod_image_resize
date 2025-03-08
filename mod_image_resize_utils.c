/**
 * mod_image_resize_utils.c
 *
 * Utility functions for the image resize module.
 */

#include "mod_image_resize.h"
#include <regex.h>
#include <MagickWand/MagickWand.h>

/**
 * Parse the URL and extract image request information.
 * Expected URL format: /WIDTHxHEIGHT/path/to/filename.ext
 */
image_request *parse_url(request_rec *r, const char *url)
{
    regex_t regex;
    regmatch_t matches[4];
    image_request *req;

    DEBUG_LOG(r, "Parsing URL: %s", url);

    // Format: /WIDTHxHEIGHT/path/to/filename.ext
    // This regex supports subdirectories in the path
    int ret = regcomp(&regex, "/([0-9]+)x([0-9]+)/(.+\\.[^\\/]+)$", REG_EXTENDED);
    if (ret)
    {
        DEBUG_LOG(r, "Error compiling regex");
        return NULL;
    }

    ret = regexec(&regex, url, 4, matches, 0);
    if (ret != 0)
    {
        DEBUG_LOG(r, "URL does not match expected format");
        regfree(&regex);
        return NULL;
    }

    req = apr_pcalloc(r->pool, sizeof(image_request));
    if (!req)
    {
        DEBUG_LOG(r, "Memory allocation error for request");
        regfree(&regex);
        return NULL;
    }

    // Extract width
    char width_str[10] = {0};
    size_t width_len = matches[1].rm_eo - matches[1].rm_so;
    if (width_len >= sizeof(width_str))
        width_len = sizeof(width_str) - 1;
    strncpy(width_str, url + matches[1].rm_so, width_len);
    req->width = atoi(width_str);

    // Extract height
    char height_str[10] = {0};
    size_t height_len = matches[2].rm_eo - matches[2].rm_so;
    if (height_len >= sizeof(height_str))
        height_len = sizeof(height_str) - 1;
    strncpy(height_str, url + matches[2].rm_so, height_len);
    req->height = atoi(height_str);

    // Extract filename with path
    req->filename = apr_pcalloc(r->pool, matches[3].rm_eo - matches[3].rm_so + 1);
    if (!req->filename)
    {
        DEBUG_LOG(r, "Memory allocation error for filename");
        regfree(&regex);
        return NULL;
    }
    strncpy(req->filename, url + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);

    // Will determine actual format when loading
    req->format = apr_pstrdup(r->pool, "unknown");

    DEBUG_LOG(r, "Parsed request: filename=%s, dimensions=%dx%d, initial format=unknown",
              req->filename, req->width, req->height);

    regfree(&regex);
    return req;
}

/**
 * Detect image type based on content (not extension).
 */
int detect_image_type(request_rec *r, const char *path, char *format_buffer, size_t buffer_size)
{
    MagickWand *wand;
    const char *format = NULL;
    char *magick_format = NULL;

    DEBUG_LOG(r, "Detecting image type for: %s", path);

    // Initialize MagickWand
    apr_thread_mutex_lock(imagemagick_mutex);
    wand = NewMagickWand();

    // Read the image
    if (MagickReadImage(wand, path) == MagickFalse)
    {
        DEBUG_LOG(r, "Error reading image for type detection");
        DestroyMagickWand(wand);
        apr_thread_mutex_unlock(imagemagick_mutex);
        return -1;
    }

    // Get the actual image format
    magick_format = MagickGetImageFormat(wand);

    if (magick_format)
    {
        DEBUG_LOG(r, "Format detected by ImageMagick: %s", magick_format);

        // Convert to standard recognized format
        if (strcasecmp(magick_format, "JPEG") == 0 ||
            strcasecmp(magick_format, "JPG") == 0)
        {
            strncpy(format_buffer, "jpg", buffer_size);
        }
        else if (strcasecmp(magick_format, "PNG") == 0)
        {
            strncpy(format_buffer, "png", buffer_size);
        }
        else if (strcasecmp(magick_format, "GIF") == 0)
        {
            strncpy(format_buffer, "gif", buffer_size);
        }
        else if (strcasecmp(magick_format, "WEBP") == 0)
        {
            strncpy(format_buffer, "webp", buffer_size);
        }
        else
        {
            // Default format if not recognized
            format = apr_pstrdup(r->pool, "jpg");
            DEBUG_LOG(r, "Format not recognized, using jpg as default");
        }

        MagickRelinquishMemory(magick_format);
    }
    else
    {
        // Default if no format detected
        format = apr_pstrdup(r->pool, "jpg");
        DEBUG_LOG(r, "Unable to determine format, using jpg as default");
    }

    // Clean up resources
    DestroyMagickWand(wand);

    apr_thread_mutex_unlock(imagemagick_mutex);

    DEBUG_LOG(r, "Final detected format: %s", format);
    return 0;
}

/**
 * Ensure a directory exists, creating it recursively if needed.
 */
apr_status_t ensure_directory_exists(apr_pool_t *pool, const char *dir)
{
    apr_status_t rv;
    apr_finfo_t finfo;

    // Check if directory exists
    rv = apr_stat(&finfo, dir, APR_FINFO_TYPE, pool);
    if (rv == APR_SUCCESS)
    {
        if (finfo.filetype == APR_DIR)
        {
            return APR_SUCCESS;
        }
        return APR_ENOTDIR;
    }

    // Create directory recursively
    rv = apr_dir_make_recursive(dir, APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE | APR_FPROT_GREAD | APR_FPROT_GEXECUTE | APR_FPROT_WREAD | APR_FPROT_WEXECUTE, pool);
    return rv;
}

/**
 * Ensure parent directory of a file exists, creating it if needed.
 */
int ensure_parent_directory_exists(request_rec *r, const char *file_path)
{
    char dir_path[512];
    char *last_slash;

    // Copy file path
    strncpy(dir_path, file_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    // Find last slash to get parent directory
    last_slash = strrchr(dir_path, '/');
    if (!last_slash)
    {
        return 0; // No slash, no directory to create
    }

    // Terminate string at last slash to get just the directory path
    *last_slash = '\0';

    // Check if directory already exists
    apr_finfo_t finfo;
    if (apr_stat(&finfo, dir_path, APR_FINFO_TYPE, r->pool) == APR_SUCCESS)
    {
        if (finfo.filetype == APR_DIR)
        {
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

    if (rv != APR_SUCCESS)
    {
        DEBUG_LOG(r, "Failed to create parent directory: %s (code: %d)",
                  dir_path, rv);
        return -1;
    }

    // Ensure permissions are correct for Apache
    if (chown(dir_path, geteuid(), getegid()) != 0)
    {
        DEBUG_LOG(r, "Warning: Unable to change directory owner: %s",
                  dir_path);
    }

    return 0;
}
