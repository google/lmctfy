# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c
PROTOC = protoc

# The directory where this Makefile resides
CLMCTFY_ROOT = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname $(CLMCTFY_ROOT))

CLMCTFY_INCLUDE = $(CLMCTFY_ROOT)/include

CXX ?= g++
AR ?= ar

# Where to place the binary outputs.
OUT_DIR = $(CLMCTFY_ROOT)/bin

INCLUDE_PROTOS = $(LMCTFY_ROOT)/include/lmctfy
UTIL_PROTOS = $(LMCTFY_ROOT)/util/task/codes

# Location of gTest and gMock.
GTEST_DIR = $(LMCTFY_ROOT)/gmock/gtest
GMOCK_DIR = $(LMCTFY_ROOT)/gmock

# Function for ensuring the output directory has been created.
create_bin = mkdir -p $(dir $(OUT_DIR)/$@)

# Function that archives all input's bin/ output into an archive.
archive_all = $(AR) $(ARFLAGS) $(OUT_DIR)/$@ $(addprefix $(OUT_DIR)/,$^)

# Gets all *_test.cc files in lmtcfy/.
TESTS = $(basename $(shell find ./ -name \*_test.cc))

CXXFLAGS = -I$(CLMCTFY_INCLUDE) -I$(LMCTFY_ROOT)/include -I$(LMCTFY_ROOT) -I$(CLMCTFY_ROOT)
CXXFLAGS += -I$(LMCTFY_ROOT)/base -I$(LMCTFY_ROOT)/lmctfy -I$(GTEST_DIR)/include \
	    -I$(GMOCK_DIR)/include -I/usr/local/include -L/usr/local/lib \
	    -I/usr/include -L/usr/lib
CXXFLAGS += -std=c++0x -fpermissive
# Add libraries to link in.
CXXFLAGS += -pthread -lrt -lre2 -lgflags

all: clmctfy.o status-c.o

%.o: gen_protos %.cc
	$(create_bin)
	$(CXX) -c $*.cc -o $(OUT_DIR)/$@ $(CXXFLAGS)

gen_protos: $(addsuffix _proto,$(INCLUDE_PROTOS) $(UTIL_PROTOS))

%_proto: %.proto
	$(PROTOC_C) $^ --c_out=$(dir $^) --proto_path=$(dir $^)

%.pb-c.o: %_proto
	$(create_bin)
	$(CXX) -c $*.pb-c.c -o $(OUT_DIR)/$@ $(CXXFLAGS)

%_test: gtest_main.a
	$(create_bin)
	$(CXX) $(CXXFLAGS) -o $(OUT_DIR)/$@ $*.cc $*_test.cc $(addprefix $(OUT_DIR)/,$^)

TEST_TMPDIR = "clmctfy_test.$$"
check: $(TESTS)
	for t in $(addprefix $(OUT_DIR)/,$^); \
		do \
			echo "***** Running $$t"; \
			rm -rf $(TEST_TMPDIR); \
			mkdir $(TEST_TMPDIR); \
			./$$t --test_tmpdir=$(TEST_TMPDIR); \
		done; \
	#rm -rf $(TEST_TMPDIR)

clean:
	rm -f $(CLMCTFY_INCLUDE)/*.pb-c.[ch]
	rm -f *.o
	rm -rf $(OUT_DIR)

.PHONY: clean all check

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
	$(CXX) -I$(GTEST_DIR) \
	       -I$(GMOCK_DIR)/include -I$(GTEST_DIR)/include \
	       $(CXXFLAGS) \
		-c $(GTEST_DIR)/src/gtest-all.cc -o $(OUT_DIR)/$@

gmock-all.o : $(GMOCK_SRCS_)
	$(create_bin)
	$(CXX) -I$(GTEST_DIR) -I$(GMOCK_DIR)	\
	       -I$(GMOCK_DIR)/include \
	       $(CXXFLAGS) \
		-c $(GMOCK_DIR)/src/gmock-all.cc -o $(OUT_DIR)/$@

gmock_main.o : $(GMOCK_SRCS_)
	$(create_bin)
	$(CXX) -I$(GTEST_DIR) $(CXXFLAGS) \
		-c $(GMOCK_DIR)/src/gmock_main.cc -o $(OUT_DIR)/$@

gtest.a : gmock-all.o gtest-all.o
	$(create_bin)
	$(archive_all)

gtest_main.a : gmock-all.o gtest-all.o gmock_main.o
	$(create_bin)
	$(archive_all)
