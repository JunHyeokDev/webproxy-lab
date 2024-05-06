/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */

#include "csapp.h"

// 제주도 사투리 
// 뽕글랑하게 먹젠 = 배부르게 먹겠다 
// 치킨 먹젠? = 치킨 묵을까?

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // 호환성과 유연성을 위해 해당 구조체로 선언.

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
        doit(connfd);  // line:netp:tiny:doit
        Close(connfd); // line:netp:tiny:close
    }
}

void doit(int fd)
{
    int is_static;
    struct stat sbuf; // file, userID , GroupID, time 등 다양한 정보가 담겨있군. (Metadata)
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request Line and headers */
    Rio_readinitb(&rio, fd); // init
    Rio_readlineb(&rio, buf, MAXLINE); // 읽어오기
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method,"GET")) { // get이면 0을 반환.
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);
    /* Parse URI from GET request */
    // stat 함수는 파일의 정보를 얻기 위해 사용되며, 파일 이름(filename)과 파일 정보를 저장할 구조체(sbuf)를 인자로 받습니다. 
    is_static = parse_uri(uri,filename,cgiargs);
    if (stat(filename, &sbuf) < 0 ) {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

    if (is_static) {
        // S_ISREG 는 일반 파일인지 여부 판단, S_IRUSR User의 읽기 권한 체크.
        // 파일이 일반 파일이 아니거나 파일의 소유자에게 읽기 권한이 없는 경우.
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd,filename, "403", "Forbidden", "Tiny couldn't read the file");
            return;
        }
        // 클라이언트에 '정적' 파일을 응답으로 전송합니다.
        serve_static(fd,filename, sbuf.st_size);
    }
    else { // 동적 파일 전송
        // S_ISREG 는 일반 파일인지 여부 판단, S_IXUSR 는 실행 권한을 체크
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden","Tiny couldn’t run the CGI program");
            return;
        }
        // Common gateway Interface arguments
        serve_dynamic(fd,filename,cgiargs);
    }
}
//   clienterror(fd,        filename,     "403",     "Forbidden",  "Tiny couldn’t run the CGI program");
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

void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    // buf에서 \r\n 이 나올 때 까지 읽음.
    while (strcmp(buf, "\r\n")) { 
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {

    char *ptr;

    if (!strstr(uri,"cgi-bin")) { // cgi-bin이 포함되지 않으면
        strcpy(cgiargs, "");
        strcpy(filename, ".");
        strcat(filename, uri);

        if(uri[strlen(uri)-1] == '/') {
            strcat(filename, "home.html");
        }
        return 1;
    }
    else { /* Dynamic content */
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr+1);
            *ptr = '\0';
        }
        else {
            strcpy(cgiargs,""); 
        }
        strcpy(filename, ".");
        strcat(filename, uri);
        return 0;
    }   
}

void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n",buf, filetype);
    Rio_writen(fd,buf,strlen(buf)); // connfd
    printf("Reponse headers: \n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);  // readonly로 파일 읽기
    srcp  = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // Mmap -> 메모리 매핑
    // 보안 및 성능 (Disk에서 읽는 것보다 메모리에서 읽는 것이 빠르므로?)
    Close(srcfd);
    Rio_writen(fd,srcp, filesize);
    Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) {
        strcpy(filetype, "text/html");
    }
    else if (strstr(filename, ".gif")) {
        strcpy(filetype, "image/gif");
    }
    else if (strstr(filename, ".png")) {
        strcpy(filetype, "image/png");
    }
    else if (strstr(filename, ".jpg")) {
        strcpy(filetype, "image/jpeg");
    }
    else if (strstr(filename, ".mpeg")) {
        strcpy(filetype, "sample/mpeg");
    }
    else {
        strcpy(filetype, "text/plain");
    }
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {

    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP reponse */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd,buf, strlen(buf));
    
    if(Fork() == 0) { // Child
        setenv("QUERY_STRING", cgiargs, 1);
        Dup2(fd, STDOUT_FILENO); // /* Redirect stdout to client */  connfd
        Execve(filename, emptylist, environ);  /* Run CGI program */
    }
    Wait(NULL);

}