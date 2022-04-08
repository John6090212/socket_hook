all:
	gcc socket_hook.c share_queue.c queue.c -o socket_hook.so  -shared -fPIC -ldl -lrt -pthread
	gcc inet_server.c share_queue.c -o inet_server -lrt -lpthread
	gcc inet_client_shm.c share_queue.c -o inet_client_shm -lrt -lpthread