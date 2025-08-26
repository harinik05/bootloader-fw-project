CC = gcc
CFLAGS = -Wall -std=c99 -g
SOURCES = bootloader.c platform.c test.c
TARGET = test_bootloader

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test clean