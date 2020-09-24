

# Set these variables in top level Makefile.
# These are safety defaults.
ifndef Cfg_File
  Cfg_File = $(HOME)/alive/alived_config.txt
endif
ifndef Def_Server
  Def_Server = localhost
endif
ifndef Def_DB_Port
  Def_DB_Port = 5679
endif


CC = gcc -Wall
AR = ar
CFLAGS = -O2

# for debugging
# CFLAGS += -g



.PHONY: all
all: alived alivectl alivedb event_dump libaliveclient.a


alived: alived.o llrb_db.o iocdb.o iocdb_access.o utility.o logging.o gentypes.o notifydb.o config_parse.o
	$(CC) -pthread alived.o llrb_db.o iocdb.o iocdb_access.o utility.o logging.o gentypes.o notifydb.o config_parse.o -o alived

alived.o: alived.c alived.h
	$(CC) $(CFLAGS) -c alived.c
llrb_db.o: llrb_db.c llrb_db.h
	$(CC) $(CFLAGS) -c llrb_db.c
iocdb.o: iocdb.c iocdb.h alived.h
	$(CC) $(CFLAGS) -c iocdb.c
iocdb_access.o: iocdb_access.c iocdb_access.h iocdb.h alived.h
	$(CC) $(CFLAGS) -c iocdb_access.c
utility.o: utility.c utility.h
	$(CC) $(CFLAGS) -c utility.c
logging.o: logging.c logging.h alived.h
	$(CC) $(CFLAGS) -c logging.c
gentypes.o: gentypes.c gentypes.h
	$(CC) $(CFLAGS) -c gentypes.c
notifydb.o: notifydb.c notifydb.h alived.h
	$(CC) $(CFLAGS) -c notifydb.c

config_parse.o: config_parse.c config_parse.h
	$(CC) $(CFLAGS) -DCFG_FILE=\"$(Cfg_File)\" -c config_parse.c

alivectl.o: alivectl.c
	$(CC) $(CFLAGS) -c alivectl.c
alivectl: alivectl.o config_parse.o
	$(CC) alivectl.o config_parse.o -o alivectl 


alive_client.o: alive_client.c alive_client.h
	$(CC) $(CFLAGS) -DDEF_SERVER=\"$(Def_Server)\" -DDEF_DB_PORT=$(Def_DB_Port) -c alive_client.c
libaliveclient.a: alive_client.o alive_client.h
	$(AR) rcs libaliveclient.a alive_client.o

alivedb.o: alivedb.c alive_client.h
	$(CC) $(CFLAGS) -DDB_SERVER=\"$(Def_Server)\" -c alivedb.c
alivedb: alivedb.o libaliveclient.a
	$(CC) alivedb.o libaliveclient.a -o alivedb

event_dump.o: event_dump.c
	$(CC) $(CFLAGS) -c event_dump.c
event_dump: event_dump.o config_parse.o
	$(CC) event_dump.o config_parse.o -o event_dump

.PHONY: clean
clean:
	-rm alived alivectl alivedb event_dump libaliveclient.a *.o

