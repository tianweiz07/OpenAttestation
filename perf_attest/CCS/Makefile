CC = gcc
LDFLAGS = -lpthread
EVENT_CACHE = event_cache

all: cache

cache: $(EVENT_CACHE).c
	$(CC) -o $(EVENT_CACHE) $(EVENT_CACHE).c $(LDFLAGS)

clean:
	rm -rf $(EVENT_CACHE)
