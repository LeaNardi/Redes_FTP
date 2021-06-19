#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>

//Auxiliar libraries
#include<ctype.h>

#define BUFSIZE 512

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code; //recv_s size of recv

    // receive the answer
    recv_s = read(sd, buffer, BUFSIZE);

    // error checking
    if (recv_s < 0) warn("Error receiving data");
    if (recv_s == 0) errx(7, "Connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    write(sd, buffer, strlen(buffer)+1);
    return;
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "USER", input);

    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    if(!recv_msg(sd, 331, NULL))
        errx(8, "Invalid User");

    // ask for password
    printf("password: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "PASS", input);

    // release memory
    free(input);

    // wait for answer and process it and check for errors
    if(recv_msg(sd, 530, NULL))
        errx(9, "Incorrect login");
    return;
}


bool port(int sd, char* ip, int puerto){
    char buffer[BUFSIZE];
    int i;
    char *port_param, *aux;
    port_param = (char*)malloc(30*sizeof(char));
    aux = (char*)malloc(8*sizeof(char));

    strcpy(port_param, ip);

    i=0;
    while(*(port_param+i) !='\0'){
        if (*(port_param+i) == '.') *(port_param+i) = ',';
        i++;
    }

    sprintf(aux, ",%d,%d",puerto/256, puerto%256);
    strcat(port_param, aux);

    send_msg(sd, "PORT", port_param);

    free(port_param);
    free(aux);

    // check for the response and return
    return recv_msg(sd, 200, buffer);
}





/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char buffer[BUFSIZE];
    long f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    int dsd, dsda;// data channel socket
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto;
    char *ip;
    ip = (char*)malloc(13*sizeof(char));

    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

      // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(2, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
    if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);
    // check for the response
    if(!recv_msg(sd, 299, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %ld bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");

    //receive the file
    while(true) {
       if (f_size < BUFSIZE) r_size = f_size;
       recv_s = read(dsda, buffer, r_size);
       if(recv_s < 0) warn("receive error");
       fwrite(buffer, 1, r_size, file);
       if (f_size < BUFSIZE) break;
       f_size = f_size - BUFSIZE;
    }

    // close data channel
    close(dsda);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;
}
/**
 * function put
 **/

 void put(int sd, char *file_name) {
    char buffer[BUFSIZE];
    long f_size;
    FILE *file;
    int dsd, dsda;// data channel socket
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto, bread;
    char *ip;
    char *file_data, *file_size;
    file_data = (char*)malloc(50*sizeof(char));
    file_size = (char*)malloc(25*sizeof(char));

    // check if file exists if not inform error to client
    file = fopen(file_name, "r");
    if (file == NULL){
        printf("El archivo no existe.\n");
        return;
    }

    //file length
    fseek(file, 0L, SEEK_END);
    f_size = ftell(file);
    rewind(file);
    sprintf(file_size, "//%ld",f_size);


    ip = (char*)malloc(13*sizeof(char));
    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

    // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(1, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
    if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    file_data=strcat(file_name,file_size);
    // send the STOR command to the server
    send_msg(sd, "STOR", file_data);
    // check for the response
    if(!recv_msg(sd, 150, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // send the file
    while(!feof(file)) {
        bread = fread(buffer, 1, BUFSIZE, file);
        if (write(dsda, buffer, bread) < 0) warn("Error sending data");
    }

    // close data channel
    close(dsda);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;
}


/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd, "QUIT", NULL);
    // receive the answer from the server
    if(!recv_msg(sd, 221, NULL))
        errx(10, "Incorrect logout");
    return;
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "put") == 0) {
            param = strtok(NULL, " ");
            put(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
    }
    free(input);
}


//Auxiliar functions
bool direccion_IP(char *string){
    char *token;
    bool verificacion = true;
    int contador=0,i;
    token = (char *) malloc(strlen(string)*sizeof(char));
    strcpy(token, string);
    token = strtok(token,".");

    while(token!=NULL){
        contador++;
        i=0;
        while(*(token+i)!='\0'){
            if(!isdigit(*(token+i))) verificacion = false;
            i++;
        }
        if(atoi(token)<0||atoi(token)>255) verificacion = false;
        token=strtok(NULL,".");
    }
    if(contador!=4) verificacion = false;
    free(token);

    return verificacion;
}

bool direccion_puerto(char *string){
    bool verificacion = true;
    int i=0;
    while(*(string+i)!='\0'){
        if(!isdigit(*(string+i))) verificacion = false;
        i++;
    }
    if(atoi(string)<0||atoi(string)>65535) verificacion = false;
    return verificacion;
}


/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;

    // arguments checking
    if(argc!=3){
        errx(1, "Error in arguments number");
    }
    if(!direccion_IP(argv[1]))
        errx(1, "Invalidad IP");
    if(!direccion_puerto(argv[2]))
        errx(1, "Invalidad Port");

    // create socket and check for errors
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        errx(2, "Cannot create socket");
    }

    // set socket data
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(argv[1]); // server address
    addr.sin_port = htons(atoi(argv[2])); // server port

    // connect and check for errors
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        errx(3, "Cannot connect to server");
    }

    // if receive hello proceed with authenticate and operate if not warning
    //construir recvmsg
    if(recv_msg(sd, 220, NULL)) {
        authenticate(sd);
        operate(sd);

    } else{
        warnx("Didn't receive data from server");
    };

    // close socket
    close(sd);

    return 0;
}
