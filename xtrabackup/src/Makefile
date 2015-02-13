# Makefile to build XtraBackup for Percona Server and different versions of MySQL
#
# Syntax:
# make [5.6]
#
# Default is 5.6 - XtraBackup for MySQL version 5.6

LIBS += -lpthread $(LDFLAGS)
DEFS = -DUNIV_LINUX -DMYSQL_SERVER

COMMON_FLAGS = -DXTRABACKUP_VERSION=\"$(XTRABACKUP_VERSION)\" -DXTRABACKUP -pedantic -Wall -Wundef -Wshadow  -fdiagnostics-show-option  -fno-strict-aliasing -Wno-strict-aliasing -Wextra -Wformat -Wno-format-nonliteral -Wno-format-security -Wno-long-long  -Wno-redundant-decls
CFLAGS += $(COMMON_FLAGS) --std=gnu99 -Wmissing-declarations
CXXFLAGS += $(COMMON_FLAGS) --std=c++11

ifneq ($(DEBUG),)
CFLAGS += -DXB_DEBUG=1
CXXFLAGS += -DXB_DEBUG=1
endif

TARGET=xtrabackup
PREFIX=/usr
BIN_DIR=$(PREFIX)/bin

COMMON_INC = -I. -I libarchive/libarchive -I quicklz
XTRABACKUPCOBJS = stream.o local.o compress.o buffer.o \
	xbstream_write.o \
	quicklz/quicklz.o
XTRABACKUPCCOBJS = xtrabackup.o
XBSTREAMOBJS = xbstream.o xbstream_write.o xbstream_read.o

LIBARCHIVE_A = libarchive/libarchive/libarchive.a

default: 5.6

# XtraBackup for MySQL 5.6
5.6: INC = $(COMMON_INC) \
	$(addprefix -isystem$(MYSQL_ROOT_DIR)/, include sql) \
	$(addprefix -isystem$(SRC_DIR)/, \
	include sql storage/innobase/include)
5.6: INNODBOBJS =

ifeq "$(wildcard $(MYSQL_ROOT_DIR)/zlib/.libs/libzlt.a)" ""
5.6: LIBZ= -lz
else
5.6: LIBZ= $(MYSQL_ROOT_DIR)/zlib/.libs/libzlt.a
endif

5.6: MYSQLOBJS = $(MYSQL_ROOT_DIR)/libmysqld/libmysqld.a
ifeq ($(shell uname -s),Linux)
5.6: LIBS += -laio -ldl -lcrypt -lcrypto -lssl -lk5crypto -lkrb5 -lgssapi_krb5
endif
5.6: TARGET := xtrabackup_innodb56
# In CMake server builds it is important to build with exactly the same preprocessor flags
# as were used to build InnoDB
5.6: DEFS = $(shell grep CXX_DEFINES \
	$(MYSQL_ROOT_DIR)/storage/innobase/CMakeFiles/innobase.dir/flags.make | \
	sed -e 's/CXX_DEFINES = //')
5.6: $(TARGET) xbstream

$(XTRABACKUPCOBJS): %.o: %.c
	$(CC) $(CFLAGS) $(INC) $(DEFS) -c $< -o $@

$(XTRABACKUPCCOBJS): %.o: %.cc
	$(CXX) $(CXXFLAGS) $(INC) $(DEFS) -c $< -o $@

xbstream.o xbstream_read.o: %.o: %.c
	$(CC) $(CFLAGS) $(INC) $(DEFS) -c $< -o $@

xbstream: $(XBSTREAMOBJS) $(MYSQLOBJS) local.o
	$(CXX) $(CXXFLAGS) $^ $(INC) $(MYSQLOBJS) $(LIBS) $(LIBZ) -o $@

xtrabackup.o: xtrabackup.cc xb_regex.h

$(TARGET): $(XTRABACKUPCCOBJS) $(XTRABACKUPCOBJS) $(INNODBOBJS) $(MYSQLOBJS) $(LIBARCHIVE_A)
	$(CXX) $(CFXXLAGS) $(XTRABACKUPCCOBJS) $(XTRABACKUPCOBJS) $(INNODBOBJS) $(MYSQLOBJS) $(LIBS) $(LIBZ) \
	$(LIBARCHIVE_A) -o $(TARGET)

clean:
	rm -f $(XTRABACKUPCCOBJS) $(XTRABACKUPCOBJS) $(XBSTREAMOBJS) xtrabackup xtrabackup_* 
