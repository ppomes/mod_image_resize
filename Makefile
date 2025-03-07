# Makefile for mod_image_resize Apache module

# Apache extension tool
APXS = apxs

# Dependency paths
MOZJPEG_PATH = /opt/mozjpeg

# Filter out problematic flags from pkg-config output
MAGICK_INCLUDE = $(shell pkg-config --cflags-only-I MagickWand-6.Q16 2>/dev/null || pkg-config --cflags-only-I MagickWand)
MAGICK_LIBS = $(shell pkg-config --libs-only-L MagickWand-6.Q16 2>/dev/null || pkg-config --libs-only-L MagickWand)
MAGICK_LIBS_NAMES = $(shell pkg-config --libs-only-l MagickWand-6.Q16 2>/dev/null || pkg-config --libs-only-l MagickWand)

# Source files
SOURCES = mod_image_resize.c mod_image_resize_utils.c mod_image_resize_processing.c

# Main target - compiles the module using apxs
all: mod_image_resize.la

mod_image_resize.la: $(SOURCES)
	$(APXS) -c -i -Wc,-I$(MOZJPEG_PATH)/include $(MAGICK_INCLUDE) \
		-Wl,-L$(MOZJPEG_PATH)/lib $(MAGICK_LIBS) \
		-Wl,-ljpeg -Wl,-lpng -Wl,-limagequant $(MAGICK_LIBS_NAMES) \
		-Wl,-rpath=$(MOZJPEG_PATH)/lib \
		$(SOURCES)

# Install the module (run after compilation)
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
