content: drivers.config event_filter.config en_us.chargen special.chargen

drivers.config event_filter.config:
	cp $(REP_DIR)/recipes/raw/drivers_interactive-linux/$@ $@

en_us.chargen special.chargen:
	cp $(REP_DIR)/src/server/event_filter/$@ $@
