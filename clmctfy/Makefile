# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c


# The directory where this Makefile resides
CLMCTFY_ROOT = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname ${CLMCTFY_ROOT})

CLMCTFY_INCLUDE = ${CLMCTFY_ROOT}/include

CXX ?= g++
AR ?= ar

# Where to place the binary outputs.
OUT_DIR = ${CLMCTFY_ROOT}/bin

# Function for ensuring the output directory has been created.
create_bin = mkdir -p $(dir $(OUT_DIR)/$@)

CXXFLAGS = -I${CLMCTFY_INCLUDE} -I${LMCTFY_ROOT}/include -I${LMCTFY_ROOT} -I${CLMCTFY_ROOT}
CXXFLAGS += -std=c++0x -fpermissive

all: clmctfy.o status-c.o

clmctfy.o: ${CLMCTFY_ROOT}/clmctfy.cc ${CLMCTFY_INCLUDE}/clmctfy.h ${CLMCTFY_ROOT}/status-internal.h ${CLMCTFY_INCLUDE}/lmctfy.pb-c.h ${CLMCTFY_INCLUDE}/codes.pb-c.h ${CLMCTFY_INCLUDE}/clmctfy-raw.h
	$(create_bin)
	${CXX} ${CXXFLAGS} -c -o $(OUT_DIR)/$@ $<
status-c.o: ${CLMCTFY_ROOT}/status-c.cc ${CLMCTFY_INCLUDE}/clmctfy.h ${CLMCTFY_INCLUDE}/codes.pb-c.h ${LMCTFY_ROOT}/util/task/status.h ${CLMCTFY_ROOT}/status-internal.h
	$(create_bin)
	${CXX} ${CXXFLAGS} -c -o $(OUT_DIR)/$@ $<

${CLMCTFY_INCLUDE}/codes.pb-c.h: ${LMCTFY_ROOT}/util/task/codes.proto
	${PROTOC_C} --c_out=${CLMCTFY_INCLUDE} --proto_path=${LMCTFY_ROOT}/util/task $<

${CLMCTFY_INCLUDE}/lmctfy.pb-c.h: ${LMCTFY_ROOT}/include/lmctfy.proto 
	${PROTOC_C} --c_out=${CLMCTFY_INCLUDE} --proto_path=${LMCTFY_ROOT}/include $<

clean:
	rm -f ${CLMCTFY_INCLUDE}/*.pb-c.[ch]
	rm -f *.o

.PHONY: clean all

