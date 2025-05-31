/*
 * echoserveri.c - An iterative echo server
 */
/* $begin echoserverimain */
#include "csapp.h"
#define STOCK_NUM 10
#define max(a, b) ((a > b) ? a : b)

typedef struct {
  int maxfd;
  fd_set read_set;
  fd_set ready_set;
  int nready;
  int maxi;
  int clientfd[FD_SETSIZE];
  rio_t clientrio[FD_SETSIZE];
} pool;

typedef struct {
  int ID;
  int left_stock;
  int price;
  int read_cnt;
  sem_t mutex;
} item;

typedef struct node {
  item *stock;
  struct node *left;
  struct node *right;
  int height; // Height of the node for balancing
} node;

void echo(int connfd);
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
void inorder_traverse(node *root, char *status);

node *left_rotate(node *x);
node *right_rotate(node *y);
int get_balance(node *n);
int height(node *n);

node *insert_stock(node *tree, int id, int left_stock, int price);
void delete_stock(node *tree, int id);
item *query_stock(node *tree, int id);

static sem_t mutex;
node *stock_tree = NULL;
item *order[STOCK_NUM] = {
    NULL,
};

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  /* Enough space for any address */ // line:netp:echoserveri:sockaddrstorage
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
      // some output command
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
/* $end echoserverimain */

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
        comp[0] = strtok_r(buf, " \n", &stateptr);
        for (int x = 1; x < 3; x++) {
          comp[x] = strtok_r(NULL, " \n", &stateptr);
        }
        if (!strcmp(comp[0], "show")) {
          // print stock_tree to client
          memset(result, '\0', MAXLINE);
          for (int i = 0; i < STOCK_NUM; i++) {
            if (order[i]) {
              P(&mutex);
              order[i]->read_cnt++;
              if (order[i]->read_cnt == 1)
                P(&order[i]->mutex);
              V(&mutex);
              sprintf(status, "%d %d %d\n", order[i]->ID, order[i]->left_stock,
                      order[i]->price);
              strcat(result, status);
              P(&mutex);
              order[i]->read_cnt--;
              if (order[i]->read_cnt == 0)
                V(&order[i]->mutex);
              V(&mutex);
            }
          }
          P(&mutex);
          Rio_writen(connfd, result, MAXLINE);
          V(&mutex);
        } else if (!strcmp(comp[0], "buy")) {
          P(&mutex);
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
          V(&mutex);
        } else if (!strcmp(comp[0], "sell")) {
          P(&mutex);
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
          V(&mutex);
        } else if (!strcmp(comp[0], "exit")) {
          P(&mutex);
          // 종료 메시지 전송
          sprintf(status, "exit\n");
          Rio_writen(connfd, status, MAXLINE);
          V(&mutex);
          break;
        }
      } else {
        P(&mutex);
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
        V(&mutex);
        Rio_readlineb(rio, buf, strlen(buf));
        Close(connfd);
        FD_CLR(connfd, &p->read_set);
        p->clientfd[i] = -1;
      }
    }
  }
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