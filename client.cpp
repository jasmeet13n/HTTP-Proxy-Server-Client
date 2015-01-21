#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<unistd.h>

#include<arpa/inet.h>
#include<netinet/in.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>

#include<iostream>
#include<fstream>
#include<sstream>
#include<string>
#include<map>

using namespace std;

// Thanks BEEJ.US for tutorial on how to use SELECT and pack and unpack functions
// Some parts of the code have been taken from BEEJ.US

unsigned short int getlength(unsigned short int len){	//make a 4-bytes-wide payload.
	unsigned short int rem = len%4;
	return len + (4-rem)%4;
}
void packi16 (char *buf, unsigned short int i){   //change the host order to network byte order (16bit)
	i = htons(i);
	memcpy(buf,&i,2);
}
void packi32(char *buf, unsigned long i){	//change the host order to network byte order (32bit)
	i = htonl(i);
	memcpy(buf,&i,4);
}
unsigned short int unpacki16(char *buf){	//change  network byte order to the host order (16bit)
	unsigned short int i;
	memcpy(&i,buf,2);
	i = ntohs(i);
	return i;
}
unsigned long unpacki32(char *buf){	//change  network byte order to the host order (32bit)
	unsigned long i;
	memcpy(&i,buf,2);
	i = ntohl(i);
	return i;
}

// generate a HTTP GET Request for given URL
char *get_http_request(char *url){
	int len = strlen(url);
	char *output = (char *)malloc((len+50)*sizeof(char));
	int i=0, k=4;
	output[0]='G';output[1]='E';output[2]='T';output[3]=' ';
	if(len>7){
		if(url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' && url[4]==':' && url[5]=='/' && url[6]=='/')
			i = 7;
	}
	int tmp = i;								// Seperate the URL into file name and host name
	while(i<len && url[tmp]!='/')
		tmp++;
	output[k++]='/';
	tmp++;
	while(tmp<len)
		output[k++]=url[tmp++];
	output[k++]=' ';output[k++]='H';output[k++]='T';output[k++]='T';output[k++]='P';output[k++]='/';
	output[k++]='1';output[k++]='.';output[k++]='0';output[k++]='\r';output[k++]='\n';
	output[k++]='H';output[k++]='o';output[k++]='s';output[k++]='t';output[k++]=':';output[k++]=' ';
	while(i<len && url[i]!='/')
		output[k++] = url[i++];
	output[k++]='\r';output[k++]='\n';output[k++]='\r';output[k++]='\n';output[k]='\0';
	return output;
}

// get the file name for storing requested data
char *get_filename(char *qq){
	char *name = (char *)malloc(512*sizeof(char));
	int k=0;
	int last = 0;
	int i=0;
	int len = strlen(qq);
	while(i < len){
		if(qq[i]=='/')
			last = i;
		i++;
	}
	if(last!=0){
		memcpy(name,&qq[last+1],(len-last-1));
		name[len-last]='\0';
	}
	if(strlen(name)==0){
		name[k++]='i';name[k++]='n';name[k++]='d';name[k++]='e';name[k++]='x';name[k++]='.';		// if filename cannot be extracted, set default name to index.html
		name[k++]='h';name[k++]='t';name[k++]='m';name[k++]='l';name[k]='\0';
		return name;
	}
	return name;
}

// get_int_addr function taken from BEEJ.US
void *get_in_addr(struct sockaddr *sa)				// get address structure for IPv4 or IPv6 addresses
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]){
	int error;
	if(argc!=4){									// show error if number of command line arguments are not equal to 4
		fprintf(stderr,"Enter PROXY SERVER IP, PROXY SERVER PORT, URL\n");
		return 1;
	}
	struct addrinfo hints;
	struct addrinfo *servinfo, *p;
	//char ipstr[INET_ADDRSTRLEN];	//INET6_ADDRSTRLEN for IPv6

	memset(&hints,0,sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if( (error = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0){	// get address info of server for given IP address and PORT number
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(error));
		exit(1);
	}

	int sockfd;
	for(p=servinfo; p!=NULL; p=p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))== -1){	// create client socket
			perror("client: socket");
			continue;
		}
		break;
	}
	if(p==NULL){
		fprintf(stderr, "my sock fd: failed to connect\n");
		return 2;
	}
	int yes=1;

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    	perror("setsockopt");
    	exit(1);
	}

	int size = 2048, num_bytes;
	char *buf = (char *)malloc((size+1)*sizeof(char));
	unsigned short int len;
	if( (error = connect(sockfd,p->ai_addr,p->ai_addrlen)) == -1){			// connect to server IP Address and PORT number
		perror("client: connect");
	}
	freeaddrinfo(servinfo);
	
	char *query = get_http_request(argv[3]);
	/*char *query = "GET / HTTP/1.0\r\n"
        "Host: www.google.com\r\n"
        "\r\n";*/
	//cout << query << endl;
    len = strlen(query);
    cout << "CLIENT: GET " << argv[3] << endl;
	if( (error = send(sockfd,query,len,0)) == -1){			// send the message to the server
		perror("client: send");
	}
	ofstream myfile;
	char *filename = get_filename(argv[3]);
	stringstream ss;
	ss << filename;

	myfile.open(ss.str().c_str(),ios::out | ios::binary);
	if(!myfile.is_open()){
		cout << "Could not open file in filesystem" << endl;
		exit(0);
	}
	bool header = true;
	bool first=true;
	while(1){
		num_bytes = recv(sockfd,buf,2000,0);			// receive message from server
		//printf("%d\n",num_bytes);
		if(first){
			stringstream tmp;
			tmp << buf;
			string line;
			getline(tmp,line);
			if(line.find("404")!=string::npos)
				cout << "SERVER: **ERROR** " << line << endl;
			else
				cout << "SERVER: " << line << endl;
			first = false;
		}
		int i=0;
		if(header){
			if(num_bytes>=4){
				for(i=3; i<num_bytes; i++){
					if(buf[i]=='\n' && buf[i-1]=='\r'){
						if(buf[i-2]=='\n' && buf[i-3]=='\r'){
							header=false;				// Detect the end of header and start storing data
							i++;
							break;
						}
					}
				}	
			}
		}
		if(!header)
			myfile.write(buf+i,num_bytes-i);			// Store received data in file 
		if(num_bytes == -1){
			perror("client: recv");
		}
		else if(num_bytes == 0){						// if number of bytes received is zero, it means server has closed the connection
			close(sockfd);
			cout << "CLIENT: File Saved at: " << filename << endl;	// Close the connection when server closes the connection
			printf("CLIENT: Connection closed by server\n");
			myfile.flush();
			myfile.close();
			break;							
		}
		myfile.flush();				// Flush the output file buffers
	}
	myfile.close();					// Close the output file
	return 0;
}

