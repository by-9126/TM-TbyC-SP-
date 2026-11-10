CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2
TARGET = main
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p input output
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./$(TARGET) --test

benchmark: $(TARGET)
	./$(TARGET) --benchmark 3

clean:
	rm -f $(TARGET)
	rm -rf output/*.txt output/*.log

.PHONY: all run test benchmark clean

