FROM debian:bullseye-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    libmagickwand-dev \
    libjpeg-dev \
    libpng-dev \
    libimagequant-dev \
    pkg-config \
    cmake \
    nasm \
    git \
    && rm -rf /var/lib/apt/lists/*

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
    LD_LIBRARY_PATH="/opt/mozjpeg/lib:${LD_LIBRARY_PATH}" \
    PKG_CONFIG_PATH="/opt/mozjpeg/lib/pkgconfig:${PKG_CONFIG_PATH}"

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

# Display debug information
RUN apachectl -M | grep image_resize

# Expose port
EXPOSE 80

# Start Apache in foreground
CMD ["apache2ctl", "-D", "FOREGROUND"]
