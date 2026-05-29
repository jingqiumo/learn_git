#!/bin/bash
# 启动完整集群：1 PD + 3 DataNodes (Raft)
BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/bin"

echo "=== DKV Full Cluster ==="

# 启动 PD
$BIN_DIR/pd_server 127.0.0.1 8100 1 &
PD_PID=$!
sleep 1
echo "PD started on :8100 (PID=$PD_PID)"

# 启动 3 个 DataNode (Raft 模式)
# 格式: data_node <api_ip> <api_port> <db_path> <nodeId> <peerId>:<peerRaftIp:peerRaftPort>...
$BIN_DIR/data_node 127.0.0.1 9100 /tmp/dkv_node1 1 \
    1:127.0.0.1:9100 2:127.0.0.1:9200 3:127.0.0.1:9300 &
PID1=$!
echo "Node1 started (PID=$PID1) api=:9100"

$BIN_DIR/data_node 127.0.0.1 9200 /tmp/dkv_node2 2 \
    1:127.0.0.1:9100 2:127.0.0.1:9200 3:127.0.0.1:9300 &
PID2=$!
echo "Node2 started (PID=$PID2) api=:9200"

$BIN_DIR/data_node 127.0.0.1 9300 /tmp/dkv_node3 3 \
    1:127.0.0.1:9100 2:127.0.0.1:9200 3:127.0.0.1:9300 &
PID3=$!
echo "Node3 started (PID=$PID3) api=:9300"

echo "PD_PID PD_PID" > /tmp/dkv_pids.txt
echo "$PID1 $PID2 $PID3" >> /tmp/dkv_pids.txt

echo ""
echo "=== Cluster Ready ==="
echo "  PD:     127.0.0.1:8100"
echo "  Node1:  127.0.0.1:9100"
echo "  Node2:  127.0.0.1:9200"
echo "  Node3:  127.0.0.1:9300"
echo ""
echo "  Try: ./bin/dkv_cli 127.0.0.1 9100"
echo "  Ctrl+C to stop"

wait
