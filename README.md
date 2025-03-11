# mod_image_resize - Apache Image Resizing Module with libvips

Apache module for on-the-fly image resizing and compression using libvips for high-performance image processing.

**⚠️ WARNING: THIS TOOL IS IN VERY EARLY ALPHA STAGE ⚠️**

## Features

- **On-the-fly image resizing** via URL parameters
- **High-performance processing** using libvips (significantly faster than ImageMagick)
- **Thread-safe operation** ideal for mpm_worker and mpm_event
- **Multi-format support** for JPEG, PNG, GIF, and WebP
- **Optimized compression** using mozjpeg (for JPEG) and pngquant (for PNG)
- **Flexible cache system** that preserves directory structure
- **Aspect ratio preservation** for professional-looking resized images

## Why libvips?

This module uses libvips instead of ImageMagick for several key reasons:

1. **Performance**: libvips is typically 4-8x faster than ImageMagick
2. **Memory efficiency**: Uses significantly less memory, especially for large images
3. **Thread-safety**: Designed from the ground up to be thread-safe
4. **Stability**: No segmentation faults or race conditions in multi-threaded environments
5. **Modern design**: Clean, well-documented API

## Requirements

- Apache 2.4+ with development headers
- libvips 8.9+ (with development headers)
- pngquant (for PNG optimization)
- libimagequant-dev

## Installation

### 1. Install Dependencies

TODO, see Dockerfile

### 2. Compile and Install the Module

```bash
make
sudo make install
```

### 3. Configure Apache

```bash
sudo cp mod_image_resize.conf /etc/apache2/conf-available/
sudo a2enconf mod_image_resize
sudo systemctl restart apache2
```

## Usage

Images can be resized on-the-fly using URLs with the following format:

```
http://your-server.com/resized/640x480/path/to/image.jpg
```

This will resize the image located at `/var/www/images/path/to/image.jpg` to dimensions 640x480, optimizing and caching the result.

## Configuration

```apache
<Location /images>
    SetHandler image-resize
    
    # Source images directory
    ImageResizeSourceDir /var/www/images
    
    # Cache directory for resized images
    ImageResizeCacheDir /var/cache/apache2/image_resize
    
    # JPEG compression quality (0-100)
    ImageResizeJpegQuality 85
    
    # PNG compression quality (min max, 0-100)
    ImageResizePngQuality 65 80
    
    # Cache lifetime in seconds (1 day = 86400)
    ImageResizeCacheMaxAge 86400
    
    # Enable debug logging (On/Off)
    ImageResizeDebug Off
</Location>
```

## Docker Testing

A Dockerfile is included for easy testing in an isolated environment:

```bash
# Build the Docker image
docker build -t mod_image_resize .

# Run the container
docker run -d -p 8080:80 \
  -v $(PWD)/images:/var/www/images \
  -v image_resize_cache:/var/cache/apache2/image_resize \
  mod_image_resize
```

## Performance Considerations

- The module is thread-safe and works well with mpm_worker and mpm_event
- Cache files are protected by a mutex to prevent race conditions
- libvips is designed for high performance and minimal memory usage

## License

This module is released under the Apache License 2.0.

