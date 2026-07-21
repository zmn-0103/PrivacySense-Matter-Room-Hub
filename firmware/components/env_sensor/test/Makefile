# Standalone host-side build for DHT22 parser unit tests.
# Tests call the REAL production parser (env_sensor_parser.c), not a copy.
# Run: make test

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror -std=c11 -O2
TARGET  ?= test_dht22_parser
VPATH   ?= ..

.PHONY: all clean test

all: $(TARGET)

$(TARGET): test_dht22_parser.c ../env_sensor_parser.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
