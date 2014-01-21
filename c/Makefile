# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c


# The directory where this Makefile resides
PWD = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname ${PWD})

INCLUDE = ${PWD}/include

CXX = g++

CXXFLAGS = -I${INCLUDE} -I${LMCTFY_ROOT}/include -I${LMCTFY_ROOT} -I${PWD}
CXXFLAGS += -std=c++0x -fpermissive

all: lmctfy-c.o status-c.o ${INCLUDE}/lmctfy.pb-c.h

lmctfy-c.o: ${PWD}/lmctfy-c.cc ${INCLUDE}/lmctfy-c.h ${PWD}/status-internal.h ${INCLUDE}/status-c.h ${INCLUDE}/lmctfy.pb-c.h ${INCLUDE}/codes.pb-c.h
	${CXX} ${CXXFLAGS} -c -o $@ $<
status-c.o: ${PWD}/status-c.cc ${INCLUDE}/status-c.h ${INCLUDE}/codes.pb-c.h ${LMCTFY_ROOT}/util/task/status.h ${PWD}/status-internal.h
	${CXX} ${CXXFLAGS} -c -o $@ $<

${INCLUDE}/codes.pb-c.h: ${LMCTFY_ROOT}/util/task/codes.proto
	${PROTOC_C} --c_out=${INCLUDE} --proto_path=${LMCTFY_ROOT}/util/task $<

${INCLUDE}/lmctfy.pb-c.h: ${LMCTFY_ROOT}/include/lmctfy.proto 
	${PROTOC_C} --c_out=${INCLUDE} --proto_path=${LMCTFY_ROOT}/include $<

clean:
	rm -f ${INCLUDE}/*.pb-c.[ch]
	rm -f *.o

.PHONY: clean all

