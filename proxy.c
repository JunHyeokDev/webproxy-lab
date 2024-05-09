#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 웹 페이지 구조체
typedef struct webCache
{
    char *uri;
    char *content;
    int content_size;
    struct webCache *prev;
    struct webCache *next;
} webCache;

// 연결 리스트 구조체
typedef struct
{
    webCache *head;
    webCache *tail;
    sem_t writer_mutex; // writer favor 동기화를 위한 세마포어
    int size;
} LinkedList;

static LinkedList *ll;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

// 프로토타입 선언
void do_Proxy(int fd);
int parse_uri(char *uri, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void divide_hostname_and_path(const char *url, char *hostname, char *path, char *port);
void *thread(void *vargp);

void LinkedList_init(LinkedList *list);
webCache *find_cache(char* uri);
void delete_cache(webCache *cache);
webCache *create_cache(char* uri, char *content, int size);
void add_cache(char* uri, char* content, int size);

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;

    ll = (LinkedList*)malloc(sizeof(LinkedList));
    LinkedList_init(ll);

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // argv[0] 은 프로그램 이름인 tiny
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr,&clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

void do_Proxy(int fd)
{
    int is_static;
    int serverfd, content_size;
    char request_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],response_buf[MAX_OBJECT_SIZE];
    char hostname[MAXLINE], path[MAXLINE];
    char filename[MAXLINE];
    char port[MAXLINE];

    rio_t request_rio, response_rio;

    /* Read request Line and headers */
    Rio_readinitb(&request_rio, fd);           
    Rio_readlineb(&request_rio, request_buf, MAXLINE); 
    printf("Request headers:\n");
    printf("%s\n", request_buf);

    sscanf(request_buf, "%s %s %s", method, uri, version);

    divide_hostname_and_path(uri, hostname, path, port);
    if (!strcasecmp(version, "HTTP/1.1"))
    {
        strcpy(version, "HTTP/1.0");
    }

    if (strcasecmp(method, "GET"))
    { // get이면 0을 반환.
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    webCache *cache = find_cache(uri);
    if (cache)
    {
        Rio_writen(fd, cache->content, cache->content_size);
        return;
    }

    // 1️⃣ Request from Proxy to web Server!
    sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
    sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr);
    sprintf(request_buf, "%sConnection: close\r\n", request_buf);
    sprintf(request_buf, "%sProxy-Connection: close\r\n\r\n", request_buf);

    serverfd = Open_clientfd(hostname, port);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
    // 1️⃣ Request from Proxy to web Server!


    // 2️⃣ Response from web to proxy! 
    Rio_readinitb(&response_rio, serverfd);
    content_size = Rio_readnb(&response_rio, response_buf, MAX_OBJECT_SIZE);
    // 2️⃣ Response from web to proxy!

    // 3️⃣ Response from proxy to client!
    Rio_writen(fd, response_buf, content_size);
    if(content_size < MAX_OBJECT_SIZE) add_cache(uri,response_buf,content_size);
    // 3️⃣ Response from proxy to client!

    Close(serverfd);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP reponse body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The error Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void divide_hostname_and_path(const char *url, char *hostname, char *path, char *port)
{
    // "http://"가 있으면 이를 제거
    const char *ptr = strstr(url, "://");
    if (ptr != NULL)
    {
        url = ptr + 3;
    }

    // hostname 추출
    ptr = strchr(url, ':');
    if (ptr != NULL)
    {
        strncpy(hostname, url, ptr - url);
        hostname[ptr - url] = '\0';
        url = ptr + 1;
    }
    else
    {
        ptr = strchr(url, '/');
        if (ptr != NULL)
        {
            strncpy(hostname, url, ptr - url);
            hostname[ptr - url] = '\0';
            url = ptr;
        }
        else
        {
            strcpy(hostname, url);
            url += strlen(url);
        }
    }

    // port 추출
    ptr = strchr(url, '/');
    if (ptr != NULL)
    {
        strncpy(port, url, ptr - url);
        port[ptr - url] = '\0';
        url = ptr;
    }
    else
    {
        strcpy(port, url);
        url += strlen(url);
    }

    // path 추출
    strcpy(path, url);

    // 포트가 없는 경우 디폴트로 5004 사용
    if (port[0] == '\0')
    {
        strcpy(port, "5004");
    }
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    do_Proxy(connfd);
    Close(connfd);
    return NULL;
}

void LinkedList_init(LinkedList *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    sem_init(&list->writer_mutex, 0, 1); // 세마포어 초기화
}

void add_cache(char* uri, char* content, int size) {
    P(&ll->writer_mutex); // 잠금 획득

    while (ll->size + size > MAX_CACHE_SIZE) {
        delete_cache(ll->tail);
    }
    webCache *new_cache = create_cache(uri,content,size);
    if (ll->head == NULL) {
        ll->head = new_cache;
    } else {
        new_cache->next = ll->head;
        ll->head->prev = new_cache;
        ll->head = new_cache;
    }
    ll->size += size;
    V(&ll->writer_mutex); // 잠금 해제
}

webCache *create_cache(char* uri, char *content, int size) {
    webCache *tmp = (webCache*) malloc(sizeof(webCache));

    tmp->uri = malloc(strlen(uri));
    strcpy(tmp->uri,uri);

    tmp->content = malloc(size);
    memcpy(tmp->content, content, size);
    
    tmp->content_size = size;
    tmp->prev = NULL;
    tmp->next = NULL;
    return tmp;
}

void delete_cache(webCache *cache) {
    P(&ll->writer_mutex); // 잠금 획득
    if (cache->prev != NULL) {
        cache->prev->next = cache->next;
    }
    else {
        ll->head = cache;
    }

    if (cache->next != NULL) {
        cache->next->prev = cache->prev;
    } else {
        ll->tail = cache;
    }

    ll->size -= cache->content_size;
    free(cache->content);
    free(cache->uri);
    free(cache);
    V(&ll->writer_mutex); // 잠금 해제
}

webCache *find_cache(char* uri) {
    webCache *cur = ll->head;
    while (cur != NULL) {
        if(!strcasecmp(cur->uri,uri)) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}