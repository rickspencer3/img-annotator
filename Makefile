CC = gcc
CFLAGS = -Wall -Wextra `pkg-config --cflags gtk+-3.0 cairo`
LDFLAGS = `pkg-config --libs gtk+-3.0 cairo`

TARGET = image_annotator
SRC = image_annotator.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean 