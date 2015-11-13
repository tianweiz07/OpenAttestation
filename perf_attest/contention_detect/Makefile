CC = gcc
LDFLAGS = -lpthread
EVENT_CACHE = event_cache
EVENT_BUS = event_bus

all: cache bus

cache: $(EVENT_CACHE).c
	$(CC) -o $(EVENT_CACHE) $(EVENT_CACHE).c $(LDFLAGS)

bus: $(EVENT_BUS).c
	$(CC) -o $(EVENT_BUS) $(EVENT_BUS).c $(LDFLAGS)

clean:
	rm -rf $(EVENT_CACHE) $(EVENT_BUS)
