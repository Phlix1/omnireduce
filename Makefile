ifeq ($(OMNIREDUCE_PATH),)
OMNIREDUCE_PATH = $(shell pwd)
export OMNIREDUCE_PATH
endif

CC  := g++
LD  := g++
SOURCEDIR  := ${OMNIREDUCE_PATH}/src
DESTDIR  := ${OMNIREDUCE_PATH}/build

INCLUDE  :=-I ${SOURCEDIR}
LDFLAGS  := -shared -lstdc++ -fPIC
LDLIBS  := -libverbs -lpthread -lboost_system -lboost_thread -lboost_chrono -lboost_program_options
CXXFLAGS  := -Wall -Wextra -fPIC -O3 -std=c++11
SOURCE:=${wildcard ${SOURCEDIR}/*.cpp}
OBJS:=${patsubst ${SOURCEDIR}/%.cpp,${SOURCEDIR}/%.o,${SOURCE}}

TARGET_LIB  := libomnireduce.so

all:${OBJS}
	${LD} ${LDFLAGS}  -o ${SOURCEDIR}/${TARGET_LIB} ${OBJS} ${LDLIBS}
	mkdir -p ${DESTDIR}/include
	cp ${SOURCEDIR}/${TARGET_LIB} ${DESTDIR}
	cp ${SOURCEDIR}/*.hpp ${DESTDIR}/include

${SOURCEDIR}/%.o:${SOURCEDIR}/%.cpp
	${CC} -c ${CXXFLAGS} $< -o ${SOURCEDIR}/$*.o ${INCLUDE}

.PHONY: clean

clean:
	rm ${SOURCEDIR}/*.so ${SOURCEDIR}/*.o -rf
	rm -rf ${DESTDIR}