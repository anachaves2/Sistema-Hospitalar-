CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -g -O2
LDFLAGS = -pthread -lrt
INCLUDES = -Iinclude

OBJS = src/main.o \
       src/triage.o \
       src/surgery.o \
       src/pharmacy.o \
       src/laboratory.o \
       src/ipc_utils.o \
       src/sync_utils.o \
       src/log_manager.o \
       src/stats_manager.o \
       src/config_parser.o

TARGET = hospital_system

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f src/*.o $(TARGET)
	rm -f logs/*.txt
	rm -f results/*/*.txt
	ipcrm -a 2>/dev/null || true
