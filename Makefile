LLVM_DIR = ${CURDIR}/llvm
BUILD_DIR = ${CURDIR}/build

all: meds

meds: meds-config
	(cd ${BUILD_DIR} && make -j`nproc`)

test:
	(cd ${BUILD_DIR} && make check-meds)

meds-config: ${BUILD_DIR}
	(cd ${BUILD_DIR} && \
	CC=clang CXX=clang++ \
		cmake -DLLVM_TARGETS_TO_BUILD=X86 \
		-DCMAKE_BUILD_TYPE=Release ${LLVM_DIR})

${BUILD_DIR}:
	mkdir -p ${BUILD_DIR}

clean:
	rm -rf ${BUILD_DIR}

.phony:
	clean meds meds-config
