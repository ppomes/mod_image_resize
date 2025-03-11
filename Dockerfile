FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    apache2 \
    apache2-dev \
    build-essential \
    libvips-dev \
    libpng-dev \
    libimagequant-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN echo "LibVips version:" && pkg-config --modversion vips

RUN mkdir -p /var/www/images /var/cache/apache2/image_resize && \
    chown -R www-data:www-data /var/www/images /var/cache/apache2/image_resize

WORKDIR /usr/src/mod_image_resize
COPY mod_image_resize.h ./
COPY mod_image_resize.c ./
COPY Makefile ./

RUN make && \
    make install

COPY mod_image_resize.conf /etc/apache2/conf-available/
RUN a2enconf mod_image_resize

RUN apachectl -M | grep image_resize || echo "Module not loaded yet"

EXPOSE 80

CMD ["apache2ctl", "-D", "FOREGROUND"]
