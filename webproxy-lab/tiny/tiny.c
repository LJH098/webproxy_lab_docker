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

/*
 * doit - Handle one HTTP request/response transaction.
 *
 * 구현 순서를 잡기 쉽게, 아래는 "코드처럼 읽히는 의사코드"만 적어둔 버전.
 * 한 단계씩 실제 코드로 바꿔가면 된다.
 */
void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  struct stat sbuf;
  int is_static;

  /*


   * 8. 동적 컨텐츠면:
   *    - regular file인지 확인
   *    - 실행 권한이 있는지 확인
   *    - 실패하면 403 에러
   *    - 성공하면 serve_dynamic(fd, filename, cgiargs);
   *
   *    체크 포인트:
   *    - !(S_ISREG(sbuf.st_mode))
   *    - !(S_IXUSR & sbuf.st_mode)
   *
   * 9. 흐름 요약:
   *    read request line
   *      -> reject non-GET
   *      -> read headers
   *      -> parse uri
   *      -> stat file
   *      -> static ? serve_static : serve_dynamic
   */

  /*
   * 1. fd를 rio 버퍼와 연결한다.
   * - Rio_readinitb(&rio, fd);
   */
  Rio_readinitb(&rio, fd);

  /*
   * 2. 요청 첫 줄(Request line)을 읽는다.
   *    - 예: "GET /index.html HTTP/1.1"
   *    - if (Rio_readlineb(...) <= 0) return;
   *    - sscanf(buf, "%s %s %s", method, uri, version);
   */
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
  {
    return;
  }
  sscanf(buf, "%s %s %s", method, uri, version);

  /*
   * 3. Tiny는 GET만 처리한다.
   *    - method가 "GET"이 아니면
   *      clienterror(fd, method, "501", "Not implemented",
   *                  "Tiny does not implement this method");
   *      return;
   *
   */
  if (strcmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  /*
   * 4. 나머지 요청 헤더를 읽어서 버린다.
   *    - read_requesthdrs(&rio);
   */

  read_requesthdrs(&rio);

  /*
   * 5. URI를 해석해서 정적/동적 컨텐츠인지 구분한다.
   *    - is_static = parse_uri(uri, filename, cgiargs);
   *    - filename: 실제 파일 경로
   *    - cgiargs: 동적 컨텐츠면 쿼리스트링
   */

  is_static = parse_uri(uri, filename, cgiargs);

  /*
   * 6. 요청한 파일이 존재하는지 stat으로 확인한다.
   *    - if (stat(filename, &sbuf) < 0) {
   *        clienterror(fd, filename, "404", "Not found",
   *                    "Tiny couldn't find this file");
   *        return;
   *      }
   */
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  /*
   * 7. 정적 컨텐츠면:
   *    - regular file인지 확인
   *    - 읽기 권한이 있는지 확인
   *    - 실패하면 403 에러
   *    - 성공하면 serve_static(fd, filename, sbuf.st_size);
   *
   *    체크 포인트:
   *    - !(S_ISREG(sbuf.st_mode))
   *    - !(S_IRUSR & sbuf.st_mode)
   *
   */

  if (is_static)
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }

    serve_static(fd, filename, sbuf.st_size);
  }
  else
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }

    serve_dynamic(fd, filename, cgiargs);
  }
}

/*
 * read_requesthdrs - Read and ignore the remaining HTTP request headers.
 *
 * 핵심 아이디어:
 * - request line(GET /... HTTP/1.1)은 이미 doit에서 읽은 상태
 * - 그 다음 줄부터 헤더들(Host, User-Agent, Accept...)이 이어진다
 * - 빈 줄("\r\n")이 나오면 헤더가 끝난 것
 * - Tiny는 헤더 내용을 따로 활용하지 않으므로 끝까지 읽어서 버리면 된다
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  /*
   * 1. 한 줄 읽는다.
   * - Rio_readlineb(rp, buf, MAXLINE);
   */
  Rio_readlineb(rp, buf, MAXLINE);

  /*
   * 2. 방금 읽은 줄이 "\r\n" 이면:
   *    - 헤더의 끝이므로 함수 종료
   *
   * 3. "\r\n" 이 아니라면:
   *    - 아직 헤더가 남아 있다는 뜻
   *    - 필요하면 printf("%s", buf); 로 출력해도 됨
   *    - 다시 다음 줄을 읽는다
   *
   * 4. 즉, 전체 구조는 사실상 아래와 같다:
   *
   *    첫 헤더 줄 읽기
   *    while (현재 줄 != "\r\n") {
   *      필요하면 출력
   *      다음 헤더 줄 읽기
   *    }
   *
   * 5. 주의:
   *    - 비교는 strcmp(buf, "\r\n")로 하는 경우가 많다
   *    - request line은 여기서 읽는 게 아니라 doit에서 먼저 읽는다
   */
  while (strcmp(buf, "\r\n"))
  {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}

/*
 * parse_uri - Parse URI into filename and CGI args.
 *
 * 반환값 규칙:
 * - 1 이면 static content
 * - 0 이면 dynamic content
 *
 * 핵심 아이디어:
 * - "cgi-bin"이 없으면 정적 컨텐츠로 본다
 * - "cgi-bin"이 있으면 동적 컨텐츠로 본다
 * - 정적이면 cgiargs는 빈 문자열
 * - 동적이면 '?' 뒤를 cgiargs로 떼고, 그 앞부분을 filename으로 쓴다
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  /*
   * 1. uri 안에 "cgi-bin"이 있는지 찾는다.
   *    - if (!strstr(uri, "cgi-bin")) 이면 static
   *    - 그렇지 않으면 dynamic
   */
  /*
   * 2. static content인 경우:
   *    - cgiargs를 빈 문자열로 만든다
   *      예: strcpy(cgiargs, "");
   *
   *    - 현재 디렉터리 "." + uri 를 붙여서 filename을 만든다
   *      예: "./index.html", "./home.html", "./images/a.jpg"
   *      예: strcpy(filename, ".");
   *          strcat(filename, uri);
   *
   *    - uri가 '/' 로 끝나면 기본 페이지를 붙인다
   *      Tiny에서는 "home.html" 을 붙임
   *      예: "/" -> "./home.html"
   *          "/foo/" -> "./foo/home.html"
   *
   *    - 마지막에 1 반환
   */
  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
    {
      strcat(filename, "home.html");
    }
    return 1;
  }

  /*
   * 3. dynamic content인 경우:
   *    - uri에서 '?' 위치를 찾는다
   *      예: ptr = index(uri, '?');
   *
   *    - '?'가 있으면:
   *      - '?' 다음 문자열을 cgiargs에 저장
   *      - '?' 자리에 '\0'를 넣어서 uri를 둘로 자른다
   *
   *      예:
   *      uri = "/cgi-bin/adder?1&2"
   *      cgiargs = "1&2"
   *      uri는 "/cgi-bin/adder" 까지만 남게 됨
   *
   *    - '?'가 없으면:
   *      - cgiargs를 빈 문자열로 만든다
   *
   *    - 현재 디렉터리 "." + 잘린 uri 를 붙여서 filename을 만든다
   *      예: "/cgi-bin/adder" -> "./cgi-bin/adder"
   *
   *    - 마지막에 0 반환
   */

  else
  {
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }

    else
    {
      strcpy(cgiargs, "");
    }

    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

/*
 * serve_static - Send a static file to the client.
 *
 * 핵심 아이디어:
 * 1. 파일 타입(Content-type)을 정한다
 * 2. HTTP 응답 헤더를 만든다
 * 3. 헤더를 먼저 클라이언트에 보낸다
 * 4. 파일 내용을 읽어서 보낸다
 *
 * 11.9 과제에서는 책 버전의 Mmap/Munmap 대신
 * Malloc + Rio_readn + Rio_writen + Free 흐름으로 구현해야 한다.
 */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /*
   * 1. filename을 보고 MIME 타입을 정한다.
   *    - get_filetype(filename, filetype);
   *
   *    예:
   *    - .html -> text/html
   *    - .gif  -> image/gif
   *    - .jpg  -> image/jpeg
   *    - 그 외 -> text/plain
   */

  get_filetype(filename, filetype);

  /*
   * 2. HTTP 응답 헤더를 문자열로 만든다.
   *
   *    보통 포함되는 줄:
   *    - "HTTP/1.0 200 OK\r\n"
   *    - "Server: Tiny Web Server\r\n"
   *    - "Connection: close\r\n"
   *    - "Content-length: %d\r\n"
   *    - "Content-type: %s\r\n\r\n"
   *
   *    즉, 상태줄 + 여러 헤더 + 마지막 빈 줄까지 만들어야 한다.
   */
  snprintf(buf, sizeof(buf),
           "HTTP/1.0 200 OK\r\n"
           "Server: Tiny Web Server\r\n"
           "Connection: close\r\n"
           "Content-length: %d\r\n"
           "Content-type: %s\r\n\r\n",
           filesize, filetype);

  /*
   * 3. 만든 헤더를 클라이언트에 먼저 보낸다.
   *    - Rio_writen(fd, buf, strlen(buf));
   *
   *    주의:
   *    - 파일 본문보다 먼저 헤더가 나가야 한다
   */
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers: \n");
  printf("%s", buf);

  /*
   * 11.9 과제용 의사코드:
   *
   * 4. 정적 파일을 읽기 전용으로 연다.
   *    - srcfd = Open(filename, O_RDONLY, 0);
   *
   * 5. 파일 크기만큼 힙 메모리를 할당한다.
   *    - srcp = Malloc(filesize);
   *
   * 6. 파일 내용을 디스크에서 버퍼(srcp)로 읽어온다.
   *    - Rio_readn(srcfd, srcp, filesize);
   *
   * 7. 파일 디스크립터를 닫는다.
   *    - Close(srcfd);
   *
   * 8. 메모리에 읽어온 파일 내용을 클라이언트에 보낸다.
   *    - Rio_writen(fd, srcp, filesize);
   *
   * 9. 힙 메모리를 해제한다.
   *    - Free(srcp);
   *
   * 즉, 11.9에서는:
   *    Mmap(...);
   *    Rio_writen(fd, srcp, filesize);
   *    Munmap(srcp, filesize);
   *
   * 가 아니라
   *
   *    srcp = Malloc(filesize);
   *    Rio_readn(srcfd, srcp, filesize);
   *    Rio_writen(fd, srcp, filesize);
   *    Free(srcp);
   *
   * 흐름으로 바뀐다.
   */
  srcfd = Open(filename, O_RDONLY, 0); // 서버 로컬 디스크에서 파일 읽는 fd
  srcp = Malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);

  Rio_writen(fd, srcp, filesize);
  Free(srcp);
}

/*
 * get_filetype - Derive MIME type from file extension.
 *
 * 핵심 아이디어:
 * - filename 안에 어떤 확장자가 들어있는지 검사한다
 * - 확장자에 맞는 MIME 타입 문자열을 filetype에 복사한다
 * - Tiny는 몇 가지만 처리하고, 나머지는 text/plain으로 둔다
 */
void get_filetype(char *filename, char *filetype)
{
  /*
   * 1. filename에 ".html" 이 있으면:
   *    - filetype = "text/html"
   *
   * 2. filename에 ".gif" 이 있으면:
   *    - filetype = "image/gif"
   *
   * 3. filename에 ".png" 이 있으면:
   *    - filetype = "image/png"
   *
   * 4. filename에 ".jpg" 또는 ".jpeg" 가 있으면:
   *    - filetype = "image/jpeg"
   *
   * 5. 위 경우가 아니면:
   *    - filetype = "text/plain"
   */

  if (strstr(filename, ".html"))
  {
    strcpy(filetype, "text/html");
  }
  else if (strstr(filename, ".gif"))
  {
    strcpy(filetype, "image/gif");
  }
  else if (strstr(filename, ".png"))
  {
    strcpy(filetype, "image/png");
  }
  else if (strstr(filename, ".jpg") || strstr(filename, ".jpeg"))
  {
    strcpy(filetype, "image/jpeg");
  }
  else if (strstr(filename, "mpg") || strstr(filename, "mpeg"))
  {
    strcpy(filetype, "video/mpeg");
  }

  else
  {
    strcpy(filetype, "text/plain");
  }
}

/*
 * serve_dynamic - Run a CGI program and send its output to the client.
 *
 * 핵심 아이디어:
 * - 정적 파일처럼 서버가 직접 파일 내용을 읽어 보내는 게 아니다
 * - CGI 프로그램을 실행하고, 그 프로그램의 표준출력(stdout)이
 *   곧 클라이언트에게 가도록 연결한다
 *
 * 주의:
 *    - 정적 컨텐츠와 달리 여기서는 서버가 파일을 직접 읽지 않는다
 *    - 실제 본문(body)은 CGI 프로그램이 stdout으로 만들어낸다
 *    - Execve에 넘길 세 번째 인자에서 environ을 사용한다
 *    - emptylist는 argv 역할이고, 보통 {NULL}로 둔다
 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  /*
   * 1. HTTP 응답의 앞부분을 먼저 보낸다.
   *
   *    Tiny 책 버전에서는 보통 아래 정도만 먼저 보낸다:
   *    - "HTTP/1.0 200 OK\r\n"
   *    - "Server: Tiny Web Server\r\n"
   *
   *    그리고 CGI 프로그램이 자기 출력으로 나머지 헤더(Content-type 등)와
   *    본문을 만들어낸다.
   */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  /*
   * 2. 자식 프로세스를 만든다.
   *    - if (Fork() == 0) { ... }
   *
   *    왜 자식을 만드나?
   *    - CGI 프로그램을 현재 서버 프로세스 대신 별도 프로세스로 실행하기 위해서
   */
  /*
   * 3. 자식 프로세스에서 할 일:
   *
   *    3-1. CGI 프로그램이 사용할 환경변수를 설정한다
   *         - setenv("QUERY_STRING", cgiargs, 1);
   *
   *         예:
   *         uri = "/cgi-bin/adder?1&2"
   *         cgiargs = "1&2"
   *         -> CGI 프로그램은 QUERY_STRING="1&2" 를 읽을 수 있음
   *
   *    3-2. 자식의 표준출력(stdout)을 클라이언트 소켓 fd로 리다이렉트한다
   *         - Dup2(fd, STDOUT_FILENO);
   *
   *         그러면 CGI 프로그램이 printf로 출력한 내용이
   *         그대로 클라이언트에게 전송된다
   *
   *    3-3. CGI 프로그램을 실행한다
   *         - Execve(filename, emptylist, environ);
   *
   *         성공하면 여기서 현재 자식 프로세스 코드는 사라지고
   *         CGI 프로그램 코드로 완전히 대체된다
   */

  if (Fork() == 0)
  {
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Close(fd);
    Execve(filename, emptylist, environ);
  }

  /*
   * 4. 부모 프로세스에서 할 일:
   *    - 보통 Wait 또는 Waitpid로 자식이 끝날 때까지 기다린다
   *
   *    왜 기다리나?
   *    - 자식 프로세스가 좀비 프로세스로 남지 않게 하려고
   */
  Wait(NULL);
}

/*
 * clienterror - Send an HTTP error response to the client.
 *
 * 핵심 아이디어:
 * - 에러 내용을 담은 HTML body를 먼저 만든다
 * - 그 다음 HTTP 상태줄과 헤더를 만든다
 * - 마지막에 헤더와 body를 순서대로 클라이언트에 보낸다
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /*
   * 1. 브라우저에 보여줄 HTML 에러 본문(body)을 만든다.
   *
   *    보통 들어가는 내용:
   *    - <html><title>Tiny Error</title>
   *    - body 태그
   *    - 에러 번호와 짧은 메시지
   *      예: "404: Not found"
   *    - 자세한 설명과 원인(cause)
   *      예: "Tiny couldn't find this file: ./foo.html"
   *    - 서버 이름
   *
   *    즉 body는 여러 sprintf를 이어 붙여서 만드는 경우가 많다.
   */
  body[0] = '\0';
  snprintf(body + strlen(body), sizeof(body) - strlen(body),
           "<html><title>Tiny Error</title>");
  snprintf(body + strlen(body), sizeof(body) - strlen(body),
           "<body bgcolor=\"ffffff\">\r\n");
  snprintf(body + strlen(body), sizeof(body) - strlen(body),
           "%s: %s\r\n", errnum, shortmsg);
  snprintf(body + strlen(body), sizeof(body) - strlen(body),
           "<p>%s: %s\r\n", longmsg, cause);
  snprintf(body + strlen(body), sizeof(body) - strlen(body),
           "<hr><em>The Tiny Web server</em>\r\n");

  /*
   * 2. HTTP 상태줄(status line)을 만든다.
   *    - 예: "HTTP/1.0 404 Not found\r\n"
   *    - errnum, shortmsg를 사용
   */

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  /*
   * 3. Content-type 헤더를 보낸다.
   *    - 에러 페이지는 HTML이므로
   *      "Content-type: text/html\r\n"
   */
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  /*
   * 4. Content-length 헤더를 보낸다.
   *    - body 문자열 길이를 계산해서 넣는다
   *    - 예: "Content-length: %d\r\n\r\n"
   *
   *    주의:
   *    - 여기서 body를 먼저 만들어놔야 strlen(body)를 쓸 수 있다
   *    - 마지막 "\r\n\r\n" 까지 보내야 헤더가 끝난다
   */

  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  /*
   * 5. 마지막으로 HTML body 자체를 보낸다.
   *    - Rio_writen(fd, body, strlen(body));
   */
  Rio_writen(fd, body, strlen(body));
}
