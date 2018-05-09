all: lib/libgps_utils.so

CFLAGS=-std=c++11 -Iinclude -Wall -Werror -fpic

lib/libgps_utils.so : src/gps_utils.cpp include/gps_utils.h 
	mkdir -p lib
	g++ -c ${CFLAGS} src/gps_utils.cpp 
	g++ -shared -o lib/libgps_utils.so gps_utils.o
	rm gps_utils.o

