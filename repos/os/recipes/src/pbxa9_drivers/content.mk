include $(GENODE_DIR)/repos/base/recipes/src/content.inc

content: src/drivers

src/drivers:
	mkdir -p $@/framebuffer $@/ps2
	cp    -r $(REP_DIR)/src/drivers/framebuffer/pl11x/*  $@/framebuffer
	cp       $(REP_DIR)/src/drivers/ps2/*.h   $@/ps2/
	cp    -r $(REP_DIR)/src/drivers/ps2/pl050 $@/ps2/
