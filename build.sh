#!/bin/bash

# Bash Color
green='\033[01;32m'
red='\033[01;31m'
blink_red='\033[05;31m'
restore='\033[0m'

clear

# Number of parallel jobs to run
THREAD="-j$(nproc)"

# LLVM 20.0.0 (https://github.com/Mandi-Sa/clang/releases/tag/amd64-kernel-arm_static-20)
CLANG_BIN="/mnt/ssd0/vauxite/prebuilts/clang-standalone/bin"

# Environment
export PATH="$CLANG_BIN:$PATH"

# Vars
ARCH="arm64"
OUT="out"

KMAKE_FLAGS=(
    -j"$(nproc)"
    ARCH="$ARCH"
    O="$OUT"

    LLVM=1
    LLVM_IAS=1

    CC="clang"
    CLANG_TRIPLE="aarch64-linux-gnu-"
)

# Kernel defconfig
DEFCONFIG="vendor/pipa_user_defconfig"

# Functions
function clean_all {
    echo
    git clean -fdx > /dev/null 2>&1
}

function make_kernel {
    clang -v
    make "${KMAKE_FLAGS[@]}" $DEFCONFIG savedefconfig
    make "${KMAKE_FLAGS[@]}"
}

DATE_START=$(date +"%s")

echo -e "${green}"
echo "-----------------"
echo "Making Kernel:"
echo "-----------------"
echo -e "${restore}"

echo

while read -p "Clean stuffs (y/n)? " cchoice
do
case "$cchoice" in
    y|Y )
        clean_all
        echo
        echo "All Cleaned now."
        break
        ;;
    n|N )
        break
        ;;
    * )
        echo
        echo "Invalid try again!"
        echo
        ;;
esac
done

echo

while read -p "Start building (y/n)? " dchoice
do
case "$dchoice" in
    y|Y )
        make_kernel
        break
        ;;
    n|N )
        echo
        echo "Abort!"
        echo
        break
        ;;
    * )
        echo
        echo "Invalid try again!"
        echo
        ;;
esac
done

echo -e "${green}"
echo "-------------------"
echo "Build Completed in:"
echo "-------------------"
echo -e "${restore}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
echo
