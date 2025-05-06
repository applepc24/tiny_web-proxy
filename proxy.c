#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void handle_client(int clientfd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, clientfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if(argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  while(1) {
    clientlen = sizeof(clientaddr);
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    handle_client(clientfd);
    Close(clientfd);
  }
}

void handle_client(int clientfd){
  rio_t request_rio, response_rio;
  char request_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];

  // 요청 라인 읽기
  Rio_readinitb(&request_rio, clientfd);
  if(!Rio_readlineb(&request_rio, request_buf, MAXLINE)){
    return;
  }
  printf("Request headers: \n%s" , request_buf);

  // method, uri 파싱
  sscanf(request_buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path);

  // 서버와 연결 시도
  int serverfd = Open_clientfd(hostname, port);
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){
    clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // 요청 라인 재구성후 서버로 전송
  sprintf(request_buf, "%s %s HTTP/1.0\r\n", method, path);
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  // 나머지 요청 헤더도 서버로 전송
  read_requesthdrs(&request_rio, serverfd, hostname);
  
  // 응답을 받아 클라이언트에게 그대로 전달
  Rio_readinitb(&response_rio, serverfd);
  size_t n;
  while ((n = Rio_readlineb(&response_rio, request_buf, MAXLINE)) > 0)
  {
    Rio_writen(clientfd, request_buf, n);
  }
  
}

void parse_uri(char *uri, char *hostname, char *port, char *path){
  int is_local_test = 1;

  char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
  char *port_ptr = strstr(hostname_ptr, ':');
  char *path_ptr = strchr(hostname_ptr, '/');

  if(path_ptr){
    strcpy(path, path_ptr);
  }else{
    strcpy(path, "/");
  }

  if(port_ptr){
    strncpy(port, port_ptr +1, path_ptr - port_ptr -1);
    port[path_ptr - port_ptr -1] = '\0';
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
    hostname[port_ptr - hostname_ptr] = '\0';
  }else{
    strcpy(port, is_local_test ? "80" : "8000");
    strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
    hostname[path_ptr - hostname_ptr] = '\0';
  }
}

void read_requesthdrs(rio_t *rp, int serverfd, char *hostname){
  char buf[MAXLINE];
  int has_host = 0;
  
  while(Rio_readlineb(rp, buf, MAXLINE) > 0){
    if(strcmp(buf, "\r\n") == 0){
      break;
    }
    if(strncasecmp(buf, "Host", 5) == 0){
      has_host =1;
      sprintf(buf, "Host: %s\r\n", hostname);
    }else if (strncasecmp(buf, "User-Agent", 11) == 0){
      strcpy(buf, user_agent_hdr);
    }else if(strncasecmp(buf, "Connection:", 11) == 0){
      strcpy(buf, "Connection: close\r\n");
    }else if(strncasecmp(buf, "Proxy-Connection:", 17) == 0){
      strcpy(buf, "Proxy-Connection: close\r\n");
    }
    Rio_writen(serverfd, buf, strlen(buf));
  }

  if(!has_host){
    sprintf(buf, "Host: %s\r\n", hostname);
    Rio_writen(serverfd, buf, strlen(buf));
  }
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  sprintf(buf, "Proxy-Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  sprintf(buf, "%s", user_agent_hdr);
  Rio_writen(serverfd, buf, strlen(buf));

  sprintf(buf, "\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    // HTTP response body 만들기
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body + strlen(body),
            "<body bgcolor=\"ffffff\">\r\n");
    sprintf(body + strlen(body),
            "%s: %s\r\n", errnum, shortmsg);
    sprintf(body + strlen(body),
            "<p>%s: %s\r\n", longmsg, cause);
    sprintf(body + strlen(body),
            "<hr><em>My Proxy Server</em>\r\n</body></html>");

    // HTTP response 헤더
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    // body 전송
    Rio_writen(fd, body, strlen(body));
}