// https://www.tutorialspoint.com/data_structures_algorithms/linked_list_program_in_c.htm
#include "list.h"

// struct node *head = NULL;
// struct node *current = NULL;

//display the list
void printList(struct node *head) {
    struct node *ptr = head;
    printf("\n[ ");
        
    //start from the beginning
    while(ptr != NULL) {
        printf("(%d) ",ptr->data);
        ptr = ptr->next;
    }
	
    printf(" ]");
}

//insert link at the first location
void insertFirst(struct node *head, int data) {
    //create a link
    struct node *link = (struct node*) malloc(sizeof(struct node));

    link->data = data;
        
    //point it to old first node
    link->next = head;
        
    //point first to new first node
    head = link;
}

//delete first item
struct node* deleteFirst(struct node *head) {
    //save reference to first link
    struct node *tempLink = head;
        
    //mark next to first link as first 
    head = head->next;
        
    //return the deleted link
    return tempLink;
}

//is list empty
bool isListEmpty(struct node *head) {
    return head == NULL;
}

int Listlen(struct node *head) {
    int length = 0;
    struct node *current;
        
    for(current = head; current != NULL; current = current->next) {
        length++;
    }
        
    return length;
}

//find a link with given key
struct node* findInList(struct node *head, int data) {

    //start from the first link
    struct node* current = head;

    //if list is empty
    if(head == NULL) {
        return NULL;
    }

    //navigate through list
    while(current->data != data) {
        
        //if it is last node
        if(current->next == NULL) {
            return NULL;
        } else {
            //go to next link
            current = current->next;
        }
    }     
        
    //if data found, return the current Link
    return current;
}

//delete a link with given key
struct node* deleteInList(struct node *head, int data) {

    //start from the first link
    struct node* current = head;
    struct node* previous = NULL;
        
    //if list is empty
    if(head == NULL) {
        return NULL;
    }

    //navigate through list
    while(current->data != data) {

        //if it is last node
        if(current->next == NULL) {
            return NULL;
        } else {
            //store reference to current link
            previous = current;
            //move to next link
            current = current->next;
        }
    }

    //found a match, update the link
    if(current == head) {
        //change first to point to next link
        head = head->next;
    } else {
        //bypass the current link
        previous->next = current->next;
    }    
        
    return current;
}

void sortList(struct node *head) {

    int i, j, k, tempData;
    struct node *current;
    struct node *next;
        
    int size = Listlen(head);
    k = size ;
        
    for ( i = 0 ; i < size - 1 ; i++, k-- ) {
        current = head;
        next = head->next;
            
        for ( j = 1 ; j < k ; j++ ) {   

            if ( current->data > next->data ) {
            tempData = current->data;
            current->data = next->data;
            next->data = tempData;
            }
            
            current = current->next;
            next = next->next;
        }
    }   
}

void reverseList(struct node** head_ref) {
   struct node* prev   = NULL;
   struct node* current = *head_ref;
   struct node* next;
	
   while (current != NULL) {
      next  = current->next;
      current->next = prev;   
      prev = current;
      current = next;
   }
	
   *head_ref = prev;
}