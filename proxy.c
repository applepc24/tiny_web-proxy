#include <stdio.h>
#include "csapp.h"

 typedef struct web_object_t
    {
      char path[MAXLINE];
      int content_length;
      char *response_ptr;
      struct web_object_t *prev, *next;
    } web_object_t;

web_object_t *rootp = NULL;
web_object_t *lastp = NULL;
int total_cache_size = 0;

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void *thread(void *vargp);
web_object_t *find_cache(char *path);
void send_cache(web_object_t *web_object, int clientfd);
void read_cache(web_object_t *web_object);
void write_cache(web_object_t *web_object);
void handle_client(int clientfd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  if(argc != 2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  while(1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

void *thread(void *vargp){
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);

  handle_client(connfd);
  Close(connfd);
  return NULL;
}

void handle_client(int clientfd) {
  rio_t request_rio, response_rio;
  char request_buf[MAXLINE];
  char method[MAXLINE] = {0}, uri[MAXLINE] = {0};
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];

  Rio_readinitb(&request_rio, clientfd);

  // 요청 라인 읽기
  if (!Rio_readlineb(&request_rio, request_buf, MAXLINE)) {
    return;
  }
  printf("Request headers:\n%s", request_buf);

  // method와 uri 파싱 & 검사
  if (sscanf(request_buf, "%s %s", method, uri) != 2 || strlen(uri) == 0) {
    clienterror(clientfd, request_buf, "400", "Bad Request", "Malformed or empty request line");
    return;
  }

  // 지원하지 않는 메서드인 경우
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(clientfd, method, "501", "Not implemented", "Proxy does not implement this method");
    return;
  }

  // URI 파싱
  parse_uri(uri, hostname, port, path);

  //  캐시 확인
  web_object_t *cached_object = find_cache(path);
  if (cached_object) {
    send_cache(cached_object, clientfd);
    read_cache(cached_object);
    return;
  }

  // 서버 연결
  int serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) {
    clienterror(clientfd, hostname, "502", "Bad Gateway", "Connection failed");
    return;
  }

  // 요청 전송
  sprintf(request_buf, "%s %s HTTP/1.0\r\n", method, path);
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  read_requesthdrs(&request_rio, serverfd, hostname);

  // 응답 헤더 전송 + Content-Length 파싱
  Rio_readinitb(&response_rio, serverfd);
  int content_length = -1;
  size_t n;

  while ((n = Rio_readlineb(&response_rio, request_buf, MAXLINE)) > 0) {
    Rio_writen(clientfd, request_buf, n);

    if (strncasecmp(request_buf, "Content-length:", 15) == 0) {
      content_length = atoi(request_buf + 15);
    }

    if (strcmp(request_buf, "\r\n") == 0)
      break; // 헤더 종료
  }

  // 본문 읽기 + 클라이언트 전송
  if (strcasecmp(method, "HEAD") != 0 && content_length > 0) {
    char *response_ptr = malloc(content_length);
    if (!response_ptr) {
      Close(serverfd);
      return;
    }

    Rio_readnb(&response_rio, response_ptr, content_length);
    Rio_writen(clientfd, response_ptr, content_length);

    // 캐싱
    if (content_length <= MAX_OBJECT_SIZE) {
      web_object_t *web_object = calloc(1, sizeof(web_object_t));
      strcpy(web_object->path, path);
      web_object->content_length = content_length;
      web_object->response_ptr = response_ptr;
      write_cache(web_object);
    } else {
      free(response_ptr);
    }
  }

  Close(serverfd);
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
    if (!uri || strlen(uri) == 0) {
        fprintf(stderr, "[ERROR] parse_uri: null or empty uri!\n");
        strcpy(hostname, "");
        strcpy(port, "80");
        strcpy(path, "/");
        return;
    }

    char *hostname_ptr = strstr(uri, "//");
    if (hostname_ptr) {
        hostname_ptr += 2;
    } else {
        hostname_ptr = uri;
    }

    char *port_ptr = strchr(hostname_ptr, ':');
    char *path_ptr = strchr(hostname_ptr, '/');

    if (path_ptr) {
        strcpy(path, path_ptr);
    } else {
        strcpy(path, "/");
    }

    if (port_ptr && port_ptr < path_ptr) {
        strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
        port[path_ptr - port_ptr - 1] = '\0';
        strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
        hostname[port_ptr - hostname_ptr] = '\0';
    } else {
        strcpy(port, "80");
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

web_object_t *find_cache(char *path)
    {
      if (!rootp) // 캐시가 비었으면
        return NULL;
      web_object_t *current = rootp;      // 검사를 시작할 노드
      while (strcmp(current->path, path)) // 현재 검사 중인 노드의 path가 찾는 path와 다르면 반복
      {
        if (!current->next) // 현재 검사 중인 노드의 다음 노드가 없으면 NULL 반환
          return NULL;

        current = current->next;          // 다음 노드로 이동
        if (!strcmp(current->path, path)) // path가 같은 노드를 찾았다면 해당 객체 반환
          return current;
      }
      return current;
    }
void send_cache(web_object_t *web_object, int clientfd)
    {
      // Response Header 생성 및 전송
      char buf[MAXLINE];
      sprintf(buf, "HTTP/1.0 200 OK\r\n");                                           // 상태 코드
      sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);                            // 서버 이름
      sprintf(buf, "%sConnection: close\r\n", buf);                                  // 연결 방식
      sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, web_object->content_length); // 컨텐츠 길이
      Rio_writen(clientfd, buf, strlen(buf));

      // 캐싱된 Response Body 전송
      Rio_writen(clientfd, web_object->response_ptr, web_object->content_length);
    }

 void read_cache(web_object_t *web_object)
    {
      if (web_object == rootp) // 현재 노드가 이미 root면 변경 없이 종료
        return;

      // 현재 노드와 이전 & 다음 노드의 연결 끊기
      if (web_object->next) // '이전 & 다음 노드'가 모두 있는 경우
      {
        // 이전 노드와 다음 노드를 이어줌
        web_object_t *prev_objtect = web_object->prev;
        web_object_t *next_objtect = web_object->next;
        if (prev_objtect)
          web_object->prev->next = next_objtect;
        web_object->next->prev = prev_objtect;
      }
      else // '다음 노드'가 없는 경우 (현재 노드가 마지막 노드인 경우)
      {
        web_object->prev->next = NULL; // 이전 노드와 현재 노드의 연결을 끊어줌
      }

      // 현재 노드를 root로 변경
      web_object->next = rootp; // root였던 노드는 현재 노드의 다음 노드가 됨
      rootp = web_object;
    }
  
  void write_cache(web_object_t *web_object)
    {
      // total_cache_size에 현재 객체의 크기 추가
      total_cache_size += web_object->content_length;

      // 최대 총 캐시 크기를 초과한 경우 -> 사용한지 가장 오래된 객체부터 제거
      while (total_cache_size > MAX_CACHE_SIZE)
      {
        total_cache_size -= lastp->content_length;
        lastp = lastp->prev; // 마지막 노드를 마지막의 이전 노드로 변경
        free(lastp->next);   // 제거한 노드의 메모리 반환
        lastp->next = NULL;
      }

      if (!rootp) // 캐시 연결리스트가 빈 경우 lastp를 현재 객체로 지정
        lastp = web_object;

      // 현재 객체를 루트로 지정
      if (rootp)
      {
        web_object->next = rootp;
        rootp->prev = web_object;
      }
      rootp = web_object;
    }