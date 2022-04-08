typedef struct mysocket mysocket;

struct mysocket {
    int domain;
    int type;
    int protocol;
    // check socket exist
    int in_use;
    // for getsockname
    int has_bind;
    struct sockaddr addr;
    // for getpeername
    struct sockaddr peer_addr;
    // for shutdown
    int is_shutdown;
    int how_shutdown;
};

mysocket socket_arr[1024];