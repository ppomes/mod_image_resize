# mod_image_resize - Apache Image Resizing Module

A high-performance Apache module for on-the-fly image resizing and compression with caching capabilities.

**⚠️ WARNING: THIS TOOL IS IN VERY EARLY ALPHA STAGE ⚠️**


## Features

- **Dynamic image resizing** via URL parameters
- **Optimized compression** using mozjpeg and libimagequant
- **Multi-format support** for JPEG, PNG, GIF, and WebP
- **Content-based format detection** regardless of file extension
- **Multi-level caching** preserving directory structures
- **Thread-safe design** for MPM worker and event
- **Configurable quality settings** for each format
- **Support for subdirectories** in image paths

## Requirements

- Apache 2.4+ with development headers
- ImageMagick with MagickWand library
- mozjpeg (optimized JPEG compression)
- libimagequant (used by pngquant for PNG compression)
- libpng development headers

## Installation

### 1. Install Dependencies

```bash
# Install required packages
sudo apt-get update
sudo apt-get install -y \
    apache2-dev \
    build-essential \
    libmagickwand-dev \
    libjpeg-dev \
    libpng-dev \
    libimagequant-dev \
    pkg-config \
    cmake \
    nasm

# Install mozjpeg
git clone https://github.com/mozilla/mozjpeg.git
cd mozjpeg
cmake -G"Unix Makefiles" \
  -DCMAKE_INSTALL_PREFIX=/opt/mozjpeg \
  -DCMAKE_C_FLAGS="-fPIC" \
  -DCMAKE_CXX_FLAGS="-fPIC" \
  -DENABLE_SHARED=TRUE
make -j$(nproc)
sudo make install
cd ..
```

### 2. Compile and Install the Module

```bash
# Clone the repository
git clone https://github.com/yourusername/mod_image_resize.git
cd mod_image_resize

# Compile the module
make

# Install the module
sudo make install

# Enable the module configuration
sudo make config

# Restart Apache
sudo systemctl restart apache2
```

## Usage

Once installed, you can access resized images using URLs like:

```
http://your-server.com/images/640x480/path/to/image.jpg
```

This will resize the image located at `/var/www/images/path/to/image.jpg` to 640x480 pixels, compress it using optimized settings, and serve it with appropriate caching headers.

## Configuration

The module can be configured using the following Apache directives:

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

## Docker Testing Environment

A Dockerfile is provided for easy testing:

```bash
# Build the Docker image
docker build -t mod_image_resize .

# Run the container
docker run -d -p 8080:80 -v $(pwd)/images:/var/www/images mod_image_resize

# Test an image resize
curl -o resized.jpg http://localhost:8080/images/640x480/test/sample.jpg
```

## Performance and Concurrency

The module is designed to work efficiently with Apache's multi-processing modules:

- **Thread Safety**: All operations are protected with appropriate mutexes
- **Cache Efficiency**: Uses filesystem cache with concurrency protection
- **MPM Compatibility**: Fully compatible with prefork, worker, and event MPMs
- **Resource Management**: Proper cleanup of all resources

## Directory Structure

```
mod_image_resize/
├── mod_image_resize.h       # Main header file
├── mod_image_resize.c       # Core module functionality
├── mod_image_resize_utils.c # Utility functions
├── mod_image_resize_processing.c # Image processing functions
├── Makefile                 # Build system
├── mod_image_resize.conf    # Apache configuration
└── Dockerfile               # For testing
```

## License

This module is released under the Apache License 2.0.

## Acknowledgments

- ImageMagick for the powerful image processing library
- Mozilla for the mozjpeg optimization library
- pngquant/libimagequant for PNG optimization
