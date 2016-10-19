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
    char desc[CONF_SIZE];           //description eg. text/html
}ContentType;

typedef struct{
    int port;                                   //port number for server
    char root[BUFFER_SIZE];                     //root folder of public files
    ContentType types[BUFFER_SIZE];             //different content types handled by server
    char defaultPage[CONF_SIZE][CONF_SIZE];     //default webpage - stored as an array with heirarchy
    int numDefault;                             //number of default page options
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
                strcpy(config.types[type_ind++].desc, spl);
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
    strcat(string, "\n");
    return string;
}

int sendFileResponse(Response res, int client_fd){
    return send(client_fd, res.filepath, BUFFER_SIZE, 0);
}

int sendContentResponse(Response res, int client_fd){
    char resString[BUFFER_SIZE];
    strcpy(resString,res.version);       //add version to resString
    strcat(resString," ");
    strcat(resString,statusString(res));        //add status code and message
    strcat(resString,"\n");
    if(res.keepAlive){
        strcat(resString,"Connection: Keep-Alive\n");
    }
    strcat(resString,"Content-Type: text/html\n");
    strcat(resString,contentLength(res));
    strcat(resString,"\n");
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
    spl = strtok(NULL, " "); //get the version
    strcpy(r.version, spl);
    strToLower(httpString);
    if(strstr(httpString,"connection: keep-alive") != NULL){
        r.keepAlive = 1;
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
                if(fileExists(config.defaultPage[i],config)){
                    strcat(req.URI,config.defaultPage[i]);
                }
            }
        }
        if(fileExists(req.URI,config)){
            strcpy(res.filepath, config.root);
            strcpy(res.filepath, req.URI);
            strip(res.filepath);                //remove \n and \t from filepath

            strcpy(res.type.ext,getFileExtension(res.filepath));    //get the file extension from filepath
            strToLower(res.type.ext);                               //convert to lowercase
            strcpy(res.type.desc,getExtDescription(res,config));
            printf("Ext: %s\nDesc: %s\n",res.type.ext,res.type.desc);
            if(strcmp(res.type.desc,"") != 0){
                res.status = 200;
                res.file = true;
            }else{
                res.status = 501;

                strcpy(content,"<html><body><h1>501	Not	Implemented</h1>Files with the \n");
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

        strcpy(content,"<html><body><h1>501	Not	Implemented</h1>\n");
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
            Response res = makeResponse(req, config);

            err = sendResponse(res,client_fd);
            if (err < 0) on_error("Client write failed\n");

        }
    }

    return 0;
}
