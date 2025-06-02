#include "csapp.h"
#define STOCK_NUM 10 /* The number of stock IDs in the stock server */
#define max(a, b) ((a > b) ? a : b) /* Macro for comparison */

/* a pool of connected descriptors */
typedef struct {
  int maxfd;                   /* Largest Descriptor in read_set */
  fd_set read_set;             /* Set of all active descriptors */
  fd_set ready_set;            /* Subset of descriptors ready for reading */
  int nready;                  /* Number of descriptors ready from select */
  int maxi;                    /* High water index to client aray */
  int clientfd[FD_SETSIZE];    /* Set of active file descriptors */
  rio_t clientrio[FD_SETSIZE]; /* Set of active read buffers */
} pool;

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

void init_pool(int listenfd,
               pool *p); /* Initializes the pool of active clients */
void add_client(int connfd,
                pool *p);    /* Adds a new client connection to the pool */
void check_clients(pool *p); /* Services client connections */

node *left_rotate(node *x);  /* Rotate the tree to the left */
node *right_rotate(node *y); /* Rotate the tree to the right */
int get_balance(node *n);    /* Check the balance of the tree */
int height(node *n);         /* Get the height of the tree */

node *insert_stock(node *tree, int id, int left_stock,
                   int price);         /* Insert the node into the tree */
void delete_stock(node *tree, int id); /* Delete the node from the tree */
item *query_stock(node *tree, int id); /* Find a specific node from the tree */

static sem_t mutex;      /* semaphore for reading */
node *stock_tree = NULL; /* The stock tree */
item *order[STOCK_NUM] = {
    NULL,
}; /* The array to preserve the stock number */

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; /* Enough space for any address */
  static pool pool;
  char client_hostname[MAXLINE], client_port[MAXLINE], status[MAXLINE];
  char *stateptr;
  int id, stock, price, n;
  FILE *fp;

  // When we execute stockserver, we need another argument named port.
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  // Open a file descriptor(port) and wait for request
  listenfd = Open_listenfd(argv[1]);
  init_pool(listenfd, &pool);

  // open the file with stock data
  fp = Fopen("stock.txt", "r");
  // read stock table
  n = 0;
  while (Fgets(status, MAXLINE, fp) != NULL) {
    id = atoi(strtok_r(status, " ", &stateptr));
    stock = atoi(strtok_r(NULL, " ", &stateptr));
    price = atoi(strtok_r(NULL, " ", &stateptr));
    stock_tree = insert_stock(stock_tree, id, stock, price);
    order[n++] = query_stock(stock_tree, id);
    memset(status, '\0', MAXLINE);
  }
  Fclose(fp);

  while (1) {
    // int Select(int  n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    // struct timeval *timeout)
    pool.ready_set = pool.read_set;
    pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);

    // If listenfd is set in the ready set of the descriptor pool, we are ready
    // to establish a connection via listenfd.
    if (FD_ISSET(listenfd, &pool.ready_set)) {
      // Accept the new client and establish the connection
      clientlen = sizeof(struct sockaddr_storage);
      connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
      add_client(connfd, &pool);
    }

    check_clients(&pool);
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

void init_pool(int listenfd, pool *p) {
  int i;
  Sem_init(&mutex, 0, 1);
  p->maxi = -1;
  for (i = 0; i < FD_SETSIZE; i++) {
    p->clientfd[i] = -1;
  }
  p->maxfd = listenfd;
  FD_ZERO(&p->read_set);
  FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool *p) {
  int i;
  p->nready--;
  for (i = 0; i < FD_SETSIZE; i++) {
    if (p->clientfd[i] < 0) {
      p->clientfd[i] = connfd;
      Rio_readinitb(&p->clientrio[i], connfd);

      FD_SET(connfd, &p->read_set);

      if (connfd > p->maxfd)
        p->maxfd = connfd;
      if (i > p->maxi)
        p->maxi = i;
      break;
    }
  }

  if (i == FD_SETSIZE) {
    app_error("Too many clients");
  }
}

void check_clients(pool *p) {
  int i, connfd, n;
  int id, stock, price;
  char buf[MAXLINE] =
      {
          '\0',
      },
       status[MAXLINE] =
           {
               '\0',
           },
       result[MAXLINE] = {
           '\0',
       };
  char *comp[3] =
      {
          NULL,
      },
       *stateptr;
  rio_t *rio;
  FILE *fp;

  for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
    connfd = p->clientfd[i];
    rio = &p->clientrio[i];
    if ((connfd > 0) && FD_ISSET(connfd, &p->ready_set)) {
      p->nready--;
      if ((n = Rio_readlineb(rio, buf, MAXLINE)) != 0) {

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
            if (order[i]) {
              order[i]->read_cnt++;
              sprintf(status, "%d %d %d\n", order[i]->ID, order[i]->left_stock,
                      order[i]->price);
              strcat(result, status);
              order[i]->read_cnt--;
            }
          }
          Rio_writen(connfd, result, MAXLINE);
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
          sprintf(status, "exit\n");
          Rio_writen(connfd, status, MAXLINE);
          break;
        }
      } else {
        // write stock data to file
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
        // Close the file after writing
        Fclose(fp);
        Rio_readlineb(rio, buf, MAXLINE);
        Fputs(buf, stdout);
        Close(connfd);
        FD_CLR(connfd, &p->read_set);
        p->clientfd[i] = -1;
      }
    }
  }
}

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
    sem_init(&z->mutex, 0, 1);

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
    P(&tree->stock->mutex);
    tree->stock->left_stock = left_stock;
    tree->stock->price = price;
    V(&tree->stock->mutex);
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
    P(&target->stock->mutex);
    node *succ = target->right;
    while (succ->left != NULL)
      succ = succ->left;

    // Copy successor data into target
    target->stock = succ->stock;

    // Remove successor node
    node *to_delete = succ;
    succ = to_delete->right;
    V(&target->stock->mutex);
    sem_destroy(&to_delete->stock->mutex);
    free(to_delete);
  } else {
    P(&target->stock->mutex);
    // One or zero children
    node *child = (target->left != NULL) ? target->left : target->right;

    if (parent == NULL) {
      // Handle case where root node is deleted
      tree->left = child;
    } else {
      current = child;
    }
    V(&target->stock->mutex);

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