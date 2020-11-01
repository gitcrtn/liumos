// HTTP server with UDP protocol.

#include "../liumlib/liumlib.h"

uint16_t port;

void StatusLine(char *response, int status) {
  switch (status) {
    case 200:
      strcpy(response, "HTTP/1.1 200 OK\n");
      break;
    case 404:
      strcpy(response, "HTTP/1.1 404 Not Found\n");
      break;
    default:
      strcpy(response, "HTTP/1.1 500 Internal Server Error\n");
  }
}

void Headers(char *response) {
  strcat(response, "Content-Type: text/html; charset=UTF-8\n");
}

void Crlf(char *response) {
  strcat(response, "\n");
}

void Body(char *response, char *message) {
  strcat(response, message);
}

void BuildResponse(char *response, int status, char *message) {
  // https://tools.ietf.org/html/rfc7230#section-3
  // HTTP-message = start-line
  //                *( header-field CRLF )
  //                CRLF
  //                [ message-body ]
  StatusLine(response, status);
  Headers(response);
  Crlf(response);
  Body(response, message);
}

void Route(char *response, char *path) {
  if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
    char *body =
        "<html>\n"
        "  <body>\n"
        "    <h1>Hello World</h1>\n"
        "    <div>\n"
        "       <p>This is a sample paragraph.</p>\n"
        "       <ul>\n"
        "           <li>List 1</li>\n"
        "           <li>List 2</li>\n"
        "           <li>List 3</li>\n"
        "       </ul>\n"
        "   </div>\n"
        " </body>\n"
        "</html>\n";
    BuildResponse(response, 200, body);
    return;
  }
  if (strcmp(path, "/example.html") == 0) {
    char *body =
        "<html>\n"
        "  <body>\n"
        "    <h1>Example Page</h1>\n"
        "    <div>\n"
        "       <p>This is a sample paragraph.</p>\n"
        "       <ul>\n"
        "           <li>List 1</li>\n"
        "           <li>List 2</li>\n"
        "       </ul>\n"
        "   </div>\n"
        " </body>\n"
        "</html>\n";
    BuildResponse(response, 200, body);
    return;
  }
  char *body =
      "<html>\n"
      "  <body>\n"
      "    <p>Page is not found.</p>\n"
      " </body>\n"
      "</html>\n";
  BuildResponse(response, 404, body);
}

void StartServer() {
  int socket_fd;
  struct sockaddr_in address;
  unsigned int addrlen = sizeof(address);

  if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    Println("Error: Failed to create a socket");
    exit(1);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(socket_fd, (struct sockaddr *) &address, addrlen) < 0) {
    Println("Error: Failed to bind a socket");
    exit(EXIT_FAILURE);
  }

  Print("Listening port: ");
  PrintNum(port);
  Print("\n");

  while (1) {
    Println("Log: Waiting for a request...\n");

    char request[SIZE_REQUEST];
    if (recvfrom(socket_fd, request, SIZE_REQUEST, 0,
                 (struct sockaddr*) &address, &addrlen) < 0) {
      Println("Error: Failed to receive a request.");
      exit(EXIT_FAILURE);
    }

    Println("----- request -----");
    Println(request);

    char *method = strtok(request, " ");
    char *path = strtok(NULL, " ");

    char* response = (char *) malloc(SIZE_RESPONSE);

    if (strcmp(method, "GET") == 0) {
      Route(response, path);
    } else {
      BuildResponse(response, 500, "Only GET method is supported.");
    }

    if (sendto(socket_fd, response, strlen(response), 0,
               (struct sockaddr *) &address, addrlen) < 0) {
      Println("Error: Failed to send a response.");
      exit(EXIT_FAILURE);
    }
  }

  close(socket_fd);
}

// Return 1 when parse succeeded, otherwise return 0.
int ParseArgs(int argc, char** argv) {
  // Set default values.
  port = 8888;

  while (argc > 0) {
    if (strcmp("--port", argv[0]) == 0 || strcmp("-p", argv[0]) == 0) {
      port = StrToNum16(argv[1], NULL);
      argc -= 2;
      argv += 2;
      continue;
    }

    return 0;
  }
  return 1;
}

int main(int argc, char *argv[]) {
  if (ParseArgs(argc-1, argv+1) == 0) {
    Println("Usage: httpserver.bin [ OPTION ]");
    Println("       -p, --port    Port number. Default: 8888");
    exit(EXIT_FAILURE);
    return EXIT_FAILURE;
  }

  StartServer();

  exit(0);
  return 0;
}
