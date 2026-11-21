CC      ?= gcc
CFLAGS  ?= -Wall -O2
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)

TARGET := scheduler_sim

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ main.c $(GTK_LIBS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
