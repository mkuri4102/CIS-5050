#!/bin/bash

NUM_REPLICAS=${1:-3}
BASE_PORT=8081
LB_PORT=8080

if [ ! -f "http_server" ]; then
    echo "Building HTTP server..."
    make
fi

if [ ! -f "load_balancer" ]; then
    echo "Building load balancer..."
    g++ -std=c++17 -Wall -Wextra -pedantic -pthread -o load_balancer load_balancer.cpp
fi

if [ ! -f "http_server" ] || [ ! -f "load_balancer" ]; then
    echo "Failed to build required executables. Exiting."
    exit 1
fi

echo "Stopping any existing servers..."
pkill -f "http_server" || true
pkill -f "load_balancer" || true
sleep 1

echo "Starting $NUM_REPLICAS HTTP server replicas..."
for i in $(seq 0 $(($NUM_REPLICAS-1))); do
    PORT=$(($BASE_PORT + $i))
    
    ./http_server $PORT &
    echo "Started HTTP server replica #$i on port $PORT"
done

echo "Starting load balancer on port $LB_PORT..."
./load_balancer --replicas $NUM_REPLICAS &

echo "Setup complete!"
echo "Load balancer is running on port $LB_PORT"
echo "Backend servers are running on ports $BASE_PORT through $((BASE_PORT+NUM_REPLICAS-1))"
echo "To access the system, connect to: http://localhost:$LB_PORT/"
echo
echo "Press Ctrl+C to stop all servers"

trap "echo 'Stopping all servers...'; pkill -f 'http_server'; pkill -f 'load_balancer'; exit 0" INT
wait 