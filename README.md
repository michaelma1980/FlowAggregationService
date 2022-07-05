# FlowAggregationService

This repo contains a sample service which can allow network flow aggregation.

# Build Instructions

```
cd
sudo apt update
sudo apt install libssl-dev gcc g++ cmake libboost1.74-tools-dev libboost-all-dev
(libboost can be a different version for different Ubuntu distribution such as libboost1.71-tools-dev)
mkdir FlowAggregationService
cd FlowAggregationService
git clone --recursive https://github.com/boostorg/boost.git
cd ~/FlowAggregationService/boost/
./bootstrap.sh --prefix=`echo $HOME`/FlowAggregationService/out/boost
b2 install
cd ~/FlowAggregationService/
git clone --recursive https://github.com/michaelma1980/FlowAggregationService
cd FlowAggregationService
cmake .
make
```

# Run/Test Instructions

```
FlowAggregationService/FlowAggregationService 127.0.0.1 8080 2 &
curl -X POST "http://localhost:8080/flows" -H 'Content-Type: application/json' -d '[{"src_app": "john", "dest_app": "michael", "vpc_id": "vpc-0", "bytes_tx": 100, "bytes_rx": 500, "hour": 1}]'
curl -X POST "http://localhost:8080/flows" -H 'Content-Type: application/json' -d '[{"src_app": "john", "dest_app": "michael", "vpc_id": "vpc-0", "bytes_tx": 100, "bytes_rx": 500, "hour": 1}]'
curl http://localhost:8080/flows?hour=1
```

# High Level Introduction

- used c++
- used boost beast library to implement an http server
- used boost json to do json serialization/de-serialization
- used a circular buffer to implement the queue for which flow data can be pushed into
  - this avoids hash table operation on the critical path for flow insertion
- used a background thread to remove flow data from the circular buffer and insert to a hash table
- the hash table is protected by a read/write lock
- get request can access the hash table directly
- to achieve strong consistency(meaning that post requests completed successfully prior to get request should be reflected in the response of the get request):
  - pause the post request insertion and drain the request queue
  - resume the post request insertion after the get request is processed

# Future Work

- The scaling limit and future improvements:
  - The scaling limit for flow request insertion is for acquiring the mutex protecting the tail pointer, which takes roughly 50 ns so QPS for insertion can achieve roughly 20,000 k/s. For flow stat retrieval the scaling limit is based on the read/write lock and the overhead of hash table lookup, so should be a much lower QPS. 
  - I intentionally designed this way so that insertion can be faster with the assumption that insertion will be frequent
  - Assuming aggregation doesn't need to happen as frequently as insertion and will only be invoked from less clients compared with insertion.
  - Didn't tune the http server so IO thread count and http server throughput can affect QPS as well
- Current implementation doesn't support service fail-over
  - with the circular buffer based implementation it should be straightforward to dump the flow data into an append-only log file sequentially
  - log file can be replicated so that server fail-over can be supported
  - writing to log file can use memory mapped file to facilitate, or use write gathered to avoid memory copy - would prefer write gathered
  - insertion QPS will be reduced depending on the disk sequential write bandwidth
- Current implementation doesn't support parallelizing the insertion based on multiple queues/servers
  - circular buffer can be partitioned based on hour or the flow tuples
  - insertion for different buffers can be done in parallel and can be done on multiple servers with a load balancer in front of them
  - partition based on flow tuple would require flow aggregation against multiple servers
- Current implementation doesn't support parallelizing the retrieval of flow stat based on hour/flow tuple
  - hash table can be partitioned by hour and protected by per-hour read/write lock
  - hash table can be further partitioned by flow tuple so that we can have per-flow tuple read/write lock
