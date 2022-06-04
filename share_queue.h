#ifndef _SHARE_QUEUE_H_
#define _SHARE_QUEUE_H_

#define min(a,b) (((a) < (b)) ? (a) : (b))
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define MESSAGE_MAX_LENGTH 1500

typedef struct share_queue share_queue;
typedef struct buffer buffer;
typedef struct message_t message_t;

struct share_queue {
    int front, rear, capacity, current_size;
    // data start offset
    int message_start_offset;
};

struct buffer {
    char *buf;
    int length;
};

// for datagram
struct message_t {
    int length;
    char buf[MESSAGE_MAX_LENGTH];
};

int stream_enqueue(void *shm, share_queue *q, char *c, int len);
buffer *stream_dequeue(void *shm, share_queue *q, int len);

// for datagram
int datagram_enqueue(void *shm, share_queue *q, message_t m);
message_t *datagram_dequeue(void *shm, share_queue *q);

int int_enqueue(void *shm, share_queue *q, int m);
int int_dequeue(void *shm, share_queue *q);

#endif