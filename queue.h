#define QUEUE_CAPACITY 15000
#define min(a,b)(a<b?a:b)

typedef struct queue queue;
typedef struct buffer buffer;

struct queue {
    int front, rear, capacity, current_size;
    // data start offset
    int message_start_offset;
};

struct buffer {
    char *buf;
    int length;
};

queue *request_queue;
queue *response_queue;

int enqueue(void *shm, queue *q, char *c, int len);
buffer *dequeue(void *shm, queue *q, int len);