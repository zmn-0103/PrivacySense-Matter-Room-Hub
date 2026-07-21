# Standalone host-side build for LD2410C parser + transaction-core tests.
# Both call the REAL production code, not a copy.
# Run: make test_all

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror -std=c11 -O2
TARGET  ?= test_parser
VPATH   ?= ..

.PHONY: all clean test test_txn test_all

all: $(TARGET)

$(TARGET): test_ld2410c_parser.c ../ld2410c_parser.c
	$(CC) $(CFLAGS) -I.. -o $@ $^

test: $(TARGET)
	./$(TARGET)

test_txn: test_ld2410c_transaction.c ../ld2410c_core.c ../ld2410c_parser.c
	$(CC) $(CFLAGS) -DLD2410C_CORE_HOST -I.. -o test_txn $^
	./test_txn

test_all: test test_txn

clean:
	rm -f $(TARGET) test_txn
