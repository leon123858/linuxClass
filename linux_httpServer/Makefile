all:
	$(CC) -Wall -o httpServer httpServer.c -lpthread -g -fsanitize=thread

indent:
	clang-format -i httpServer.c

clean:
	rm -f httpServer
