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

const size_t  H_SIZE_LIMIT = 8 * 1024;
const size_t  MSG_LIMIT =  1024 * 1024; //We will send at one time only this amount of data

struct my_io{	//Appending buffer for our watcher
	struct ev_io watcher;
	void * in_buffer;
	void * out_buffer;
	size_t in_size;
	size_t out_size;
	bool got_header;
};


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

int parse_http_string()
{
	//TODO think of params of the func and what to return
	return 0;
}


//------------------------------------------------------------
//------------------------Callbacks---------------------------


void write_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	//TODO Write stuff and kill watcher when finished
	return;
}


void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
	const char * http_header_end = "\r\n\r\n";
	struct my_io * x_watcher = (struct my_io *)watcher;
	if(x_watcher -> got_header)
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
//	kfree(watcher);
	}
	Buffer[RecvSize] = '\0';
	
	char * end_sym = strstr(Buffer,http_header_end);
//	std::cout << strstr(Buffer, http_header_end) << std::endl;
	
	size_t cur_msg_size = x_watcher -> in_size;
	
	if(end_sym == NULL)
		cur_msg_size += RecvSize;
	else
		cur_msg_size += end_sym - Buffer;

	if(cur_msg_size > H_SIZE_LIMIT)
	{
		std::cout << "Bad request" << std::endl;
		x_watcher -> in_size = 0;
		return;
	}

	memcpy((char *)x_watcher -> in_buffer + x_watcher -> in_size, Buffer, cur_msg_size - x_watcher -> in_size);
	x_watcher -> in_size = cur_msg_size;

	if(end_sym != NULL)
	{
		printf("Got an HTTP Request Message END at  %p\n", end_sym);
		x_watcher -> got_header = true; 
		ev_io_init(watcher, write_cb, watcher -> fd, EV_WRITE);
		ev_io_start(loop,watcher);
	}

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


int main(int argc, char * argv[])
{
	std::string directory = "htocs/";
	int port = 80;

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
	SockAddr.sin_port = htons(8080); //Listening for http on 80 port
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
