# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
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
    mkdir -p /opt/mozjpeg/lib/ && \
    if [ -d "/opt/mozjpeg/lib64" ]; then \
      ln -sf /opt/mozjpeg/lib64/* /opt/mozjpeg/lib/ || true; \
    fi

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
    ninja install

# Add MozJPEG to the pkg-config path
ENV PKG_CONFIG_PATH=/opt/mozjpeg/lib/pkgconfig:/usr/lib/pkgconfig

# Download and compile libvips with MozJPEG support
WORKDIR /tmp
RUN git clone https://github.com/libvips/libvips.git && \
    cd libvips && \
    git checkout v8.16.0 && \
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
    ninja install

# Compile the Apache module
WORKDIR /usr/src/mod_image_resize
COPY . .
RUN ldconfig && \
    make LDFLAGS="-L/opt/mozjpeg/lib -ljpeg" && \
    make install

# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    apache2 \
    libarchive13 \
    libpng16-16 \
    libimagequant0 \
    libglib2.0-0 \
    libexpat1 \
    libfftw3-double3 \
    libexif12 \
    libtiff5 \
    libwebp7 \
    libwebpdemux2 \
    libwebpmux3 \
    libgsf-1-114 \
    liblcms2-2 \
    libpango-1.0-0 \
    libpangocairo-1.0-0 \
    librsvg2-2 \
    liborc-0.4-0 \
    && rm -rf /var/lib/apt/lists/*

# Copy MozJPEG libraries
COPY --from=builder /opt/mozjpeg/lib/*.so* /opt/mozjpeg/lib/

# Copy cgif library
COPY --from=builder /usr/lib/x86_64-linux-gnu/libcgif* /usr/lib/x86_64-linux-gnu/

# Copy libvips libraries
COPY --from=builder /usr/lib/x86_64-linux-gnu/libvips*.so* /usr/lib/x86_64-linux-gnu/

# Copy the Apache module
COPY --from=builder /usr/lib/apache2/modules/mod_image_resize.so /usr/lib/apache2/modules/

# Copy Apache configuration
COPY --from=builder /usr/src/mod_image_resize/mod_image_resize.conf /etc/apache2/conf-available/
COPY --from=builder /etc/apache2/mods-available/image_resize.load /etc/apache2/mods-available/

# Setup library paths and ldconfig
RUN echo "/opt/mozjpeg/lib" > /etc/ld.so.conf.d/mozjpeg.conf && \
    ldconfig

# Enable the module and configuration
RUN a2enconf mod_image_resize && \
    a2enmod image_resize || true

# Create necessary directories
RUN mkdir -p /var/www/images && \
    mkdir -p /var/cache/apache2/image_resize && \
    chown -R www-data:www-data /var/www/images /var/cache/apache2/image_resize

# Expose HTTP port
EXPOSE 80

# Configure Apache to run in foreground
CMD ["apache2ctl", "-D", "FOREGROUND"]
