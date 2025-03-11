# Use Ubuntu as base image
FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install base dependencies
RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    cmake \
    git \
    nasm \
    pkg-config \
    libpng-dev \
    libimagequant-dev \
    pngquant \
    libarchive-dev \
    libglib2.0-dev \
    libexpat-dev \
    libfftw3-dev \
    libexif-dev \
    libtiff-dev \
    libwebp-dev \
    libgsf-1-dev \
    liblcms2-dev \
    libpango1.0-dev \
    librsvg2-dev \
    meson \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Purge existing libvips and libjpeg packages to avoid conflicts
RUN apt-get update && \
    apt-get purge -y libvips42* libvips-dev libvips-doc libvips-tools libjpeg-turbo* libjpeg8* libjpeg9* && \
    apt-get autoremove -y && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Compile and install MozJPEG
WORKDIR /tmp
RUN git clone https://github.com/mozilla/mozjpeg.git && \
    cd mozjpeg && \
    cmake -G"Unix Makefiles" \
      -DCMAKE_INSTALL_PREFIX=/opt/mozjpeg \
      -DCMAKE_C_FLAGS="-fPIC" \
      -DCMAKE_CXX_FLAGS="-fPIC" \
      -DENABLE_SHARED=TRUE \
      -DPNG_LIBRARY=/usr/lib/x86_64-linux-gnu/libpng.so && \
    make -j$(nproc) && \
    make install && \
    # Create lib directory if it doesn't exist and create symlinks
    mkdir -p /opt/mozjpeg/lib/ && \
    if [ -d "/opt/mozjpeg/lib64" ]; then \
      ln -sf /opt/mozjpeg/lib64/* /opt/mozjpeg/lib/ || true; \
    fi && \
    # Link MozJPEG libraries to standard locations
    ln -sf /opt/mozjpeg/lib/libjpeg.so /usr/lib/libjpeg.so && \
    ln -sf /opt/mozjpeg/lib/libjpeg.so.62 /usr/lib/libjpeg.so.62 && \
    ln -sf /opt/mozjpeg/lib/libjpeg.so.62.4.0 /usr/lib/libjpeg.so.62.4.0 && \
    ldconfig

# Create pkg-config file for MozJPEG
RUN mkdir -p /opt/mozjpeg/lib/pkgconfig && \
    echo 'prefix=/opt/mozjpeg' > /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'exec_prefix=${prefix}' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'libdir=${exec_prefix}/lib' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'includedir=${prefix}/include' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo '' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'Name: mozjpeg' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'Description: Mozilla JPEG library' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'Version: 4.1.1' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'Libs: -L${libdir} -ljpeg' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc && \
    echo 'Cflags: -I${includedir}' >> /opt/mozjpeg/lib/pkgconfig/mozjpeg.pc

# Install cgif for GIF support
WORKDIR /tmp
RUN git clone https://github.com/dloebl/cgif.git && \
    cd cgif && \
    meson setup build \
      --prefix=/usr \
      --buildtype=release \
      --default-library=shared && \
    cd build && \
    ninja && \
    ninja install && \
    ldconfig

# Add MozJPEG to the pkg-config path and LD_LIBRARY_PATH
ENV PKG_CONFIG_PATH=/opt/mozjpeg/lib/pkgconfig:/usr/lib/pkgconfig
ENV LD_LIBRARY_PATH=/opt/mozjpeg/lib:$LD_LIBRARY_PATH

# Download and compile libvips with MozJPEG support using Meson
WORKDIR /tmp
RUN git clone https://github.com/libvips/libvips.git && \
    cd libvips && \
    git checkout v8.16.0 && \
    # Setup build with Meson
    LDFLAGS="-L/opt/mozjpeg/lib" \
    CPPFLAGS="-I/opt/mozjpeg/include" \
    meson setup build \
      --prefix=/usr \
      --buildtype=release \
      --default-library=shared \
      -Djpeg=enabled \
      -Dcgif=enabled \
      -Dpdfium=disabled \
      -Dmagick=disabled \
      -Dquantizr=enabled \
      -Dspng=disabled \
      -Dpng=enabled && \
    cd build && \
    ninja && \
    ninja install && \
    ldconfig

# Display library dependencies to verify
RUN echo "Checking libvips dependencies:" && \
    ldd /usr/lib/*/libvips.so | grep jpeg

# Copy module source code
WORKDIR /usr/src/mod_image_resize
COPY . .

RUN make LDFLAGS="-L/opt/mozjpeg/lib -ljpeg" && \
    make install

# Verify Apache module dependencies
RUN ldd /usr/lib/apache2/modules/mod_image_resize.so | grep jpeg

# Create necessary directories
RUN mkdir -p /var/www/images && \
    mkdir -p /var/cache/apache2/image_resize && \
    chown -R www-data:www-data /var/www/images /var/cache/apache2/image_resize

# Copy Apache configuration
RUN cp mod_image_resize.conf /etc/apache2/conf-available/ && \
    a2enconf mod_image_resize && \
    a2enmod image_resize || true

# Clean up
RUN rm -rf /tmp/* && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Expose HTTP port
EXPOSE 80

# Configure Apache to run in foreground
CMD ["apache2ctl", "-D", "FOREGROUND"]
