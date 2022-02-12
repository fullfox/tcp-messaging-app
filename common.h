#define MSG_LEN 1024
#define SERV_PORT "8080"
#define SERV_ADDR "127.0.0.1"

#define BACKLOG 20

struct info {
  short s;
  long l;
};

//SEND version secure
int safeSend(int fd, void *buf, int size, int iOffset) {
    int ret = 0, offset = 0;
    while (offset != size) {
        if (-1 == (ret = send(fd, buf + offset, size - offset, iOffset))) {
            perror("Writing from client socket");
            return -1;
        }
        offset += ret;
    }
    return offset;
}

//Supprime le \n finale d'une chaine str
void trim(char * str){
  if(str[strlen(str)-1] == '\n')
    str[strlen(str)-1] = '\0';
  return;
}

//Enleve les " d'un string str
void unquote(char * str, char * res){
  int i = 0;
  while(*str){
    if(*str != '"'){
        *(res+i) = *str;
        i++;
    }
    str++;
  }
  *(res+i) = *str;
}

//Retourne le nom d'un fichier filename a partir de son chemin filepath
void getFilenameFromPath(char * filepath, char * filename){
  strcpy(filename,filepath);
  while (*filepath) {
    if(*filepath++ == '/')
      strcpy(filename,filepath);
  }
  return;
}

//Renvoie le nb d'arg d'un string str interprété comme une commande
int split_len(char * str){
  int i = 0;
  while (*str) {
    if(*str++ == ' ')
      i++;
  }
  return i;
}

//Renvoie dans r le n-ieme arg d'un string str interprété comme une commande
//ex: split("/un deux trois",1,r); -> r vaut "deux"
char * split(char * str, int n, char * r){
  if (n > split_len(str)) {
    return 0;
  }
  for (int i = 0; i < n; i++) {
    while (*++str != ' ') {}
    str++;
  }
  int j = 0;
  while (*str != ' ' && *str != '\n' && *str) {
    *(r+j) = *str++;
    j++;
  }
  *(r+j) = '\0';
  return 0;
}

//Renvoie dans r le reste du string après ne n-1 ième argument de str interprété comme une commande
//ex: split2("/un deux trois",1,r); -> r vaut "deux trois"
char * split2(char * str, int n, char * r){
  if (n > split_len(str)) {
    return 0;
  }
  for (int i = 0; i < n; i++) {
    while (*++str != ' ') {}
    str++;
  }
  int j = 0;
  while (*str != '\n' && *str) {
    *(r+j) = *str++;
    j++;
  }
  *(r+j) = '\0';
  return 0;
}

//CREATION D'UNE SOCKET D'ECOUTE
int socket_listen_and_bind(const char *port, char * addr) {
  int listen_fd = -1;
  if (-1 == (listen_fd = socket(AF_INET, SOCK_STREAM, 0))) {
    perror("Socket");
    exit(EXIT_FAILURE);
  }
  //printf("Listen socket descriptor %d\n", listen_fd);

  int yes = 1;
  if (-1 ==
      setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  struct addrinfo indices;
  memset(&indices, 0, sizeof(struct addrinfo));
  indices.ai_family = AF_INET;
  indices.ai_socktype = SOCK_STREAM;
  indices.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  struct addrinfo *res, *tmp;

  int err = 0;
  if (0 != (err = getaddrinfo(NULL, port, &indices, &res))) {
    errx(1, "%s", gai_strerror(err));
  }

  tmp = res;
  while (tmp != NULL) {
    if (tmp->ai_family == AF_INET) {
      struct sockaddr_in *sockptr = (struct sockaddr_in *)(tmp->ai_addr);
      struct in_addr local_address = sockptr->sin_addr;
      strcpy(addr,inet_ntoa(local_address));
      //printf("Binding to %s on port %hd\n", inet_ntoa(local_address),ntohs(sockptr->sin_port));

      if (-1 == bind(listen_fd, tmp->ai_addr, tmp->ai_addrlen)) {
        perror("Binding");
      }
      if (-1 == listen(listen_fd, BACKLOG)) {
        perror("Listen");
      }
      return listen_fd;
    }
    tmp = tmp->ai_next;
  }
  return listen_fd;
}
