#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
export CHECK_LOG="$PWD/coursework/logs/security-fixes-clippy.log"
bash coursework/scripts/modified-clippy.sh