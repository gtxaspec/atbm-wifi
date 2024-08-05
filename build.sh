#!/bin/bash

# Function to display usage information
usage() {
    echo "Usage: $0 <kernel_dir_path> <cross_compile_toolchain>"
    echo "Example: $0 /path/to/kernel/sources mipsel-linux-"
    exit 1
}

# Check if the correct number of arguments is provided
if [[ $# -ne 2 ]]; then
    usage
fi

# Assign arguments to variables
KERNEL_DIR="$1"
CROSS_COMPILE_TOOLCHAIN="$2"

# Check if kernel directory is specified
if [[ -z "$KERNEL_DIR" ]]; then
    echo "Error: Kernel directory path not specified."
    usage
fi

# Check if cross-compile toolchain is specified
if [[ -z "$CROSS_COMPILE_TOOLCHAIN" ]]; then
    echo "Error: Cross-compile toolchain not specified."
    usage
fi

# Run make command
make V=1 -f Makefile \
KERDIR="$KERNEL_DIR" \
CROSS_COMPILE="$CROSS_COMPILE_TOOLCHAIN" \
arch=mips
#PWD=$(pwd) \
#M=$(pwd)/.

#-C "$KERNEL_DIR" \

#make -f Makefile KERDIR=~/output/aoqee_c1/build/linux-e5a2711375ab064060c9a03ac54a321ba66fb269/  CROSS_COMPILE=mipsel-linux-  arch=mips

make V=1 -f Makefile \
KERDIR="$KERNEL_DIR" \
CROSS_COMPILE="$CROSS_COMPILE_TOOLCHAIN" \
arch=mips \
install
