CC = gcc
CFLAGS = -Wall
# Only one source file: fs-sim.c
SRCS = fs-sim.c
TARGET = fs
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

compile: $(SRCS)
	$(CC) $(CFLAGS) -c $(SRCS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean compile