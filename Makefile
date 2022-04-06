all:
	gcc socket_hook.c queue.c -o socket_hook.so  -shared -fPIC -ldl -lrt -pthread
	gcc inet_server.c queue.c -o inet_server -lrt -lpthread
	gcc inet_client_shm.c queue.c -o inet_client_shm -lrt -lpthread