/**
 * mod_image_resize_processing.c
 *
 * Image processing functions for resizing and compression.
 */

#include "mod_image_resize.h"
#include <wand/MagickWand.h>
#include <jpeglib.h>
#include <jerror.h>
#include <libimagequant.h>
#include <png.h>

/**
 * Read an image and resize it using MagickWand.
 */
image_data *read_and_resize_image(request_rec *r, const char *path, int width, int height, char **format)
{
    MagickWand *wand;
    image_data *img_data;
    unsigned char *data;
    size_t data_size;
    char *magick_format = NULL;

    // DEBUG_LOG(r, "Reading and resizing image: %s -> %dx%d", path, width, height);

    // Initialize MagickWand
    MagickWandGenesis();
    wand = NewMagickWand();

    // Read the image
    if (MagickReadImage(wand, path) == MagickFalse)
    {
        char *description;
        ExceptionType severity;

        description = MagickGetException(wand, &severity);
        DEBUG_LOG(r, "Error reading image: %s", description);
        MagickRelinquishMemory(description);

        DestroyMagickWand(wand);
        MagickWandTerminus();
        return NULL;
    }

    // Get actual image format if not determined yet
    if (*format == NULL || strcmp(*format, "unknown") == 0)
    {
        magick_format = MagickGetImageFormat(wand);

        if (magick_format)
        {
            DEBUG_LOG(r, "Detected format: %s", magick_format);

            // Convert to standard recognized format
            if (strcasecmp(magick_format, "JPEG") == 0 ||
                strcasecmp(magick_format, "JPG") == 0)
            {
                *format = "jpg";
            }
            else if (strcasecmp(magick_format, "PNG") == 0)
            {
                *format = "png";
            }
            else if (strcasecmp(magick_format, "GIF") == 0)
            {
                *format = "gif";
            }
            else if (strcasecmp(magick_format, "WEBP") == 0)
            {
                *format = "webp";
            }
            else
            {
                // Default format if not recognized
                *format = "jpg";
                DEBUG_LOG(r, "Format not recognized, using jpg as default");
            }

            MagickRelinquishMemory(magick_format);
        }
        else
        {
            // Default if no format detected
            *format = "jpg";
            DEBUG_LOG(r, "Unable to determine format, using jpg as default");
        }
    }

    DEBUG_LOG(r, "Image read successfully, resizing...");

    // Log original format for debugging
    magick_format = MagickGetImageFormat(wand);
    DEBUG_LOG(r, "Original image format: %s", magick_format ? magick_format : "unknown");
    if (magick_format)
        MagickRelinquishMemory(magick_format);

    // Get original dimensions
    size_t orig_width = MagickGetImageWidth(wand);
    size_t orig_height = MagickGetImageHeight(wand);
    DEBUG_LOG(r, "Original dimensions: %zux%zu", orig_width, orig_height);

    // Resize image - adaptation for different ImageMagick versions
    MagickBooleanType resize_result;
#if MagickLibVersion >= 0x700
    resize_result = MagickResizeImage(wand, width, height, LanczosFilter);
#else
    resize_result = MagickResizeImage(wand, width, height, LanczosFilter, 1.0);
#endif

    if (resize_result == MagickFalse)
    {
        char *description;
        ExceptionType severity;

        description = MagickGetException(wand, &severity);
        DEBUG_LOG(r, "Error resizing image: %s", description);
        MagickRelinquishMemory(description);

        DestroyMagickWand(wand);
        MagickWandTerminus();
        return NULL;
    }

    // Get dimensions after resizing
    int w = MagickGetImageWidth(wand);
    int h = MagickGetImageHeight(wand);
    DEBUG_LOG(r, "Dimensions after resizing: %dx%d", w, h);

    // Set RGBA format to ensure we have correct data
    MagickSetImageFormat(wand, "RGBA");

    // Extract pixels
    DEBUG_LOG(r, "Extracting image data...");
    data = MagickGetImageBlob(wand, &data_size);
    DEBUG_LOG(r, "Extracted data size: %zu bytes", data_size);

    if (!data || data_size == 0)
    {
        DEBUG_LOG(r, "Failed to extract image data");
        if (data)
            MagickRelinquishMemory(data);
        DestroyMagickWand(wand);
        MagickWandTerminus();
        return NULL;
    }

    // Create result structure
    img_data = apr_pcalloc(r->pool, sizeof(image_data));
    if (!img_data)
    {
        DEBUG_LOG(r, "Memory allocation error for ImageData structure");
        MagickRelinquishMemory(data);
        DestroyMagickWand(wand);
        MagickWandTerminus();
        return NULL;
    }

    // Allocate memory for data in the APR pool
    img_data->data = apr_pcalloc(r->pool, data_size);
    if (!img_data->data)
    {
        DEBUG_LOG(r, "Memory allocation error for image data");
        MagickRelinquishMemory(data);
        DestroyMagickWand(wand);
        MagickWandTerminus();
        return NULL;
    }

    // Copy data
    memcpy(img_data->data, data, data_size);
    MagickRelinquishMemory(data);

    img_data->size = data_size;
    img_data->width = w;
    img_data->height = h;
    img_data->channels = 4; // RGBA default

    DEBUG_LOG(r, "ImageData structure created successfully");

    // Clean up MagickWand
    DestroyMagickWand(wand);
    MagickWandTerminus();

    return img_data;
}

/**
 * Compress a JPEG image using libjpeg.
 */
int compress_jpeg(request_rec *r, const image_data *img_data,
                  const char *output_path, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *outfile;
    JSAMPROW row_pointer[1];

    DEBUG_LOG(r, "Compressing JPEG to: %s (quality: %d)", output_path, quality);

    // Open output file
    if ((outfile = fopen(output_path, "wb")) == NULL)
    {
        DEBUG_LOG(r, "Cannot open output file for writing");
        return -1;
    }

    // Initialize error handler
    cinfo.err = jpeg_std_error(&jerr);

    // Initialize compression structure
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    // Set compression parameters
    cinfo.image_width = img_data->width;
    cinfo.image_height = img_data->height;
    cinfo.input_components = 3; // RGB (ignore alpha channel)
    cinfo.in_color_space = JCS_RGB;

    DEBUG_LOG(r, "Configuring compression: %dx%d, %d components",
              cinfo.image_width, cinfo.image_height, cinfo.input_components);

    // Apply default settings and modify them
    jpeg_set_defaults(&cinfo);

    // Set quality (0-100)
    jpeg_set_quality(&cinfo, quality, TRUE);

    // Enable progressive mode (like mozjpeg)
    jpeg_simple_progression(&cinfo);

    // Start compression
    jpeg_start_compress(&cinfo, TRUE);

    // Prepare RGB data from RGBA (without alpha)
    int row_stride = img_data->width * 3; // 3 for RGB
    unsigned char *rgb_data = apr_pcalloc(r->pool, row_stride * img_data->height);
    if (!rgb_data)
    {
        DEBUG_LOG(r, "Memory allocation error for RGB data");
        jpeg_destroy_compress(&cinfo);
        fclose(outfile);
        return -1;
    }

    // Convert RGBA -> RGB (ignore alpha channel)
    for (int y = 0; y < img_data->height; y++)
    {
        for (int x = 0; x < img_data->width; x++)
        {
            for (int c = 0; c < 3; c++)
            { // Only R, G, B
                rgb_data[y * row_stride + x * 3 + c] =
                    img_data->data[y * img_data->width * 4 + x * 4 + c];
            }
        }
    }

    DEBUG_LOG(r, "Data converted from RGBA to RGB");

    // Compress line by line
    while (cinfo.next_scanline < cinfo.image_height)
    {
        // Pointer to current line
        row_pointer[0] = &rgb_data[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    // Finish compression
    jpeg_finish_compress(&cinfo);
    DEBUG_LOG(r, "JPEG compression complete");

    // Clean up resources
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);

    return 0;
}

/**
 * Compress a PNG image using libimagequant.
 */
int compress_png(request_rec *r, const image_data *img_data,
                 const char *output_path, int quality_min, int quality_max)
{
    liq_attr *attr;
    liq_image *image;
    liq_result *result;
    FILE *outfile;

    DEBUG_LOG(r, "Compressing PNG to: %s (quality: %d-%d)",
              output_path, quality_min, quality_max);

    // Initialize libimagequant
    attr = liq_attr_create();
    if (!attr)
    {
        DEBUG_LOG(r, "Failed to create libimagequant attribute");
        return -1;
    }

    // Set quality (0-100)
    liq_set_quality(attr, quality_min, quality_max);

    // Check image data before quantization
    DEBUG_LOG(r, "Image data: %dx%d, %zu bytes",
              img_data->width, img_data->height, img_data->size);

    if (img_data->data == NULL || img_data->size == 0)
    {
        DEBUG_LOG(r, "Invalid image data for quantization");
        liq_attr_destroy(attr);
        return -1;
    }

    // Create image from data
    image = liq_image_create_rgba(attr, img_data->data,
                                  img_data->width, img_data->height, 0);
    if (!image)
    {
        DEBUG_LOG(r, "Failed to create libimagequant image");
        liq_attr_destroy(attr);
        return -1;
    }

    DEBUG_LOG(r, "Image created for quantization");

    // Quantize image
    int quant_result = liq_image_quantize(image, attr, &result);
    if (quant_result != LIQ_OK)
    {
        DEBUG_LOG(r, "Quantization failed: code %d", quant_result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    DEBUG_LOG(r, "Image quantized successfully");

    // Allocate memory for quantized image
    size_t pixels_size = img_data->width * img_data->height;
    unsigned char *pixels = apr_pcalloc(r->pool, pixels_size);
    if (!pixels)
    {
        DEBUG_LOG(r, "Memory allocation error for quantized pixels");
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    // Get quantized image
    liq_write_remapped_image(result, image, pixels, pixels_size);
    DEBUG_LOG(r, "Remapped image obtained");

    // Get palette
    const liq_palette *palette = liq_get_palette(result);
    DEBUG_LOG(r, "Palette obtained with %d colors", palette->count);

    // Open output PNG file
    outfile = fopen(output_path, "wb");
    if (!outfile)
    {
        DEBUG_LOG(r, "Cannot open output PNG file");
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    // Initialize libpng for writing
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        DEBUG_LOG(r, "Failed to create PNG structure");
        fclose(outfile);
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    // Initialize PNG info structure
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        DEBUG_LOG(r, "Failed to create PNG info structure");
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(outfile);
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    // Error handling setup
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        DEBUG_LOG(r, "Error during PNG writing");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(outfile);
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    // Initialize writing
    png_init_io(png_ptr, outfile);

    // Write header
    png_set_IHDR(png_ptr, info_ptr, img_data->width, img_data->height, 8,
                 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    // Configure palette
    png_color png_palette[256];
    for (int i = 0; i < palette->count; i++)
    {
        png_palette[i].red = palette->entries[i].r;
        png_palette[i].green = palette->entries[i].g;
        png_palette[i].blue = palette->entries[i].b;
    }
    png_set_PLTE(png_ptr, info_ptr, png_palette, palette->count);

    // Configure transparency if needed
    if (palette->count > 0 && palette->entries[0].a < 255)
    {
        DEBUG_LOG(r, "Configuring PNG transparency");
        png_byte trans[256];
        for (int i = 0; i < palette->count; i++)
        {
            trans[i] = palette->entries[i].a;
        }
        png_set_tRNS(png_ptr, info_ptr, trans, palette->count, NULL);
    }

    // Write header
    png_write_info(png_ptr, info_ptr);
    DEBUG_LOG(r, "PNG header written");

    // Allocate row pointers
    png_bytep *row_pointers = apr_pcalloc(r->pool, sizeof(png_bytep) * img_data->height);
    if (!row_pointers)
    {
        DEBUG_LOG(r, "Memory allocation error for PNG row pointers");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(outfile);
        liq_result_destroy(result);
        liq_image_destroy(image);
        liq_attr_destroy(attr);
        return -1;
    }

    for (int y = 0; y < img_data->height; y++)
    {
        row_pointers[y] = pixels + y * img_data->width;
    }

    // Write image data
    png_write_image(png_ptr, row_pointers);
    DEBUG_LOG(r, "PNG image data written");

    // Finish writing
    png_write_end(png_ptr, NULL);
    DEBUG_LOG(r, "PNG writing complete");

    // Clean up
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(outfile);
    liq_result_destroy(result);
    liq_image_destroy(image);
    liq_attr_destroy(attr);

    return 0;
}

/**
 * Process an image - resize and compress according to format.
 */
int process_image(request_rec *r, const image_resize_config *cfg,
                  const image_request *req, const char *output_path)
{
    char input_path[512];
    image_data *img_data;
    int ret = -1;
    char *format = req->format;

    // Build path to source image
    apr_snprintf(input_path, sizeof(input_path), "%s/%s", cfg->image_dir, req->filename);
    DEBUG_LOG(r, "Source image path: %s", input_path ? input_path : "NULL");

    // Check if source image exists
    apr_finfo_t finfo;
    if (apr_stat(&finfo, input_path, APR_FINFO_TYPE, r->pool) != APR_SUCCESS)
    {
        DEBUG_LOG(r, "Source image not found");
        return -1; // File not found
    }

    DEBUG_LOG(r, "Output path: %s", output_path ? output_path : "NULL");

    // Ensure parent directory exists before processing
    if (ensure_parent_directory_exists(r, output_path) != 0)
    {
        DEBUG_LOG(r, "Failed to create parent directory for cache file");
        return -1;
    }

    // If format is unknown, detect image type
    if (strcmp(format, "unknown") == 0)
    {
        char format_buffer[10] = {0};
        if (detect_image_type(r, input_path, format_buffer, sizeof(format_buffer)) != 0)
        {
            DEBUG_LOG(r, "Could not determine image type");
            return -1;
        }

        format = apr_pstrdup(r->pool, format_buffer);
        DEBUG_LOG(r, "Detected image type: %s", format ? format : "NULL");
    }

    // Read and resize image
    char *format_str = apr_pstrdup(r->pool, req->format);
    img_data = read_and_resize_image(r, input_path, req->width, req->height, &format_str);
    if (!img_data)
    {
        DEBUG_LOG(r, "Failed to resize image");
        return -1;
    }

    DEBUG_LOG(r, "Image resized successfully, compressing...");

    // Compress according to format
    if (strcmp(format, "jpg") == 0)
    {
        ret = compress_jpeg(r, img_data, output_path, cfg->jpeg_quality);
    }
    else if (strcmp(format, "png") == 0)
    {
        ret = compress_png(r, img_data, output_path, cfg->png_quality_min, cfg->png_quality_max);
    }
    else
    {
        DEBUG_LOG(r, "Unsupported format for compression: %s", format);
        // Default to JPEG
        ret = compress_jpeg(r, img_data, output_path, cfg->jpeg_quality);
    }

    DEBUG_LOG(r, "Compression result: %s (code: %d)",
              ret == 0 ? "success" : "failure", ret);

    // Check output file size
    if (apr_stat(&finfo, output_path, APR_FINFO_SIZE, r->pool) == APR_SUCCESS)
    {
        DEBUG_LOG(r, "Output file size: %" APR_OFF_T_FMT " bytes", finfo.size);
    }
    else
    {
        DEBUG_LOG(r, "Unable to get output file size");
    }

    return ret;
}
