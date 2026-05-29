#!/bin/bash
# 停止集群
if [ -f /tmp/dkv_pids.txt ]; then
    PIDS=$(cat /tmp/dkv_pids.txt)
    echo "Stopping DKV nodes: $PIDS"
    kill $PIDS 2>/dev/null
    rm /tmp/dkv_pids.txt
else
    echo "No PID file found, trying pkill..."
    pkill -f "data_node" 2>/dev/null
fi
echo "Done"
