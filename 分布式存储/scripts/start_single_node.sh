#!/bin/bash
# 启动单节点（Phase 1）
# Usage: ./start_single_node.sh

BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/bin"
DB_PATH="/tmp/dkv_node1"

mkdir -p "$BIN_DIR"
rm -rf "$DB_PATH"

echo "=== Starting DKV Single Node (Phase 1) ==="
echo "Node: 127.0.0.1:9100"
echo "DB:   $DB_PATH"
echo ""

$BIN_DIR/data_node 127.0.0.1 9100 "$DB_PATH"
