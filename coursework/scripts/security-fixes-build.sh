#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
export BUILD_LOG="$PWD/coursework/logs/security-fixes-build.log"
bash coursework/scripts/modified-build.sh