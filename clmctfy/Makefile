# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c


# The directory where this Makefile resides
PWD = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname ${PWD})

CINCLUDE = ${PWD}/include

CXX ?= g++
AR ?= ar

CXXFLAGS = -I${CINCLUDE} -I${LMCTFY_ROOT}/include -I${LMCTFY_ROOT} -I${PWD}
CXXFLAGS += -std=c++0x -fpermissive

all: clmctfy.o status-c.o

clmctfy.o: ${PWD}/clmctfy.cc ${CINCLUDE}/clmctfy.h ${PWD}/status-internal.h ${CINCLUDE}/lmctfy.pb-c.h ${CINCLUDE}/codes.pb-c.h ${CINCLUDE}/clmctfy-raw.h
	${CXX} ${CXXFLAGS} -c -o $@ $<
status-c.o: ${PWD}/status-c.cc ${CINCLUDE}/clmctfy.h ${CINCLUDE}/codes.pb-c.h ${LMCTFY_ROOT}/util/task/status.h ${PWD}/status-internal.h
	${CXX} ${CXXFLAGS} -c -o $@ $<

${CINCLUDE}/codes.pb-c.h: ${LMCTFY_ROOT}/util/task/codes.proto
	${PROTOC_C} --c_out=${CINCLUDE} --proto_path=${LMCTFY_ROOT}/util/task $<

${CINCLUDE}/lmctfy.pb-c.h: ${LMCTFY_ROOT}/include/lmctfy.proto 
	${PROTOC_C} --c_out=${CINCLUDE} --proto_path=${LMCTFY_ROOT}/include $<

clean:
	rm -f ${CINCLUDE}/*.pb-c.[ch]
	rm -f *.o

.PHONY: clean all

