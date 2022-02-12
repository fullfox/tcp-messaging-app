#include <arpa/inet.h>
#include <err.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#include "common.h"
#include "msg_struct.h"

// Ce code re utilise le squelette du cours

int handle_connect(char * srv_addr, char * srv_port) {
  struct addrinfo hints, *result, *rp;
  int sfd;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(srv_addr, srv_port, &hints, &result) != 0) {
    perror("getaddrinfo()");
    exit(EXIT_FAILURE);
  }
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1) {
      continue;
    }
    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
      break;
    }
    close(sfd);
  }
  if (rp == NULL) {
    fprintf(stderr, "Could not connect\n");
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(result);
  return sfd;
}


void loop(int sockfd) {
	char * username = malloc(NICK_LEN);
	strcpy(username,"default");

	int connected = 0; //connected to a lobby ?
	int file_waiting = 0;

  char buff[MSG_LEN];
	char fichier[MSG_LEN];
  int n;
  struct pollfd pollfds[4];
  pollfds[0].fd = STDIN_FILENO;
  pollfds[0].events = POLLIN;
  pollfds[0].revents = 0;
  pollfds[1].fd = sockfd;
  pollfds[1].events = POLLIN;
  pollfds[1].revents = 0;
	pollfds[2].fd = 0;
	pollfds[2].events = 0;
	pollfds[2].revents = 0;
  pollfds[3].fd = 0;
	pollfds[3].events = 0;
	pollfds[3].revents = 0;

  while (1) {
    printf("$");
    fflush(stdout);
    if (poll(pollfds, 4, -1) == -10)
      break;

    // Si STDIN (donc entrée utilisateur)
    if (pollfds[0].revents & POLLIN) {
      char ch;
      n = 0;
      memset(buff, 0, MSG_LEN);
      while (read(STDIN_FILENO, &ch, 1) > 0) {
        buff[n++] = ch;
        if (ch == '\n')
          break;
      }
			char cmd[MSG_LEN]; split(buff,0,cmd);
			char arg1[MSG_LEN]; split(buff,1,arg1);
			char arg2[MSG_LEN]; split(buff,2,arg2);
			struct message * paquet = malloc(sizeof(struct message));
      memset(paquet,0,sizeof(struct message));

			char * payload = malloc(MSG_LEN * sizeof(char));
			*payload = '\0';

			strncpy(paquet->nick_sender, username, strlen(username));
			paquet->pld_len = 0;
			paquet->infos[0] = '\0';

      //Switch des commandes possibles:
			if (strcmp(cmd, "/nick") == 0) {
				strcpy(username,arg1);
				paquet->type = NICKNAME_NEW;
				strcpy(paquet->infos, arg1);

			} else if (strcmp(cmd, "/help") == 0) {
				paquet->type = ECHO_SEND; paquet->pld_len = 0;
        printf("Les commandes existantes sont les suivantes:"
        "/nick <pseudo> : permet de se renommer en <pseudo>\n"
        "/who : liste les utilisateurs du serveur\n"
        "/whois <utilisateur> : donne des informations sur l'utilisateur <pseudo>\n"
        "/msgall <message> : Envoie un message <message> à tout le monde\n"
        "/msg <utilisateur> <message> : Envoie un message privé <message> à l'utilisateur <utilisateur>\n"
        "/quit : Quitte le salon courant\n"
        "/channel_list : Liste les salons existants\n"
        "/create <salon> : Creer et rejoins le salon <salon>\n"
        "/join <salon> : Rejoins le salon <salon>\n"
        "/send <utilisateur> <chemin_fichier> : Propose à l'utilisateur <utilisateur> de recevoir le fichier <chemin_fichier>\n"
        "/help : Affiche l'aide\n");

			} else if (strcmp(cmd, "/who") == 0) {
				paquet->type = NICKNAME_LIST;

			} else if (strcmp(cmd, "/whois") == 0) {
				paquet->type = NICKNAME_INFOS;
				strcpy(paquet->infos, arg1);

			} else if (strcmp(cmd, "/msgall") == 0) {
        split2(buff,1,arg1);
				paquet->pld_len = strlen(arg1);
				paquet->type = BROADCAST_SEND;
				strcpy(payload, arg1);

			} else if (strcmp(cmd, "/msg") == 0) {
				split2(buff,2,arg2);
				paquet->pld_len = strlen(arg2);
			 	paquet->type = UNICAST_SEND;
				strcpy(paquet->infos, arg1);
				strcpy(payload, arg2);

			} else if (strcmp(cmd, "/quit") == 0) {
				paquet->type = MULTICAST_QUIT;
				connected = 0;

			} else if (strcmp(cmd, "/channel_list") == 0) {
				paquet->type = MULTICAST_LIST;

			} else if (strcmp(cmd, "/create") == 0) {
				paquet->type = MULTICAST_CREATE;
				connected = 1;

				split2(buff,1,arg1);
				strcpy(paquet->infos, arg1);

			} else if (strcmp(cmd, "/join") == 0) {
				paquet->type = MULTICAST_JOIN;
				connected = 1;

				split2(buff,1,arg1);
				strcpy(paquet->infos, arg1);

			} else if (strcmp(cmd, "/send") == 0) {
				paquet->type = FILE_REQUEST;
				strcpy(paquet->infos, arg1);
				char path[MSG_LEN]; split2(buff,2,path);
        char clean_path[1024];
				unquote(path,clean_path);

				file_waiting = open(clean_path, O_RDONLY);

				char filename[MSG_LEN]; getFilenameFromPath(clean_path, filename);
				strcpy(payload, filename);
				paquet->pld_len = strlen(payload);

        if(file_waiting == -1){
          file_waiting = 0;
          printf("[Client] Fichier non trouvé.\n"); //Pour rien faire
          paquet->pld_len = 0;
          paquet->type = ECHO_SEND;
        }

			} else {
				paquet->pld_len = strlen(buff);

				if(connected){//Si connecté à un salon
					paquet->type = MULTICAST_SEND;
				} else {//Sinon
					paquet->type = ECHO_SEND;
				}
				strcpy(payload, buff);
			}

			int taille = sizeof(struct message) + MSG_LEN*sizeof(char);
			char * p = malloc(taille);
			memcpy(p, paquet, sizeof(struct message));
			memcpy(p+sizeof(struct message), payload, MSG_LEN);

			if (safeSend(sockfd, p, taille, 0) <= 0) {
				break;
			}
			//Libération
			free(paquet);free(payload);free(p);

      pollfds[0].revents = 0;

    }

    //SI SOCKET SRV
    if (pollfds[1].revents & POLLIN) {
			printf("\33[2K\r"); //Efface la derniere ligne du stdout (le "$")
      memset(buff, 0, MSG_LEN);
      if (recv(sockfd, buff, MSG_LEN, 0) <= 0)
        break;
			trim(buff);



      //RECEPTION DE DONNEE DU SERVEUR POUR LA RECEPTION DE FICHIER
      //On va utilisé \x01 pour FILE_REQUEST le client, \x02 pour FILE_ACCEPT et \x04 pour FILE_REJECT

			if(buff[0] == '\x01'){
        //File request
				char user[NICK_LEN];

				split(buff,1,user);
				split2(buff,2,fichier);

				struct message * paquet = malloc(sizeof(struct message));
				memset(paquet,0,sizeof(struct message));

				char * payload = malloc(MSG_LEN * sizeof(char));
				*payload = '\0';

				strncpy(paquet->nick_sender, username, strlen(username));
				paquet->pld_len = 0;

				printf("[Client] %s souhaite vous envoyer le fichier %s. [y/n]\n?", user, fichier);

				char retour[2];
				scanf("%s",retour);

				if(retour[0] == 'y'){
					printf("[Client] Fichier accepté, téléchargement en cours...\n");
					paquet->type = FILE_ACCEPT;
					strcpy(paquet->infos,user);

					char port[] = "8081";

					int sockfd2 = socket_listen_and_bind(port, payload);
					pollfds[2].fd = sockfd2;
          pollfds[2].events = POLLIN;

					strcat(payload," ");
					strcat(payload,port);

					paquet->pld_len = strlen(payload);

				} else {
					printf("[Client] Fichier refusé.\n");
					paquet->type = FILE_REJECT;
					strcpy(paquet->infos,user);

				}

				int taille = sizeof(struct message) + MSG_LEN*sizeof(char);
				char * p = malloc(taille);
				memcpy(p, paquet, sizeof(struct message));
				memcpy(p+sizeof(struct message), payload, MSG_LEN);

				if (safeSend(sockfd, p, taille, 0) <= 0) {
					break;
				}
				free(paquet);free(payload);free(p);

			} else if(buff[0] == '\x02' && file_waiting){
        //Envoie fichier - file accept
				char addr[24];split(buff,1,addr);
				char port[6];split(buff,2,port);

				int sfd = handle_connect(addr, port);

				//via <sys/stat.h>
				struct stat st;
				fstat(file_waiting, &st);
				int file_size = st.st_size;

        void * data = malloc(1024);

        safeSend(sfd, "\x00", 1, 0);

        int transmis = 0; int a_transmettre = 0;
        while (file_size) {
          a_transmettre = 1024;
          if(file_size<1024)
            a_transmettre = file_size;

          read(file_waiting, data, a_transmettre);
          safeSend(sfd, data, a_transmettre, transmis);

          file_size-=a_transmettre;
          transmis+=a_transmettre;
        }

				printf("[Client] Fichier envoyé (%i octets transmis).\n", transmis);

        free(data);
        close(file_waiting);
        file_waiting = 0;
				close(sfd);

			}	else if(buff[0] == '\x04' && file_waiting){
        //File reject
				file_waiting = 0;
				char user[NICK_LEN];
				split(buff,1,user);
				printf("[Client] %s a refusé l'échange.\n", user);

			} else {
        //Message normal du serveur
				printf("%s\n", buff);
			}

      pollfds[1].revents = 0;
    }
    if (pollfds[1].revents & POLLHUP) {
			//Si fermeture du serveur
      printf("\33[2K\r");
      break;
    }

		if(pollfds[2].revents & POLLIN) {
			//Connexion p2p
      struct sockaddr client_addr;
      socklen_t size = sizeof(client_addr);
      int client_fd;
      if (-1 == (client_fd = accept(pollfds[2].fd, &client_addr, &size))) {
        perror("Accept");
      }

      pollfds[3].fd = client_fd;
      pollfds[3].events = POLLIN;
			pollfds[2].revents = 0;
		}

    if(pollfds[3].revents & POLLIN){
      //Reception fichier ou ack
			memset(buff, 0, MSG_LEN);

			//utiliser fichier
      char clean_fichier[MSG_LEN];
      unquote(fichier,clean_fichier);
      char nvx_fichier[MSG_LEN] = "r";
      strcat(nvx_fichier, clean_fichier);

			FILE * fs = fopen(nvx_fichier, "w");
			int t = 0;

			while ((n = recv(pollfds[3].fd, buff, 1024, 0)) > 0) {
				t += n;
        fwrite((void *) buff, n, 1, fs);
	    	}
      fclose(fs);

			printf("\r[Client] \"%s\" recu (%i octets).\n",nvx_fichier,t);

			close(pollfds[2].fd);
      close(pollfds[3].fd);
			pollfds[2].fd = 0;
      pollfds[3].fd = 0;
      pollfds[3].revents = 0;
    }
  }
}


int main(int argc, char *argv[]) {
  if (argc != 3) {
		printf("Usage: ./client server_address port_number\n");
		exit(EXIT_FAILURE);
	}

  int sfd;
  sfd = handle_connect(argv[1],argv[2]);

  loop(sfd);

  close(sfd);
  return EXIT_SUCCESS;
}
