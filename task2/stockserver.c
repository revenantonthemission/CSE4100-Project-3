#include "csapp.h"
#define NTHREADS 4 /* The number of threads in the worker thread pool */
#define SBUFSIZE 16 /* The size of buffer shared by the master thread & worker threads */
#define STOCK_NUM 10 /* The number of stock IDs in the stock server */
#define max(a, b) ((a > b) ? a : b) /* Macro for comparison */

typedef struct {
  int *buf;    /* Buffer array */
  int n;       /* Maximum number of slots */
  int front;   /* buf[(front+1)%n] is the first item */
  int rear;    /* buf[rear%n] is the last item */
  sem_t mutex; /* Protects accesses to buf */
  sem_t slots; /* Counts available slots */
  sem_t items; /* Counts available items */
} sbuf_t;

typedef struct {
  int ID;         /* Stock ID */
  int left_stock; /* The number of stocks left in the market */
  int price;      /* The price of this stock */
  int read_cnt;   /* The number of clients reading this item */
  sem_t mutex;    /* Semaphore for safe writing */
} item;

typedef struct node {
  item *stock;        /* The stock */
  struct node *left;  /* The left subtree of this node */
  struct node *right; /* The right subtree of this node */
  int height;         /* Height of the subtree */
} node;

static void init_check_order();  /* initialize mutex */
void check_order(int connfd); /* client */
void *thread(void *vargs);    /* thread function */

void sbuf_init(sbuf_t *sp, int n);      /* Initialize shared buffer */
void sbuf_deinit(sbuf_t *sp);           /* Deinitialize shared buffer */
void sbuf_insert(sbuf_t *sp, int item); /* Insert item into shared buffer */
int sbuf_remove(sbuf_t *sp);            /* remove item from shared buffer */

node *left_rotate(node *x);  /* Rotate the tree to the left */
node *right_rotate(node *y); /* Rotate the tree to the right */
int get_balance(node *n);    /* Check the balance of the tree */
int height(node *n);         /* Get the height of the tree */

node *insert_stock(node *tree, int id, int left_stock,
                   int price);         /* Insert the node into the tree */
void delete_stock(node *tree, int id); /* Delete the node from the tree */
item *query_stock(node *tree, int id); /* Find a specific node from the tree */

sbuf_t sbuf;             /* shared buffer */
static sem_t mutex;      /* semaphore for reading */
node *stock_tree = NULL; /* The stock tree */
item *order[STOCK_NUM] = {
    NULL,
}; /* The array to preserve the stock number */

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  char status[MAXLINE], *stateptr;
  int id, stock, price, n;
  FILE *fp;

  /* When we execute stockserver, we need another argument named port. */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  /* Open a file descriptor(port) and wait for request */
  listenfd = Open_listenfd(argv[1]);

  /* initialize the shared buffer */
  sbuf_init(&sbuf, SBUFSIZE);

  /* Create worker threads */
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  /* open the file with stock data */
  fp = Fopen("stock.txt", "r");
  /* read stock table from the file and make the stock tree*/
  n = 0;
  while (Fgets(status, MAXLINE, fp) != NULL) {
    id = atoi(strtok_r(status, " ", &stateptr));
    stock = atoi(strtok_r(NULL, " ", &stateptr));
    price = atoi(strtok_r(NULL, " ", &stateptr));
    stock_tree = insert_stock(stock_tree, id, stock, price);
    order[n++] = query_stock(stock_tree, id);
    memset(status, '\0', MAXLINE);
  }
  /* close the file */
  Fclose(fp);

  /* Manage connection */
  while (1) {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfd);
  }

  /* delete the stock tree */
  while (stock_tree->left != NULL || stock_tree->right != NULL) {
    if (stock_tree->left != NULL) {
      delete_stock(stock_tree, stock_tree->left->stock->ID);
    } else if (stock_tree->right != NULL) {
      delete_stock(stock_tree, stock_tree->right->stock->ID);
    }
  }

  exit(0);
}

/* initialize mutex */
static void init_check_order(void) { Sem_init(&mutex, 0, 1); }

/* client */
void check_order(int connfd) {
  int n, id, stock;
  char buf[MAXLINE],
      status[MAXLINE] =
          {
              "\0",
          },
      result[MAXLINE] =
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

  /* Execute init_echo_cnt once  */
  Pthread_once(&once, init_check_order);

  /* Initialize robust I/O*/
  Rio_readinitb(&rio, connfd);

  /* Continuously read a line from the client */
  while ((n = Rio_readlineb(&rio, buf, MAXLINE) != 0)) {

    printf("server received %d bytes\n", n);

    /* Parse the line from the client */
    comp[0] = strtok_r(buf, " \n", &stateptr);
    for (int x = 1; x < 3; x++) {
      comp[x] = strtok_r(NULL, " \n", &stateptr);
    }

    /* Do the appropriate action based on the parsed line */
    if (!strcmp(comp[0], "show")) {
      /* show the stock data */
      memset(result, '\0', MAXLINE);
      for (int i = 0; i < STOCK_NUM; i++) {
        if (order[i]) {                /* if the stock exists */
          P(&mutex);                   /* get the lock for reading */
          order[i]->read_cnt++;        /* increase the read count */
          if (order[i]->read_cnt == 1) /* after it is properly increased */
            P(&order[i]->mutex);       /* get the lock for item */
          V(&mutex);                   /* free the lock for reading*/
          sprintf(status, "%d %d %d\n", order[i]->ID, order[i]->left_stock,
                  order[i]->price);
          strcat(result, status);
          P(&mutex);                   /* get the lock for reading */
          order[i]->read_cnt--;        /* decrease the read count */
          if (order[i]->read_cnt == 0) /* after it is properly decreased */
            V(&order[i]->mutex);       /* free the lock for item */
          V(&mutex);                   /* free the lock for reading */
        }
      }
      P(&mutex); /* get the lock */
      Rio_writen(connfd, result, MAXLINE);
      V(&mutex); /* free the lock */
    } else if (!strcmp(comp[0], "buy")) {
      id = atoi(comp[1]);
      stock = atoi(comp[2]);
      item *stock_item = query_stock(stock_tree, id);
      P(&stock_item->mutex);
      if (stock_item == NULL || stock_item->left_stock < stock) {
        sprintf(status, "Not enough left stocks\n");
        Rio_writen(connfd, status, MAXLINE);
      } else {
        stock_item->left_stock -= stock;
        sprintf(status, "[buy] success\n");
        Rio_writen(connfd, status, MAXLINE);
      }
      V(&stock_item->mutex);
    } else if (!strcmp(comp[0], "sell")) {
      id = atoi(comp[1]);
      stock = atoi(comp[2]);
      item *stock_item = query_stock(stock_tree, id);
      P(&stock_item->mutex);
      if (stock_item == NULL) {
        sprintf(status, "[sell] fail\n");
        Rio_writen(connfd, status, MAXLINE);
      } else {
        stock_item->left_stock += stock;
        sprintf(status, "[sell] success\n");
        Rio_writen(connfd, status, MAXLINE);
      }
      V(&stock_item->mutex);
    } else if (!strcmp(comp[0], "exit")) {
      P(&mutex);
      // send message to the client
      sprintf(status, "exit\n");
      Rio_writen(connfd, status, MAXLINE);
      V(&mutex);
      break;
    }
  }

  /* Save the stock tree to the file */
  P(&mutex);
  fp = Fopen("stock.txt", "w");
  memset(result, '\0', MAXLINE);
  for (int i = 0; i < STOCK_NUM; i++) {
    if (order[i]) {
      sprintf(status, "%d %d %d\n", order[i]->ID, order[i]->left_stock,
              order[i]->price);
      strcat(result, status);
    }
  }
  Write(fileno(fp), result, strlen(result));
  Fclose(fp);
  V(&mutex);
}

/* thread function */
void *thread(void *args) {
  Pthread_detach(Pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    check_order(connfd);
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

/* Rotate the tree to the left */
node *left_rotate(node *x) {
  node *y = x->right;
  node *T2 = y->left;

  y->left = x;
  x->right = T2;

  x->height = max(height(x->left), height(x->right)) + 1;
  y->height = max(height(y->left), height(y->right)) + 1;

  return y;
}

/* Rotate the tree to the right */
node *right_rotate(node *y) {
  node *x = y->left;
  node *T2 = x->right;

  x->right = y;
  y->left = T2;

  y->height = max(height(y->left), height(y->right)) + 1;
  x->height = max(height(x->left), height(x->right)) + 1;

  return x;
}

/* Check the balance of the tree */
int get_balance(node *n) {
  return n == NULL ? 0 : height(n->left) - height(n->right);
}

/* Get the height of the tree */
int height(node *n) { return n == NULL ? 0 : n->height; }

/* Insert the node into the tree */
node *insert_stock(node *tree, int id, int left_stock, int price) {
  if (tree == NULL) {
    item *z = malloc(sizeof(item));
    z->ID = id;
    z->left_stock = left_stock;
    z->price = price;
    z->read_cnt = 0;
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

/* Delete the node from the tree */
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

/* Find a specific node from the tree */
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