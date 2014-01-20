# TODO(monnand): Check protoc-c is installed
PROTOC_C = protoc-c


# The directory where this Makefile resides
PWD = $(shell dirname $(abspath $(lastword $(MAKEFILE_LIST))))

LMCTFY_ROOT = $(shell dirname ${PWD})

INCLUDE= ${PWD}/include

${INCLUDE}/lmctfy.pb-c.h: ${LMCTFY_ROOT}/include/lmctfy.proto 
	${PROTOC_C} --c_out=${INCLUDE} --proto_path=${LMCTFY_ROOT}/include $<

