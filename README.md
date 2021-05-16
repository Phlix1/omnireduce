# OmniReduce
OmniReduce is an efficient sparse collective communication  library. It maximizes effective bandwidth use by exploiting the sparsity of data.

OmniReduce can run on either TCP or RDMA network. For TCP network, OmniReduce uses Intel DPDK for kernel bypass. GPUDirect can also be used where available. We only introduce the RDMA version of OmniReduce here.

## Getting Started
The simplest way to start is to use our [docker image](https://github.com/Phlix1/omnireduce/tree/master/docker). We provide a [tutorial](https://github.com/Phlix1/omnireduce/blob/master/docs/tutorial.md) to help you run OmniReduce with docker image quickly.
Below, we introduce how to build and use OmniReduce.

### Building
OmniReduce is built to run on Linux and the dependencies include CUDA, ibverbs and Boost C++ library.
To build OmniReduce, run:

    git clone https://github.com/Phlix1/omnireduce.git
    cd omnireduce
    make USE_CUDA=ON


### Examples
Basic examples are provided under the [example](https://github.com/Phlix1/omnireduce/tree/master/example) folder. 
To reproduce the evaluation in our SIGCOMM'21 paper, find the code at this [repo](https://github.com/Phlix1/exps).
## Frameworks Integration
OmniReduce is only integrated with PyTorch currently. The integration method is under the [frameworks_integration](https://github.com/Phlix1/omnireduce/tree/master/frameworks_integration/pytorch_patch) folder.

## Limitations

- Only support AllReduce operation
- Only support int32 and float data type

## Publications

[OmniReduce](https://sands.kaust.edu.sa/project/omnireduce/) accepted at SIGCOMM’21.
