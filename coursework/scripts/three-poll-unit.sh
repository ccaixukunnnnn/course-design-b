#!/bin/bash
set -o pipefail
cd /mnt/d/OS\ experiment/course-design-b
bash coursework/scripts/test-event-poll.sh 2>&1 | tee coursework/logs/three-poll-unit.log
exit "${PIPESTATUS[0]}"