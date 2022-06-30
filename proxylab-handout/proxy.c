#include <stdio.h>

#include "cache.h"
#include "csapp.h"

#define CONCURRENCY 8

#define HEADER_HOST "Host:"
#define HEADER_USER_AGENT "User-Agent:"
#define HEADER_CONNECTION "Connection:"
#define HEADER_PROXY_CONNECTION "Proxy-Connection:"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct {
    char hostname[MAXLINE];
    int port;
    char abs_path[MAXLINE];
} http_uri;

typedef struct {
    int *buf;    /* Buffer array */
    int n;       /* Maximum number of slots */
    int front;   /* buf[(front+1)%n] is first item */
    int rear;    /* buf[rear%n] is last item */
    sem_t mutex; /* Protects accesses to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;

void *thread(void *vargp);
void doit(int fd);
int read_requesthdrs(rio_t *rio, char *header, http_uri *uri);
int parse_uri(char *path, http_uri *uri);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

static cache c;

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    sbuf_t sbuf;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    sbuf_init(&sbuf, MAXBUF);
    cache_init(&c);
    listenfd = Open_listenfd(argv[1]);

    for (int i = 0; i < CONCURRENCY; i++) Pthread_create(&tid, NULL, thread, &sbuf);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    sbuf_t *sbuf = vargp;
    while (1) {
        int connfd = sbuf_remove(sbuf); /* Remove connfd from buf */
        doit(connfd);                   /* Service client */
        Close(connfd);
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) {
    int proxy_fd;
    ssize_t n;
    int size;
    char buf[MAXLINE], method[MAXLINE], path[MAXLINE], version[MAXLINE], header[MAXLINE], obj_buf[MAX_OBJECT_SIZE];
    char *obj;
    rio_t rio, rio_proxy;

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))  // line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, path, version);  // line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                 // line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }  // line:netp:doit:endrequesterr

    /* Parse URI from GET request */
    http_uri uri;
    if (parse_uri(path, &uri) < 0) {
        clienterror(fd, "uri should begin with http://", "400", "Bad Request", "Proxy failed to parse the scheme");
        return;
    }

    if (read_requesthdrs(&rio, header, &uri) < 0) {
        clienterror(fd, "read fd error", "400", "Bad Request", "Proxy failed to parse the HTTP header");
        return;
    }

    sprintf(path, "http://%s:%d%s", uri.hostname, uri.port, uri.abs_path);

    /* Read cache */
    if ((size = cache_get(&c, path, &obj)) > 0) {
        if (rio_writen(fd, obj, size) < 0) {
            clienterror(fd, "write error", "500", "Internal Server Error", "Proxy failed to forward the request");
        }
        cache_read_done(&c);
    } else {
        sprintf(buf, "%d", uri.port);
        proxy_fd = open_clientfd(uri.hostname, buf);
        if (proxy_fd < 0) {
            clienterror(fd, strerror(errno), "500", "Internal Server Error", "Proxy failed to connect the host");
            return;
        }

        rio_readinitb(&rio_proxy, proxy_fd);
        if (rio_writen(proxy_fd, header, strlen(header)) < 0) {
            clienterror(fd, "write error", "500", "Internal Server Error", "Proxy failed to forward the request");
            return;
        }
        size = 0;
        while ((n = rio_readlineb(&rio_proxy, buf, MAXLINE))) {
            if (rio_writen(fd, buf, n) < 0) {
                clienterror(fd, "write error", "500", "Internal Server Error", "Proxy failed to forward the request");
                return;
            }
            if (size + n < MAX_OBJECT_SIZE) {
                memcpy(obj_buf + size, buf, n);
            }
            size += n;
        }
        if (size < MAX_OBJECT_SIZE) {
            cache_add(&c, path, strlen(path), obj_buf, size);
        }
        close(proxy_fd);
    }
}
/* $end doit */

int read_requesthdrs(rio_t *rio, char *header, http_uri *uri) {
    ssize_t size;
    uint8_t host_exist = 0;
    char buf[MAXLINE];

    sprintf(header, "GET %s HTTP/1.0\r\n", uri->abs_path);

    while ((size = rio_readlineb(rio, buf, MAXLINE)) > 0) {
        if (!strcmp(buf, "\r\n")) break;

        if (!strncasecmp(buf, HEADER_HOST, strlen(HEADER_HOST))) {
            host_exist = 1;
        }
        if (!strncasecmp(buf, HEADER_USER_AGENT, strlen(HEADER_USER_AGENT)) ||
            !strncasecmp(buf, HEADER_CONNECTION, strlen(HEADER_CONNECTION)) ||
            !strncasecmp(buf, HEADER_PROXY_CONNECTION, strlen(HEADER_PROXY_CONNECTION)))
            continue;
        strcat(header, buf);
    }

    if (size < 0) return -1;
    if (!host_exist) sprintf(header + strlen(header), "Host: %s\r\n", uri->hostname);

    strcat(header, user_agent_hdr);
    strcat(header, "Connection: close\r\n");
    strcat(header, "Proxy-Connection: close\r\n");
    strcat(header, "\r\n");
    return 0;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *path, http_uri *uri) {
    if (strncasecmp(path, "http://", 7)) {
        return -1;
    }
    char *begin = path + strlen("http://");
    char *colon = strstr(begin, ":");
    if (colon) {
        *colon = '\0';
        strcpy(uri->hostname, begin);
        sscanf(colon + 1, "%d%s", &uri->port, uri->abs_path);
        *colon = ':';
    } else {
        char *slash = strstr(begin, "/");
        if (slash) {
            *slash = '\0';
            strcpy(uri->hostname, begin);
            *slash = '/';
            strcpy(uri->abs_path, slash);
        } else {
            strcpy(uri->hostname, begin);
            strcpy(uri->abs_path, "/");
        }
        uri->port = 80;
    }

    return 0;
}
/* $end parse_uri */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf,
            "<body bgcolor="
            "ffffff"
            ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */

/* Create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;                  /* Buffer holds max of n items */
    sp->front = sp->rear = 0;   /* Empty buffer iff front == rear */
    Sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    Sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    Sem_init(&sp->items, 0, 0); /* Initially, buf has 0 items */
}

/* Clean up buffer sp */
void sbuf_deinit(sbuf_t *sp) { Free(sp->buf); }

/* Insert item onto the rear of shared buffer sp */
void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);                          /* Wait for available slot */
    P(&sp->mutex);                          /* Lock the buffer */
    sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
    V(&sp->mutex);                          /* Unlock the buffer */
    V(&sp->items);                          /* Announce available item */
}

/* Remove and return the first item from buffer sp */
int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);                           /* Wait for available item */
    P(&sp->mutex);                           /* Lock the buffer */
    item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
    V(&sp->mutex);                           /* Unlock the buffer */
    V(&sp->slots);                           /* Announce available slot */
    return item;
}