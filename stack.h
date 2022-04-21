#ifndef _STACK_H_
#define _STACK_H_

// C program for array implementation of stack
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Stack Stack;

// A structure to represent a stack
struct Stack {
    int top;
    unsigned capacity;
    int* array;
};

struct Stack* createStack(unsigned capacity);
int isFullStack(struct Stack* stack);
int isEmptyStack(struct Stack* stack);
void push(struct Stack* stack, int item);
int pop(struct Stack* stack);
void destroyStack(struct Stack* stack);

#endif