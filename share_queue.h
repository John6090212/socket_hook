#define QUEUE_CAPACITY 15000
#define min(a,b)(a<b?a:b)

typedef struct share_queue share_queue;
typedef struct buffer buffer;

struct share_queue {
    int front, rear, capacity, current_size;
    // data start offset
    int message_start_offset;
};

struct buffer {
    char *buf;
    int length;
};

share_queue *request_queue;
share_queue *response_queue;

int share_enqueue(void *shm, share_queue *q, char *c, int len);
buffer *share_dequeue(void *shm, share_queue *q, int len);