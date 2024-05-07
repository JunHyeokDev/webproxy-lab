#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

// 프로토타입 선언
void do_Proxy(int fd);
void read_requesthdrs(int fd, rio_t *rp, char *header);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void divide_hostname_and_path(const char* url, char *hostname, char *path, char *port);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];


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
        connfd = Accept(listenfd, (SA *)&clientaddr, // sockaddr == SA
                        &clientlen);                 // line:netp:tiny:accept
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                    0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        do_Proxy(connfd);  // line:netp:tiny:doit
        Close(connfd); // line:netp:tiny:close
    }
    return 0;
}

void do_Proxy(int fd) {

    // clientfd, connfd, listenfd 3개 모두 있어야 프록시의 역할을 할 수 있음. (? 추정 ?)
    int is_static;
    int  serverfd;
    struct stat sbuf; // file, userID , GroupID, time 등 다양한 정보가 담겨있군. (Metadata)
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char buf_for_server[MAXLINE];
    char buf_from_server[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    char filename[MAXLINE];
    char port[MAXLINE];


    rio_t rio, response_rio;

    /* Read request Line and headers */
    Rio_readinitb(&rio, fd); // init
    Rio_readlineb(&rio, buf, MAXLINE); // 읽어오기
    printf("Request headers:\n");
    printf("%s\n", buf);
    // curl --proxy http://localhost:7777 --output home.html http://localhost:8000/home.html
    sscanf(buf, "%s %s %s", method, uri, version);

    divide_hostname_and_path(uri,hostname,path, port);

    if (!strcasecmp(version,"HTTP/1.1")){
        strcpy(version,"HTTP/1.0");
    }

    // printf("method : %s\n",method);
    // printf("uri : %s\n",uri);
    // printf("version : %s\n",version);
    // printf("\n\n");
    // printf("hostname : %s\n",hostname);
    // printf("path : %s\n",path);
    // printf("port : %s\n",port);

    if (strcasecmp(method,"GET")) { // get이면 0을 반환.
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }
    sprintf(buf_for_server, "%s %s %s\r\n", method, path, "HTTP/1.0");                
    sprintf(buf_for_server, "%s%s\r\n", buf_for_server,user_agent_hdr);
    sprintf(buf_for_server, "%sConnection: close\r\n", buf_for_server);
    sprintf(buf_for_server, "%sProxy-Connection: close\r\n\r\n",buf_for_server);
    
    printf("%s",buf_for_server);

    serverfd = Open_clientfd(hostname, port);                                           
    Rio_writen(serverfd, buf_for_server, strlen(buf_for_server));               

    ssize_t bsize;
    Rio_readinitb(&response_rio, serverfd);
    while ((bsize = Rio_readlineb(&response_rio, buf_from_server, MAX_OBJECT_SIZE)) > 0) {     
        // printf("buf_from_server : %s\n",buf_from_server);
        Rio_writen(fd, buf_from_server, bsize);
        if (!strcmp(buf_from_server, "\r\n"))                                              
            break;
    }

    while ((bsize = Rio_readlineb(&response_rio, buf_from_server, MAX_OBJECT_SIZE)) > 0) {     
        // printf("buf_from_server : %s\n",buf_from_server);
        Rio_writen(fd, buf_from_server, bsize);                                        
    }
    Close(serverfd);
}

void serve_static(int fd, char *filename, int filesize ,char *method) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n",buf, filetype);
    Rio_writen(fd,buf,strlen(buf)); // connfd
    printf("Reponse headers: \n");
    printf("%s", buf);

    if (!strcasecmp(method,"HEAD")) {
        return;
    }

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);  // readonly로 파일 읽기
    //srcp  = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // Mmap -> 메모리 매핑
    srcp = (char*)malloc(filesize);
    Rio_readn(srcfd,srcp,filesize);
    // 보안 및 성능 (Disk에서 읽는 것보다 메모리에서 읽는 것이 빠르므로?)
    Close(srcfd);
    Rio_writen(fd,srcp, filesize);
    //Munmap(srcp, filesize);
    free(srcp);
}

void clienterror(int fd, char *cause, char*errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];
    
    /* Build the HTTP reponse body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
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


void divide_hostname_and_path(const char* url, char *hostname, char *path, char *port) {
    // "http://"가 있으면 이를 제거
    const char* ptr = strstr(url, "://");
    if (ptr != NULL) {
        url = ptr + 3;
    }

    // hostname 추출
    ptr = strchr(url, ':');
    if (ptr != NULL) {
        strncpy(hostname, url, ptr - url);
        hostname[ptr - url] = '\0';
        url = ptr + 1;
    } else {
        ptr = strchr(url, '/');
        if (ptr != NULL) {
            strncpy(hostname, url, ptr - url);
            hostname[ptr - url] = '\0';
            url = ptr;
        } else {
            strcpy(hostname, url);
            url += strlen(url);
        }
    }

    // port 추출
    ptr = strchr(url, '/');
    if (ptr != NULL) {
        strncpy(port, url, ptr - url);
        port[ptr - url] = '\0';
        url = ptr;
    } else {
        strcpy(port, url);
        url += strlen(url);
    }

    // path 추출
    strcpy(path, url);

    // 포트가 없는 경우 디폴트로 5004 사용
    if (port[0] == '\0') {
        strcpy(port, "5004");
    }
}