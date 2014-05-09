# This Makefile is heavily based on LevelDB's:
# https://code.google.com/p/leveldb/

#-----------------------------------------------
# Uncomment exactly one of the lines labelled (A), (B), and (C) below
# to switch between compilation modes.

OPT ?= -O2 -DNDEBUG       # (A) Production use (optimized mode)
# OPT ?= -g2              # (B) Debug mode, w/ full line-level debugging symbols
# OPT ?= -O2 -g2 -DNDEBUG # (C) Profiling mode: opt, but w/debugging symbols
#-----------------------------------------------

# Use default if no configuration specified.
CXX ?= g++
AR ?= ar

# Version number of lmctfy.
VERSION = "\"0.5.0\""

# TODO(vmarmol): Ensure our dependencies are installed
PROTOC = protoc

# Function for getting a set of source files.
get_srcs = $(shell find $(1) -name \*.cc -a ! -name \*_test.cc | tr "\n" " ")

INCLUDE_PROTOS = include/config include/virtual_host include/lmctfy \
                 include/namespaces
UTIL_PROTOS = util/task/codes
BASE_SOURCES = $(call get_srcs,base/)
FILE_SOURCES = $(call get_srcs,file/)
INCLUDE_SOURCES = $(call get_srcs,include/) \
		  $(addsuffix .pb.cc,$(INCLUDE_PROTOS))
UTIL_SOURCES = $(call get_srcs,util/) $(addsuffix .pb.cc,$(UTIL_PROTOS))
STRINGS_SOURCES = $(call get_srcs,strings/)
THREAD_SOURCES = $(call get_srcs,thread/)
LIBLMCTFY_SOURCES =$(shell find lmctfy/ -name \*.cc -a ! -name \*_test.cc \
		   -a ! -path \*cli/\* | tr "\n" " ")
CLI_SOURCES = $(call get_srcs,lmctfy/cli/)
NSINIT_SOURCES = nscon/init.cc nscon/init_impl.cc
NSCON_SOURCES = $(filter-out $(NSINIT_SOURCES),$(call get_srcs,nscon/))

# The objects for the system API (both release and test versions).
SYSTEM_API_OBJS = global_utils/fs_utils.o \
		  global_utils/mount_utils.o \
		  global_utils/time_utils.o \
		  system_api/kernel_api.o \
		  system_api/kernel_api_singleton.o \
		  system_api/libc_fs_api_impl.o \
		  system_api/libc_fs_api_singleton.o \
		  system_api/libc_net_api.o \
		  system_api/libc_process_api.o \
		  system_api/libc_time_api.o
SYSTEM_API_TEST_OBJS = global_utils/fs_utils_test_util.o \
		       global_utils/mount_utils_test_util.o \
		       global_utils/time_utils_test_util.o \
		       system_api/kernel_api.o \
		       system_api/kernel_api_test_util.o \
		       system_api/libc_fs_api_impl.o \
		       system_api/libc_fs_api_test_util.o \
		       system_api/libc_net_api_test_util.o \
		       system_api/libc_process_api_test_util.o \
		       system_api/libc_time_api_test_util.o

# Gets all *_test.cc files in lmtcfy/.
TESTS = $(basename $(shell find lmctfy/ nscon/ -name \*_test.cc \
	-a ! -name \*_integration_test.cc))

# Gets all *_integration_test.cc files in lmtcfy/.
INTEGRATION_TESTS = $(basename $(shell find lmctfy/ nscon/ testing/ -name \
  \*_integration_test.cc))

# Where to place the binary outputs.
OUT_DIR = bin

# Location of gTest and gMock.
GTEST_DIR = gmock/gtest
GMOCK_DIR = gmock

CXXFLAGS += $(OPT)

# Determine GCC version.
GCC_VERSION = $(shell $(CXX) -dumpversion | awk -F'.' \
	      '{printf "%d%02d%02d", $$1, $$2, $$3}')

# Helper comparison function.
IF = $(if $(shell [ "$(1)" -$(2) "$(3)" ] && echo "1"),$(4),$(5))

# Use c++11 for GCC >=4.7.0 and c++0x for earlier versions. Additionally,
# set -fpermissive for 4.7.x to work around '<::'.
CXXFLAGS += $(call IF,$(GCC_VERSION),ge,40700,\
	    $(call IF,$(GCC_VERSION),lt,40800,\
	    -std=c++11 -fpermissive,-std=c++11),\
	    -std=c++0x)

# Add defines.
CXXFLAGS += -DHASH_NAMESPACE=std -DHAVE_LONG_LONG -DGTEST_HAS_STRING_PIECE_ \
	    -DLMCTFY_VERSION=$(VERSION)

# Add libraries to link in.
CXXFLAGS += -pthread -lrt -lre2 -lgflags -lapparmor

# Add include and library paths.
CXXFLAGS += -I. -I./include -I./base -I./lmctfy -I$(GTEST_DIR)/include \
	    -I$(GMOCK_DIR)/include -I/usr/local/include -L/usr/local/lib \
	    -I/usr/include -L/usr/lib

# Add proto flags.
CXXFLAGS += `pkg-config --cflags --libs protobuf`

CLI = lmctfy
NSCON = lmctfy-nscon
NSINIT = lmctfy-nsinit
LIBRARY = liblmctfy.a
CREAPER = lmctfy-creaper

# Function for ensuring the output directory has been created.
create_bin = mkdir -p $(dir $(OUT_DIR)/$@)

# Function that archives all input's bin/ output into an archive.
archive_all = $(AR) $(ARFLAGS) $(OUT_DIR)/$@ $(addprefix $(OUT_DIR)/,$^)

# Function for turning source file names to their object file names (.cc -> .o).
source_to_object = $(addsuffix .o,$(basename $(1)))

default: all

all: $(LIBRARY) $(NSINIT) $(NSCON) $(CLI) $(CREAPER)

INSTALL = /usr/bin/install
INSTALL_PROGRAM = $(INSTALL) -m 755
DESTDIR = /usr/local/bin

install: all
	$(INSTALL_PROGRAM) $(OUT_DIR)/lmctfy/cli/$(CLI) $(DESTDIR)/$(CLI)
	$(INSTALL_PROGRAM) $(OUT_DIR)/nscon/cli/$(NSCON) $(DESTDIR)/$(NSCON)
	$(INSTALL_PROGRAM) $(OUT_DIR)/nscon/$(NSINIT) $(DESTDIR)/$(NSINIT)
	$(INSTALL_PROGRAM) $(OUT_DIR)/$(CREAPER) $(DESTDIR)/$(CREAPER)

TEST_TMPDIR = "/tmp/lmctfy_test.$$$$"
check: $(TESTS)
	for t in $(addprefix $(OUT_DIR)/,$^); \
		do \
			echo "***** Running $$t"; \
			rm -rf $(TEST_TMPDIR); \
			mkdir $(TEST_TMPDIR); \
			./$$t --test_tmpdir=$(TEST_TMPDIR) || exit 1; \
		done; \
	rm -rf $(TEST_TMPDIR)
	echo "All tests pass!"

check_integration: $(INTEGRATION_TESTS)
	for t in $(addprefix $(OUT_DIR)/,$^); \
		do \
			echo "***** Running $$t"; \
			rm -rf $(TEST_TMPDIR); \
			mkdir $(TEST_TMPDIR); \
			./$$t --test_tmpdir=$(TEST_TMPDIR) || exit 1; \
		done; \
	rm -rf $(TEST_TMPDIR)
	echo "All tests pass!"

clean:
	-rm -rf $(OUT_DIR)
	-rm -f `find . -type f -name '*.pb.*'`

examples/simple_existing: examples/simple_existing.o $(LIBRARY)
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/$@ $(addprefix $(OUT_DIR)/,$^) $(CXXFLAGS)

# All common base sources (non-lmctfy and non-nscon).
COMMON_SOURCES = $(INCLUDE_SOURCES) $(BASE_SOURCES) $(STRINGS_SOURCES) \
		 $(FILE_SOURCES) $(THREAD_SOURCES) $(UTIL_SOURCES)

# All sources needed by the library (minus the system API).
LIBRARY_SOURCES = $(COMMON_SOURCES) $(LIBLMCTFY_SOURCES) $(NSCON_SOURCES)


# The lmctfy library without the system API. This is primarily an internal
# target.
lmctfy_no_system_api.a: $(call source_to_object,$(LIBRARY_SOURCES))
	$(create_bin)
	$(archive_all)

# The lmctfy library with the real system API.
$(LIBRARY): $(call source_to_object,$(LIBRARY_SOURCES)) $(SYSTEM_API_OBJS)
	$(create_bin)
	$(archive_all)

# Objects of the lmctfy CLI.
lmctfy_cli.a: $(call source_to_object,$(CLI_SOURCES))
	$(create_bin)
	$(archive_all)

$(CLI): lmctfy_cli.a $(LIBRARY)
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/lmctfy/cli/$@ $(addprefix $(OUT_DIR)/,$^) $(CXXFLAGS)

# Objects of the nscon CLI.
nscon_cli.a: $(call source_to_object,$(NSCON_SOURCES) $(COMMON_SOURCES)) \
             $(SYSTEM_API_OBJS)
	$(create_bin)
	$(archive_all)

$(NSCON): nscon_cli.a
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/nscon/cli/$@ $(addprefix $(OUT_DIR)/,$^) $(CXXFLAGS)

# Objects of the nsinit CLI.
nsinit_cli.a: $(call source_to_object,$(NSINIT_SOURCES) $(COMMON_SOURCES))
	$(create_bin)
	$(archive_all)

$(NSINIT): nsinit_cli.a
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/nscon/$@ $(addprefix $(OUT_DIR)/,$^) $(CXXFLAGS)

$(CREAPER): lmctfy-creaper.go
	$(create_bin)
	go build -o $(OUT_DIR)/lmctfy-creaper lmctfy-creaper.go

%_integration_test: gtest_main.a $(LIBRARY)
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/$@ $*_integration_test.cc $(addprefix $(OUT_DIR)/,$^) \
		$(CXXFLAGS)

%_real_test: gtest_main.a $(LIBRARY)
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/$@ $*.cc $*_real_test.cc $(addprefix $(OUT_DIR)/,$^) \
		$(CXXFLAGS)

%_test: gtest_main.a $(SYSTEM_API_TEST_OBJS) nscon_cli.a lmctfy_cli.a \
        lmctfy_no_system_api.a
	$(create_bin)
	$(CXX) -o $(OUT_DIR)/$@ $*.cc $*_test.cc $(addprefix $(OUT_DIR)/,$^) \
		$(CXXFLAGS)

%_proto: %.proto
	$(PROTOC) $^ --cpp_out=.

%.pb.o: %_proto
	$(create_bin)
	$(CXX) -c $*.pb.cc -o $(OUT_DIR)/$@ $(CXXFLAGS)

gen_protos: $(addsuffix _proto,$(INCLUDE_PROTOS) $(UTIL_PROTOS))

%.o: gen_protos %.cc
	$(create_bin)
	$(CXX) -c $*.cc -o $(OUT_DIR)/$@ $(CXXFLAGS)

# Rules for Building Google Test and Google Mock (based on gmock's example).

# All Google Test headers.  Usually you shouldn't change this definition.
GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
		$(GTEST_DIR)/include/gtest/internal/*.h

# All Google Mock headers. Note that all Google Test headers are
# included here too, as they are #included by Google Mock headers.
# Usually you shouldn't change this definition.
GMOCK_HEADERS = $(GMOCK_DIR)/include/gmock/*.h \
		$(GMOCK_DIR)/include/gmock/internal/*.h \
		$(GTEST_HEADERS)

GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)
GMOCK_SRCS_ = $(GMOCK_DIR)/src/*.cc $(GMOCK_HEADERS)

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
gtest-all.o : $(GTEST_SRCS_)
	$(create_bin)
	$(CXX) -I$(GTEST_DIR) $(CXXFLAGS) -c \
		$(GTEST_DIR)/src/gtest-all.cc -o $(OUT_DIR)/$@

gmock-all.o : $(GMOCK_SRCS_)
	$(create_bin)
	$(CXX) -I$(GTEST_DIR) -I$(GMOCK_DIR) $(CXXFLAGS) \
		-c $(GMOCK_DIR)/src/gmock-all.cc -o $(OUT_DIR)/$@

gmock_main.o : $(GMOCK_SRCS_)
	$(create_bin)
	$(CXX) -I$(GTEST_DIR) -I$(GMOCK_DIR) $(CXXFLAGS) \
		-c $(GMOCK_DIR)/src/gmock_main.cc -o $(OUT_DIR)/$@

gtest.a : gmock-all.o gtest-all.o
	$(create_bin)
	$(archive_all)

gtest_main.a : gmock-all.o gtest-all.o gmock_main.o
	$(create_bin)
	$(archive_all)
