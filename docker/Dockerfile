FROM nvidia/cuda:10.1-devel-ubuntu18.04

ARG OMNIREDUCE_BASE_PATH=/usr/local
ARG OMNIREDUCE_PATH=$OMNIREDUCE_BASE_PATH/omnireduce
ARG OMNIREDUCE_GIT_LINK=https://github.com/Phlix1/omnireduce.git
ARG OMNIREDUCE_BRANCH=master

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --allow-downgrades --allow-change-held-packages --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        ibverbs-providers \
        ibverbs-utils \
        iputils-ping \
        less \
        libboost-chrono-dev \
        libboost-program-options-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libcudnn7=7.6.5.32-1+cuda10.1 \
        libibverbs-dev \
        libnccl2=2.4.8-1+cuda10.1 \
        libnccl-dev=2.4.8-1+cuda10.1 \
        libnuma-dev \
        librdmacm-dev \
        lsb-release \
        net-tools \
        openssh-client \
        openssh-server \
        perftest \
        rdmacm-utils \
        tzdata \
        vim \
        wget && \
    mkdir -p /var/run/sshd && \
    rm -rf /var/lib/apt/lists/*

# mambaforge
RUN cd /usr/local && \
    wget -O Mambaforge.sh https://github.com/conda-forge/miniforge/releases/latest/download/Mambaforge-$(uname)-$(uname -m).sh && \
    bash Mambaforge.sh -b -p /usr/local/mambaforge && \
    /usr/local/mambaforge/bin/conda init && \
    rm Mambaforge.sh
ENV PATH="/usr/local/mambaforge/bin${PATH:+:}$PATH" \
    LIBRARY_PATH="/usr/local/mambaforge/lib${LIBRARY_PATH:+:}$LIBRARY_PATH" \
    LD_LIBRARY_PATH="/usr/local/mambaforge/lib${LD_LIBRARY_PATH:+:}$LD_LIBRARY_PATH" \
    CPLUS_INCLUDE_PATH="/usr/local/mambaforge/include${CPLUS_INCLUDE_PATH:+:}$CPLUS_INCLUDE_PATH" \
    C_INCLUDE_PATH="/usr/local/mambaforge/include${C_INCLUDE_PATH:+:}$C_INCLUDE_PATH"

RUN mamba install -y -c pytorch -c conda-forge -c defaults \
        cffi \
        cmake \
        dataclasses \
        future \
        git \
        magma-cuda101 \
        mkl \
        mkl-include \
        ninja \
        numpy=1.20 \
        pip \
        python=3.7 \
        pyyaml \
        requests \
        scipy \
        setuptools \
        six=1.15.0 \
        typing_extensions && \
    pip install tensorflow-gpu==1.15 && \
    rm -f /usr/bin/python && ln -s /usr/local/mambaforge/bin/python /usr/bin/python

# Install Open MPI
RUN mkdir /tmp/openmpi && \
    cd /tmp/openmpi && \
    wget https://download.open-mpi.org/release/open-mpi/v4.0/openmpi-4.0.6.tar.gz && \
    tar zxf openmpi-4.0.6.tar.gz && \
    cd openmpi-4.0.6 && \
    ./configure --enable-orterun-prefix-by-default && \
    make -j $(nproc) all && \
    make install && \
    ldconfig && \
    rm -rf /tmp/openmpi

ENV OMPI_ALLOW_RUN_AS_ROOT=1 \
    OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1


# omnireduce
RUN cd $OMNIREDUCE_BASE_PATH && \
    git clone --branch $OMNIREDUCE_BRANCH $OMNIREDUCE_GIT_LINK && \
    cd $OMNIREDUCE_PATH && \
    make USE_CUDA=ON && \
    cp $OMNIREDUCE_PATH/build/libomnireduce.so /usr/lib/x86_64-linux-gnu/ && \
    cp -r $OMNIREDUCE_PATH/build/include/omnireduce /usr/include/ && \
    cd $OMNIREDUCE_PATH/example && \
    CUDA_HOME=/usr/local/cuda/ make USE_CUDA=ON

# pytorch
RUN cd $OMNIREDUCE_BASE_PATH && \
    git clone --depth 1 --branch v1.7.1 https://github.com/pytorch/pytorch.git && \
    cd $OMNIREDUCE_BASE_PATH/pytorch && \
    git submodule update --init --recursive --depth 1 && \
    git apply $OMNIREDUCE_PATH/frameworks_integration/pytorch_patch/omnireduce-pytorch.patch && \
    USE_SYSTEM_NCCL=0 BUILD_TEST=0 BUILD_CAFFE2_OPS=0 python setup.py install
    # https://github.com/pytorch/pytorch/issues/13061

# horovod
RUN cd $OMNIREDUCE_BASE_PATH && \
    git clone --depth 1 --branch v0.19.4 https://github.com/horovod/horovod.git && \
    cd $OMNIREDUCE_BASE_PATH/horovod && \
    git submodule update --init --recursive --depth 1 && \
    ln -s $OMNIREDUCE_PATH third_party/omnireduce && \
    git apply third_party/omnireduce/frameworks_integration/horovod_patch/omnireduce-horovod.patch && \
    HOROVOD_CUDA_HOME=/usr/local/cuda HOROVOD_NCCL_LIB=/usr/lib/x86_64-linux-gnu HOROVOD_NCCL_INCLUDE=/usr/include CC=`which gcc` CXX=`which g++` HOROVOD_WITHOUT_MXNET=1 HOROVOD_WITH_MPI=1 HOROVOD_WITH_TENSORFLOW=1 HOROVOD_WITH_PYTORCH=1 HOROVOD_NCCL_LINK=SHARED HOROVOD_GPU_ALLREDUCE=OMNI HOROVOD_GPU_BROADCAST=NCCL python setup.py install

##experiments
ARG EXPS_BASE_PATH=/home
ARG EXPS_PATH=$EXPS_BASE_PATH/exps
ARG EXPS_GIT_LINK=https://github.com/Phlix1/exps.git

RUN cd $EXPS_BASE_PATH && git clone $EXPS_GIT_LINK

#For benchmark
RUN cd $EXPS_PATH/benchmark && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For DeepLight
RUN pip install -U scikit-learn
RUN cd $EXPS_PATH/models/DeepLight && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For LSTM
RUN mamba install -y -c conda-forge -c defaults -c pytorch cython
RUN cd $EXPS_PATH/models/LSTM/lm/log_uniform && make && python setup.py install
RUN cd $EXPS_PATH/models/LSTM && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For NCF
RUN mamba install -y -c conda-forge numpy-indexed
RUN pip install mlperf_compliance
RUN cd $EXPS_PATH/models/NCF && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For CNN
RUN pip install Pillow
RUN pip install torchvision --no-dependencies
RUN pip install --extra-index-url https://developer.download.nvidia.com/compute/redist --upgrade nvidia-dali-cuda100
RUN cd /usr/local && git clone https://github.com/NVIDIA/apex && cd apex && git reset --hard a651e2c24ecf97cbf367fd3f330df36760e1c597 && \
    pip install -v --disable-pip-version-check --no-cache-dir --global-option="--cpp_ext" --global-option="--cuda_ext" ./
RUN cd $EXPS_PATH/models/CNN && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For BERT
RUN pip install nvidia-pyindex
RUN pip install nvidia-dllogger
RUN mamba install -y unzip
RUN cd $EXPS_PATH/models/BERT/dataset/checkpoint && \
    wget --content-disposition https://api.ngc.nvidia.com/v2/models/nvidia/bert_pyt_ckpt_large_qa_squad11_amp/versions/19.09.0/zip -O bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip && \
    unzip bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip
RUN cd $EXPS_PATH/models/BERT && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg && mkdir results

RUN pip install jupyter
RUN pip install matplotlib
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN sed -i 's/#   StrictHostKeyChecking ask/    StrictHostKeyChecking no/' /etc/ssh/ssh_config
COPY --from=nvidia/cuda:10.0-devel-ubuntu18.04 /usr/local/cuda-10.0 /usr/local/cuda-10.0

