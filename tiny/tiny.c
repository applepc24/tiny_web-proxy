/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

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
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);


  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if(is_static){
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
  }else{
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    // 첫 줄 헤더 읽기 (Host, Connection 등과 무관하게 처리 시작)
    Rio_readlineb(rp, buf, MAXLINE);

    // 빈 줄이 나올 때까지 반복해서 헤더 읽기
    // HTTP 헤더의 마지막 줄은 반드시 "\r\n" (빈 줄)로 끝남
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE); // 다음 줄 읽기
        printf("%s", buf);               // 디버깅용: 헤더 출력
    }

    return; // 함수 끝 (실제로는 아무 일도 안 하고 헤더만 소모함)
}

int parse_url(char *uri, char *filename, char *cgiargs) {
    char *ptr;

    // [정적 콘텐츠 처리] - "cgi-bin"이 없으면 정적 파일로 처리
    if (!strstr(uri, "cgi-bin")) {
        strcpy(cgiargs, "");      // CGI 인자는 필요 없으므로 빈 문자열로 초기화
        strcpy(filename, ".");    // 현재 디렉토리에서 시작 (상대 경로)
        strcat(filename, uri);    // uri를 filename 뒤에 붙임 → ex) "./index.html"

        // URI가 '/'로 끝나면 디폴트 파일 "home.html" 추가
        if (uri[strlen(uri) - 1] == '/') {
            strcat(filename, "home.html"); // ex) "./help/" → "./help/home.html"
        }

        return 1;  // 정적 콘텐츠를 의미
    } 
    // [동적 콘텐츠 처리] - "cgi-bin"이 포함되어 있으면 CGI 실행 요청
    else {
        ptr = strchr(uri, '?');   // '?' 문자가 있는지 검사 (쿼리 스트링)

        if (ptr) {
            strcpy(cgiargs, ptr + 1); // '?' 다음 부분을 CGI 인자로 저장
            *ptr = '\0';              // '?' 자리를 NULL로 바꿔서 URI를 자름
        } else {
            strcpy(cgiargs, "");      // 쿼리 문자열이 없으면 빈 문자열
        }

        strcpy(filename, ".");    // 현재 디렉토리 시작
        strcat(filename, uri);    // 남은 URI를 붙여서 실행할 CGI 파일 경로 생성

        return 0;  // 동적 콘텐츠 (CGI)
    }
}

void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    get_filetype(filename, filetype); // 확장자 기반으로 MIME 타입 결정

    // HTTP 응답 헤더 구성
    sprintf(buf, "HTTP/1.0 200 OK\r\n");                          // 상태 라인
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);          // 서버 정보
    sprintf(buf, "%sConnection: close\r\n", buf);                // 연결 종료 예정
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);     // 바디 크기
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);   // 파일 유형

    Rio_writen(fd, buf, strlen(buf)); // 응답 헤더 전송
    printf("Response headers: \n");
    printf("%s", buf);                // 서버 측 로그 출력

    // 파일을 열고 mmap으로 메모리에 매핑
    srcfd = Open(filename, O_RDONLY, 0); // 파일 읽기 전용으로 열기
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일 → 메모리 매핑
    Close(srcfd); // 파일 디스크립터는 더 이상 필요 없으므로 닫기

    Rio_writen(fd, srcp, filesize); // 매핑된 메모리에서 클라이언트로 파일 내용 전송
    Munmap(srcp, filesize);         // 매핑 해제
}

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) {
        strcpy(filetype, "text/html");
    } else if (strstr(filename, ".gif")) {
        strcpy(filetype, "image/gif"); // 오타 있었음: "imgae" → "image"
    } else if (strstr(filename, ".png")) {
        strcpy(filetype, "image/png");
    } else if (strstr(filename, ".jpg")) {
        strcpy(filetype, "image/jpeg");
    } else {
        strcpy(filetype, "text/plain"); // 기본값 (알 수 없는 확장자)
    }
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXLINE], *emptylist[] = { NULL };

    // HTTP 상태 코드와 헤더 전송 (CGI 응답 시작)
    sprintf(buf, "HTTP/1.0 200 OK\r\n");             // 상태 코드 라인
    Rio_writen(fd, buf, strlen(buf));                // 클라이언트에게 전송

    sprintf(buf, "Server: Tiny Web Server\r\n");     // 서버 정보 헤더
    Rio_writen(fd, buf, strlen(buf));                // 클라이언트에게 전송

    // 자식 프로세스를 생성 (fork): CGI 프로그램은 자식 프로세스에서 실행됨
    if (Fork() == 0) {
        // [자식 프로세스에서 실행되는 코드]

        // QUERY_STRING 환경변수 설정 (예: "x=1&y=2")
        // CGI 프로그램은 이 환경변수로 쿼리 파라미터를 받음
        setenv("QUERY_STRING", cgiargs, 1);

        // 표준 출력을 클라이언트 소켓(fd)로 리디렉션
        // CGI 프로그램의 printf → 브라우저로 전달됨
        Dup2(fd, STDOUT_FILENO);

        // CGI 프로그램 실행 (filename 경로에 있는 실행 파일)
        // 현재 자식 프로세스는 완전히 이 프로그램으로 덮어써짐
        Execve(filename, emptylist, environ);

        // execve는 성공하면 돌아오지 않음.
        // 실패하면 이 아래 코드가 실행되므로, 일반적으로 여기서 exit() 처리도 추가함
    }

    // 부모 프로세스는 자식이 끝날 때까지 기다림
    Wait(NULL);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    // 1. HTML 응답 바디 구성 시작
    sprintf(body, "<html><title>Tiny Error</title>");
    
    // ⚠️ 오타 있음! bgcolr → bgcolor
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);  // 흰색 배경 설정
    
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);   // 상태코드 출력 (ex: 404: Not Found)
    
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);  // 긴 설명과 에러 원인 추가
    
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body); // 서버 서명
    
    // 2. HTTP 응답 헤더 작성
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);   // 상태 줄 (ex: HTTP/1.0 404 Not Found)
    Rio_writen(fd, buf, strlen(buf));                       // 클라이언트에 전송
    
    sprintf(buf, "Content-type: text/html\r\n");            // MIME 타입 지정
    Rio_writen(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));  // 본문 길이 + 헤더 종료
    Rio_writen(fd, buf, strlen(buf));
    
    // 3. HTML 바디 전송
    Rio_writen(fd, body, strlen(body));
}