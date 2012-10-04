LESS         = less-444
LESS_TGZ     = $(LESS).tar.gz
LESS_URL     = http://www.greenwoodsoftware.com/less/$(LESS_TGZ)

#
# Interface to top-level prepare Makefile
#
PORTS += $(LESS)

prepare:: $(CONTRIB_DIR)/$(LESS)

#
# Port-specific local rules
#
$(DOWNLOAD_DIR)/$(LESS_TGZ):
	$(VERBOSE)wget -c -P $(DOWNLOAD_DIR) $(LESS_URL) && touch $@

$(CONTRIB_DIR)/$(LESS): $(DOWNLOAD_DIR)/$(LESS_TGZ)
	$(VERBOSE)tar xfz $< -C $(CONTRIB_DIR) && touch $@
	$(VERBOSE)patch -d contrib/ -N -p0 < src/noux-pkg/less/tty_fd.patch

