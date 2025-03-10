FROM debian:bullseye-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    libjpeg-dev \
    libpng-dev \
    libwebp-dev \
    libimagequant-dev \
    pkg-config \
    cmake \
    nasm \
    git \
    libxml2-dev \
    libssl-dev \
    libghc-zlib-dev \
    libde265-dev \
    libheif-dev \
    libx11-dev \
    && rm -rf /var/lib/apt/lists/*

# Installation de ImageMagick 7
WORKDIR /tmp
RUN git clone https://github.com/ImageMagick/ImageMagick.git && \
    cd ImageMagick && \
    ./configure --with-modules --enable-shared --disable-static --enable-hdri && \
    make -j$(nproc) && \
    make install && \
    ldconfig && \
    cd .. && \
    rm -rf ImageMagick

# Install mozjpeg
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
    cd .. && \
    rm -rf mozjpeg

# Environment variables
ENV PATH="/opt/mozjpeg/bin:${PATH}" \
    LD_LIBRARY_PATH="/opt/mozjpeg/lib:/usr/local/lib:${LD_LIBRARY_PATH}" \
    PKG_CONFIG_PATH="/opt/mozjpeg/lib/pkgconfig:/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}"

# Create necessary directories
RUN mkdir -p /var/www/images /var/cache/apache2/image_resize && \
    chown -R www-data:www-data /var/www/images /var/cache/apache2/image_resize

# Copy source code
WORKDIR /usr/src/mod_image_resize
COPY mod_image_resize.h ./
COPY mod_image_resize.c ./
COPY mod_image_resize_utils.c ./
COPY mod_image_resize_processing.c ./
COPY Makefile ./

# Compile and install the module
RUN make && \
    make install

# Copy Apache configuration
COPY mod_image_resize.conf /etc/apache2/conf-available/
RUN a2enconf mod_image_resize

# Expose port
EXPOSE 80

# Start Apache in foreground
CMD ["apache2ctl", "-D", "FOREGROUND"]
