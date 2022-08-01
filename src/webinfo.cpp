#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>
#include <errno.h>
#include <iterator>
#include <unistd.h>
#include <netdb.h>
#include <regex.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

using namespace std;

#define BUF_MAX 150

typedef struct params {
    int lSize;
    int serverID;
    int lastObj;
    int typeObj;
    int all;
    string responses[4];  // 0-L, 1-A, 2-M, 3-T
    string url;
} tParams, *ptrParams;

typedef struct url {
  int port;
  int redirection;
  string protocol;
  string server;
  string path;
} tUrl, *ptrUrl;

enum states {
    S_OK,       // do nothing
    S_REDIR,    // redirection
    S_NEWLOC,   // found new redirect location
    S_FAIL4,    // Error 4xx
    S_FAIL5,    // Error 5xx
};

enum errors {
    E_OK,
    E_ARG,
    E_URL,
    E_REGCOMP,
    E_REGADR,
    E_PROTOCOL,
    E_SOCKET,
    E_GETHOST,
    E_CON,
    E_CLOSE,
    E_WRITE,
    E_READ,
    E_ERROR4,
    E_ERROR5,
};

const char *EMSG[] = {
    "VSE JE OK",                                       // E_OK
    "Spatne zadane parametry",                         // E_ARG
    "Chybny tvar URL",                                 // E_URL
    "Chyba pri kompilace regexp",                      // E_REGCOMP
    "Zadana adresa neodpovida validnimu tvaru",        // E_REGADR
    "Spatny vstupni protokol",                         // E_PROTOCOL
    "Chyba pri vytvareni socketu",                     // E_SOCKET
    "Chyba pri prekladu jmena",                        // E_GETHOST
    "Chyba pri navazovani spojeni",                    // E_CON
    "Chyba pri uzavirani spojeni",                     // E_CLOSE
    "Chyba pri posilani GET pozadavku",                // E_WRITE
    "Chyba pri prijimani odpovedi",                    // E_READ
    "4xx: Error",                                      // E_ERROR4
    "5xx: Error",                                      // E_ERROR5,

};

void showHelp() {
    printf("Napoveda k projektu do predmetu IPK (Projekt 3)\n");
    printf("Autor: David Konar (xkonar07@stud.fit.vutbr.cz\n\n");
    printf("Mozne prepinace:\n");
    printf("\t-l\tvelikost objektu\n");
    printf("\t-m\tinformace o poslednim objektu\n");
    printf("\t-s\tidentifikace serveru\n");
    printf("\t-t\ttyp obsahu souboru\n\n");
}


void showError(int error) {
    fprintf(stderr, "%s\n", EMSG[error]);
}

int myStrCpy(char* str1, char* str2, int from, int num) {
  int x = 0;

  for (x = 0; x < num; x++) {
      str1[x] = str2[from+x];
  }
  str1[x] = '\0';

  return EXIT_SUCCESS;
}

int processArgs(int argc, char** argv, tParams *par) {

    int i = 0;
    int c;
    par->url.clear();
    par->url.append(argv[argc-1]);
    // too many (few) arguments
    if(argc < 2) {
        showHelp();
        return EXIT_FAILURE;
    }

    // process arguments, except the last one - that is the URL
    par->all = 1;
    printf("Argc: %d\n",argc);

    opterr = 0;
    while ((c = getopt (argc, argv, "lsmt--")) != -1)	{
        i++;
        switch (c)
        {
            case 'l':
                if(par->lSize == 0)
                par->lSize = i;
                par->all = 0;
                break;
            case 's':
                if(par->serverID == 0)
                par->serverID = i;
                par->all = 0;
                break;
            case 'm':
                if(par->lastObj  == 0)
                par->lastObj = i;
                par->all = 0;
                break;
            case 't':
                if(par->typeObj == 0)
                par->typeObj = i;
                par->all = 0;
                break;
            case '?':
                showHelp();
                return EXIT_FAILURE;
            default:
                showHelp();
                return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

int connectToServer(int *s, tUrl *myUrl, int *error) {
    struct sockaddr_in sin;
    struct hostent *hptr;

    if ( ((*s) = socket(PF_INET, SOCK_STREAM, 0 ) ) < 0) { /* create socket*/
            (*error) = E_SOCKET;
            return EXIT_FAILURE;
    }
        sin.sin_family = PF_INET;              /*set protocol family to Internet */
        sin.sin_port = htons(myUrl->port);  /* set port no. */

  if ( (hptr =  gethostbyname(myUrl->server.c_str()) ) == NULL){
            (*error) = E_GETHOST;
            return EXIT_FAILURE;
  }

  memcpy( &sin.sin_addr, hptr->h_addr, hptr->h_length);

  if (connect ((*s), (struct sockaddr *)&sin, sizeof(sin) ) < 0 ){
            (*error) = E_CON;
            return EXIT_FAILURE;
  }
    return EXIT_SUCCESS;
}


int sendData(int s, int *error, tUrl *myUrl) {
        string msg_send;


        msg_send.append("GET ");
        msg_send.append("/");
        msg_send.append(" HTTP/1.1\r\n");
        msg_send.append("Host: ");
        msg_send.append(myUrl->server);
        msg_send.append("\r\n");
        msg_send.append("Connection: close\r\n");
        msg_send.append("\r\n");

        if ( write(s, msg_send.c_str(), msg_send.length()) < 0 ) {  /* send message to server */
            (*error) = E_WRITE;
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

int toPrintOrNot(char* string, tParams* myParams, int* error) {

    regmatch_t pmatch[2];
    regex_t re;
    int status;
    char pattern1[] = "^(Content-Length:)(.*)$";
    char pattern2[] = "^(Server:)(.*)$";
    char pattern3[] = "^(Last-Modified:)(.*)$";
    char pattern4[] = "^(Content-Type:)(.*)$";

    if(myParams->lSize > 0) {
          if((regcomp( &re, pattern1, REG_EXTENDED)) != 0) {
                  printf("n6avratova hodnota: %d\n", status);
                  (*error) = E_REGCOMP;
                  return EXIT_FAILURE;
          }
          status = regexec( &re, string, 2, pmatch, 0);
          if(status == 0){
              myParams->responses[0].append(string, 0, strlen(string));
              myParams->lSize = myParams->lSize*(-1);
              return S_OK;
          }
    }
    if(myParams->serverID > 0) {
        if((regcomp( &re, pattern2, REG_EXTENDED)) != 0) {
                printf("n7avratova hodnota: %d\n", status);
                (*error) = E_REGCOMP;
                return EXIT_FAILURE;
        }
        status = regexec( &re, string, 2, pmatch, 0);
        if(status == 0){
            myParams->responses[1].append(string, 0, strlen(string));
            myParams->serverID = myParams->serverID*(-1);
            return S_OK;
        }
    }
    if(myParams->lastObj > 0) {
        if((regcomp( &re, pattern3, REG_EXTENDED)) != 0) {
                printf("n8avratova hodnota: %d\n", status);
                (*error) = E_REGCOMP;
                return EXIT_FAILURE;
        }
        status = regexec( &re, string, 2, pmatch, 0);
        if(status == 0){
            myParams->responses[2].append(string, 0, strlen(string));
            myParams->lastObj = myParams->lastObj*(-1);
            return S_OK;
        }
    }
    if(myParams->typeObj > 0) {
          if((regcomp( &re, pattern4, REG_EXTENDED)) != 0) {
                  printf("n9avratova hodnota: %d\n", status);
                  (*error) = E_REGCOMP;
                  return EXIT_FAILURE;
          }
          status = regexec( &re, string, 2, pmatch, 0);
          if(status == 0){
              myParams->responses[3].append(string, 0, strlen(string));
              myParams->typeObj = myParams->typeObj*(-1);
              return S_OK;
          }
    }

    return S_OK;
}

int lookForLocation(char* string, tParams* myParams, int* error) {
    regmatch_t pmatch[3];
    regex_t re;
    int status;
    char pattern[] = "^(Location:) (.*)(\r.*)$";

    if((regcomp( &re, pattern, REG_EXTENDED)) != 0) {
              printf("n10avratova hodnota: %d\n", status);
              (*error) = E_REGCOMP;
              return EXIT_FAILURE;
      }
      status = regexec( &re, string, 3, pmatch, 0);
      if(status == 0){
          if((pmatch[2].rm_eo - pmatch[2].rm_so) != 0) {
              myParams->url.clear();
              myParams->url.append(string, pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);
          }
          return S_NEWLOC;
      } else {
          return S_REDIR;
      }

      return S_REDIR;
}


int isRedirection(char* string, int* error) {
  regmatch_t pmatch[3];
  regex_t re;
  int status;

    // patterns for each type of HTTP status
  char pattern1[] = "^.*HTTP.*(1[0-9][0-9]).*?$";
  char pattern2[] = "^.*2[0-9][0-9].*$";
  char pattern3[] = "^.*3[0-9][0-9].*$";
  char pattern4[] = "^.*4[0-9][0-9].*$";
  char pattern5[] = "^.*5[0-9][0-9].*$";

   if((regcomp( &re, pattern2, REG_EXTENDED)) != 0) {
            printf("n2avratova hodnota: %d\n", status);
            (*error) = E_REGCOMP;
            return EXIT_FAILURE;
    }
    status = regexec( &re, string, 1, pmatch, 0);
    if(status == 0){
        return S_OK;
    }


   if((regcomp( &re, pattern3, REG_EXTENDED)) != 0) {
            printf("n3avratova hodnota: %d\n", status);
            (*error) = E_REGCOMP;
            return EXIT_FAILURE;
    }
    status = regexec( &re, string, 1, pmatch, 0);
    if(status == 0){
        return S_REDIR;
    }


    if((regcomp( &re, pattern4, REG_EXTENDED)) != 0){
            printf("n4avratova hodnota: %d\n", status);
            (*error) = E_REGCOMP;
            return EXIT_FAILURE;
    }
    status = regexec( &re, string, 1, pmatch, 0);
    if(status == 0){
        return S_FAIL4;
    }


    if((regcomp( &re, pattern5, REG_EXTENDED)) != 0)  {
            printf("n5avratova hodnota: %d\n", status);
            (*error) = E_REGCOMP;
            return EXIT_FAILURE;
    }
    status = regexec( &re, string, 1, pmatch, 0);
    if(status == 0){
        return S_FAIL5;
    }
return S_OK;
}

int readResponse(int s, tUrl *myUrl, tParams *myParams, int *error) {
    int n, prev, state;
    int cnt = 0;
    char ch;
    char line[250] = {0};
    int goOn = 1;

    while(goOn > 0) {
            if ( ( n = read(s, &ch, 1 ) ) <0) {  /* read message from server */
                (*error) = E_READ;
                return EXIT_FAILURE;
            }
            line[cnt] = ch;
            cnt = cnt + 1;
            if(isspace(ch) && prev == 1) {
                goOn = 0;
            }
            if(ch == '\n') {
                prev = 1;
                line[cnt] = '\0';
                if(goOn == 1) { // check HTTP status on 1st line
                      state = isRedirection(line, error);
                      if((*error)) {
                          return EXIT_FAILURE;
                      }
                      // if REDIRECTION was found
                      if(state == S_REDIR)
                          myUrl->redirection = myUrl->redirection+1;
                      else
                          myUrl->redirection = 0;
                }  else {   // all the other lines to process

                        // in the case of REDIRECTION - look for "Location:" line... ant then parse new URI
                      if(state == S_REDIR) {
                            state = lookForLocation(line, myParams, error);
                            if((*error)) {
                                return EXIT_FAILURE;
                            }
                            // new Location was found, break and connect to new address
                      } else if (state == S_NEWLOC) {
                          break;

                            // some error was found, show it on stderr and finish...
                      } else if (state == S_FAIL4) {
                          (*error) = E_ERROR4;
                          return EXIT_FAILURE;

                      } else if (state == S_FAIL4) {
                          (*error) = E_ERROR5;
                          return EXIT_FAILURE;

                      } else { // if no setting was set -> print all the lines imediatally
                          ;
                      }
                }
                if(myParams->all > 0) {
                    printf("%s", line);
                }
                else {
                    printf("tisk?\n");
                    toPrintOrNot(line, myParams, error);
                }
                cnt = 0;
                line[cnt] = '\0';
                goOn++; // line counter
            } else {
                prev = 0;
            }
    }
    return EXIT_SUCCESS;
}


int parseURI(tUrl* myUrl, char const* string, int* error) {

    regex_t re;
    regmatch_t pmatch[5];

    int status;
    int x = 0;
    char buf[10];
    int fail = 0;

    char* cp_string = new char[strlen(string) + 1];    // make a copy
    strcpy(cp_string, string);                         //   of s1...

    // pattern represents valid URL characters

    char pattern[] = "^([A-z]+://)?([a-zA-Z.0-9%_-]+)(:[0-9]+)?(/[a-zA-Z0-9,._+/&~=%\?-]*)?";

    if((status = regcomp( &re, pattern, REG_EXTENDED)) != 0) {
            (*error) = E_REGCOMP;
            return EXIT_FAILURE;
    }
    printf("Retezec(%d): %s\n", status, cp_string);

    // parse each part of the URI - domain, protocol, path
    status = regexec( &re, cp_string, 5, pmatch, 0);
    if(status == 0){
    	   if((pmatch[1].rm_eo - pmatch[1].rm_so) != 0)  {

            		myUrl->server.append(cp_string, pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so - 3);
            		while(myUrl->protocol[x]) {
            		    myUrl->protocol[x] = tolower(myUrl->protocol[x]);
            		    x = x++;
        		    }
                    x = 0;
                    printf("PROT: %s\n", myUrl->protocol.c_str());
            		if(strcmp(myUrl->protocol.c_str(), "http") != 0) {
              		    (*error) = E_PROTOCOL;
              		    return EXIT_FAILURE;
            		}
        	  } else {
        		    myUrl->protocol.append("http");

        	  }
                myUrl->server.append(cp_string, pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);


            if((pmatch[3].rm_eo - pmatch[3].rm_so) != 0)  {
                    myStrCpy(buf, cp_string, pmatch[3].rm_so+1, pmatch[3].rm_eo-pmatch[3].rm_so);
                    printf("BUF:%s\n", buf);
                    myUrl->port = atoi(buf);
      	    } else {
      		        myUrl->port = 80;

      	    }

      	    if((pmatch[4].rm_eo - pmatch[4].rm_so) != 0)  {
                    myUrl->path.append(cp_string, pmatch[4].rm_so, pmatch[4].rm_eo - pmatch[4].rm_so - 3);

      	    } else {
      		      myUrl->path.append("/");

    	     }
    } else {
        	 (*error) = E_REGADR;
        	 return EXIT_FAILURE;
    }

    printf("Protocol: %s\n", myUrl->protocol.c_str());
    printf("Server: %s\n", myUrl->server.c_str());
    printf("Path: %s\n", myUrl->path.c_str());
    printf("Port %d\n", myUrl->port);

    return EXIT_SUCCESS;
}


int main(int argc, char** argv) {
    int s, q;
    int loop = 0;
    tUrl myUrl;
    tParams myParams;
    myParams.all = 1;
    int error = E_OK;

    if(processArgs(argc, argv, &myParams)) {
        showError(E_ARG);
        return EXIT_FAILURE;
    }

  //myParams.url.append(argv[argc-2]);
  printf("URI: %s\n",myParams.url.c_str());

    do {
        myUrl.path.clear();
        myUrl.server.clear();
        myUrl.protocol.clear();

        if((parseURI(&myUrl, myParams.url.c_str(), &error)) == EXIT_FAILURE) {
            showError(error);
            return EXIT_FAILURE;
        }


        if(connectToServer(&s, &myUrl, &error) == EXIT_FAILURE) {
            showError(error);
            return EXIT_FAILURE;
        }

        if(sendData(s, &error, &myUrl) == EXIT_FAILURE) {
            showError(error);
            return EXIT_FAILURE;
        }

        if(readResponse(s, &myUrl, &myParams, &error) == EXIT_FAILURE) {
            showError(error);
            return EXIT_FAILURE;

        }
         if(myUrl.redirection == 0) {
            if(myParams.all == 0) {
                printf("tiskl bych...\n");
                    for(q = 0; q > -5; q--) {
                        if(myParams.lSize == q) {
                            printf("%s", myParams.responses[0].c_str());
                        }
                        if(myParams.serverID == q) {
                            printf("%s", myParams.responses[1].c_str());
                        }
                        if(myParams.lastObj == q) {
                            printf("%s", myParams.responses[2].c_str());
                        }
                        if(myParams.typeObj == q) {
                            printf("%s", myParams.responses[3].c_str());
                        }
                    }
            }
            break;
        }
        else {
            loop++;
        }
    } while(loop <= 5);

  if (close(s) < 0) {
    showError(E_CLOSE);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
