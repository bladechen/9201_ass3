#!/bin/bash

if [ $# -gt 1 ]; then
    echo "Usage ./build_all.sh PATH/TO/ROOT"
    exit 0
fi

current_path=`pwd`
# If path is not supplied dont include --ostree
if [ $# -eq 0 ]; then
    SRC_PATH=~/cs3231/asst3-src
    KERN_ROOT=~/cs3231/root
    cd $SRC_PATH
    ./configure
# If path is not supplied include --ostree=path/to/root
else
    SRC_PATH=$current_path
    KERN_ROOT=$SRC_PATH/../root
    cd $SRC_PATH
    ./configure --ostree=$KERN_ROOT
    echo "Main PATH: $SRC_PATH"
    echo "Kernal Root: $KERN_ROOT"
fi

echo "----------- Making Userland NOW -----------"
# Confifure the kernel
bmake
bmake install
bash $SRC_PATH/kern/build_kern.sh $SRC_PATH/kern

echo "----------- Copying the gdbinit and conf files to root -----------"
if [ ! -f $KERN_ROOT/.gdbinit ]
then
    cp $SRC_PATH/root_config/.gdbinit $KERN_ROOT/
fi

if [ ! -f $KERN_ROOT/sys161-asst3.conf ]
then
    cp $SRC_PATH/root_config/sys161-asst3.conf $KERN_ROOT/
fi

echo "----------- Finished Making Userland -----------"
cd $current_path
