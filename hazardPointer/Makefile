all:
	$(CC) -Wall -o list list.c -lpthread -g -fsanitize=thread

indent:
	clang-format -i rcu_list.c

clean:
	rm -f rcu_list
