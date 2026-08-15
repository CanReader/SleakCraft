#!/usr/bin/env bash
set -e

# Script to generate Doxygen HTML documentation for BOTH SleakEngine and SleakCraft

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${ROOT_DIR}"

if ! command -v doxygen &> /dev/null; then
    echo "Error: Doxygen is not installed or not in PATH."
    exit 1
fi

echo "==============================================="
echo "1. Generating Doxygen Documentation for SleakEngine"
echo "==============================================="
(cd Engine && doxygen Doxyfile)
echo "SleakEngine Docs generated at Engine/docs/html/index.html"

echo ""
echo "==============================================="
echo "2. Generating Doxygen Documentation for SleakCraft"
echo "==============================================="
doxygen Doxyfile
echo "SleakCraft Docs generated at docs/html/index.html"

echo ""
echo "==============================================="
echo "All Documentation Suites Successfully Generated!"
echo "SleakEngine Main Page: Engine/docs/html/index.html"
echo "SleakCraft Main Page:  docs/html/index.html"
echo "==============================================="
