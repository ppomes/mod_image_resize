# mod_image_resize - Apache Image Resizing Module with libvips

Apache module for on-the-fly image resizing and compression using libvips for high-performance image processing.

**⚠️ WARNING: THIS TOOL IS IN VERY EARLY ALPHA STAGE ⚠️**

## Features

- **On-the-fly image resizing** via URL parameters
- **High-performance processing** using libvips
- **Thread-safe operation** ideal for mpm_worker and mpm_event
- **Multi-format support** for JPEG, PNG, GIF, and WebP
- **Optimized compression** using a unified quality factor for all image formats
- **Smart caching** with optional source modification time checking
- **Flexible cache system** that preserves directory structure
- **Aspect ratio preservation** for resized images

## Why libvips?

This module uses libvips for several key advantages:

1. **Exceptional performance**: Process images rapidly with minimal resource usage
2. **Memory efficiency**: Optimized streaming architecture that minimizes memory footprint
3. **Thread-safety**: Designed from the ground up for concurrent operations
4. **Stability**: Robust behavior in multi-threaded environments
5. **Modern design**: Clean, well-documented API for easy integration

## Requirements

- Apache 2.4+ with development headers
- libvips 8.9+ (with development headers)
- pngquant (for PNG optimization)
- libimagequant-dev
- mozjpeg (for JPG optimization)
- cgif (for GIF support)

## Installation

### Option 1: Manual Compilation

#### 1. Install Dependencies

See the Dockerfile for a complete list of dependencies and build instructions.

#### 2. Compile and Install the Module

```bash
make
sudo make install
```

#### 3. Configure Apache

```bash
sudo cp mod_image_resize.conf /etc/apache2/conf-available/
sudo a2enconf mod_image_resize
sudo systemctl restart apache2
```

### Option 2: Debian/Ubuntu Package (Tested on Ubuntu 24.04)

#### 1. Install Build Dependencies

```bash
sudo apt-get update
sudo apt-get install debhelper devscripts build-essential apache2-dev \
  cmake git nasm pkg-config libpng-dev libimagequant-dev pngquant \
  libarchive-dev libglib2.0-dev libexpat-dev libfftw3-dev libexif-dev \
  libtiff-dev libwebp-dev libgsf-1-dev liblcms2-dev libpango1.0-dev \
  librsvg2-dev meson ninja-build patchelf
```

#### 2. Build the Package

```bash
dpkg-buildpackage -us -uc -b
```

This will create a `.deb` package in the parent directory.

#### 3. Install the Package

```bash
sudo dpkg -i ../mod-image-resize_*.deb
sudo apt-get install -f  # To resolve any missing dependencies
```

The package includes:
- The Apache module installed to `/usr/lib/apache2/modules/`
- Custom compiled libvips with MozJPEG support
- MozJPEG optimized JPEG library in `/opt/mozjpeg/lib/`
- Integrated cgif library for efficient GIF support
- Apache configuration in `/etc/apache2/conf-available/`
- Cache directory in `/var/cache/apache2/image_resize/`

## Usage

Images can be resized on-the-fly using URLs with the following format:

```
http://your-server.com/resized/640x480/path/to/image.jpg
```

This will resize the image located at `/var/www/images/path/to/image.jpg` to dimensions 640x480, optimizing and caching the result.

## Configuration

```apache
<Location /resized>
    SetHandler image-resize
    
    # Source images directory
    ImageResizeSourceDir /var/www/images
    
    # Cache directory for resized images
    ImageResizeCacheDir /var/cache/apache2/image_resize
    
    # Universal image compression quality (0-100)
    ImageResizeQuality 75
    
    # Cache-Control max-age in seconds (1 day = 86400)
    ImageResizeCacheMaxAge 86400
    
    # Enable mutex for cache operations (On/Off)
    ImageResizeMutex On
    
    # Check if source image is newer than cached image (On/Off)
    ImageResizeCheckMTime Off
</Location>
```

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `ImageResizeSourceDir` | Directory containing source images | `/var/www/images` |
| `ImageResizeCacheDir` | Directory for storing resized images | `/var/cache/apache2/image_resize` |
| `ImageResizeQuality` | Universal image compression quality (0-100) | `75` |
| `ImageResizeCacheMaxAge` | Cache-Control max-age value in seconds | `86400` (1 day) |
| `ImageResizeMutex` | Enable mutex for cache operations | `On` |
| `ImageResizeCheckMTime` | Check if source image is newer than cached | `Off` |

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
- Cache operations can be protected by a mutex to prevent race conditions (configurable)
- The mutex is only used when writing to the cache, not when reading
- HTTP caching is controlled via Cache-Control and Expires headers (configurable with ImageResizeCacheMaxAge)
- libvips is optimized for high performance and minimal memory usage
- You can disable the mutex in environments where file locking is not necessary
- Setting `ImageResizeCheckMTime` to `On` may have a performance impact but ensures cache freshness

## HTTP Status Codes

- `404 Not Found`: Returned when the source image doesn't exist
- `400 Bad Request`: Returned for invalid URLs
- `500 Internal Server Error`: Returned for processing errors

## License

This module is released under the Apache License 2.0.
