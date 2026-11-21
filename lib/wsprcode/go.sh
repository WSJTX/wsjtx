#!/bin/sh
# Build wsprcode through the project so it uses the shared WSPR packer.
cmake --build "${1:-../../build}" --target wsprcode
