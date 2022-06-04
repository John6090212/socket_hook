#include "socket_hook.h"

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec  = t2.tv_sec - t1.tv_sec;
    if (td->tv_sec > 0 && td->tv_nsec < 0)
    {
        td->tv_nsec += NS_PER_SECOND;
        td->tv_sec--;
    }
    else if (td->tv_sec < 0 && td->tv_nsec > 0)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

// for debug
void my_log_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        log_trace("%02x", m[i]);
    }
}

// just compare family and port
int cmp_addr(const struct sockaddr *a, const struct sockaddr *b){
    struct sockaddr_in *ai = (struct sockaddr_in *)a;
    struct sockaddr_in *bi = (struct sockaddr_in *)b;

    if(ai->sin_family != bi->sin_family)
        return 0;
    else if(ai->sin_port != bi->sin_port)
        return 0;
    else
        return 1;
}

// poll timer
void my_signal_handler(int signum){
    if(signum == SIGRTMIN){
        __sync_val_compare_and_swap(&poll_timeout, false, true);
    }
}

int my_createtimer(timer_t *timer){
    struct sigevent evp = (struct sigevent){
        .sigev_value.sival_ptr = timer,
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo = SIGRTMIN
    };

    return timer_create(CLOCK_REALTIME, &evp, timer);
}

int my_poll_settimer(int timeout){
    timer_t timer = poll_timer;
    
    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = (time_t)(timeout / 1000),
            .tv_nsec = (long)(timeout % 1000) * 1000000
        }
    };

    if(timer_settime(timer, 0, &new_value, NULL) == -1){
        log_error("poll settimer failed, %s", strerror(errno));
        return -1;
    }

    return 0;
}

int my_poll_stoptimer(void){
    timer_t timer = poll_timer;

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        }
    };    
    if(timer_settime(timer, TIMER_ABSTIME, &new_value, NULL) == -1){
        log_error("poll stoptimer failed");
        return -1;
    }

    return 0;
}

void init_share_queue(int i, bool is_stream){
    //initialize mutex attr
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    int share_capacity = is_stream ? STREAM_QUEUE_CAPACITY : DATAGRAM_QUEUE_CAPACITY;
    share_queue req_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = share_capacity,
        .current_size = 0,
        .message_start_offset = (i+1)*2*sizeof(share_queue)+(i+1)*2*sizeof(pthread_mutex_t)+i*2*STREAM_QUEUE_CAPACITY
    };
    memcpy(shm_ptr+i*sizeof(share_unit), &req_queue, sizeof(share_queue));

    share_queue res_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = share_capacity,
        .current_size = 0,
        .message_start_offset = (i+1)*2*sizeof(share_queue)+(i+1)*2*sizeof(pthread_mutex_t)+(i*2+1)*STREAM_QUEUE_CAPACITY
    };
    memcpy(shm_ptr+i*sizeof(share_unit)+sizeof(share_queue), &res_queue, sizeof(share_queue));
    
    pthread_mutex_t *request_lock = (pthread_mutex_t *)(shm_ptr+i*sizeof(share_unit)+2*sizeof(share_queue));
    if(pthread_mutex_init(request_lock, &attr) != 0) log_error("pthread_mutex_init request lock failed");

    pthread_mutex_t *response_lock = (pthread_mutex_t *)(shm_ptr+i*sizeof(share_unit)+2*sizeof(share_queue)+sizeof(pthread_mutex_t));
    if(pthread_mutex_init(response_lock, &attr) != 0) log_error("pthread_mutex_init response lock failed");
}

void init_connect_accept_queue(void){
    connect_queue con_queue = (connect_queue){
        .front = 0,
        .rear = CONNECT_QUEUE_CAPACITY - 1,
        .size = 0,
        .capacity = CONNECT_QUEUE_CAPACITY,
        .queue_start_offset = sizeof(connect_queue)+sizeof(accept_queue)+2*sizeof(pthread_mutex_t)
    };
    connect_queue_ptr = (connect_queue *)connect_shm_ptr;
    memcpy(connect_queue_ptr, &con_queue, sizeof(connect_queue));

    accept_queue acc_queue = (accept_queue){
        .front = 0,
        .rear = ACCEPT_QUEUE_CAPACITY - 1,
        .size = 0,
        .capacity = ACCEPT_QUEUE_CAPACITY,
        .queue_start_offset = sizeof(connect_queue)+sizeof(accept_queue)+2*sizeof(pthread_mutex_t)+CONNECT_QUEUE_CAPACITY*sizeof(connection)
    };
    accept_queue_ptr = (accept_queue *)(connect_shm_ptr+sizeof(connect_queue));
    memcpy(accept_queue_ptr, &acc_queue, sizeof(accept_queue));

    // initialize mutex lock for connect queue and accept queue
    //initialize mutex attr
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    
    connect_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue));
    if(pthread_mutex_init(connect_lock, &attr) != 0) log_error("pthread_mutex_init connect_lock failed");
    
    accept_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue)+sizeof(pthread_mutex_t));
    if(pthread_mutex_init(accept_lock, &attr) != 0) log_error("pthread_mutex_init accept_lock failed");
}

int init_socket(int fd, int domain, int type, int protocol){
    socket_arr[fd] = (mysocket){
        .domain = domain,
        .type = type,
        .protocol = protocol,
        .has_bind = 0,
        .in_use = 1,
        .shutdown_read = 0,
        .shutdown_write = 0,
        .msg_more_buf = NULL,
        .msg_more_size = 0,
        .share_unit_index = -1,
        .file_status_flags = 0,
        .pollfds_index = -1,
        .is_accept_fd = 0,
        .is_server = 0,
        .control_sock = -1,
        .is_udp = false
    };
    socket_arr[fd].share_unit_index = pop(available_share_unit);
    if(socket_arr[fd].share_unit_index == INT_MIN){
        log_fatal("need to increase SOCK_NUM");
        return -1;
    }
    if(type == SOCK_DGRAM){
        init_share_queue(socket_arr[fd].share_unit_index, false);
        socket_arr[fd].is_udp = true;
    }
    else
        init_share_queue(socket_arr[fd].share_unit_index, true);

    socket_arr[fd].request_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_queue);
    socket_arr[fd].response_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_queue);
    socket_arr[fd].request_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_lock);
    socket_arr[fd].response_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_lock);

    return 0;
}

int init_close_unit(int share_unit_index){
    if(share_unit_index < 0 || share_unit_index >= SOCKET_NUM){
        log_error("share unit index out of bound\n");
        return -1;
    }

    close_arr[share_unit_index].client_read = 0;
    close_arr[share_unit_index].client_write = 0;
    close_arr[share_unit_index].server_read = 0;
    close_arr[share_unit_index].server_write = 0;

    return 0;
}

void init_function_pointer(void){
    original_socket = dlsym(RTLD_NEXT, "socket");
    original_bind = dlsym(RTLD_NEXT, "bind");
	original_listen = dlsym(RTLD_NEXT, "listen");
	original_accept = dlsym(RTLD_NEXT, "accept");
	original_connect = dlsym(RTLD_NEXT, "connect");
	original_close = dlsym(RTLD_NEXT, "close");
	original_shutdown = dlsym(RTLD_NEXT, "shutdown");
	original_getsockname = dlsym(RTLD_NEXT, "getsockname");
    original_recv = dlsym(RTLD_NEXT, "recv");
    original_send = dlsym(RTLD_NEXT, "send");
    original_socketpair = dlsym(RTLD_NEXT, "socketpair");
    original_getpeername = dlsym(RTLD_NEXT, "getpeername");
    original_read = dlsym(RTLD_NEXT, "read");
    original_write = dlsym(RTLD_NEXT, "write");
    original_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    original_getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    original_fcntl = dlsym(RTLD_NEXT, "fcntl");
    original_fcntl64 = dlsym(RTLD_NEXT, "fcntl64");
    original_ioctl = dlsym(RTLD_NEXT, "ioctl");
    original_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    original_sendto = dlsym(RTLD_NEXT, "sendto");
    original_recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    original_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    original_poll = dlsym(RTLD_NEXT, "poll");
    original_fork = dlsym(RTLD_NEXT, "fork");
    original_select = dlsym(RTLD_NEXT, "select");
}

void init_logging(void){
    log_set_quiet(true);
    log_fd = -2;
    time_t cur_t = time(0);
    struct tm* t = localtime(&cur_t);
    char log_name[100] = {0};
    snprintf(log_name, 100, "socket_hook_%04u-%02u-%02u-%02u:%02u:%02u.log", 
      t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    FILE *fp = fopen((const char *)log_name, "w+");
    if(fp && fileno(fp) != -1){
        log_fd = fileno(fp);
        log_add_fp(fp, LOG_ERROR);
    }
}

void init_share_memory(void){
    // initialize communication share memory
    shm_name = getenv("AFLNET_SHARE_MESSAGE_SHM");
    if(shm_name == NULL){
        log_error("shm_name getenv failed");
        exit(999);
    }
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        log_error("message_sm shm_open failed");
        exit(999);
    }
    ftruncate(shm_fd, COMMUNICATE_SHM_SIZE);

    shm_ptr = mmap(NULL, COMMUNICATE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == (void *)-1){
        log_error("shm_ptr mmap failed");
    }

    // initialize connect share memory
    connect_shm_name = getenv("AFLNET_SHARE_CONNECT_SHM");
    if(connect_shm_name == NULL){
        log_error("connect_shm_name getenv failed");
        exit(999);
    }
    connect_shm_fd = shm_open(connect_shm_name, O_CREAT | O_RDWR, 0666);
    if (connect_shm_fd < 0){
        log_error("connect_sm shm_open failed");
        exit(999);
    }
    ftruncate(connect_shm_fd, CONNECT_SHM_SIZE);

    connect_shm_ptr = mmap(NULL, CONNECT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, connect_shm_fd, 0);
    if(connect_shm_ptr == (void *)-1){
        log_error("connect_shm_ptr mmap failed");
    }
    init_connect_accept_queue();

    // initialize request/response queue
    for(int i = 0; i < SOCKET_NUM; i++){
        if(server == TINYDTLS)
            init_share_queue(i, false);
        else
            init_share_queue(i, true);
    }

    // initialize close share memory
    close_shm_name = getenv("AFLNET_SHARE_CLOSE_SHM");
    if(close_shm_name == NULL){
        log_error("close_shm_name getenv failed");
        exit(999);
    }
    close_shm_fd = shm_open(close_shm_name, O_CREAT | O_RDWR, 0666);
    if (close_shm_fd < 0){
        log_error("close_shm_fd shm_open failed");
        exit(999);
    }
    ftruncate(close_shm_fd, CLOSE_SHM_SIZE);

    close_shm_ptr = mmap(NULL, CLOSE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, close_shm_fd, 0);
    if(close_shm_ptr == (void *)-1){
        log_error("close_shm_ptr mmap failed");
    }
    memset(close_shm_ptr, 0, CLOSE_SHM_SIZE);
    close_arr = (close_unit *)close_shm_ptr;
}

__attribute__((constructor)) void init(){
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &hook_start_time);

    // initialize server type
    server = TINYDTLS;
    
    init_function_pointer();

    init_logging();

    init_share_memory();

    // initialize available fd stack
    available_fd = createStack(1024);
    for(int i = 0; i <=1023; i++){
        push(available_fd, i);
    }

    // initialize available share unit queue
    available_share_unit = createStack(SOCKET_NUM);
    for(int i = SOCKET_NUM - 1; i >= 0; i--){
        push(available_share_unit, i);
    }

    // initialize socket array
    memset(socket_arr, 0, 1024*sizeof(mysocket));

    // initialize random seed
    srand(time(NULL));
    

    // initialize poll timer
    poll_timeout = false;
    poll_timer = NULL;
    signal(SIGRTMIN, my_signal_handler);

    // initialize select check number
    for(int i = 0; i < 64; i++){
        select_check_num[i] = 1UL << i;
    }

    // for malformed packet to avoid control socket timeout
    read_count = 0;
    if(server == TINYDTLS)
        tinydtls_fd = -1;

    // for smart affinity
    cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    aflnet_cpu_id = sched_getcpu();
    next_cpu_id = (aflnet_cpu_id + 1) % cpu_count;
    log_trace("cpu_count: %d", cpu_count);
    log_trace("aflnet_cpu_id: %d", aflnet_cpu_id);
    log_trace("next_cpu_id: %d", next_cpu_id);
}

__attribute__((destructor)) void fin(){
    if(log_fd >= 0)
        original_close(log_fd);
    asm("xor %rdi, %rdi");
    asm("mov $60, %rax");
    asm("syscall");
}

bool is_valid_fd(int sockfd){
    if(sockfd >= 0 && sockfd <= 1023 && socket_arr[sockfd].in_use == 1)
        return true;
    else
        return false;
}

bool not_hook_socket_type(int domain, int type, int protocol){
    // filter out non-tcp and unix socket for dnsmasq
    if(server == DNSMASQ)
        return !(domain == AF_INET && type == SOCK_STREAM);
    else if(server == TINYDTLS)
        return !(domain == AF_INET6 && type == SOCK_DGRAM);
    else
        return true;
}

int socket(int domain, int type, int protocol){
    log_trace("hook socket()!");
    if(not_hook_socket_type(domain, type, protocol)){
        int fd = original_socket(domain, type, protocol);
        log_trace("fd = %d", fd);
        return fd;
    }

    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    int fd = pop(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        log_error("pop fd failed");
        return -1;
    }
    if (init_socket(fd, domain, type, protocol) == -1){
        log_error("init socket failed, fd=%d", fd);
        return -1;
    }
    log_trace("use self management fd: %d", fd);
    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("socket time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }

    if(server == TINYDTLS){
        tinydtls_fd = fd;
        memset(&tinydtls_peer_addr, 0, sizeof(struct sockaddr_in6));
        tinydtls_peer_addr.sin6_family = AF_INET6;
        tinydtls_peer_addr.sin6_port = htons(8888);
        const unsigned char localhost_bytes[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
        memcpy(&tinydtls_peer_addr.sin6_addr, localhost_bytes, sizeof(localhost_bytes));
    }

    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    log_trace("hook bind(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd)) 
        return original_bind(sockfd, addr, addrlen);

    socket_arr[sockfd].has_bind = 1;
    memcpy(&socket_arr[sockfd].addr, addr, addrlen);

    if(socket_arr[sockfd].is_udp){
        socket_arr[sockfd].is_server = 1;

        socket_arr[sockfd].control_sock = original_socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if(socket_arr[sockfd].control_sock == -1)
            log_error("control socket create failed");

        struct sockaddr_un serveraddr;
        memset(&serveraddr, 0, sizeof(serveraddr));
        serveraddr.sun_family = AF_UNIX;
        char *control_sock_name = getenv("CONTROL_SOCKET_NAME");
        if(control_sock_name == NULL){
            log_error("control_socket_name getenv failed");
            exit(999);
        }
        strncpy(serveraddr.sun_path, control_sock_name, sizeof(serveraddr.sun_path));
        
        int i;
        for(i = 0; i < 10000; i++){
            if(original_connect(socket_arr[sockfd].control_sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == 0) 
                break;
            usleep(10);
        }
        if(i == 1000){
            log_error("control socket connect failed, %s", strerror(errno));
            return -1;
        }

        char control_buf[22];
        snprintf(control_buf, 22, "share_unit_index:%d", socket_arr[sockfd].share_unit_index);
        log_trace("send share_unit_index = %d", socket_arr[sockfd].share_unit_index);
        ssize_t n;
        if((n = original_send(socket_arr[sockfd].control_sock, control_buf, sizeof(control_buf), MSG_NOSIGNAL)) <= 0){
            log_error("control socket send normal failed, n=%d, %s",n , strerror(errno));
        }
    }

    return 0;
}

int listen(int sockfd, int backlog){
    log_trace("hook listen(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd)){
        return original_listen(sockfd, backlog);
    }
    
    // for poll
    socket_arr[sockfd].is_accept_fd = 1;

    return 0;
}

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    log_trace("hook accept(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_accept(sockfd, addr, addrlen);
    
    struct timespec start, finish, delta;
    int fd = 0;    
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    
    /*
    if(socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(connect_queue_ptr->size == 0){
            log_error("accept would block");
            errno = EWOULDBLOCK;
            return -1;
        }
    }*/
    fd = pop(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    if(init_socket(fd, socket_arr[sockfd].domain, socket_arr[sockfd].type, socket_arr[sockfd].protocol) == -1)
        return -1;
    socket_arr[fd].is_server = 1;

    // save bind address to share memory
    if(socket_arr[sockfd].has_bind == 1 && socket_arr[fd].share_unit_index >= 0 && socket_arr[fd].share_unit_index < SOCKET_NUM){
        while(connect_queue_ptr->size == 0)
            usleep(0);
        if(pthread_mutex_lock(connect_lock) != 0) log_error("pthread_mutex_lock connect_lock failed");
        connection *c = Connect_dequeue(connect_shm_ptr, connect_queue_ptr);
        if(pthread_mutex_unlock(connect_lock) != 0) log_error("pthread_mutex_unlock connect_lock failed");
        if(c != NULL){
            // accept connection and save share unit index to accept queue
            if(cmp_addr(&socket_arr[sockfd].addr, &(c->addr))){
                acception a = (acception){
                    .client_fd = c->client_fd,
                    .share_unit_index = socket_arr[fd].share_unit_index
                };
                
                // init close unit before client accept
                init_close_unit(socket_arr[fd].share_unit_index);
                while(accept_queue_ptr->size == accept_queue_ptr->capacity)
                    usleep(0);
                if(pthread_mutex_lock(accept_lock) != 0) log_error("pthread_mutex_lock accept_lock failed");
                if(Accept_enqueue(connect_shm_ptr, accept_queue_ptr, a) == -1)
                    log_error("accept queue enqueue failed");
                if(pthread_mutex_unlock(accept_lock) != 0) log_error("pthread_mutex_unlock accept_lock failed");
            }
            free(c);
        }
        else{
            log_error("connect queue dequeue failed");
            return -1;
        }
    }
    else{
        log_error("not bind or share unit index out of bound!");
        return -1;
    }

    struct sockaddr_in *fake_addr;
    if(addr != NULL)
        fake_addr = (struct sockaddr_in *) addr;
    else
        fake_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    struct in_addr net_addr;
    // suppose client use localhost to connect
    char fa[] = "127.0.0.1";
    inet_aton(fa, &net_addr);
    fake_addr->sin_addr = net_addr;
    // get random port for peer
    fake_addr->sin_port = rand() % 10000 + 10000;
    fake_addr->sin_family = AF_INET;
    // save peer_addr
    memcpy(&socket_arr[fd].peer_addr, fake_addr, sizeof(struct sockaddr));
    if(addr == NULL)
        free(fake_addr); 

    // connect to control server
    socket_arr[fd].control_sock = original_socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if(socket_arr[fd].control_sock == -1)
        log_error("control socket create failed");

    struct sockaddr_un serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sun_family = AF_UNIX;
    char *control_sock_name = getenv("CONTROL_SOCKET_NAME");
    if(control_sock_name == NULL){
        log_error("control_socket_name getenv failed");
        exit(999);
    }
    strncpy(serveraddr.sun_path, control_sock_name, sizeof(serveraddr.sun_path));
    
    if(original_connect(socket_arr[fd].control_sock, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1)
        log_error("control socket connect failed");
    
    if(server == DNSMASQ)
        read_count = 0;
    
    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("accept time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    log_trace("hook connect(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_connect(sockfd, addr, addrlen);
        
    
    memcpy(&socket_arr[sockfd].peer_addr, addr, sizeof(struct sockaddr));

    return 0;
}

int close(int fd){
    if(fd == log_fd){
        log_trace("hook close(), log_fd=%d",fd);
        return 0;
    }

    if(!is_valid_fd(fd))
        return original_close(fd);

    log_trace("hook close(), fd=%d",fd);
    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);

    push(available_share_unit, socket_arr[fd].share_unit_index);
    // clear address in connect sockaddr array
    init_connect_accept_queue();
    if(socket_arr[fd].share_unit_index >= 0){
        close_arr[socket_arr[fd].share_unit_index].server_read = 1;
        close_arr[socket_arr[fd].share_unit_index].server_write = 1;
    }
    // close control socket
    if(socket_arr[fd].control_sock != -1)
        original_close(socket_arr[fd].control_sock);
    memset(&socket_arr[fd], 0, sizeof(mysocket));
    push(available_fd, fd);

    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("close time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return 0;   
}

int shutdown(int sockfd, int how){
    log_trace("hook shutdown(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_shutdown(sockfd, how);

    
    if(how == SHUT_RD){
        socket_arr[sockfd].shutdown_read = 1;
        if(socket_arr[sockfd].share_unit_index >= 0)
            close_arr[socket_arr[sockfd].share_unit_index].server_read = 1;
    }
    else if(how == SHUT_WR){
        socket_arr[sockfd].shutdown_write = 1;
        if(socket_arr[sockfd].share_unit_index >= 0)
            close_arr[socket_arr[sockfd].share_unit_index].server_write = 1;
    }
    else if(how == SHUT_RDWR){
        socket_arr[sockfd].shutdown_read = 1;
        socket_arr[sockfd].shutdown_write = 1;
        if(socket_arr[sockfd].share_unit_index >= 0){
            close_arr[socket_arr[sockfd].share_unit_index].server_read = 1;
            close_arr[socket_arr[sockfd].share_unit_index].server_write = 1;
        }
    }
    struct timespec finish, delta;
    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);  
        sub_timespec(hook_start_time, finish, &delta);
        log_info("shutdown relative time: %lld.%.9ld", delta.tv_sec, delta.tv_nsec);
        log_info("shutdown clock time: %lld.%.9ld", finish.tv_sec, finish.tv_nsec);   
    }
    return 0;
}

int getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    log_trace("hook getsockname(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_getsockname(sockfd, addr, addrlen);

    
    memcpy(addr, &socket_arr[sockfd].addr, sizeof(struct sockaddr));   

    return 0;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    log_trace("hook recv(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_recv(sockfd, buf, len, flags);

    
    // deal with shutdown
    if(socket_arr[sockfd].shutdown_read || close_arr[socket_arr[sockfd].share_unit_index].client_write)
        return 0;

    // handle nonblocking flag
    if(flags & MSG_DONTWAIT || socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(socket_arr[sockfd].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    
    ssize_t count = 0;
    // while loop for MSG_WAITALL
    while(count != len){
        while(socket_arr[sockfd].request_queue->current_size == 0){
            if(close_arr[socket_arr[sockfd].share_unit_index].client_write)
                return 0;
            usleep(0);
        }

        if(pthread_mutex_lock(socket_arr[sockfd].request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
        if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_STREAM){
            if(flags & MSG_PEEK){
                char *m_arr = (char *)(shm_ptr+socket_arr[sockfd].request_queue->message_start_offset);
                memcpy(buf+count, &m_arr[socket_arr[sockfd].request_queue->front], min(socket_arr[sockfd].request_queue->current_size,len-count)*sizeof(char));
                count += min(socket_arr[sockfd].request_queue->current_size,len-count);
            }
            else{
                buffer *b = stream_dequeue(shm_ptr, socket_arr[sockfd].request_queue, len-count);
                if(b->buf != NULL){
                    // tcp will discard the received byte, rather than save in buffer
                    if(!(flags & MSG_TRUNC)){
                        memcpy(buf+count, b->buf, b->length *sizeof(char));
                    }
                    count += b->length;
                    free(b->buf);
                }
                free(b);
            }
        }
        // in other case, MSG_TRUNC return the real length of the packet and discard the oversized part
        else if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_DGRAM){
            message_t *m = datagram_dequeue(shm_ptr, socket_arr[sockfd].request_queue);
            if(m->length != 0){
                memcpy(buf, m->buf, min(len,m->length));
                count = (flags & MSG_TRUNC) ? m->length : min(len,m->length);
            }
            free(m);
        }
        else{
            log_fatal("not implement socket type in recv!");
            exit(999);
        }
        if(pthread_mutex_unlock(socket_arr[sockfd].request_lock) != 0) log_error("pthread_mutex_unlock request lock failed");
        // skip if no MSG_WAITALL or socket type is datagram (MSG_WAITALL has no effect)
        if(!(flags & MSG_WAITALL) || (socket_arr[sockfd].type == SOCK_DGRAM)) break;
    }

    return count;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags){
    log_trace("hook send(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_send(sockfd, buf, len, flags);

    
    // deal with shutdown
    if(socket_arr[sockfd].shutdown_write)
        raise(SIGPIPE);

    // handle nonblocking flag
    if(flags & MSG_DONTWAIT || socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity){
        if(close_arr[socket_arr[sockfd].share_unit_index].client_read)
            raise(SIGPIPE);
        usleep(0);
    }

    ssize_t count = 0;
    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_STREAM){
        if((flags & MSG_MORE) && (socket_arr[sockfd].msg_more_size + len < LO_MSS)){
            // save stream in temporary buffer
            if(socket_arr[sockfd].msg_more_buf == NULL){
                socket_arr[sockfd].msg_more_buf = malloc(len*sizeof(char));
                socket_arr[sockfd].msg_more_size = len;
                memcpy(socket_arr[sockfd].msg_more_buf, buf, len);
                count = len;
            }
            // temporary buffer already exist
            else{
                socket_arr[sockfd].msg_more_buf = realloc(socket_arr[sockfd].msg_more_buf, (socket_arr[sockfd].msg_more_size+len)*sizeof(char));
                memcpy(socket_arr[sockfd].msg_more_buf+socket_arr[sockfd].msg_more_size, buf, len);
                socket_arr[sockfd].msg_more_size += len;
                count = len;
            }
        }
        // temporary buffer length might exceed MSS, force to send stream
        else if(socket_arr[sockfd].msg_more_buf != NULL){
            int total_len = socket_arr[sockfd].msg_more_size + len;
            int send_times = (int)(total_len / LO_MSS) + 1;
            char *temp_buf = malloc(total_len*sizeof(char));
            memcpy(temp_buf, socket_arr[sockfd].msg_more_buf, socket_arr[sockfd].msg_more_size);
            memcpy(temp_buf+socket_arr[sockfd].msg_more_size, buf, len);

            int temp_count = 0;
            for(int i = 0; i < send_times; i++){
                if(i == 0){
                    temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf, min(total_len,LO_MSS));
                    if(temp_count >= 0)
                        count = max((temp_count - socket_arr[sockfd].msg_more_size), 0);
                    if(send_times == 1){
                        if(temp_count < 0)
                            count = temp_count;
                        break;
                    }    
                }
                else{
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity)
                        usleep(0);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, total_len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
                }             
            }
            // clean temporary buffer
            free(socket_arr[sockfd].msg_more_buf);
            socket_arr[sockfd].msg_more_buf = NULL;
            socket_arr[sockfd].msg_more_size = 0;
            free(temp_buf);
        }
        // no temporary buffer
        else{
            int send_times = (int)(len / LO_MSS) + 1;
            int temp_count = 0;
            for(int i = 0; i < send_times; i++){
                if(i == 0){
                    temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf, min(len,LO_MSS));
                    if(temp_count > 0)
                        count = temp_count;
                    if(send_times == 1){
                        if(temp_count < 0)
                            count = temp_count;
                        break;
                    }   
                }
                else{
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity)
                        usleep(0);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
                }             
            }
        }
    }
    else if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_DGRAM){
        message_t m = {
            .length = len,
        };
        if(len > MESSAGE_MAX_LENGTH)
            log_error("need to increase MESSAGE_MAX_LENGTH, len=%d", len);    
        memcpy(m.buf, buf, min(len,MESSAGE_MAX_LENGTH));
        if(datagram_enqueue(shm_ptr, socket_arr[sockfd].response_queue, m) == 0)
            count = len;
    }
    else{
        log_fatal("not implement socket type in send!");
        exit(999);
    }
    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
    
    return count;
}

int socketpair(int domain, int type, int protocol, int sv[2]){
    // filter out non-tcp and unix socket (dnsmasq not use this function)
    if(domain == AF_UNIX || type != SOCK_STREAM)
        return original_socketpair(domain, type, protocol, sv);

    log_trace("hook socketpair()!");
    int fd1 = pop(available_fd);
    if(fd1 == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    if (init_socket(fd1, domain, type, protocol) == -1)
        return -1;

    int fd2 = pop(available_fd);
    if(fd2 == INT_MIN){
        memset(&socket_arr[fd1], 0, sizeof(mysocket));
        push(available_fd, fd1);
        errno = EMFILE;
        return -1;
    }
    if (init_socket(fd2, domain, type, protocol) == -1)
        return -1;

    sv[0] = fd1;
    sv[1] = fd2; 

    return 0;
}

int getpeername(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    log_trace("hook getpeername(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_getpeername(sockfd, addr, addrlen);
        
    
    memcpy(addr, &socket_arr[sockfd].peer_addr, sizeof(struct sockaddr));
    log_trace("getpeername return 0");
    return 0;
}

ssize_t read(int fd, void *buf, size_t count){
    log_trace("hook read(), fd=%d, count=%ld", fd, count);
    if(!is_valid_fd(fd))
        return original_read(fd, buf, count);

    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    
    // deal with shutdown
    if(socket_arr[fd].shutdown_read || close_arr[socket_arr[fd].share_unit_index].client_write){
        log_trace("read shutdown");
        return 0;
    }

    // deal with dnsmasq malformed packet
    if(server == DNSMASQ){
        read_count++;
        if(read_count == 4){
            log_trace("send malformed to control server");
            // send message through control socket
            const char control_buf[] = "malformed";
            ssize_t n;
            if((n = original_send(socket_arr[fd].control_sock, control_buf, sizeof(control_buf), MSG_NOSIGNAL)) <= 0){
                log_error("control socket send malformed failed, n=%d", n);
            } 
            read_count = 1;
        }
    }

    // handle nonblocking flag
    if(socket_arr[fd].file_status_flags & O_NONBLOCK){
        //log_trace("non-blocking read");
        if(socket_arr[fd].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            log_trace("read EWOULDBLOCK");
            return -1;
        }
    }
    log_trace("start blocking");
    while(__sync_bool_compare_and_swap(&socket_arr[fd].request_queue->current_size, 0, 0)){
        if(__sync_fetch_and_add(&close_arr[socket_arr[fd].share_unit_index].client_write, 0)){
            if(PROFILING_TIME){
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                log_info("read (client close) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            }
            return 0;
        }
        usleep(0);
    }
    log_trace("start get lock");
    if(pthread_mutex_lock(socket_arr[fd].request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
    ssize_t my_count = 0;
    buffer *b = stream_dequeue(shm_ptr, socket_arr[fd].request_queue, count);
    if(b->buf != NULL){
        memcpy(buf, b->buf, b->length *sizeof(char));
        my_count = b->length;
        free(b->buf);
    }
    free(b);
    if(pthread_mutex_unlock(socket_arr[fd].request_lock) != 0) log_error("pthread_mutex_unlock request_lock failed");
    // my_log_hex(buf, my_count);
    log_trace("read return %ld", my_count);
    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("read (normal) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return my_count;
}

ssize_t write(int fd, const void *buf, size_t count){
    log_trace("hook write(), fd=%d, count=%ld", fd, count);
    if(!is_valid_fd(fd))
        return original_write(fd, buf, count);
    
    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    
    // my_log_hex(buf, count);
    // deal with shutdown
    if(socket_arr[fd].shutdown_write)
        raise(SIGPIPE);

    // handle nonblocking flag
    if(socket_arr[fd].file_status_flags & O_NONBLOCK){
        if(socket_arr[fd].response_queue->current_size == socket_arr[fd].response_queue->capacity){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[fd].response_queue->current_size == socket_arr[fd].response_queue->capacity){
        if(close_arr[socket_arr[fd].share_unit_index].client_read){
            if(PROFILING_TIME){
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                log_info("write (client close) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            }
            log_trace("write raise SIGPIPE");
            raise(SIGPIPE);
        }
        usleep(0);
    }

    if(pthread_mutex_lock(socket_arr[fd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    ssize_t my_count = 0;
    my_count = stream_enqueue(shm_ptr, socket_arr[fd].response_queue, (char *)buf, count);
    if(pthread_mutex_unlock(socket_arr[fd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
    
    // send message through control socket
    log_trace("send after_write to control server");
    const char control_buf[] = "after_write";
    ssize_t n;
    if((n = original_send(socket_arr[fd].control_sock, control_buf, sizeof(control_buf), MSG_NOSIGNAL)) <= 0){
        log_error("control socket send normal failed, n=%d, %s",n , strerror(errno));
    }

    if(server == DNSMASQ)
        read_count = 0;

    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);  
        sub_timespec(hook_start_time, finish, &delta);
        log_info("write relative time: %lld.%.9ld", delta.tv_sec, delta.tv_nsec);
        log_info("write clock time: %lld.%.9ld", finish.tv_sec, finish.tv_nsec);
        sub_timespec(start, finish, &delta);
        log_info("write (normal) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return my_count;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    log_trace("hook setsockopt(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_setsockopt(sockfd, level, optname, optval, optlen);

    
    return 0;
}

int getsockopt(int sockfd, int level, int optname, void *restrict optval, socklen_t *restrict optlen){
    log_trace("hook getsockopt(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd))
        return original_getsockopt(sockfd, level, optname, optval, optlen);
    
    
    return 0;
}

int fcntl(int fd, int cmd, ...){
    log_trace("hook fcntl(), fd=%d", fd);
    va_list ap;
    va_start(ap, cmd);
    int flags;
    if(!is_valid_fd(fd)){
        switch(cmd){
            case F_GETFD:
                va_end(ap);
                return original_fcntl(fd, cmd);
            case F_SETFD:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl(fd, cmd, flags);
            case F_GETFL:
                va_end(ap);
                return original_fcntl(fd, cmd);
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl(fd, cmd, flags);
            default:
                va_end(ap);
                log_fatal("fcntl cmd not implement yet");
                exit(999);
        }
    }
    else{
        switch(cmd){
            case F_GETFL:
                va_end(ap);
                log_trace("cmd=F_GETFL, return %d", socket_arr[fd].file_status_flags);
                return socket_arr[fd].file_status_flags;
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                socket_arr[fd].file_status_flags = flags;
                log_trace("cmd=F_SETFL, return %d", socket_arr[fd].file_status_flags);
                return 0;
            default:
                va_end(ap);
                log_fatal("fcntl cmd not implement yet");
                exit(999);
        }
    }
}

int fcntl64(int fd, int cmd, ...){
    va_list ap;
    va_start(ap, cmd);
    int flags;
    if(!is_valid_fd(fd)){
        log_trace("hook fcntl64(), fd=%d", fd);
        switch(cmd){
            case F_GETFD:
                va_end(ap);
                return original_fcntl64(fd, cmd);
            case F_SETFD:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl64(fd, cmd, flags);
            case F_GETFL:
                va_end(ap);
                return original_fcntl64(fd, cmd);
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl64(fd, cmd, flags);
            default:
                va_end(ap);
                log_fatal("fcntl64 cmd not implement yet");
                exit(999);
        }
    }
    else{
        log_trace("hook fcntl64(), fd=%d", fd);
        switch(cmd){
            case F_GETFL:
                va_end(ap);
                log_trace("cmd=F_GETFL, return %d", socket_arr[fd].file_status_flags);
                return socket_arr[fd].file_status_flags;
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                socket_arr[fd].file_status_flags = flags;
                log_trace("cmd=F_SETFL, return %d", socket_arr[fd].file_status_flags);
                return 0;
            default:
                va_end(ap);
                log_fatal("fcntl64 cmd not implement yet");
                exit(999);
        }
    }
}

int ioctl(int fd, unsigned long request, ...){
    va_list ap;
    va_start(ap, request);
    log_trace("hook ioctl(), fd=%d", fd);
    if(!is_valid_fd(fd)){
        struct ifreq *ifr;
        struct timeval *tv;
        struct arpreq *arp;
        switch(request){
            case SIOCGIFNAME:
            case SIOCGIFFLAGS:
            case SIOCGIFADDR:
            case SIOCGIFMTU:
            case SIOCGIFINDEX:
                ifr = va_arg(ap, struct ifreq*);
                va_end(ap);
                return original_ioctl(fd, request, ifr);
            case SIOCGSTAMP:
                tv = va_arg(ap, struct timeval*);
                va_end(ap);
                return original_ioctl(fd, request, tv);
            case SIOCSARP:
                arp = va_arg(ap, struct arpreq*);
                va_end(ap);
                return original_ioctl(fd, request, arp);
        }
    }
    
    va_end(ap);
    return 0;
}

ssize_t recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len){
    log_trace("hook recvfrom(), fd=%d, length=%ld", socket, length);
    if(!is_valid_fd(socket))
        return original_recvfrom(socket, buffer, length, flags, address, address_len);
    
    if(server == TINYDTLS)
        read_count++;
    
    // handle nonblocking flag
    if(flags & MSG_DONTWAIT || socket_arr[socket].file_status_flags & O_NONBLOCK){
        if(socket_arr[socket].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    
    ssize_t count = 0;
    // while loop for MSG_WAITALL
    while(count != length){
        while(socket_arr[socket].request_queue->current_size == 0)
            usleep(0);

        if(pthread_mutex_lock(socket_arr[socket].request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
        // in other case, MSG_TRUNC return the real length of the packet and discard the oversized part
        if(socket_arr[socket].type == SOCK_DGRAM){
            message_t *m = datagram_dequeue(shm_ptr, socket_arr[socket].request_queue);
            if(m->length != 0){
                memcpy(buffer, m->buf, min(length,m->length));
                count = (flags & MSG_TRUNC) ? m->length : min(length,m->length);
            }
            free(m);
        }
        else{
            log_fatal("not implement socket type in recvfrom!");
            exit(999);
        }
        if(pthread_mutex_unlock(socket_arr[socket].request_lock) != 0) log_error("pthread_mutex_unlock request lock failed");
        // skip if no MSG_WAITALL or socket type is datagram (MSG_WAITALL has no effect)
        if(!(flags & MSG_WAITALL) || (socket_arr[socket].type == SOCK_DGRAM)) break;
    }

    if(server == TINYDTLS){
        if(address != NULL)
            memcpy(address, &tinydtls_peer_addr, sizeof(tinydtls_peer_addr));
        if(address_len != NULL)
            *address_len = sizeof(tinydtls_peer_addr);
    }

    return count;
}

ssize_t sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len){
    log_trace("hook sendto(), fd=%d, length=%ld", socket, length);
    if(!is_valid_fd(socket))
        return original_sendto(socket, message, length, flags, dest_addr, dest_len);
    
    if(server == TINYDTLS)
        read_count = 0;
    
    if(flags & MSG_DONTWAIT || socket_arr[socket].file_status_flags & O_NONBLOCK){
        if(socket_arr[socket].response_queue->current_size == socket_arr[socket].response_queue->capacity){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[socket].response_queue->current_size == socket_arr[socket].response_queue->capacity)
        usleep(0);

    ssize_t count = 0;
    if(pthread_mutex_lock(socket_arr[socket].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    if(socket_arr[socket].type == SOCK_DGRAM){
        message_t m = {
            .length = length,
        };
        if(length > MESSAGE_MAX_LENGTH)
            log_error("need to increase MESSAGE_MAX_LENGTH, len=%d", length);
        memcpy(m.buf, message, min(length,MESSAGE_MAX_LENGTH));
        if(datagram_enqueue(shm_ptr, socket_arr[socket].response_queue, m) == 0)
            count = min(length,MESSAGE_MAX_LENGTH);
    }
    else{
        log_fatal("not implement socket type in sendto!");
        exit(999);
    }
    if(pthread_mutex_unlock(socket_arr[socket].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
    
    // send message through control socket
    log_trace("send after_write to control server");
    const char control_buf[] = "after_write";
    ssize_t n;
    if((n = original_send(socket_arr[socket].control_sock, control_buf, sizeof(control_buf), MSG_NOSIGNAL)) <= 0){
        log_error("control socket send normal failed, n=%d, %s",n , strerror(errno));
    }

    return count;
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags){
    log_trace("hook recvmsg(), fd=%d", socket);
    if(!is_valid_fd(socket))
        return original_recvmsg(socket, message, flags);
    else{
        
        log_fatal("recvmsg not implement this part");
        exit(999);
    }
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags){
    log_trace("hook sendmsg(), fd=%d", socket);
    if(!is_valid_fd(socket))
        return original_sendmsg(socket, message, flags);
    else{
        
        log_fatal("sendmsg not implement this part");
        exit(999);
    }
}

// poll result for hook fd
int hook_fd_poll(struct pollfd *fds, nfds_t nfds){
    int rv = 0;
    int fd;
    for(int i = 0; i < nfds; i++){
        // clear revents
        fds[i].revents = 0;
        fd = fds[i].fd;
        if(socket_arr[fd].share_unit_index >= 0 && (close_arr[socket_arr[fd].share_unit_index].client_write || close_arr[socket_arr[fd].share_unit_index].client_read)){
            fds[i].revents = fds[i].events;
            rv++;
            log_info("hook_fd_poll after client shutdown or close socket");
            return rv;
        }
        // check POLLIN meaning
        if(socket_arr[fd].is_accept_fd){
            if((fds[i].events & POLLIN) && connect_queue_ptr->size > 0){
                fds[i].revents |= POLLIN;
                rv++;
            }
        }
        else{
            if(fds[i].events & POLLIN){
                // check connection direction
                if((socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size > 0) || (!socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size > 0)){
                    fds[i].revents |= POLLIN;
                    rv++;
                }
            }
            else if(fds[i].events & POLLOUT){
                // check connection direction
                if((socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size < socket_arr[fd].response_queue->capacity) || (!socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size < socket_arr[fd].request_queue->capacity)){
                    fds[i].revents |= POLLOUT;
                    rv++; 
                }
            }
        }
    }
    return rv;
}

nfds_t get_poll_hook_nfds(struct pollfd *fds, nfds_t nfds){
    nfds_t hook_nfds = 0;
    for(int i = 0; i < nfds; i++){
        if(is_valid_fd(fds[i].fd))
            hook_nfds++;
    }
    return hook_nfds;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout){
    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook poll(), nfds=%ld, timeout=%d", nfds, timeout);
    int rv = 0, hook_rv = 0;
    // save nfds for hook fd in poll
    nfds_t hook_nfds = get_poll_hook_nfds(fds, nfds);
    log_debug("hook_nfds: %ld", hook_nfds);

    if(!USE_DNSMASQ_POLL){
        log_fatal("not implement yet");
        exit(999);
    }

    if(timeout == 0){
        if(nfds-hook_nfds > 0)
            rv = original_poll(fds, nfds-hook_nfds, timeout);
        // log_debug("poll rv: %d", rv);
        if(rv == -1)
            return rv;

        if(hook_nfds > 0)
            hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);
        // log_debug("hook_rv: %d", hook_rv);
        return rv+hook_rv;
    }


    // timeout > 0 or timeout < 0
    if(nfds-hook_nfds > 0){
        if(timeout > 0){
            if(poll_timer == NULL && my_createtimer(&poll_timer) == -1){
                log_error("poll_timer create failed");
                return -1;
            }

            poll_timeout = false;
            if(my_poll_settimer(timeout) == -1)
                return -1;
        }

        while(1){
            hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);
            if(hook_rv > 0){
                log_debug("hook_rv: %d", hook_rv);
                if(timeout > 0 && my_poll_stoptimer() == -1)
                    return -1;
                
                rv = original_poll(fds, nfds-hook_nfds, 0);

                if(rv == -1){
                    if(PROFILING_TIME){
                        clock_gettime(CLOCK_REALTIME, &finish);
                        sub_timespec(start, finish, &delta);
                        log_info("original poll (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    }
                    return rv;
                }

                log_debug("poll rv: %d", rv);
                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("poll (hook+ n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return hook_rv+rv;               
            }

            rv = original_poll(fds, nfds-hook_nfds, 0);
            if(rv == -1){
                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("original poll (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return rv;
            }
            else if(rv > 0){
                log_debug("poll rv: %d", rv);
                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("poll (hook- n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return hook_rv+rv;
            }            

            if(__sync_bool_compare_and_swap(&poll_timeout, true, true)){
                poll_timeout = 0;

                rv = original_poll(fds, nfds-hook_nfds, 0);
                if(rv == -1){
                    if(PROFILING_TIME){
                        clock_gettime(CLOCK_REALTIME, &finish);
                        sub_timespec(start, finish, &delta);
                        log_info("original poll (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    }
                    return rv;
                }

                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(hook_start_time, finish, &delta);
                    log_info("poll relative time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    log_info("poll clock time: %lld.%.9ld", finish.tv_sec, finish.tv_nsec);
                    sub_timespec(start, finish, &delta);
                    if(rv == 0)
                        log_info("poll (timeout) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);                                
                    else
                        log_info("poll (hook- n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return hook_rv+rv;
            }
            usleep(0);
        }
    }

    // no original poll
    if(timeout == -1){
        // log_debug("timeout is -1");
        while(!hook_rv){
            hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);
            usleep(0);
        }

        if(PROFILING_TIME){
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            log_info("poll (only hook fd infinite loop) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
        }
        return hook_rv;
    }
    
    // no original poll and timeout > 0
    if(poll_timer == NULL && my_createtimer(&poll_timer) == -1){
        log_error("poll_timer create failed");
        return -1;
    }

    poll_timeout = false;
    if(my_poll_settimer(timeout) == -1)
        return -1;

    while(!hook_rv){
        hook_rv = hook_fd_poll(fds, hook_nfds);
        if(__sync_bool_compare_and_swap(&poll_timeout, true, true)){
            poll_timeout = false;
            if(PROFILING_TIME){
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                log_info("only hook fd poll timeout time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            }
            return 0;
        }
        usleep(0);
    }

    if(my_poll_stoptimer() == -1)
        return -1;

    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("only hook fd poll time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return hook_rv;
}

pid_t fork(void){
    log_trace("hook fork!");
    // change cpu affinity
    cpu_set_t c;
    CPU_ZERO(&c);
    CPU_SET(next_cpu_id, &c);
    if (sched_setaffinity(0, sizeof(c), &c))
        log_error("sched_setaffinity failed");
    log_trace("pin process to cpu %d", next_cpu_id);

    //Update next_cpu_id
    next_cpu_id = (next_cpu_id + 1) % cpu_count;
    if(next_cpu_id == aflnet_cpu_id)
        next_cpu_id = (next_cpu_id + 1) % cpu_count;

    return original_fork();
}

int get_select_nfds(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, int *normal_nfds, int **hook_nfds, int *hook_nfds_len, bool *read_hook_fd, bool *write_hook_fd, bool *except_hook_fd){
    // get index upper bound
    int fd_array_index = (nfds - 1) / 64;
    // int fd_long_index = (nfds - 1) % 64;

    int fd, hook_fd_count = 0, hook_bit_count = 0;
    bool hook_fd[1024];
    memset(hook_fd, false, 1024);

    // clear hook fd in all fd_set, get normal_nfds and hook_fd 
    if(readfds){
        for(int i = 0; i <= fd_array_index; i++){
            if(readfds->fds_bits[i] == 0)
                continue;
            
            for(int j = 0; j < 64; j++){
                if(readfds->fds_bits[i] & select_check_num[j]){
                    fd = i * 64 + j;
                    if(is_valid_fd(fd)){
                        if(!hook_fd[fd])
                            hook_fd_count++;
                        hook_bit_count++;
                        hook_fd[fd] = true;
                        read_hook_fd[fd] = true;
                        FD_CLR(fd, readfds);
                    }
                    else
                        *normal_nfds = max(*normal_nfds, fd+1);
                }
            }
        }
    }

    if(writefds){
        for(int i = 0; i <= fd_array_index; i++){
            if(writefds->fds_bits[i] == 0)
                continue;
            
            for(int j = 0; j < 64; j++){
                if(writefds->fds_bits[i] & select_check_num[j]){
                    fd = i * 64 + j;
                    if(is_valid_fd(fd)){
                        if(!hook_fd[fd])
                            hook_fd_count++;
                        hook_bit_count++;
                        hook_fd[fd] = true;
                        write_hook_fd[fd] = true;
                        FD_CLR(fd, writefds);
                    }
                    else
                        *normal_nfds = max(*normal_nfds, fd+1);
                }
            }
        }
    }

    if(exceptfds){
        for(int i = 0; i <= fd_array_index; i++){
            if(exceptfds->fds_bits[i] == 0)
                continue;
            
            for(int j = 0; j < 64; j++){
                if(exceptfds->fds_bits[i] & select_check_num[j]){
                    fd = i * 64 + j;
                    if(is_valid_fd(fd)){
                        if(!hook_fd[fd])
                            hook_fd_count++;
                        hook_bit_count++;
                        hook_fd[fd] = true;
                        except_hook_fd[fd] = true;
                        FD_CLR(fd, exceptfds);
                    }
                    else
                        *normal_nfds = max(*normal_nfds, fd+1);
                }
            }
        }
    }

    //log_trace("hook_fd_count: %d", hook_fd_count);
    // put all hook fd in an array
    if(hook_fd_count > 0 && hook_nfds != NULL){
        *hook_nfds = (int *)malloc(hook_fd_count * sizeof(int));
        if(*hook_nfds == NULL){
            log_error("hook_nfds malloc failed, %s", strerror(errno));
            exit(999);
        }

        int count = 0;
        for(int i = 1023; i >= 0; i--){
            if(hook_fd[i]){
                (*hook_nfds)[count] = i;
                count++;
            }

            if(count > hook_fd_count)
                break;
        }
        *hook_nfds_len = hook_fd_count;
    }

    return hook_bit_count;
}

int hook_fd_select(int *hook_nfds, int hook_nfds_len, int hook_bit_count, 
        bool *read_hook_fd, bool *write_hook_fd, bool *except_hook_fd){
    int fd;
    bool tmp_read_fd[1024], tmp_write_fd[1024], tmp_except_fd[1024];
    memcpy(tmp_read_fd, read_hook_fd, 1024*sizeof(bool));
    memcpy(tmp_write_fd, write_hook_fd, 1024*sizeof(bool));
    memcpy(tmp_except_fd, except_hook_fd, 1024*sizeof(bool));

    for(int i = 0; i < hook_nfds_len; i++){
        fd = hook_nfds[i];
        if(socket_arr[fd].is_accept_fd){
            if(tmp_read_fd[fd] && !(connect_queue_ptr->size > 0)){
                tmp_read_fd[fd] = false;
                hook_bit_count--;
            }
        }
        else{
            if(tmp_read_fd[fd]){
                // check connection direction
                if(!(socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size > 0) && !(!socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size > 0)){
                    tmp_read_fd[fd] = false;
                    hook_bit_count--;
                }
            }
            else if(tmp_write_fd[fd]){
                // check connection direction
                if(!(socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size < socket_arr[fd].response_queue->capacity) && !(!socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size < socket_arr[fd].request_queue->capacity)){
                    tmp_write_fd[fd] = false;
                    hook_bit_count--;
                }
            }
            else if(tmp_except_fd[fd]){
                log_fatal("except_hook_fd not implement yet");
                return -1;
            }
        }
    }

    if(hook_bit_count > 0){
        memcpy(read_hook_fd, tmp_read_fd, 1024*sizeof(bool));
        memcpy(write_hook_fd, tmp_write_fd, 1024*sizeof(bool));
        memcpy(except_hook_fd, tmp_except_fd, 1024*sizeof(bool));
    }

    return hook_bit_count;
}

void update_fd_set(int *hook_nfds, int hook_nfds_len, fd_set *readfds, 
        fd_set *writefds, fd_set *exceptfds, bool *read_hook_fd, bool *write_hook_fd, bool *except_hook_fd){
    int fd;
    for(int i = 0; i < hook_nfds_len; i++){
        fd = hook_nfds[i];
        if(read_hook_fd[fd] && readfds != NULL){
            FD_SET(fd, readfds);
        }
        if(write_hook_fd[fd] && writefds != NULL)
            FD_SET(fd, writefds);
        if(except_hook_fd[fd] && exceptfds != NULL)
            FD_SET(fd, exceptfds);
    }
}

int my_select_settimer(struct timeval *timeout){
    timer_t timer = poll_timer;
    
    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = timeout->tv_sec,
            .tv_nsec = (long)timeout->tv_usec * 1000
        }
    };

    if(timer_settime(timer, 0, &new_value, NULL) == -1){
        log_error("select settimer failed, %s", strerror(errno));
        return -1;
    }

    return 0;
}

int my_select_stoptimer(void){
    timer_t timer = poll_timer;

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        }
    };    
    if(timer_settime(timer, TIMER_ABSTIME, &new_value, NULL) == -1){
        log_error("select stoptimer failed");
        return -1;
    }

    return 0;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, 
        fd_set *exceptfds, struct timeval *timeout){

    struct timespec start, finish, delta;
    if(PROFILING_TIME)
        clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook select(), nfds=%ld, timeout=%ld sec, %ld usec", nfds, timeout->tv_sec, timeout->tv_usec);

    // send message to control
    if(server == TINYDTLS && read_count == 1 && tinydtls_fd >= 0){
        // remove message from request queue to avoid request queue being exhausted
        if(socket_arr[tinydtls_fd].request_queue->current_size > 0){
            message_t *m = datagram_dequeue(shm_ptr, socket_arr[tinydtls_fd].request_queue);
            free(m);
        }
        log_trace("send malformed to control server");
        // send message through control socket
        const char control_buf[] = "malformed";
        ssize_t n = 0;
        while(n < sizeof(control_buf)){
            if((n = original_send(socket_arr[tinydtls_fd].control_sock, control_buf, sizeof(control_buf), MSG_NOSIGNAL)) <= 0){
                if(errno == EINTR)
                    continue;
                log_error("control socket send malformed failed, n=%d, %s", n, strerror(errno));
                break;
            }
        }
        read_count = 0;
    }

    int normal_nfds = -1;
    int *hook_nfds = NULL;
    int hook_nfds_len, hook_bit_count;
    bool read_hook_fd[1024], write_hook_fd[1024], except_hook_fd[1024];
    memset(read_hook_fd, false, 1024);
    memset(write_hook_fd, false, 1024);
    memset(except_hook_fd, false, 1024);

    hook_bit_count = get_select_nfds(nfds, readfds, writefds, exceptfds, 
        &normal_nfds, &hook_nfds, &hook_nfds_len, read_hook_fd, write_hook_fd, except_hook_fd);
    log_trace("hook_bit_count: %d", hook_bit_count);

    int result = 0, hook_result = 0;
    struct timeval zero_timeout = (struct timeval) {
        .tv_sec = 0,
        .tv_usec = 0
    };
    // return immediately
    if(timeout != NULL && timeout->tv_sec == 0 && timeout->tv_usec == 0){
        result = original_select(normal_nfds, readfds, writefds, exceptfds, timeout);
        if(result == -1){
            if(hook_nfds != NULL)
                free(hook_nfds);
            return -1;
        }

        hook_result = hook_fd_select(hook_nfds, hook_nfds_len, hook_bit_count, read_hook_fd, write_hook_fd, except_hook_fd);
        if(hook_result == -1){
            if(hook_nfds != NULL)
                free(hook_nfds);
            return -1;
        }
        
        if(hook_result > 0)
            update_fd_set(hook_nfds, hook_nfds_len, readfds, writefds, exceptfds, read_hook_fd, write_hook_fd, except_hook_fd);

        if(hook_nfds != NULL)
                free(hook_nfds);
        return result+hook_result;
    }

    // has normal fd
    if(normal_nfds >= 0){
        log_trace("normal_nfds >= 0");
        if(timeout > 0){
            if(poll_timer == NULL && my_createtimer(&poll_timer) == -1){
                log_error("poll_timer create failed");
                return -1;
            }

            poll_timeout = false;
            if(my_select_settimer(timeout) == -1)
                return -1;
        }

        while(1){
            hook_result = hook_fd_select(hook_nfds, hook_nfds_len, hook_bit_count, read_hook_fd, write_hook_fd, except_hook_fd);
            if(hook_result > 0){
                log_debug("hook_result: %d", hook_result);
                if(timeout > 0 && my_select_stoptimer() == -1)
                    return -1;
                
                result = original_select(normal_nfds, readfds, writefds, exceptfds, &zero_timeout);

                if(result == -1){
                    if(PROFILING_TIME){
                        clock_gettime(CLOCK_REALTIME, &finish);
                        sub_timespec(start, finish, &delta);
                        log_info("original select (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    }
                    return result;
                }
                log_debug("select result: %d", result);

                update_fd_set(hook_nfds, hook_nfds_len, readfds, writefds, exceptfds, read_hook_fd, write_hook_fd, except_hook_fd);

                if(hook_nfds != NULL)
                    free(hook_nfds);

                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("select (hook+ n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return hook_result+result;               
            }

            result = original_select(normal_nfds, readfds, writefds, exceptfds, &zero_timeout);
            if(result == -1){
                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("original select (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return result;
            }
            else if(result > 0){
                log_debug("select result: %d", result);
                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("select (hook- n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }
                return hook_result+result;
            }            

            if(__sync_bool_compare_and_swap(&poll_timeout, true, true)){
                poll_timeout = 0;

                result = original_select(normal_nfds, readfds, writefds, exceptfds, &zero_timeout);
                if(result == -1){
                    if(PROFILING_TIME){
                        clock_gettime(CLOCK_REALTIME, &finish);
                        sub_timespec(start, finish, &delta);
                        log_info("original select (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    }
                    return result;
                }

                if(PROFILING_TIME){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(hook_start_time, finish, &delta);
                    log_info("select relative time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    log_info("select clock time: %lld.%.9ld", finish.tv_sec, finish.tv_nsec);
                    sub_timespec(start, finish, &delta);
                    if(result == 0)
                        log_info("select (timeout) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);                                
                    else
                        log_info("select (hook- n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                }

                if(hook_nfds != NULL)
                    free(hook_nfds);

                return hook_result+result;
            }
            usleep(0);
        }
    }

    // only hook fd and timeout is NULL
    if(timeout == NULL){
        while(!hook_result){
            hook_result = hook_fd_select(hook_nfds, hook_nfds_len, hook_bit_count, read_hook_fd, write_hook_fd, except_hook_fd);
            usleep(0);
        }

        if(hook_nfds != NULL)
            free(hook_nfds);

        if(PROFILING_TIME){
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            log_info("select (only hook fd infinite loop) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
        }
        return hook_result;
    }
    
    // only hook fd and timeout > 0
    if(poll_timer == NULL && my_createtimer(&poll_timer) == -1){
        log_error("poll_timer create failed");
        return -1;
    }

    poll_timeout = false;
    if(my_select_settimer(timeout) == -1)
        return -1;

    while(!hook_result){
        log_trace("hook_nfds_len: %d, hook_bit_count: %d", hook_nfds_len, hook_bit_count);
        hook_result = hook_fd_select(hook_nfds, hook_nfds_len, hook_bit_count, read_hook_fd, write_hook_fd, except_hook_fd);
        log_trace("hook_result: %d", hook_result);
        if(__sync_bool_compare_and_swap(&poll_timeout, true, true)){
            poll_timeout = false;
            if(PROFILING_TIME){
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                log_info("only hook fd select timeout time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            }

            if(hook_nfds != NULL)
                free(hook_nfds);

            return 0;
        }
        usleep(0);
    }

    if(my_select_stoptimer() == -1)
        return -1;

    if(hook_result > 0)
        update_fd_set(hook_nfds, hook_nfds_len, readfds, writefds, exceptfds, read_hook_fd, write_hook_fd, except_hook_fd);

    if(hook_nfds != NULL)
        free(hook_nfds);

    if(PROFILING_TIME){
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("only hook fd select time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    }
    return hook_result;
}