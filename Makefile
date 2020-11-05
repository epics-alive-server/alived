

prefix         = /usr/local
bin_dir        = $(prefix)/bin
include_dir    = $(prefix)/include
lib_dir        = $(prefix)/lib


# Default configuration file location for alived, alivectl, and tools
Cfg_File = $(HOME)/alive/alived_config.txt


-include LocalOptions


#####################################################

export Cfg_File


.PHONY : all clean install uninstall

all:
	make -C src all

clean:
	make -C src clean


INSTALL_MKDIR = mkdir -p
INSTALL_BIN   = install -c -m 0755
INSTALL_OTHER = install -c -m 0644
UNINSTALL_RM = -rm

install : all
	$(INSTALL_MKDIR) $(DESTDIR)$(bin_dir)
	$(INSTALL_BIN) src/alived $(DESTDIR)$(bin_dir)/
	$(INSTALL_BIN) src/alivectl $(DESTDIR)$(bin_dir)/

uninstall :
	$(UNINSTALL_RM) $(DESTDIR)$(bin_dir)/alived
	$(UNINSTALL_RM) $(DESTDIR)$(bin_dir)/alivectl

