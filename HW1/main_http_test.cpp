#include <iostream>
#include <algorithm>
#include <set>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <ev.h>
#include <errno.h>
#include <string.h>


#include <arpa/inet.h>


#define HTTP_GET 0x01
#define HTTP_HEAD 0x02
#define HTTP_POST 0x08


#define en_EN 0x01


#define HTTP_OK 200
#define HTTP_FILE_NOT_FOUND 404


#define CONTENT_TEXT_HTML 1
#define CONTENT_JPEG 2


const size_t  H_SIZE_LIMIT = 8 * 1024;
const size_t  MSG_LIMIT =  1024 * 1024; //We will send at one time only this amount of data
const size_t  MAX_HOST_SIZE = 1024; //Max hostname len
const size_t  MAX_PATH_SIZE = 1024; //Max file path len

char files_dir[MAX_PATH_SIZE];
char default_file[MAX_PATH_SIZE];

struct my_io{	//Appending buffer for our watcher
	struct ev_io watcher;
	void * in_buffer;
	void * out_buffer;
	size_t in_size;
	size_t out_size;
	size_t send_offset;
	bool got_header;
};


struct HTTP_Request{
	int type;
	char host[MAX_HOST_SIZE];
	int accept_language;	
	char path[MAX_PATH_SIZE];
	int content_type;
	int fd;
	size_t f_size;
	char fname[MAX_PATH_SIZE]; //TODO map
};



//---------------------------------------------------------------
//----------------------------Helpers----------------------------
//---------------------------------------------------------------



int set_nonblock(int fd)
{
	int flags;
#if defined(O_NONBLOCK)
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
} 

int parse_http_string(char * str, size_t size, struct HTTP_Request * res)
{
	//We parse http header contained in str and fill the necessary params of HTTP_Request structure
	size_t i = 0;
	for(i = 0; i < size; i++)
	{
		if(str[i] != ' ' && str[i] != '\t')
			break;
	}

	std::cout << "Line is: \n"<< str <<std::endl;
	std::cout<< "Parsing debug info! Checking if last symbols are brbnbrbn "<<(str[size-1]=='\n')<<" "<<(str[size-2]=='\r')<< " "<<
		(str[size-3]=='\n')<< " "<<(str[size-4] == '\r')<<std::endl;
	if(i == size)
		return -1; //Critical - empty string given
	if(size - i < 3)
		return -1; //Critical - bad string
	if(str[i] == 'G' && str[i+1] == 'E' && str[i+2] == 'T' )
	{
		res -> type = HTTP_GET;
		i+=3;
	}
	else if(str[i] == 'P' && str[i+1] == 'O' && str[i+2] == 'S' && str[i+3] == 'T')
	{
		res -> type = HTTP_POST;
		i+=4;
	}
	else if(str[i] == 'H' && str[i+1] == 'E' && str[i+2] == 'A' && str[i+3] == 'D')
	{
		res -> type = HTTP_HEAD;
		i+=4;
	}
	
	for(;i < size; i++)
	{
		if(str[i] != ' ' && str[i] != '\t')
			break;

	}
	if( i == size )
		return -1;
	
	int k =0;
	while(str[i + k]!=' ')
	{
		res -> path[k] = str[i+k];
		k++;
	}

	i+=k;
	if(k == 1 && res->path[0] == '/')
	{
		std::cout << "Default file is set: " << default_file << std::endl;
		strcpy(res->path, default_file);
		std::cout << "Now path in res -> path is: "<<res->path<<std::endl;
		//zero = strlen()		
	}
	else
		res -> path[i] = 0;
	
	if(strstr(res->path, ".jpg")!=NULL) //TODO Fix and check on the end of the str
	{
			res->content_type = CONTENT_JPEG;
	}
	else
		res->content_type = CONTENT_TEXT_HTML;
	//TODO - Parse more, but for now on it's enough

	return 0;
}


int form_HTTP_reply_header(struct HTTP_Request * request, char * res) //Returns response status
{ //TODO std string, can break char*
	char * str_begin = request -> path;
	if(request -> path [0] == '/')
		str_begin+=1;
	char * fname = (char*)malloc(MAX_PATH_SIZE * sizeof(char));
	char dtstring[80];
	strcpy(fname, files_dir);
	strcpy(fname+strlen(fname), str_begin);
	std::cout << "Full file path:" << std::endl;
	std::cout << fname << std::endl;	
	int fd = open(fname,O_RDONLY);
	std::cout << "Tried openning file, returned "<< fd<< std::endl;
	strcpy(request -> fname, fname);
	time_t  timev;
	time(&timev);
	struct tm* timeinfo = localtime(&timev);
	strftime(dtstring,80,"%d-%m-%Y %I:%M:%S", timeinfo);
	std::cout << "Current datetime: "<< dtstring <<std::endl;
	
	request -> fd = fd;
	if(fd < 0)
	{
		std::string x = std::string("HTTP/1.1 404 Not Found\nDate: ") + std::string(dtstring) + std::string("\nServer: MyTestServ\r\n\r\n");
		strcpy(res,x.c_str());
		return HTTP_FILE_NOT_FOUND;
	}
	else
	{ //TODO ostringstream
		size_t len = lseek(fd,0,SEEK_END);
		request -> f_size = len;
		std::cout << "File length is " << len <<std::endl;
		std::string x ="";
		if(request -> content_type == CONTENT_TEXT_HTML )
		{
			 x = std::string("HTTP/1.1 200 Ok\nDate: ") + std::string(dtstring) + std::string("\nContent-Type: text/html\nContent-Length:")+
				std::to_string(len)+std::string("\r\n\r\n");
		}
		else if( request ->content_type == CONTENT_JPEG )
		{
			  x = std::string("HTTP/1.1 200 Ok\nDate: ") + std::string(dtstring) + std::string("\nContent-Type: image/jpeg\nContent-Length:")     +	                        std::to_string(len)+std::string("\r\n\r\n");

		}
	       	//TODO text/xml is a patch right now - make it work like it has to
		strcpy(res,x.c_str());
		
	}	
	return HTTP_OK;
}


//------------------------------------------------------------
//------------------------Callbacks---------------------------
//------------------------------------------------------------

//TODO High Priority finish reading
void write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	//TODO Write stuff and kill watcher when finished
	struct my_io * x_watcher = (struct my_io *)watcher;
	if(x_watcher -> send_offset != 0)
	{
		x_watcher->send_offset += send(watcher->fd,(char*)x_watcher -> out_buffer + x_watcher -> send_offset,
			       	x_watcher -> out_size - x_watcher -> send_offset,MSG_NOSIGNAL);
		if (x_watcher -> send_offset == x_watcher -> out_size)
		{
			ev_io_stop(loop,watcher);
		}
		return;
			
	}
	struct HTTP_Request * req = (struct HTTP_Request *)malloc(sizeof(struct HTTP_Request));
	parse_http_string((char*) x_watcher -> in_buffer, x_watcher -> in_size, req);

	//TODO Form an http reply header and give data if type is GET or POST
	if(req -> type == HTTP_GET)
	{
		std::cout << "ITS GET" << std::endl;
	}
	else if(req -> type == HTTP_POST)
	{
		std::cout << "ITS POST" << std::endl;
	}
	else if(req -> type == HTTP_HEAD)
	{
		std::cout <<"ITS HEAD"<<std::endl;
	}
	std::cout << "File path:"<<std::endl;
	std::cout << req -> path << std::endl;
	char * reply_header = (char*)malloc(sizeof(char) * H_SIZE_LIMIT);
	std::cout << "Starting to form HTTP reply header." << std::endl;
	int res = form_HTTP_reply_header(req, reply_header);
	//TODO Make send buffering
	if(res == HTTP_FILE_NOT_FOUND)
	{
		//TODO echo some 404 content page
		std::cout << "Sending HTTP 404"<<std::endl;
		send(watcher->fd,reply_header,strlen(reply_header),MSG_NOSIGNAL);
		ev_io_stop(loop,watcher);
		return;
	}
	else
	{
		//TODO Check if GET or POST and send content. For now it is sending the header
		std::cout<< "Sending HTTP 200"<<std::endl;
		send(watcher->fd,reply_header,strlen(reply_header),MSG_NOSIGNAL); //TODO Buffer
	}
	if(req->type == HTTP_GET || req->type == HTTP_POST)
	{
		int fd = open(req -> fname, O_RDONLY);
	//	if(req -> content_type == CONTENT_TEXT_HTML)
	//	{
		//	int fd = open(req -> fname, O_RDONLY);
			std::cout << "File descriptor: "<< fd << " | File size: " <<req->f_size<<std::endl;
			std::cout << "Sending html body which is:"<<std::endl;
			//TODO Here we send file body - make buffering
			void * msg_body = malloc( req -> f_size); //TODO vector char
			int r =	read( fd, msg_body, req -> f_size );
			std::cout <<(char*) msg_body << std::endl;
			std::cout << "Have read "<<r<<" bytes from file"<<std::endl;
			x_watcher -> out_buffer = msg_body;
			x_watcher -> send_offset = 0;
			x_watcher -> out_size = req->f_size;
			x_watcher -> send_offset = send(watcher -> fd,(char*) msg_body, req -> f_size, MSG_NOSIGNAL); //TODO send return -1
			std::cout<<"Have sent "<<x_watcher->send_offset<<" bytes"<<std::endl;
			if(x_watcher ->send_offset == req -> f_size)
			{	
				//send(watcher->fd, ">", 1,MSG_NOSIGNAL);
				free(msg_body);
			}

	//	}
	//	else if(req->content_type == CONTENT_JPEG)
	//	{
	//		std::cout <<"Sending jpg file"<<std::endl;
	//		void * pic_body = malloc( req -> f_size );
	//		int r = read(fd, pic_body, req->f_size);
	//		send(watcher -> fd, (char*) pic_body, req -> f_size, MSG_NOSIGNAL);
	//		free(pic_body);
	//	}
	//	else
	//	{
			//TODO For now on our server cant handle any other type of content
	//	}
	}
	ev_io_stop(loop,watcher);

	return;
}


void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	const char * http_header_end = "\r\n\r\n";
//	struct my_io * x_watcher = (struct my_io *)malloc(sizeof(struct my_io));
	struct my_io * p_watcher = (struct my_io *)watcher; //reinterpret cast
	if(p_watcher -> got_header)
	{
		return; // TODO This is a patch, we ignore everything after header until we reply
	}
	if(EV_ERROR & revents)
	{
		return;
	}
	static char Buffer[H_SIZE_LIMIT];
	int RecvSize = recv(watcher->fd, Buffer, H_SIZE_LIMIT, MSG_NOSIGNAL);
	printf("r_sz = %d \t len = %zu\n", RecvSize, strlen(Buffer));

	if(RecvSize <= 0)
	{
		printf("%d\n",__LINE__);
		ev_io_stop(loop, watcher);
//	kfree(watcher); +return
	}
	Buffer[RecvSize] = '\0';
	
	char * end_sym = strstr(Buffer,http_header_end);
//	std::cout << strstr(Buffer, http_header_end) << std::endl;
//	std::cout << "RecvSize : " << RecvSize << std::endl;
	size_t cur_msg_size = p_watcher -> in_size;
	
	if(end_sym == NULL)
		cur_msg_size += RecvSize;
	else
		cur_msg_size += end_sym - Buffer;

	if(cur_msg_size > H_SIZE_LIMIT)
	{
		std::cout << "Bad request" << std::endl;
		p_watcher -> in_size = 0;
		return;
	}
	std::cout << "Starting memcpy"<<std::endl;
	memcpy((char *)p_watcher -> in_buffer + p_watcher -> in_size, Buffer, cur_msg_size - p_watcher -> in_size);
	p_watcher -> in_size = cur_msg_size;
	//*((char*)(p_watcher->in_buffer) +  cur_msg_size)  = 0;
	//printf( "Cur end sym: %p\n",end_sym);
	//std::cout << "Cur in_buf: "<<(char*)p_watcher -> in_buffer<<std::endl;	
	if(end_sym == NULL)
		end_sym = strstr((char*)p_watcher -> in_buffer, http_header_end); //Searching again for header end to prevent header end segmentation
	//printf( "Now end_sym is: %p\b",end_sym);
	if(end_sym != NULL)
	{
		ev_io_stop(loop,watcher);
		printf("Got an HTTP Request Message END at  %p\n", end_sym);
		//TODO To be a good server it should watch for a request type, and then read everything after header, but not this time
		//TODO Free last watcher !!!!!!!! 		
		struct my_io * x_watcher = (struct my_io *)watcher;//malloc(sizeof(struct my_io));
		//x_watcher -> in_buffer = malloc(sizeof(char)*H_SIZE_LIMIT);
		//x_watcher -> in_size = p_watcher -> in_size;
		x_watcher -> out_buffer = NULL;
		x_watcher -> out_size = 0;
		x_watcher -> got_header = true;
		x_watcher -> send_offset = 0;
	        //memcpy(x_watcher -> in_buffer, p_watcher -> in_buffer, p_watcher -> in_size);	
		ev_io_init((struct ev_io *)x_watcher, write_cb, watcher -> fd, EV_WRITE);
		ev_io_start(loop,(struct ev_io*)x_watcher);
		//TODO Kill reader watcher after getting all stuff from user
//TODO parse here not in write
	}
	//TODO Patch connection close
	return;
}


void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	struct my_io *w_client = (struct my_io*) malloc (sizeof(struct my_io));
	w_client -> in_buffer = malloc(sizeof(char) * H_SIZE_LIMIT);
	w_client -> out_buffer = NULL; //TODO don't know if that will be the same
	w_client -> in_size = 0;
	w_client -> out_size = 0;
	w_client -> got_header = false;
	if(EV_ERROR & revents)
	{
		return;
	}

	int SlaveSocket = accept(watcher->fd, 0, 0);
	if(SlaveSocket < 0)
	{
		return;
	}

	set_nonblock(SlaveSocket);
	ev_io_init((struct ev_io *)w_client, read_cb, SlaveSocket, EV_READ);
	//ev_io_init((struct ev_io *)w_client, write_cb, SlaveSocket, EV_WRITE);
	ev_io_start(loop, (struct ev_io*)w_client);
}


//------------------------------------------------------------
//------------------------Main--------------------------------
//------------------------------------------------------------



int main(int argc, char * argv[])
{

	//Getting options from command prompt
	char *dvalue = NULL;
	char *hvalue = NULL;
	char *pvalue = NULL; 
	int index;
	int c;

	opterr = 0;
	while ((c = getopt (argc, argv, "d:p:h:")) != -1)
	{
		switch(c)
		{
			case 'd':
				std::cout << "-d : "<<optarg <<std::endl;
				dvalue = optarg;
				break;
			case 'p':
				std::cout << "-p : "<<optarg <<std::endl;
			       pvalue = optarg;
			       break;
			case 'h':
			       std::cout << "-h : "<<optarg <<std::endl;
				hvalue = optarg;
		 		break;
			case '?':
				if(isprint(optopt))
				{
					std::cout << "Unknown option" << std::endl;
				}
				else
				{
					std::cout << "Incorrect input" <<std::endl;
					return -1;
				}		
			default:
				return -2;
		}
	}

	std::cout << "Got vals for -d -p -h as :" << dvalue << " "<< pvalue << " "<< hvalue<<std::endl;

	// Starting work with sockets

	std::string directory = "htocs/";
	int port = atoi(pvalue);
	std::cout<<"Port num in int is : " <<port<<std::endl;
	char default_files_dir[] = "files/";

	if(dvalue != NULL)
	{
		strcpy(files_dir, dvalue);

		std::cout<<"The files dir is: "<<files_dir<<std::endl;
	}
	else{
		strcpy(files_dir,default_files_dir);
	}
	char d_file[] = "index.html";
	strcpy(default_file,d_file);	
	//TODO Parse CMD arguments for flags

	struct ev_loop *loop = ev_default_loop(0);
	struct ev_io w_accept; //Accepts HTTP requests, opens slave sockets


	int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if(MasterSocket == -1)
	{
		std::cout << strerror(errno) << std::endl; //Could not open Master Socket
		return 1;
	}

	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(port); //Listening for http on 8080 port - TODO GetOpt from command prompt
	if(hvalue != NULL)
		SockAddr.sin_addr.s_addr = inet_addr(hvalue);
	else
		SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int Result = bind(MasterSocket, (struct sockaddr *) &SockAddr, sizeof(SockAddr));

	if(Result == -1)
	{
		std::cout << strerror(errno) << std::endl;
		return 1;
	}

	set_nonblock(MasterSocket);

	Result = listen(MasterSocket, SOMAXCONN);

	if(Result == -1)
	{
		std::cout << strerror(errno) << std::endl;
		return 1;
	}

	ev_io_init(&w_accept, accept_cb, MasterSocket, EV_READ);
	ev_io_start(loop, &w_accept);
	ev_run(loop);
	return 0;
}
