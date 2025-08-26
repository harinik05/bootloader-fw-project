#!/bin/bash

echo "Building and running bootloader tests..."

make clean
make

if [ $? -eq 0 ]; then
    echo "Build successful. Running tests..."
    make test
else
    echo "Build failed!"
    exit 1
fi