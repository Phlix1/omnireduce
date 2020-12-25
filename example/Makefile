MPICXX  := mpicxx
CC  := g++
INCLUDE  :=-I../build/include
LIBPATH  :=-L../build
LDLIBS  := -libverbs -lpthread -lboost_system -lboost_thread -lboost_chrono -lboost_program_options -lomnireduce
CXXFLAGS  := -Wall -Wextra -fPIC -O3 -std=c++11

APPS  := worker aggregator

all: ${APPS}

worker: worker_test.cpp
	${MPICXX} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

aggregator: aggregator_test.cpp
	${CC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}
