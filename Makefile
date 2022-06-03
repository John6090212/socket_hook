all:
	gcc -Wall -g socket_hook.c log.c list.c stack.c share_queue.c queue.c -o socket_hook.so -shared -fPIC -ldl -lrt -lpthread
	gcc -Wall -g inet_server.c share_queue.c -o inet_server -lrt -lpthread
	gcc inet_udp_server.c share_queue.c -o inet_udp_server -lrt -lpthread
	gcc inet_udp_client.c -o inet_udp_client
	gcc -Wall -g inet_client.c -o inet_client
	gcc -Wall -g inet_client_shm.c log.c share_queue.c queue.c -o inet_client_shm -lrt -lpthread
	gcc -Wall -g simple_hook.c log.c -o simple_hook.so -shared -fPIC -ldl