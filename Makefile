EXECUTABLE_NAME=KNES
CPP=g++
INC=
CPPFLAGS=-Wall -Wextra -Werror -Wshadow -pedantic -Ofast -std=gnu++14 -fomit-frame-pointer -march=native -flto -fpeel-loops -ftracer -ftree-vectorize
LIBS=-lportaudio -lglfw -lGL
CPPSOURCES=$(wildcard *.cpp)
CSOURCES=$(wildcard *.c)

OBJECTS=$(CPPSOURCES:.cpp=.o) $(CSOURCES:.c=.o)

.PHONY : all
all: $(CPPSOURCES) $(CSOURCES) $(EXECUTABLE_NAME)

$(EXECUTABLE_NAME) : $(OBJECTS)
	$(CPP) $(CPPFLAGS) $(OBJECTS) $(PROFILE) -o $@ $(LIBS)

%.o:%.cpp
	$(CPP) -c $(INC) $(CPPFLAGS) $(PROFILE) $< -o $@

%.o:%.c
	$(CPP) -c $(INC) $(CPPFLAGS) $(PROFILE) $< -o $@

.PHONY : clean
clean:
	rm -rf *.o $(EXECUTABLE_NAME)
