# Makefile for mod_image_resize Apache module with libvips

# Apache extension tool
APXS = apxs

# Dependencies
VIPS_CFLAGS = $(shell pkg-config --cflags vips)
VIPS_LIBS = $(shell pkg-config --libs vips)

# Filter problematic flags
VIPS_INCLUDE = $(shell pkg-config --cflags-only-I vips)
VIPS_LIBS_NAMES = $(shell pkg-config --libs-only-l vips)

# Source files
SOURCES = mod_image_resize.c

# Main target - compiles the module using apxs
all: mod_image_resize.la

mod_image_resize.la: $(SOURCES)
	$(APXS) -c -i -Wc,$(VIPS_INCLUDE) \
		$(VIPS_LIBS_NAMES) \
		-Wl,-rpath=/usr/local/lib \
		$(SOURCES)

# Install the module
install:
	$(APXS) -i -a -n image_resize .libs/mod_image_resize.so

# Create configuration file
config: mod_image_resize.conf
	cp mod_image_resize.conf /etc/apache2/conf-available/
	a2enconf mod_image_resize

# Test Apache configuration
test:
	apachectl configtest

# Restart Apache
restart:
	apachectl restart

# Clean up
clean:
	rm -f *.o *.lo *.la *.slo
	rm -rf .libs

.PHONY: all install config test restart clean
