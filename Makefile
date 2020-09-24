

prefix         = /usr/local
system_bin_dir = $(prefix)/bin
bin_dir        = $(prefix)/bin
include_dir    = $(prefix)/include
lib_dir        = $(prefix)/lib


# Default configuration file location for alived, alivectl, and tools
Cfg_File = $(HOME)/alive/alived_config.txt

# Settings for clients, currently just alivedb
# Default server IP address
Def_Server = localhost
# Default server database port
Def_DB_Port = 5679


-include LocalOptions


#####################################################

export Cfg_File
export Def_Server
export Def_DB_Port


.PHONY : all clean install uninstall

all:
	make -C src all

clean:
	make -C src clean


INSTALL_MKDIR = mkdir -p
INSTALL_BIN   = install -c -m 0755
INSTALL_OTHER = install -c -m 0644
UNINSTALL_RM = -rm
UNINSTALL_RMDIR = -rmdir

install : all
	$(INSTALL_MKDIR) $(DESTDIR)$(system_bin_dir)
	$(INSTALL_MKDIR) $(DESTDIR)$(bin_dir)
	$(INSTALL_MKDIR) $(DESTDIR)$(lib_dir)
	$(INSTALL_MKDIR) $(DESTDIR)$(include_dir)
	$(INSTALL_BIN) src/alived $(DESTDIR)$(system_bin_dir)/
	$(INSTALL_BIN) src/alivectl $(DESTDIR)$(system_bin_dir)/
	$(INSTALL_BIN) src/alivedb $(DESTDIR)$(bin_dir)/
	$(INSTALL_OTHER) src/libaliveclient.a $(DESTDIR)$(lib_dir)/
	$(INSTALL_OTHER) src/alive_client.h $(DESTDIR)$(include_dir)/

uninstall :
	$(UNINSTALL_RM) $(DESTDIR)$(system_bin_dir)/alived
	$(UNINSTALL_RM) $(DESTDIR)$(system_bin_dir)/alivectl
	$(UNINSTALL_RM) $(DESTDIR)$(bin_dir)/alivedb
	$(UNINSTALL_RM) $(DESTDIR)$(lib_dir)/libaliveclient.a
	$(UNINSTALL_RM) $(DESTDIR)$(include_dir)/alive_client.h

