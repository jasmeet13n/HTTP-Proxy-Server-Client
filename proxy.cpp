#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<unistd.h>
#include<fcntl.h>

#include<arpa/inet.h>
#include<netinet/in.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include<algorithm>
#include<iostream>
#include<sstream>
#include<fstream>
#include<string>
#include<map>

#define STDIN 0

// Thanks BEEJ.US for tutorial on how to use SELECT and pack and unpack functions
// Some parts of the code have been taken from BEEJ.US

using namespace std;

struct userinfo{					// user info structure
	string username;
	int status;
	int ip;
};

unsigned short int client_count;	// global variable to store client count
map<int, struct userinfo> users;	// map to store client info
int MAXCLIENTS;
int BACKLOG;

uint8_t get_msg_type(char *packet){	// extract 7 bit type of message from packet
	uint8_t type;
	type = (unsigned char)packet[1];
	type &= 0x7F;
	return type;
}

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

#define SIZE 10

map<int,int> requester;	// map to store requester of current request
map<int,int> fetcher;	// map to store fetcher of current requester
map<int,int> type;		// map to store type of socket : requester/fetcher
map<int,struct parameters> gold;	// map to store progress of request and important parameters like cache block assigned
map<string,int> whichBlock;	// map to store which cache block belongs to current URL
map<int,string> request;	// map to store the requested url of current client
map<int,bool> checkRand;	// map to check if random number generated already exists

// struct to store file name and host name from http request
struct info{
	char file[1024];
	char host[1024];
};

// struct to store parameters of each cache block
struct cache_block{
	int last;
	int inUse;
	int expr;
	string expr_date;
	string host_file;
};

// struct to store paremeters of each request
struct parameters{
	int expires;
	int length;
	int cb;
	char filename[200];
	int offset;
	bool del;
	bool conditional;
};

// declaring cache 
struct cache_block cache[SIZE];

// initializing cache
void initCache(){
	for(int i=0; i<SIZE; i++){
		cache[i].last=i+1;
		cache[i].inUse=0;
	}
}

// bring the last use block to front of LRU cache
void bringToFront(int block){
	if(cache[block].last==1)
		return;
	//cout << "Bringing to Front " << block << endl;
	cache[block].last = 1;
	for(int i=0; i<SIZE; i++){
		if(i!=block){
			cache[i].last++;
			if(cache[i].last>SIZE)
				cache[i].last = SIZE;
		}
	}
	return;
}

// get Free cache block from unused blocks in LRU fashion
int getFreeBlock(){
	int end = SIZE;
	int ans = -1;
	//for(int i=0; i<SIZE; i++)
	//	cout << cache[i].last << ",";
	//cout << endl;
	while(end>0){
		for(int i=0; i<SIZE; i++){
			if(cache[i].last==end && cache[i].inUse==0){
				ans = i;
				cache[ans].inUse=-2;
				//cache[ans].last=1;
				//cout << "Ans: "<< ans << endl;
				end=0;
				break;
			}
		}
		end--;
	}
	return ans;
}

// function to check if the current URL requested exists in cache or not
int checkCache(string host_file){
	if(whichBlock.find(host_file)==whichBlock.end()){
		return -1;
	}
	int cb = whichBlock[host_file];
	if(cache[cb].host_file == host_file)
		return cb;
	return -1;
}

// check if a block in cache is STALE or NOT
bool isExpired(int cb){
	time_t raw_time;
	time(&raw_time);
	struct tm * utc;
	utc = gmtime(&raw_time);
	raw_time = mktime(utc);
	//cout << "CACHE : " << cache[cb].expr << " :: CURRENT : " << raw_time << endl;
	if(cache[cb].expr-raw_time>0)
		return false;
	return true;
}

// generate random number with current time as seed
int getRandomNumber(){
	srand(time(NULL));
	return rand();
}

// function to parse the incoming HTTP request, decodes the File Name and Host Name 
struct info * parse_http_request(char *buf, int num_bytes){
	struct info* output = (struct info *)malloc(sizeof(struct info));
	int i;
	for(i=0; i<num_bytes; i++){
		if(buf[i]==' '){
			i++;
			break;
		}
	}
	int k=0;
	output->file[0]='/';
	if(buf[i+8]=='\r' && buf[i+9]=='\n'){
		output->file[1]='\0';
	}
	else{
		while(buf[i]!=' ')
			output->file[k++]=buf[i++];
		output->file[k]='\0';
	}
	i+=1;
	while(buf[i++]!=' ');
	k=0;
	while(buf[i]!='\r')
		output->host[k++]=buf[i++];
	output->host[k]='\0';
	return output;
}

int main(int argc, char *argv[]){
	if(argc!=3){						// if user arguments are not equal to 4 then give an error
		fprintf(stderr,"usage: PROXY SERVER IP, PROXY SERVER PORT\n");
		return 1;
	}
	BACKLOG = 10;				// setting the BACKLOG same as MAX CLIENTS to be handled

	struct addrinfo hints, *servinfo, *p;
	//char ipstr[INET_ADDRSTRLEN];		//INET6_ADDRSTRLEN for IPv6

	//Getting my address
	memset(&hints,0,sizeof(hints));		// initializing the hints structure to be given to getaddrinfo
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	//hints.ai_flags = AI_PASSIVE;
	int sockfd,error;
	if((error = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0){	// get the address info for given IP and PORT number
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(error));
		exit(1);
	}
	for(p=servinfo; p!=NULL; p=p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))== -1){	// create the server socket
			perror("server: socket");
			continue;
		}
		int yes=1;
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    		perror("setsockopt");
		}
		if((error = bind(sockfd, p->ai_addr, p->ai_addrlen))== -1){		// bind the server to the IP address and port
			perror("server: bind");
			continue;
		}
		break;
	}
	if(p==NULL){
		fprintf(stderr, "my sock fd: failed to bind\n");
		return 2;
	}

	printf("SERVER: Proxy Server Started\n");												
	if( (error = listen(sockfd, BACKLOG)) == -1){		// server starts listening with BACKLOG equal to the MAXCLIENTS
		perror("server: listen");
	}
	freeaddrinfo(servinfo);

	// Initializing the required variables
	char buf[2048],time_buf[256],tmpname[512];

	fd_set master, read_fds, write_fds, master_write;	// Sets for Select Non-Blocking I/O
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_ZERO(&master_write);
	FD_SET(sockfd,&master);

	int fdmax = sockfd, c_sockfd, i, num_bytes;

	struct sockaddr_storage client_addr;
	socklen_t addr_len;

	client_count = 0;
	initCache();

	struct tm tm,*utc;
	time_t rawtime;

	while(1){
		read_fds = master;								// select between file descriptors, select changes the read_fds set so always make a copy from master set
		write_fds = master_write;
		if( (error = select(fdmax+1, &read_fds, &write_fds, NULL, NULL)==-1)){
			perror("server: select");
			exit(4);
		}
		for(i=0; i<=fdmax; i++){
			if(FD_ISSET(i,&write_fds)){					// Write to file descriptor
				if(FD_ISSET(i,&master)==false){								// Handle case when client disconnects in middle of transfer
					cout << "SERVER: Client "<< i << ": Disconnected in middle of transfer" << endl;
					FD_CLR(i,&master);
					FD_CLR(i,&master_write);
					close(i);
					if(gold[i].cb==-1){
							//cout << "SOCKET: " << i << " ==> " << "Remove Temp File" << endl;
							if(gold[i].del==true)
								remove(gold[i].filename);	// Delete the tmp file
						}
						else if(gold[i].cb != -1){
							bringToFront(gold[i].cb);
							cache[gold[i].cb].inUse-=1;		// Decrease the in Use counter by 1
						}
						continue;
				}
				ifstream myfile1;
				myfile1.open(gold[i].filename,ios::in | ios::binary); // open the cache file or tmp file
				if(!myfile1.is_open()){
					cout << gold[i].filename << endl;
					cout << "SOCKET: " << i << " ==> " << "Could Not Open Cache File" << endl;
					FD_CLR(i,&master_write);
				}
				else{
					myfile1.seekg((gold[i].offset)*2000,ios::beg);		// Seek the file with the offset in the parameters of the request and increase the offset by 1
					gold[i].offset+=1;
					myfile1.read(buf,2000);                  // Read next 512 bytes
                    num_bytes = myfile1.gcount();
                    //cout << num_bytes << endl;
                    if(num_bytes!=0){
						if( (error = send(i,buf,num_bytes,0)) == -1){			// send the message to the server
								perror("client: send");
						}
					}
					if(num_bytes<2000){						// reached the end of FILE, now close the connection
						FD_CLR(i,&master_write);
						FD_CLR(i,&master);			
						close(i);							// Connection closed with client and removed from master Read and Write sets
						cout << "SERVER: **DONE** Connection closed to client " << i << ": **DONE**" << endl;
						if(gold[i].cb==-1){
							//cout << "SOCKET: " << i << " ==> " << "Remove Temp File" << endl;
							if(gold[i].del==true)
								remove(gold[i].filename);	// Delete the tmp file
						}
						else if(gold[i].cb != -1){
							bringToFront(gold[i].cb);
							cache[gold[i].cb].inUse-=1;		// Decrease the in Use counter by 1
							//cout << "SOCKET: " << i << " ==> " << "Decrease inUse by 1 : " << cache[gold[i].cb].inUse<<endl;
						}
					}
				}
				myfile1.close();
			}
			else if(FD_ISSET(i,&read_fds)){
				if(i==sockfd){
					addr_len = sizeof(client_addr);		// handle new clients here
					if( (c_sockfd = accept(sockfd, (struct sockaddr *)&client_addr,&addr_len)) == -1){	// accept a new connection
						perror("server: accept");
					}
					else{
						FD_SET(c_sockfd,&master);
						type[c_sockfd]=0;
						client_count++;					// increase client count
						if(c_sockfd > fdmax)
							fdmax = c_sockfd;
						//cout << "SOCKET: " << i << " ==> " << "CLIENT CONNECTED " << c_sockfd << endl;
						cout << "***************************************************************" << endl;
						cout << "SERVER: New client connected at Socket: " << c_sockfd << endl;
					}
				}
				else{
					if((num_bytes = recv(i,buf,sizeof(buf),0)) <=0){
						if(num_bytes == 0){				// If received bytes are zero, it means client has disconnected, so remove its allocated resources
						}
						else{
							perror("server: recv");
						}								
						close(i);
						FD_CLR(i,&master);
						if(type[i]==1){
							FD_SET(requester[i],&master_write);		// if it was a fetcher, then start sending back to the requester
							//cout << "SOCKET: " << i << " ==> " << "Fetcher Disconnected" << endl;
							int j = requester[i];
							int bb = gold[j].cb;
							if(bb==-1){
								// Got No Block return from tmp file
							}
							else{
								// Copy tmp file to cache block and serve from cache block
								ifstream myfile3;
								myfile3.open(gold[j].filename,ios::in | ios::binary);
								string line;
								bool tof=false;
								if(myfile3.is_open()){
									getline(myfile3,line);
									cout << "SERVER: Fetcher "<<i <<": for client "<<requester[i] << ": " << line << endl;
									if(line.find("304")!=string::npos)		// Check for 304 responses
										tof = true;
									bool flag = false;
									while(!myfile3.eof()){
										getline(myfile3,line);
										if(line.compare("\r") == 0){
											break;
										}
										string line_cp = line;
										//cout << line << endl;
										transform(line.begin(), line.end(), line.begin(), ::tolower);
										if(line.find("expires:")==0){		// Check for the Expires Field
											flag = true;
											int pos = 8;
											if(line[pos]==' ')
												pos++;
											stringstream s2;
											s2 << line.substr(pos);
											getline(s2,line);
											//cache[bb].expr_date = line_cp.substr(pos,line_cp.find("GMT")-pos-1);
											line = line_cp.substr(pos);
											cache[bb].expr_date = line;
											//cout << cache[bb].expr_date << endl;
											memset(&tm, 0, sizeof(struct tm));
											strptime(line.c_str(), "%a, %d %b %Y %H:%M:%S ", &tm);		// Update the Cache Block expires field with received value
											cache[bb].expr = mktime(&tm);
											break;
										}
									}
									myfile3.close();
									if(!flag){
										time(&rawtime);
										utc = gmtime(&rawtime);
										strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S ",utc);	// If Expires field was not present, set the current time as cache block expire time
										cache[bb].expr = mktime(utc);
										cache[bb].expr_date = string(time_buf) + string("GMT");
									}
								}
								time(&rawtime);
								utc = gmtime(&rawtime);
								strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S ",utc);
								cout << "SERVER: **Proxy Server Time**: " << time_buf << "GMT" << endl;
								cout << "SERVER: **File Expires Time**: " << cache[bb].expr_date << endl;

								if(tof==true){
									if(gold[j].conditional){	// if 304 response comes, serve from Cache Block and delete the tmp file
										remove(gold[j].filename);
										stringstream s4;
										s4 << bb;
										strcpy(tmpname,s4.str().c_str());
										strcpy(gold[j].filename,tmpname);									
										gold[j].del = false;
									}
								}

								if(tof==false){					// If it is not 304 response, see if we can write to the cache block assigned, or get a new cache block
									if(cache[bb].inUse!=1){
										if(cache[bb].inUse>1){
											cache[bb].inUse-=1;
											int new_cb;
											new_cb = getFreeBlock();
											if(new_cb==-1){
												gold[j].del = true;
												gold[j].cb = new_cb;
												bb = new_cb;
												cout << "SERVER: Client " << i << ": Could Not find new free block" << endl;
											}
											else{
												cout << "SERVER: Client" << i << ": Got new block because earlier was in use and we got a 200" << endl;
												gold[j].cb = new_cb;
												cache[new_cb].expr = cache[bb].expr;
												cache[new_cb].expr_date = cache[bb].expr_date;
												bb = new_cb;
											}
										}
									}
									else{
										cache[bb].inUse = -2;
										//cout << "SOCKET: " << i << " ==> " << "Cache Block is free for writing" << endl;
									}
								}

								if(bb!=-1 && cache[bb].inUse==-2)	// Prepare the cache block and the parameters, remove the tmp file
								{
									//cout << "Still came here" << endl;
									stringstream s3;
									s3 << bb;
									strcpy(tmpname,s3.str().c_str());
									ofstream myfile4;
									myfile4.open(tmpname,ios::out | ios::binary);
									myfile4.close();
									remove(tmpname);
									rename(gold[j].filename,tmpname);
									strcpy(gold[j].filename,tmpname);
									cache[bb].host_file = string(request[requester[i]]);
									whichBlock[cache[bb].host_file]=bb;
									gold[j].del = false;
									cache[bb].inUse = 1;
								}
							}
						}
						else{
							cout << "SERVER: Client " << i << ": Disconnected" << endl;		// check if requester disconnected
						}
					}
					else if(type[i]==1){
						ofstream myfile2;		// Fetcher writes the data into a tmp file for sending back to requester later
						myfile2.open(gold[requester[i]].filename,ios::out | ios::binary | ios::app);
						if(!myfile2.is_open()){ 
							cout << "some error in opening file" << endl;
						}
						else{
							myfile2.write(buf,num_bytes);
							myfile2.close();
						}
					}
					else if(type[i]==0){
						struct info* tmp;
						tmp = parse_http_request(buf,num_bytes);
						request[i] = string(tmp->host)+string(tmp->file);
						cout << "SERVER: Client "<< i << ": GET Request: " << request[i] << endl;
						int cb = checkCache(request[i]);
						//cout << "SOCKET: " << i << " ==> " << "First cb" << cb <<endl;
						bool expired = false;
						gold[i].conditional = false;
						if(cb!=-1){
							if(cache[cb].inUse>=0){
								expired = isExpired(cb);
								if(expired){
									//cout << "SOCKET: " << i << " ==> " << "Expired Cache Block Send Contional Get" << endl;
									gold[i].conditional = true;		// Data is stale, so do conditional get
								}
								else{
									gold[i].cb = cb;		// Cache Hit happened, return data from cache
									gold[i].offset = 0;
									gold[i].del = false;
									cache[cb].inUse += 1;
									stringstream ss;
									ss << cb;
									strcpy(gold[i].filename,ss.str().c_str());
									FD_SET(i,&master_write);
									cout << "SERVER: Client " << i << ": Cache Hit at Block "<< cb << ": **Not STALE**" << endl;
									//cout << "SOCKET: " << i << " ==> " << "RETURN FROM CACHE BLOCK" << endl;
									continue;
								}
							}
						}
						if(cb==-1 || expired){
							string tmp_str = "GET "+string(tmp->file)+" HTTP/1.0\r\nHost: "+string(tmp->host)+"\r\n\r\n";
							num_bytes = tmp_str.length();
							strcpy(buf,tmp_str.c_str());
							if(!expired){
								//cout << "here" << endl;
								cb = getFreeBlock();
								cout << "SERVER: Client "<< i << ": Cache Miss: Allocated Block " << cb << endl;	// if Not Stale get new cache block
								//cout <<buf << endl;
							}
							else{
								cache[cb].inUse+=1;
								buf[num_bytes-2]='\0';
								string req = string(buf);
								string new_req = req+"If-Modified-Since: "+cache[cb].expr_date +"\r\n\r\n\0";		// If Stale send If-Modified-Since field in the header
								num_bytes=new_req.length();
								strcpy(buf,new_req.c_str());
								//cout << buf << endl;
								cout << "SERVER: Client " << i << ": Cache Hit at Block "<< cb << ": **STALE** : **Conditional Get**" << endl;
								cout << "SERVER: Client " << i << ": If-Modified-Since: "<< cache[cb].expr_date << endl;
							}
							gold[i].cb = cb;
							//cout << "SOCKET: " << i << " ==> " << "GOT BLOCK " << gold[i].cb << endl;
							int new_socket;
							memset(&hints,0,sizeof(hints));		// initializing the hints structure to be given to getaddrinfo
							hints.ai_family = AF_INET;
							hints.ai_socktype = SOCK_STREAM;
							if((error = getaddrinfo(tmp->host, "80", &hints, &servinfo)) != 0){	// get the address info for given IP and PORT number
								fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(error));
								exit(1);
							}
							for(p=servinfo; p!=NULL; p=p->ai_next){
								if((new_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol))== -1){	// create the server socket
									perror("server: socket");
									continue;
								}
								break;
							}
							if(p==NULL){
								fprintf(stderr, "my sock fd: failed to bind\n");
								return 2;
							}
							if( (error = connect(new_socket,p->ai_addr,p->ai_addrlen)) == -1){			// connect to server IP Address and PORT number
								perror("server: server connect");
							}	
							freeaddrinfo(servinfo);
							
							type[new_socket]=1;
							fetcher[i] = new_socket;
							requester[new_socket] = i;

							gold[i].del = true;				// setting the parameters of the request
							gold[i].offset = 0;
							stringstream ss;
							int rr = getRandomNumber();		// generate random number for tmp file name
							while(checkRand.find(rr) != checkRand.end() ){
								rr = (rr+1)%1000000007;		// if requests are at same time the rand() will generate same random number so increment by 1 till we get a new number
							}
							checkRand[rr]=true;
							ss << "tmp_"<< rr;
							strcpy(gold[i].filename,ss.str().c_str());
							//cout << "SERVER: Client " << i << " ==> " << "Tmp filename " << gold[i].filename << endl;
							ofstream touch;
							touch.open(gold[i].filename,ios::out | ios::binary);		// touch the tmp file
							if(touch.is_open())
								touch.close();

							FD_SET(new_socket,&master);
							if(new_socket > fdmax)
								fdmax = new_socket;

							//cout << "SOCKET: " << i << " ==> " << "new socket created " << new_socket << endl;
							cout << "SERVER: Fetcher started at socket " << new_socket << ": for client " << i << endl;
							if( (error = send(new_socket,buf,num_bytes,0)) == -1){			// send the get or conditional get request
								perror("client: send");
							}
						}
						free(tmp);
					}
				}
			}
		}
	}
	return 0;
}
