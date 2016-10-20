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
#include <pthread.h>
#include <sys/time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024
#define CONF_SIZE 40
#define CONFIG_PATH "./ws.conf"


//throws error
#define on_error(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(1); }

typedef struct{
    char ext[CONF_SIZE];            //eg. .html
    char desc[CONF_SIZE];           //description eg. text/html
}ContentType;

typedef struct{
    int port;                                   //port number for server
    char root[BUFFER_SIZE];                     //root folder of public files
    ContentType types[BUFFER_SIZE];             //different content types handled by server
    char defaultPage[CONF_SIZE][CONF_SIZE];     //default webpage - stored as an array with heirarchy
    int numDefault;                             //number of default page options
    int timeout;                                //timeout in seconds for pipelining requests
    int numTypes;                               //number of types handled by the server
}Config;

typedef struct{
    char method[10];                            //GET,PUT,ETC
    char URI[BUFFER_SIZE];                      //file name
    char version[BUFFER_SIZE];                  // HTTP/1.1
    int keepAlive;                              // 1 if Connection: Keep-Alive specified
}Request;

typedef struct{
    char version[BUFFER_SIZE];
    int status;
    bool keepAlive;
    bool file;                                  //true if response is a file, false if it is custom html - as these two cases are handled differently
    char filepath[BUFFER_SIZE];                 //contains the full file path of the response - eg. combines the document root with the request URI
    char customHTML[BUFFER_SIZE];               //contains a string of HTML which will be sent
    ContentType type;
}Response;

//passed as arguments to threads
typedef struct{
    Config config;
    int client_fd;
}ThreadArgs;

//convert string to lowercase
void strToLower(char *str){
	int i=0;
	while(str[i] != '\n' && str[i] != '\r' && str[i] != EOF){
		str[i] = tolower(str[i]);
		i++;
	}
}

//removes \t or \r\n from a string
void strip(char *s) {
    char *p2 = s;
    while(*s != '\0') {
    	if(*s != '\t' && *s != '\n' && *s != '\r') {
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
    strip(path);        //ensure path doesnt contain newline or tab (\r\n or \t)
	if(access( path, F_OK ) != -1 ) {
    	// file exists
		return true;
	} else {
    	// file doesn't exist
		return false;
	}
}

//gets filename extension - found this function on stackoverflow
const char *getFileExtension(const char *fspec) {
    char *e = strrchr(fspec, '.');
    if (e == NULL)
        e = "";
    return e;
}

//takes a filename extension such as .html and returns the description ie. 'text/html'
//which is found in the config struct
//if the file type isnt handled by the server -> "" will be returned
const char *getExtDescription(Response res, Config c) {
    char *desc = malloc(BUFFER_SIZE);
    for(int i=0;i<c.numTypes;i++){
        //check if the extension is equal to any of those specified in config
        if(strcmp(c.types[i].ext,res.type.ext) == 0){
            strcpy(desc,c.types[i].desc);
            return desc;
        }
    }
    desc = "";
    return desc;
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
    on_error("Could not find config file\r\n");

    int dir_ind = 0, type_ind = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        if(line[0] != '#' && read > 0){         //check if empty line or comment
            if(line[0] == '.'){
                char *spl = strtok(line," ");
                strcpy(config.types[type_ind].ext, spl);
                spl = strtok(NULL," ");
                strcpy(config.types[type_ind++].desc, spl);
            }else{
                char * spl = strtok (line," ");
                if(strcmp(spl,"Listen") == 0){
                    spl = strtok(NULL," ");
                    config.port = atoi(spl);
                }else if(strcmp(spl,"DocumentRoot") == 0){
                    spl = strtok(NULL," ");
                    strcpy(config.root,spl);
                }else if(strcmp(spl,"Timeout") == 0){
                    spl = strtok(NULL," ");
                    config.timeout = atoi(spl);
                    printf("Timeout: %i",config.timeout);
                }else if(strcmp(spl,"DirectoryIndex") == 0){
                    spl = strtok(NULL, " ");
                    while(spl != NULL ){
                        strcpy(config.defaultPage[dir_ind++], spl);
                        spl = strtok(NULL, " ");
                    }
                }
            }
        }
    }
    config.numDefault = dir_ind;
    config.numTypes = type_ind;
    fclose(fp);
    if (line)
    free(line);
    return config;
}

char *statusString(Response res){
    char *string = malloc(BUFFER_SIZE);
    switch (res.status) {
        case 200:
            strcpy(string,"200 OK");
            break;
        case 400:
            strcpy(string,"400 Bad Request");
            break;
        case 404:
            strcpy(string,"404 Not Found");
            break;
        case 500:
            strcpy(string,"500 Internal Server Error");
        case 501:
            strcpy(string,"501 Not Implemented");
    }
    return string;
}

char *contentLength(Response res){
    char *string = malloc(BUFFER_SIZE);
    strcpy(string, "Content-Length: ");
    char length[BUFFER_SIZE];
    sprintf(length, "%lu", strlen(res.customHTML));
    strcat(string, length);
    strcat(string, "\r\n");
    return string;
}

char *fileLength(int lengthOfFile){
    char *string = malloc(BUFFER_SIZE);
    strcpy(string, "Content-Length: ");
    char length[BUFFER_SIZE];
    sprintf(length, "%d", lengthOfFile);
    strcat(string, length);
    strcat(string, "\r\n");
    return string;
}

int sendFileResponse(Response res, int client_fd){
    FILE *fp = fopen(res.filepath, "rb");

    if(fp == NULL){
        on_error("Error opening file %s\r\n",res.filepath);
    }
    printf("File opened!\r\n");
    fseek(fp, 0, SEEK_END);
    int lengthOfFile = ftell(fp);
    printf("File length: %i\r\n",lengthOfFile);
    rewind(fp);

    char *resString = malloc(BUFFER_SIZE);

    char header[BUFFER_SIZE];
    strcpy(header,res.version);              //add version to resString
    strcat(header," ");
    strcat(header,statusString(res));        //add status code and message
    strcat(header,"\r\n");
    if(res.keepAlive){
        strcat(header,"Connection: Keep-Alive\r\n");
    }else{
        strcat(resString,"Connection: Close\r\n");
    }
    strcat(header,"Content-Type: ");
    strcat(header,res.type.desc);
    strcat(header,fileLength(lengthOfFile));

    strcat(header,"\r\n");

    printf("%s\r\n",header);

    strcpy(resString,header);

    int buf = BUFFER_SIZE - strlen(resString);
    char *content = malloc(buf);
    int toRead = lengthOfFile;
    fread(content, 1, buf, fp);                             //read first 'buf' lines of content
    memcpy(resString+strlen(header), content, buf);         //memcpy used instead of strcpy so that binary files are handled
    int err = send(client_fd, resString, BUFFER_SIZE, 0);
    while(!feof(fp)){
        memset(resString,0,BUFFER_SIZE);
        fread(resString, 1, BUFFER_SIZE, fp);
        err = send(client_fd, resString, BUFFER_SIZE, 0);
    }
    printf("SENT!\r\n");

    fclose(fp);
    return err;
}

int sendContentResponse(Response res, int client_fd){
    char resString[BUFFER_SIZE];
    strcpy(resString,res.version);       //add version to resString
    strcat(resString," ");
    strcat(resString,statusString(res));        //add status code and message
    strcat(resString,"\r\n");
    if(res.keepAlive){
        strcat(resString,"Connection: Keep-Alive\r\n");
    }else{
        strcat(resString,"Connection: Close\r\n");
    }
    strcat(resString,"Content-Type: text/html\r\n");
    strcat(resString,contentLength(res));
    strcat(resString,"\r\n");
    strcat(resString,res.customHTML);
    return send(client_fd, resString, BUFFER_SIZE, 0);
}

int sendResponse(Response res, int client_fd){
    if(res.file) return sendFileResponse(res,client_fd);
    else return sendContentResponse(res,client_fd);
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
    spl = strtok(NULL, " "); //get the URI
    strcpy(r.URI, spl);
    strToLower(httpString);
    if(strstr(httpString,"connection: keep-alive") != NULL){
        r.keepAlive = 1;
    }
    if(strstr(httpString,"http/1.0") != NULL){
        strcpy(r.version,"HTTP/1.0");
    }else{
        strcpy(r.version,"HTTP/1.1");
    }

    return r;
}


Response makeResponse(Request req, Config config){
    Response res;
    strcpy(res.version,req.version);        //copy version from the request
    res.file = false;   //set default

    char content[BUFFER_SIZE];

    if(strcmp(req.method,"GET") == 0){
        if(strcmp(req.URI,"/") == 0){
            for(int i = 0;i<config.numDefault;i++){
                char def[BUFFER_SIZE];
                strcpy(def,req.URI);
                strcat(def,config.defaultPage[i]);
                if(fileExists(def,config)){
                    strcat(req.URI,config.defaultPage[i]);
                    break;
                }
            }
        }
        if(fileExists(req.URI,config)){
            strcpy(res.filepath, config.root);
            strcat(res.filepath, req.URI);
            strip(res.filepath);                //remove \r\n and \t from filepath
            printf("path: %s",res.filepath);

            strcpy(res.type.ext,getFileExtension(res.filepath));    //get the file extension from filepath
            strToLower(res.type.ext);                               //convert to lowercase
            strcpy(res.type.desc,getExtDescription(res,config));
            printf("Ext: %s\r\nDesc: %s\r\n",res.type.ext,res.type.desc);
            if(strcmp(res.type.desc,"") != 0){
                res.status = 200;
                res.file = true;
            }else{
                res.status = 501;

                strcpy(content,"<html><body><h1>501	Not	Implemented</h1>Files with the \r\n");
                strcat(content,res.type.ext);
                strcat(content," extension are not handled by the server :(</body></html>");

                strcpy(res.customHTML,content);
            }

        }else{
            res.status = 404;
            strcpy(content,"<html><body><h1>404 Not Found</h1>");
            strcat(content,req.URI);
            strcat(content," could not be found</body></html>");

            strcpy(res.customHTML,content);
        }
    }else if(
        strcmp(req.method,"HEAD") == 0 ||
        strcmp(req.method,"POST") == 0 ||
        strcmp(req.method,"PUT") == 0 ||
        strcmp(req.method,"DELETE") == 0 ||
        strcmp(req.method,"CONNECT") == 0 ||
        strcmp(req.method,"OPTIONS") == 0 ||
        strcmp(req.method,"TRACE") == 0){

        res.status = 501;

        strcpy(content,"<html><body><h1>501	Not	Implemented</h1>\r\n");
        strcat(content,req.method);
        strcat(content," method not implemented by the server :(</body></html>");

        strcpy(res.customHTML,content);
    }else{
        res.status = 400;
        strcpy(content,"<html><body><h1>400 Bad Request</h1>");
        strcat(content,req.method);
        strcat(content," is not a valid HTTP method</body></html>");

        strcpy(res.customHTML,content);
    }

    return res;
}

//thread function for handling a socket
void *socketThread(void *args){
    ThreadArgs *realArgs = args;
    char buf[BUFFER_SIZE];
    for(;;) {
        fd_set rfds;
        /* Wait up to five seconds. */
        struct timeval tv;
        int client_fd = realArgs->client_fd;
        int n = client_fd+1;
        int retval;
        /* Watch stdin (fd client_fd) to see when it has input. */
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        tv.tv_sec = realArgs->config.timeout;
        tv.tv_usec = 0;
        //printf("retval: %i\r\n",retval);
        retval = select(n, &rfds, NULL, NULL, &tv);

        if (retval == -1){
            on_error("Error on select()\r\n");
        }else if(retval > 0){
            int read = recv(realArgs->client_fd, buf, BUFFER_SIZE, 0);
            if (!read) break; // done reading
            if (read < 0) on_error("Client read failed\r\n");
            printf("%s\r\n",buf);
            Request req = parseRequest(buf);
            Response res = makeResponse(req, realArgs->config);

            int err = sendResponse(res,realArgs->client_fd);
            if (err < 0) on_error("Client write failed\r\n");
            //if keep-alive connection wasnt specified then close the socket
            if(!res.keepAlive){
                printf("Connection: Close\r\n");
                free(realArgs);
                return 0;
            }
        }else{
            printf("Timeout!\r\n");
            free(realArgs);
            return 0;
        }
        printf("keeping connection alive!\r\n");
    }
    free(realArgs);
    return 0;
}

int main (int argc, char *argv[]) {
    Config config = setServerConfig();
    validateConfig(config);

    int server_fd, client_fd, err;
    struct sockaddr_in server, client;
    char buf[BUFFER_SIZE];
    char res[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) on_error("Could not create socket\r\n");

    server.sin_family = AF_INET;
    server.sin_port = htons(config.port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    int opt_val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);

    err = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
    if (err < 0) on_error("Could not bind socket\r\n");

    err = listen(server_fd, 128);
    if (err < 0) on_error("Could not listen on socket\r\n");

    printf("Server is listening on %d\r\n\r\n", config.port);

    while (1) {
        socklen_t client_len = sizeof(client);
        client_fd = accept(server_fd, (struct sockaddr *) &client, &client_len);

        if (client_fd < 0) on_error("Could not establish new connection\r\n");
        pthread_t tid;
        //create pointer that stores thread arguments - we need the server config struct and the client_fd
        ThreadArgs *args = malloc(sizeof *args);
        args->config = config;
        args->client_fd = client_fd;
        //create a new thread
        if(pthread_create(&tid, NULL, socketThread,args)){
            //free the memory associated with args variable before terminating
            free(args);
        }
    }

    return 0;
}
