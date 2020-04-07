OBJECTS = routing_table_split.o linked_list.o read_bgp.o lulea_trie.o
#DEBUG = yes
# -msse4.2 needed to get hardware instruction for popcount on x86
CFLAGS = -O2 -Wall -msse4.2 -I../../src/libbgpdump-1.6.0
ifdef DEBUG
	CFLAGS += -DDEBUG -g -fsanitize=address -fsanitize=leak
#	CFLAGS += -fsanitize=address -fsanitize=leak
endif
LIBS = ../../src/libbgpdump-1.6.0/libbgpdump.a -lbz2 -lz 

all: lulea_trie_poc

clean:
	rm $(OBJECTS) lulea_trie_poc

lulea_trie_poc: $(OBJECTS)
	$(CC) -o lulea_trie_poc $(CFLAGS) $(OBJECTS) $(LIBS)
