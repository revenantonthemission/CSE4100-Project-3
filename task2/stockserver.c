/*
 * echoserveri.c - An iterative echo server
 */
/* $begin echoserverimain */
#include "csapp.h"
#include <pthread.h>
#include <stdio.h>
#define NTHREADS 4
#define SBUFSIZE 16
#define max(a, b) ((a > b) ? a : b)

typedef struct {
  int *buf;    /* Buffer array */
  int n;       /* Maximum number of slots */
  int front;   /* buf[(front+1)%n] is first item */
  int rear;    /* buf[rear%n] is last item */
  sem_t mutex; /* Protects accesses to buf */
  sem_t slots; /* Counts available slots */
  sem_t items; /* Counts available items */
} sbuf_t;

typedef struct {
  int ID;
  int left_stock;
  int price;
  int readcnt;
  sem_t mutex;
} item;

typedef struct node {
  item *stock;
  struct node *left;
  struct node *right;
  int height; // Height of the node for balancing
} node;

static void init_echo_cnt();
void echo_cnt(int connfd);
void *thread(void *vargs);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void inorder_traverse(node *root, char *status);
node *left_rotate(node *x);
node *right_rotate(node *y);
int get_balance(node *n);
int height(node *n);

node *insert_stock(node *tree, int id, int left_stock, int price);
void delete_stock(node *tree, int id);
item *query_stock(node *tree, int id);

sbuf_t sbuf;
static int byte_cnt;
static sem_t mutex;
node *stock_tree = NULL;

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  char client_hostname[MAXLINE], status[MAXLINE], *stateptr;
  int id, stock, price;
  FILE *fp;

  // When we execute stockserver, we need another argument named port.
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  // Open a file descriptor(port) and wait for request
  listenfd = Open_listenfd(argv[1]);
  sbuf_init(&sbuf, SBUFSIZE);

  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  // open the file with stock data
  fp = Fopen("stock.txt", "r");
  // read stock table
  while (Fgets(status, MAXLINE, fp) != NULL) {
    id = atoi(strtok_r(status, " ", &stateptr));
    stock = atoi(strtok_r(NULL, " ", &stateptr));
    price = atoi(strtok_r(NULL, " ", &stateptr));
    stock_tree = insert_stock(stock_tree, id, stock, price);
    memset(status, '\0', MAXLINE);
  }
  Fclose(fp);

  while (1) {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfd);
  }

  // delete the stock tree
  while (stock_tree->left != NULL || stock_tree->right != NULL) {
    if (stock_tree->left != NULL) {
      delete_stock(stock_tree, stock_tree->left->stock->ID);
    } else if (stock_tree->right != NULL) {
      delete_stock(stock_tree, stock_tree->right->stock->ID);
    }
  }

  exit(0);
}
/* $end echoserverimain */

static void init_echo_cnt(void) {
  Sem_init(&mutex, 0, 1);
  byte_cnt = 0;
}

void echo_cnt(int connfd) {
  int n, id, stock, price;
  char buf[MAXLINE],
      status[MAXLINE] =
          {
              "\0",
          },
      *comp[3] =
          {
              "\0",
          },
      *stateptr;
  FILE *fp;
  rio_t rio;
  static pthread_once_t once = PTHREAD_ONCE_INIT;

  Pthread_once(&once, init_echo_cnt);
  Rio_readinitb(&rio, connfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE) != 0)) {
    P(&mutex);
    // do some operation
    byte_cnt += n;

    comp[0] = strtok_r(buf, " \n", &stateptr);
    for (int x = 1; x < 3; x++) {
      comp[x] = strtok_r(NULL, " \n", &stateptr);
    }
    if (!strcmp(comp[0], "show")) {
      // print stock_tree to client
      node *current = stock_tree;
      memset(status, '\0', MAXLINE);
      inorder_traverse(current, status);
      Rio_writen(connfd, status, MAXLINE);
    } else if (!strcmp(comp[0], "buy")) {
      id = atoi(comp[1]);
      stock = atoi(comp[2]);
      item *stock_item = query_stock(stock_tree, id);
      if (stock_item == NULL || stock_item->left_stock < stock) {
        sprintf(status, "Not enough left stocks\n");
        Rio_writen(connfd, status, MAXLINE);
      } else {
        stock_item->left_stock -= stock;
        sprintf(status, "[buy] success\n");
        Rio_writen(connfd, status, MAXLINE);
      }
    } else if (!strcmp(comp[0], "sell")) {
      id = atoi(comp[1]);
      stock = atoi(comp[2]);
      item *stock_item = query_stock(stock_tree, id);
      if (stock_item == NULL) {
        sprintf(status, "[sell] fail\n");
        Rio_writen(connfd, status, MAXLINE);
      } else {
        stock_item->left_stock += stock;
        sprintf(status, "[sell] success\n");
        Rio_writen(connfd, status, MAXLINE);
      }
    } else if (!strcmp(comp[0], "exit")) {
      // 종료 메시지 전송
      sprintf(status, "exit\n");
      Rio_writen(connfd, status, MAXLINE);
      V(&mutex);
      break;
    }
    V(&mutex);
  }
  // write stock data to file
  fp = Fopen("stock.txt", "w");
  memset(status, '\0', MAXLINE);
  inorder_traverse(stock_tree, status);
  Write(fileno(fp), status, strlen(status));
  // Close the file after writing
  Fclose(fp);
}

void *thread(void *args) {
  Pthread_detach(Pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    echo_cnt(connfd);
    Close(connfd);
  }
}

/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = Calloc(n, sizeof(int));
  sp->n = n;                  /* Buffer holds max of n items */
  sp->front = sp->rear = 0;   /* Empty buffer iff front == rear */
  Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
  Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
  Sem_init(&sp->items, 0, 0); /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp) { Free(sp->buf); }
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item) {
  P(&sp->slots);                          /* Wait for available slot */
  P(&sp->mutex);                          /* Lock the buffer */
  sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
  V(&sp->mutex);                          /* Unlock the buffer */
  V(&sp->items);                          /* Announce available item */
}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp) {
  int item;
  P(&sp->items);                           /* Wait for available item */
  P(&sp->mutex);                           /* Lock the buffer */
  item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
  V(&sp->mutex);                           /* Unlock the buffer */
  V(&sp->slots);                           /* Announce available slot */
  return item;
}
/* $end sbuf_remove */
/* $end sbufc */

void inorder_traverse(node *root, char *status) {
  if (root == NULL || root->stock == NULL)
    return;

  inorder_traverse(root->left, status);
  sprintf(status + strlen(status), "%d %d %d\n", root->stock->ID,
          root->stock->left_stock, root->stock->price);
  inorder_traverse(root->right, status);
}

node *left_rotate(node *x) {
  node *y = x->right;
  node *T2 = y->left;

  y->left = x;
  x->right = T2;

  x->height = max(height(x->left), height(x->right)) + 1;
  y->height = max(height(y->left), height(y->right)) + 1;

  return y;
}

node *right_rotate(node *y) {
  node *x = y->left;
  node *T2 = x->right;

  x->right = y;
  y->left = T2;

  y->height = max(height(y->left), height(y->right)) + 1;
  x->height = max(height(x->left), height(x->right)) + 1;

  return x;
}

int get_balance(node *n) {
  return n == NULL ? 0 : height(n->left) - height(n->right);
}

int height(node *n) { return n == NULL ? 0 : n->height; }

node *insert_stock(node *tree, int id, int left_stock, int price) {
  if (tree == NULL) {
    item *z = malloc(sizeof(item));
    z->ID = id;
    z->left_stock = left_stock;
    z->price = price;
    z->readcnt = 0;
    Sem_init(&z->mutex, 0, 1);

    node *new_node = malloc(sizeof(node));
    new_node->stock = z;
    new_node->left = new_node->right = NULL;
    new_node->height = 1;
    return new_node;
  }

  if (id < tree->stock->ID)
    tree->left = insert_stock(tree->left, id, left_stock, price);
  else if (id > tree->stock->ID)
    tree->right = insert_stock(tree->right, id, left_stock, price);
  else {
    tree->stock->left_stock = left_stock;
    tree->stock->price = price;
    return tree;
  }

  tree->height = 1 + max(height(tree->left), height(tree->right));

  int balance = get_balance(tree);

  if (balance > 1 && id < tree->left->stock->ID)
    return right_rotate(tree);

  if (balance < -1 && id > tree->right->stock->ID)
    return left_rotate(tree);

  if (balance > 1 && id > tree->left->stock->ID) {
    tree->left = left_rotate(tree->left);
    return right_rotate(tree);
  }

  if (balance < -1 && id < tree->right->stock->ID) {
    tree->right = right_rotate(tree->right);
    return left_rotate(tree);
  }

  return tree;
}
void delete_stock(node *tree, int id) {
  node *current = tree->left;
  node *parent = NULL;

  // Find node to delete
  while (current != NULL && current->stock->ID != id) {
    parent = current;
    if (id < current->stock->ID)
      current = current->left;
    else
      current = current->right;
  }

  if (current == NULL)
    return; // Not found

  node *target = current;

  // Case: two children
  if (target->left != NULL && target->right != NULL) {
    node *succ = target->right;
    while (succ->left != NULL)
      succ = succ->left;

    // Copy successor data into target
    target->stock = succ->stock;

    // Remove successor node
    node *to_delete = succ;
    succ = to_delete->right;
    sem_destroy(&to_delete->stock->mutex);
    free(to_delete);
  } else {
    // One or zero children
    node *child = (target->left != NULL) ? target->left : target->right;

    if (parent == NULL) {
      // Handle case where root node is deleted
      tree->left = child;
    } else {
      current = child;
    }

    sem_destroy(&target->stock->mutex);
    free(target);
  }
}
item *query_stock(node *tree, int id) {
  if (tree == NULL)
    return NULL;

  node *current = tree;
  while (current != NULL) {
    if (id == current->stock->ID)
      return current->stock;
    else if (id < current->stock->ID)
      current = current->left;
    else
      current = current->right;
  }
  return NULL;
}