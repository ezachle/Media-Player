CC := g++
# todo: add conditional for the OS
CFLAGS := -g -Wall -Wextra $(shell pkg-config --cflags libavcodec libavformat libswscale libavutil libswresample sdl3)
LDFLAGS := $(shell pkg-config --libs libavcodec libavformat libswscale libavutil libswresample sdl3)

SRC := main.cpp VideoState.cpp
OBJ := $(SRC:.cpp=.o)
BUILD_NAME := MediaPlayer

all : $(BUILD_NAME)

$(BUILD_NAME) : $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

%.o : %.cpp
	$(CC) $(CFLAGS) -c $< -o $@
	
clean:
	rm $(BUILD_NAME) $(OBJ)
