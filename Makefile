all:
	gcc -Wall socket_hook.c log.c list.c stack.c share_queue.c queue.c -o socket_hook.so -shared -fPIC -ldl -lrt -lpthread
	gcc -Wall inet_server.c share_queue.c -o inet_server -lrt -lpthread
#gcc inet_udp_server.c share_queue.c -o inet_udp_server -lrt -lpthread
#gcc inet_udp_client.c -o inet_udp_client
	gcc -Wall inet_client.c -o inet_client
	gcc -Wall inet_client_shm.c log.c share_queue.c queue.c -o inet_client_shm -lrt -lpthread