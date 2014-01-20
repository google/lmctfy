# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c


# The directory where this Makefile resides
PWD = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname ${PWD})

INCLUDE= ${PWD}/include

all: ${INCLUDE}/codes.pb-c.h ${INCLUDE}/lmctfy.pb-c.h

${INCLUDE}/codes.pb-c.h: ${LMCTFY_ROOT}/util/task/codes.proto
	${PROTOC_C} --c_out=${INCLUDE} --proto_path=${LMCTFY_ROOT}/util/task $<

${INCLUDE}/lmctfy.pb-c.h: ${LMCTFY_ROOT}/include/lmctfy.proto 
	${PROTOC_C} --c_out=${INCLUDE} --proto_path=${LMCTFY_ROOT}/include $<

clean:
	rm -f ${INCLUDE}/*.pb-c.[ch]

.PHONY: clean all

