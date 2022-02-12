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
#include <time.h>
#include <fcntl.h>

#include "common.h"
#include "msg_struct.h"

// Ce code re utilise le squelette du cours

typedef struct user user;
struct user {
  int fd;
  int channel;
	char nick[NICK_LEN];
  char addr[20];
  char port[6];
  char time[100];
  struct sockaddr_in * sockaddr_in;
  user* next;
};
user * lastUser = NULL;


typedef struct channel channel;
struct channel {
  char name[INFOS_LEN];
  int id;
  channel* next;
};
channel * lastChannel = NULL;
int channelCount = 1;


//DECONNEXION D'UN UTILISATEUR
void clientDisconnect(int fd){
  printf("\n =>Client on socket %i has disconnected from server.\n", fd);
  user * u = lastUser;
  user * userASupprimer;
  user * uPrecedent;

  int cId = 0;
  char * nom;

  while (u != NULL) {
    if(u->fd == fd){
      nom = u->nick;
      cId = u->channel;
      userASupprimer = u;
      if(u == lastUser){
        //Si c'est le dernier user de la liste
        lastUser = u->next;
      } else {
        //Sinon on raccorde le suivant au précédent
        uPrecedent->next = u->next;
      }
      break;
    }
    uPrecedent = u;
    u = u->next;
  }

  if(cId != 0){
    //Si l'utilisateur était sur un salon, on notifie la deconnexion aux autres
    char answer[MSG_LEN] = "[Server] ";
    strcat(answer, nom);
    strcat(answer," viens de quitter le salon.\n");
    u = lastUser;
    while (u != NULL) {
      if(u->channel == cId){
        safeSend(u->fd, answer, strlen(answer), 0);
      }
      u = u->next;
    }
  }

  free(userASupprimer); //puis on libère
  close(fd); //fermer la socket
}


//BOUCLE DU SERVEUR (600 lignes)
//Cette fonction prend bcp de ligne a cause du switch case.
//Placer chaque case dans une fonction dédié alourdie davantage le code que ne le rend lisible

void serverLoop(int listen_fd){
  int nfds = BACKLOG; //nb de client gerable à la fois
  struct pollfd pollfds[nfds];

  //Premier slot => socket d'écoute listen_fd
  pollfds[0].fd = listen_fd;
  pollfds[0].events = POLLIN;
  pollfds[0].revents = 0;
  //Le reste en attente
  for (int i = 1; i < nfds; i++) {
    pollfds[i].fd = -1;
    pollfds[i].events = 0;
    pollfds[i].revents = 0;
  }

  // server loop
  while (1) {
    if (-1 == poll(pollfds, nfds, -1)) {
      perror("Poll");
    }

    //Pour chaque pollfd
    for (int i = 0; i < nfds; i++) {

      //Si socket d'écoute (donc nvl connection)
      if (pollfds[i].fd == listen_fd && pollfds[i].revents & POLLIN) {
        struct sockaddr client_addr;
        socklen_t size = sizeof(client_addr);
        int client_fd;
        if (-1 == (client_fd = accept(listen_fd, &client_addr, &size))) {
          perror("Accept");
        }
        // display client connection information
        struct sockaddr_in *sockptr = (struct sockaddr_in *)(&client_addr);
        struct in_addr client_address = sockptr->sin_addr;
        printf("\n =>Connection succeeded on socket %i and client used %s:%hu \n",client_fd,
          inet_ntoa(client_address), ntohs(sockptr->sin_port));

        user * currentUser = malloc(sizeof(user));
        currentUser->fd = client_fd;
        currentUser->sockaddr_in = sockptr;
        currentUser->channel = 0;
        strcpy(currentUser->addr,inet_ntoa(client_address));
        sprintf(currentUser->port, "%hu", ntohs(sockptr->sin_port));

        //Snippet pour le temps en string
        time_t rn = time(NULL);
        char * time_str = ctime(&rn);
        time_str[strlen(time_str)-1] = '\0';

        strcpy(currentUser->time,time_str);
        currentUser->next = lastUser;
        strcpy(currentUser->nick,"default");
        lastUser = currentUser;

        for (int j = 0; j < nfds; j++) {
          if (pollfds[j].fd == -1) {
            pollfds[j].fd = client_fd;
            pollfds[j].events = POLLIN;
            break;
          }
        }

        char answer[MSG_LEN] = "[Server] Bienvenue sur le lobby du serveur,\npensez à choisir un pseudo via /nick <pseudo>\n";
        safeSend(client_fd, answer, strlen(answer), 0);

        pollfds[i].revents = 0;

      } else if (pollfds[i].fd != listen_fd && pollfds[i].revents & POLLHUP) {
        //Deconnexion 1
        clientDisconnect(pollfds[i].fd);
        pollfds[i].fd = -1;
        pollfds[i].events = 0;
        pollfds[i].revents = 0;

      } else if (pollfds[i].fd != listen_fd && pollfds[i].revents & POLLIN) {
        // If a socket different from the listening socket is active
        int sockfd = pollfds[i].fd;

        char buff[sizeof(struct message) + MSG_LEN];
        memset(buff, 0, sizeof(struct message)+MSG_LEN);

        //RECEPTION
        if (recv(sockfd, buff, sizeof(struct message)+MSG_LEN, 0) <= 0) {
          //Deconnexion 2
          clientDisconnect(pollfds[i].fd);
          pollfds[i].fd = -1;
          pollfds[i].events = 0;
          pollfds[i].revents = 0;
          break;
        }

        struct message * paquet = malloc(sizeof(struct message));
        char * payload = malloc(MSG_LEN*sizeof(char));
        memcpy(paquet,buff,sizeof(struct message));
        memcpy(payload,buff+sizeof(struct message),paquet->pld_len);
        printf("\n  -  %s received from %s (infos: %s) (payload length: %i)\n", msg_type_str[paquet->type], paquet->nick_sender, paquet->infos, paquet->pld_len);

        //printf("Debug payload: %s\n", payload);


        switch (paquet->type) {
          case NICKNAME_NEW:
          {
            char answer[MSG_LEN] = "[Server] Pseudo deja utilisé!\n";
            int autorisation = 1;

            for (int l = 0; l < strlen(paquet->infos); l++) {
              if(!((paquet->infos[l] >= 'A' && paquet->infos[l] <= 'z') || (paquet->infos[l] >= '0' && paquet->infos[l] <= '9')) ){
                autorisation = 0;
                memset(answer,0,MSG_LEN);
                strcat(answer,"[Server] Caractère(s) invalide(s): pseudo refusé !\n"); //Seules les chiffres et les lettres sont autorisées.\n
              }
            }

            user * u = lastUser;
            while (u != NULL) {
              if(strcmp(u->nick, paquet->infos) == 0){
                autorisation = 0;
                break;
              }
              u = u->next;
            }
            if(autorisation){
              user * u = lastUser;
              while (u != NULL) {
                if(u->fd == pollfds[i].fd){
                  strcpy(u->nick, paquet->infos);
                  memset(answer,0,MSG_LEN);
                  strcat(answer,"[Server] Vous vous prénommez désormais ");
                  strcat(answer,paquet->infos);
                  strcat(answer," !\n");
                  break;
                }
                u = u->next;
              }
            }

            safeSend(sockfd, answer, strlen(answer), 0);
            break;
          }



          case NICKNAME_LIST:
          {
            char answer[MSG_LEN] = "";
            strcat(answer,"[Server] Liste des utilisateurs: \n");

            user * u = lastUser;
            while (u != NULL) {
              strcat(answer, " - ");
              strcat(answer, u->nick);
              strcat(answer, "\n");
              u = u->next;
            }
            safeSend(sockfd, answer, strlen(answer), 0);
            break;
          }



          case NICKNAME_INFOS:
          {
            char answer[MSG_LEN] = "[Server] Utilisateur non trouvé.\n";
            user * u = lastUser;
            while (u != NULL) {
              if(strcmp(u->nick, paquet->infos) == 0){
                memset(answer,0,MSG_LEN);
                strcat(answer,"[Server] ");
                strcat(answer, paquet->infos);
                strcat(answer," est connecté sur ");
                strcat(answer, u->addr);
                strcat(answer, ":");
                strcat(answer, u->port);
                strcat(answer, " depuis ");
                strcat(answer, u->time);
                break;
              }

              u = u->next;
            }
            strcat(answer, "\n");
            safeSend(sockfd, answer, strlen(answer), 0);
            break;
          }



          case ECHO_SEND:
            safeSend(sockfd, payload, paquet->pld_len, 0);
            break;



          case UNICAST_SEND:
            {
            char answer[MSG_LEN] = "[Server] Utilisateur non trouvé. Echec de l'envoi du message.\n";
            user * u = lastUser;
            while (u != NULL) {
              if(strcmp(u->nick, paquet->infos) == 0){
                memset(answer,0,MSG_LEN);
                strcat(answer,"[");
                strcat(answer, paquet->nick_sender);
                strcat(answer, " -> ");
                strcat(answer, u->nick);
                strcat(answer, "] ");
                strcat(answer, payload);
                strcat(answer, "\n");
                safeSend(u->fd, answer, strlen(answer), 0);
                break;
              }
              u = u->next;
            }
            safeSend(sockfd, answer, strlen(answer), 0);
            break;
            }



          case BROADCAST_SEND:
          {
            char answer[MSG_LEN];
            memset(answer,0,MSG_LEN);
            strcat(answer,"[");
            strcat(answer, paquet->nick_sender);
            strcat(answer, "] ");
            strcat(answer, payload);
            strcat(answer, "\n");

            user * u = lastUser;
            while (u != NULL) {
              safeSend(u->fd, answer, strlen(answer), 0);
              u = u->next;
            }
            break;
            }



          case MULTICAST_CREATE:
            {
            int autorisation = 1;
            char answer[MSG_LEN] = "";

            user * u = lastUser;
            while (u != NULL) {
              if(u->fd == sockfd){
                if(u->channel != 0){
                  autorisation = 0;
                  strcpy(answer,"[Server] Quitter le salon actuel pour en créer un nouveau.\n");
                }
                break;
              }
              u = u->next;
            }

            if(autorisation){
              //verif si nom libre
              channel * c = lastChannel;
              while (c != NULL) {
                if(strcmp(c->name, paquet->infos) == 0){
                  autorisation = 0;
                  break;
                }
                c = c->next;
              }
              strcpy(answer,"[Server] Echec: salon deja existant.'");
            }

            if(autorisation){

              channel * nvxChannel = malloc(sizeof(struct channel));
              strcpy(nvxChannel->name,paquet->infos);
              nvxChannel->id = channelCount++;
              nvxChannel->next = lastChannel;
              lastChannel = nvxChannel;

              user * u = lastUser;
              while (u != NULL) {
                if(u->fd == pollfds[i].fd){
                  u->channel = nvxChannel->id;
                  break;
                }
                u = u->next;
              }

              strcpy(answer,"[Server] Salon '");
              strcat(answer, nvxChannel->name);
              strcat(answer, "' créé !\n Vous venez de rejoindre le salon '");
              strcat(answer, nvxChannel->name);
              strcat(answer, "'.\n");
            }
            safeSend(sockfd, answer, strlen(answer), 0);

            break;
            }



          case MULTICAST_LIST:
            {
            char answer[MSG_LEN] = "[Server] Liste des salons: \n";
            user * u;
            channel * c = lastChannel;
            while (c != NULL) {
              strcat(answer, " - ");
              strcat(answer, c->name);

              //Compter le nb de gens dans chaques channels
              int pop = 0;
              u = lastUser;
              while (u != NULL) {
                if(u->channel == c->id)
                  pop++;
                u = u->next;
              }

              char pop_str[20];
              sprintf(pop_str," (%i / 20)\n",pop);
              strcat(answer, pop_str);

              c = c->next;
            }
            safeSend(sockfd, answer, strlen(answer), 0);
            break;
            }



          case MULTICAST_JOIN:
            {
              char answer[MSG_LEN] = "[Server] Salon non trouvé.\n";
              int cId = 0;

              channel * c = lastChannel;
              while (c != NULL) {
                if(strcmp(c->name, paquet->infos) == 0){
                  cId = c->id;
                  user * u = lastUser;
                  while (u != NULL) {
                    if(u->fd == pollfds[i].fd){
                      u->channel = c->id;
                      strcpy(answer,"[Server] ");
                      strcat(answer, u->nick);
                      strcat(answer, " viens de rejoindre le salon '");
                      strcat(answer, c->name);
                      strcat(answer, "'.\n");
                    break;
                    }
                    u = u->next;
                  }

                  //On notifie la connexion aux autres
                  u = lastUser;
                  while (u != NULL) {
                    if(u->channel == cId){
                      safeSend(u->fd, answer, strlen(answer), 0);
                    }
                    u = u->next;
                  }

                  break;
                }
                c = c->next;
              }
              if(cId == 0){safeSend(sockfd, answer, strlen(answer), 0);}

            break;
            }




          case MULTICAST_SEND:
            {
            char answer[MSG_LEN];
            strcpy(answer,"[");
            int cId = 0;
            user * u = lastUser;
            while (u != NULL) {
              if(u->fd == pollfds[i].fd){
                cId = u->channel;
                strcat(answer,u->nick);
                break;
              }
              u = u->next;
            }

            if(cId == 0){
              strcpy(answer,"[Server] Vous n'êtes connectés à aucun salon.\n Faite /quit pour revenir au lobby.\n");
              safeSend(sockfd, answer, strlen(answer), 0);
              break;
            }

            strcat(answer,"@");

            channel * c = lastChannel;
            while (c != NULL) {
              if(c->id == cId){
                strcat(answer,c->name);
              }
              c = c->next;
            }

            strcat(answer,"] ");
            strcat(answer,payload);

            u = lastUser;
            while (u != NULL) {
              if(u->channel == cId){
                safeSend(u->fd, answer, strlen(answer), 0);
              }
              u = u->next;
            }
            break;
            }




          case MULTICAST_QUIT:
            {
            char answer[MSG_LEN] = "[Server] Vous venez de quitter le salon, retour au lobby.\n";

            //On recup l'id et on enleve l'utilisateur du channel
            user * u = lastUser;
            int cId = 0;
            char * nom;

            while (u != NULL) {
              if(u->fd == pollfds[i].fd){
                cId = u->channel;
                nom = u->nick;
                u->channel = 0;
                break;
              }
              u = u->next;
            }

            if(cId != 0){
              //Si utilisateur dans un salon
              //On check si le salon est vide
              int nonVide = 0;
              u = lastUser;
              while (u != NULL) {
                if(u->channel == cId){
                  nonVide = 1;
                  break;
                }
                u = u->next;
              }

              if(!nonVide){
                //Si channel vide -> suppression du channel
                strcat(answer,"[Server] Salon vide: suppression du salon '");

                channel * c = lastChannel;
                channel * channelASupprimer;
                channel * cPrecedent;

                while (c != NULL) {
                  if(c->id == cId){
                    strcat(answer, c->name);
                    channelASupprimer = c;
                    if(c == lastChannel){
                      //Si c'est le dernier channel de la liste
                      lastChannel = c->next;
                    } else {
                      cPrecedent->next = c->next;
                    }
                    break;
                  }
                  cPrecedent = c;
                  c = c->next;
                }

                free(channelASupprimer); //puis on libère

                strcat(answer,"'.\n");
              } else {
                //On notifie la connexion aux autres
                char answer2[MSG_LEN] = "[Server] ";
                strcat(answer2, nom);
                strcat(answer2," viens de quitter le salon.\n");
                u = lastUser;
                while (u != NULL) {
                  if(u->channel == cId){
                    safeSend(u->fd, answer2, strlen(answer2), 0);
                  }
                  u = u->next;
                }

              }
              safeSend(sockfd, answer, strlen(answer), 0);

            }
            break;
            }

          //On va utilisé \x01 pour FILE_REQUEST le client, \x02 pour FILE_ACCEPT et \x04 pour FILE_REJECT

          case FILE_REQUEST:
            {
            //check si le pseudo existe
            int autorisation = 0;
            char answer[MSG_LEN] = "[Server] Utilisateur non trouvé.\n";

            char sender_nick[NICK_LEN];
            user * u = lastUser;
            while (u != NULL) {
              if(u->fd == pollfds[i].fd){
                strcpy(sender_nick,u->nick);
                break;
              }
              u = u->next;
            }

            u = lastUser;
            while (u != NULL) {
              if(strcmp(u->nick,paquet->infos) == 0){
                autorisation = 1;
                strcpy(answer,"\x01 ");
                strcat(answer, sender_nick);
                strcat(answer, " \"");
                strcat(answer, payload);
                strcat(answer, "\"");
                safeSend(u->fd, answer, strlen(answer), 0);
                break;
              }
              u = u->next;
            }

            if(!autorisation){
              safeSend(pollfds[i].fd, answer, strlen(answer), 0);
            }

            break;
            }


          case FILE_ACCEPT:
            {
              user * u = lastUser;
              while (u != NULL) {
                if(strcmp(u->nick,paquet->infos) == 0){
                  char answer[MSG_LEN] = "\x02 ";
                  strcat(answer, payload);
                  safeSend(u->fd, answer, strlen(answer), 0);
                  break;
                }
                u = u->next;
              }

            break;
            }

          case FILE_REJECT:
            {
              char sender_nick[NICK_LEN];
              user * u = lastUser;
              while (u != NULL) {
                if(u->fd == pollfds[i].fd){
                  strcpy(sender_nick,u->nick);
                  break;
                }
                u = u->next;
              }
              u = lastUser;
              while (u != NULL) {
                if(strcmp(u->nick,paquet->infos) == 0){
                  char answer[MSG_LEN] = "\x04 ";
                  strcat(answer, sender_nick);
                  safeSend(u->fd, answer, strlen(answer), 0);
                  break;
                }
                u = u->next;
              }
            break;
            }
        }


        free(paquet); free(payload);
        pollfds[i].revents = 0;
      }
    }
  }
}





int main(int argc, char const *argv[]) {
  if (argc != 2) {
		printf("Usage: ./server port_number\n");
		exit(EXIT_FAILURE);
	}


  const char *port = argv[1];
  char addr[24];

  int listen_fd = -1;
  if (-1 == (listen_fd = socket_listen_and_bind(port,addr))) {
    printf("Could not create, bind and listen properly\n");
    return 1;
  }

  printf("Serveur lancé sur %s:%s ...\n", addr, port);

  //NETCODE
  serverLoop(listen_fd);

  return 0;
}
