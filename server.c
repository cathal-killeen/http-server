#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define CONF_SIZE 40
#define CONFIG_PATH "./ws.conf"
#define on_error(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(1); }

typedef struct{
    char ext[CONF_SIZE];            //eg. .html
    char type[CONF_SIZE];   //eg. text/html
}ContentType;

typedef struct{
    int port;                               //port number for server
    char root[BUFFER_SIZE];                   //root folder of public files
    ContentType types[CONF_SIZE];           //different content types handled by server
    char defaultPage[CONF_SIZE][CONF_SIZE]; //default webpage - stored as an array with heirarchy
    int numDefault;
}Config;

typedef struct{
    char method[10];                        //GET,PUT,ETC
    char filename[BUFFER_SIZE];             //file name
    char version[BUFFER_SIZE];              // HTTP/1.1
    int keepAlive;                          // 1 if Connection: Keep-Alive specified
}Request;

//convert string to lowercase
void strToLower(char *str){
	int i=0;
	while(str[i] != '\n' && str[i] != EOF){
		str[i] = tolower(str[i]);
		i++;
	}
}

//removes \t or \n from a string
void strip(char *s) {
    char *p2 = s;
    while(*s != '\0') {
    	if(*s != '\t' && *s != '\n') {
    		*p2++ = *s++;
    	} else {
    		++s;
    	}
    }
    *p2 = '\0';
}

//checks to see if requested file exists
bool fileExists(char *fn, Config c){
    char path[BUFFER_SIZE];
    strcpy(path,c.root);
    strcat(path,fn);
    strip(path);        //ensure path doesnt contain newline or tab (\n or \t)
    printf("path: %s\n",path);
	if(access( path, F_OK ) != -1 ) {
    	// file exists
		return true;
	} else {
    	// file doesn't exist
		return false;
	}
}

//parses the ws.conf file and stores values in Config struct
Config setServerConfig(){
    Config config;
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen(CONFIG_PATH, "r");
    if (fp == NULL)
    on_error("Could not find config file\n");

    int dir_ind = 0, type_ind = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        if(line[0] != '#' && read > 0){         //check if empty line or comment
            printf("%s\n",line);
            if(line[0] == '.'){
                char *spl = strtok(line," ");
                printf("ext = %s\n",spl);
                strcpy(config.types[type_ind].ext, spl);
                spl = strtok(NULL," ");
                printf("%s\n",spl);
                strcpy(config.types[type_ind++].type, spl);
            }else{
                char * spl = strtok (line," ");
                printf("%s\n",spl);
                if(strcmp(spl,"Listen") == 0){
                    spl = strtok(NULL," ");
                    printf("%s",spl);
                    config.port = atoi(spl);
                }else if(strcmp(spl,"DocumentRoot") == 0){
                    spl = strtok(NULL," ");
                    printf("%s",spl);
                    strcpy(config.root,spl);
                }else if(strcmp(spl,"DirectoryIndex") == 0){
                    spl = strtok(NULL, " ");
                    while(spl != NULL ){
                        strcpy(config.defaultPage[dir_ind++], spl);
                        printf("%s\n",spl);
                        spl = strtok(NULL, " ");
                    }
                    config.numDefault = dir_ind;
                }
            }
        }
    }
    fclose(fp);
    if (line)
    free(line);
    return config;
}

//checks if configuration is valid - will terminate if values are missing or invalid
void validateConfig(Config c){
    //port
    if(!c.port)
        on_error("Port number must be defined in config file");
    if(c.port < 1024)
        on_error("Port numbers below 1024 are not allowed. Please update in config file");
    //root
    if(c.root[0] == '\0')
        on_error("DocumentRoot must be specified in config");
    //DirectoryIndex
    if(c.defaultPage[0][0] == '\0')
        on_error("DirectoryIndex must be specified in config");
}

Request parseRequest(char* httpString){
    Request r;
    r.keepAlive = 0;
    char* spl = NULL;
    spl = strtok(httpString, " "); //get the method
    strcpy(r.method, spl);
    spl = strtok(NULL, " "); //get the filename
    strcpy(r.filename, spl);
    spl = strtok(NULL, " "); //get the version
    strcpy(r.version, spl);
    strToLower(httpString);
    if(strstr(httpString,"connection: keep-alive") != NULL){
        r.keepAlive = 1;
    }
    return r;
}

int main (int argc, char *argv[]) {
    Config config = setServerConfig();
    validateConfig(config);

    int server_fd, client_fd, err;
    struct sockaddr_in server, client;
    char buf[BUFFER_SIZE];
    char res[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) on_error("Could not create socket\n");

    server.sin_family = AF_INET;
    server.sin_port = htons(config.port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    int opt_val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);

    err = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
    if (err < 0) on_error("Could not bind socket\n");

    err = listen(server_fd, 128);
    if (err < 0) on_error("Could not listen on socket\n");

    printf("Server is listening on %d\n", config.port);

    while (1) {
        socklen_t client_len = sizeof(client);
        client_fd = accept(server_fd, (struct sockaddr *) &client, &client_len);

        if (client_fd < 0) on_error("Could not establish new connection\n");

        while (1) {
            int read = recv(client_fd, buf, BUFFER_SIZE, 0);
            if (!read) break; // done reading
            if (read < 0) on_error("Client read failed\n");
            printf("%s\n",buf);
            Request req = parseRequest(buf);
            char content[BUFFER_SIZE];

            if(strcmp(req.method,"GET") == 0){
                printf("Filename: %s",req.filename);
                if(strcmp(req.filename,"/")){
                    for(int i = 0;i<config.numDefault;i++){
                        if(fileExists(config.defaultPage[i],config)){
                            strcat(req.filename,config.defaultPage[i]);
                        }
                    }
                }
                if(fileExists(req.filename,config)){
                    printf("FILE EXISTS!");
                }else{
                    strcpy(content,"<html><body><h1>404 Not Found</h1>");
                    strcat(content,req.filename);
                    strcat(content," could not be found</body></html>");
                    strcpy(res,req.version);
                    strcat(res," 404 Not Found\n");
                    strcat(res,"Content-Length: ");
                    char contentLength[BUFFER_SIZE];
                    sprintf(contentLength, "%lu", strlen(content));
                    strcat(res,contentLength);
                    strcat(res,"\nContent-Type: text/html\n\n");
                    strcat(res,content);
                    err = send(client_fd, res, read, 0);
                    if (err < 0) on_error("Client write failed\n");
                }
            }else if(
                strcmp(req.method,"HEAD") == 0 ||
                strcmp(req.method,"POST") == 0 ||
                strcmp(req.method,"PUT") == 0 ||
                strcmp(req.method,"DELETE") == 0 ||
                strcmp(req.method,"CONNECT") == 0 ||
                strcmp(req.method,"OPTIONS") == 0 ||
                strcmp(req.method,"TRACE") == 0){

                strcpy(content,"<html><body><h1>501	Not	Implemented</h1>\n");
                strcat(content,req.method);
                strcat(content," method not implemented by the server :(</body></html>");
                strcpy(res,req.version);
                strcat(res," 501 Not Implemented\n");
                strcat(res,"Content-Length: ");
                char contentLength[BUFFER_SIZE];
                sprintf(contentLength, "%lu", strlen(content));
                strcat(res,contentLength);
                strcat(res,"\nContent-Type: text/html\n\n");
                strcat(res,content);
                err = send(client_fd, res, read, 0);
                if (err < 0) on_error("Client write failed\n");
            }else{
                strcpy(content,"<html><body><h1>400 Bad Request</h1>");
                strcat(content,req.method);
                strcat(content," is not a valid HTTP method</body></html>");
                strcpy(res,req.version);
                strcat(res," 400 Bad Request\n");
                strcat(res,"Content-Length: ");
                char contentLength[BUFFER_SIZE];
                sprintf(contentLength, "%lu", strlen(content));
                strcat(res,contentLength);
                strcat(res,"\nContent-Type: text/html\n\n");
                strcat(res,content);
                err = send(client_fd, res, read, 0);
                if (err < 0) on_error("Client write failed\n");
            }
        }
    }

    return 0;
}
