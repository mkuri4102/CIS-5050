#!/bin/bash

# Script to start all 9 tablet nodes

# Array to store all PIDs
declare -a pids

# Cleanup function to kill all processes
cleanup() {
  echo "Caught Ctrl+C. Terminating all processes..."
  for pid in "${pids[@]}"; do
    echo "Killing process $pid"
    kill -9 $pid 2>/dev/null
  done
  echo "All processes terminated."
  exit 0
}

# Set up trap for Ctrl+C
trap cleanup SIGINT SIGTERM

echo "Starting all tablet nodes..."

# Start the master node first
echo "Starting master node..."
./master config.txt &
master_pid=$!
pids+=($master_pid)
echo "Master node started with PID: $master_pid"

# Wait a moment for master to initialize
sleep 2

# Start all 9 tablet nodes
for i in {0..8}
do
  echo "Starting tablet $i..."
  ./tablet config.txt $i &
  tablet_pid=$!
  pids+=($tablet_pid)
  echo "Tablet $i started with PID: $tablet_pid"
done

echo "All nodes started. Use 'ps aux | grep tablet' or 'ps aux | grep master' to check running processes."
echo "Press Ctrl+C to terminate all processes."

# Wait indefinitely to keep the script running and handle Ctrl+C
while true; do
  sleep 1
done 