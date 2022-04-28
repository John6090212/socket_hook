#ifndef _LIST_H_
#define _LIST_H_
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

struct node {
   int data;
   struct node *next;
};

void printList(struct node *head);
void insertFirst(struct node *head, int data);
struct node* deleteFirst(struct node *head);
bool isListEmpty(struct node *head);
int Listlen(struct node *head);
struct node* findInList(struct node *head, int data);
struct node* deleteInList(struct node *head, int data);
void sortList(struct node *head);
void reverseList(struct node** head_ref);

#endif