#! /bin/bash
set -eux
# test .. this file should be in `pwd`!!!

export CXX_OPTIMIZE_FLAGS="-ggdb3 -O1"
export CXX_DEFINES="-DNDEBUG -DBOOST_FILESYSTEM_VERSION=3"

BUILDTOOLS_TO_USE=$(realpath ${WORKSPACE}/BUILDS/volumedriver-buildtools-5.0/release)

VOLUMEDRIVER_DIR=$(realpath $1)
. ${VOLUMEDRIVER_DIR}/src/buildscripts/get_revision.sh

BUILD_DIR=${VOLUMEDRIVER_DIR}/build
export RUN_TESTS=yes
export USE_MD5_HASH=yes
export CLEAN_BUILD=yes
export RECONFIGURE_BUILD=yes
export TEMP=${TEMP:-${VOLUMEDRIVER_DIR}/tmp}
export FOC_PORT_BASE=${FOC_PORT_BASE:-19250}
export BUILD_DEBIAN_PACKAGES=no
export BUILD_DOCS=no
export SKIP_TESTS="pylint python"
export SKIP_BUILD=no
export FAILOVERCACHE_TEST_PORT=${FOC_PORT_BASE}
export COVERAGE=nyet
export ARAKOON_BINARY=/usr/bin/arakoon
export ARAKOON_PORT_BASE=${ARAKOON_PORT_BASE:-$((FOC_PORT_BASE + 10))}
export VFS_PORT_BASE=${VFS_PORT_BASE:-$((FOC_PORT_BASE + 20))}
export USE_CLANG=no
export VD_EXTRA_VERSION=`get_debug_extra_version $VOLUMEDRIVER_DIR`
export SUPRESS_WARNINGS=yes
export USE_ASAN=yes
export LD_PRELOAD=/usr/lib/gcc/x86_64-linux-gnu/5/libasan.so

# adds
# * -Wno-mismatched-tags to disable "class X was previously declared as struct"
#   This is a pretty harmless (and actually perfectly valid) thing AFAICT.
# * -Wno-deprecated-register to disable "warning: 'register' storage class specifier
#    is deprecated"
#   We get a lot of these through boost and don't use it ourselves.
export CXX_WARNINGS="-Wall -Wextra -Wno-unknown-pragmas -Wctor-dtor-privacy -Wsign-promo -Woverloaded-virtual -Wnon-virtual-dtor -Wno-mismatched-tags -Wno-deprecated-register -Wno-unused-local-typedef -Wno-unused-parameter"

#export ASAN_SYMBOLIZER_PATH=${BUILDTOOLS_TO_USE}/bin/llvm-symbolizer
export ASAN_OPTIONS=symbolize=1
export LSAN_OPTIONS=exitcode=0

rm -rf ${TEMP}
mkdir -p ${TEMP}
mkdir -p ${BUILD_DIR}

ln -sf $VOLUMEDRIVER_DIR/src/buildscripts/builder.sh $BUILD_DIR

pushd $BUILD_DIR

# temporarily switch off error checking as we need to process the pylint output
# (see below)
set +e
./builder.sh $BUILDTOOLS_TO_USE $VOLUMEDRIVER_DIR
ret=$?
set -e

# the violations plugin wants a relative path - fix up the pylint output
find . -name \*_pylint.out -exec sed -i "s#${VOLUMEDRIVER_DIR}/##" {} \;

popd

exit ${ret}
