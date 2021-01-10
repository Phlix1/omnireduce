MPICC  := /usr/bin/mpicxx
CC  := g++
INCLUDE  :=-I/usr/include/mpich -I../build/include -I${CONDA_PREFIX}/include
LIBPATH  :=-L/usr/lib/ -L../build -L${CONDA_PREFIX}/lib
LDLIBS  := -libverbs -lboost_system -lboost_thread -lboost_chrono -lboost_program_options -lomnireduce 
CXXFLAGS  := -Wall -Wextra -fPIC -O3 -std=c++11 

APPS  :=  worker aggregator
ifeq ($(USE_CUDA),ON)
APPS += cuda_worker
CXXFLAGS += -DUSE_CUDA
LDLIBS += -lcudart
endif

all: ${APPS}

worker: worker_test.cpp
	${MPICC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS} 

cuda_worker: cuda_worker_test.cpp
	${MPICC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

aggregator: aggregator_test.cpp
	${CC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
