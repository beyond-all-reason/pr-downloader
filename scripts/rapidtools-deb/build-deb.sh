#!/bin/bash
set -e
cd $(dirname $(realpath $0))/pkg
dpkg-buildpackage -us -uc -b
