// synchronize
#include <semaphore.h>
#include <pthread.h>

#define SEM_REQUEST_FULL "request_full"
#define SEM_REQUEST_EMPTY "request_empty"
#define SEM_RESPONSE_FULL "response_full"
#define SEM_RESPONSE_EMPTY "response_empty"

int shm_fd;
void *shm_ptr;

sem_t *sem_request_full;
sem_t *sem_request_empty;
pthread_mutex_t *request_lock;

sem_t *sem_response_full;
sem_t *sem_response_empty;
pthread_mutex_t *response_lock;