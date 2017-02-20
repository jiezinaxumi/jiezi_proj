#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include "myconfig.h"
#include "listen.h"
#include "util.h"
#include "fdinfo.h"

struct fdinfo_listen {
	struct conn *conn;
	int listenid;
};

struct listener {
	int fd;
	char *path;
	void (*func)(int);
};

static int nlistening = 0;
static struct listener *listener;
static int so_sndbuf;
static int so_rcvbuf;

static inline void add_listener(int fd, void (*func)(int), char *name) {
	name = strdup(name);
	if(name == NULL) 
		return;

	struct fdinfo_listen *info;
	set_stub(fd, listenstub);
	info = (struct fdinfo_listen *)(fdinfo + fd);
	info->listenid = nlistening;
	listener[nlistening].fd = fd;
	listener[nlistening].path = name;
	listener[nlistening].func = func;
	nlistening++;
}
static inline int already_listen(char *name) {
	int i;
	for(i = 0; i < nlistening; i++) {
		if(listener[i].path == NULL)
			continue;
		if(!strcmp(name, listener[i].path))
			return 1;
	}
	return 0;
}
static int init_socket_tcp(char *portstr, struct ifreq *ifr, int nifr) {
	struct sockaddr_in addr;
	int fd;
	int optval;
	int port = 80;
	int i1, i2;
	char *p, *ifc = NULL;
	char *path;

	if(portstr == NULL || portstr[0]=='\0') 
		return -1;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	
	if(portstr[0] == '*') {
	    if(portstr[1]=='\0') { /* "*" */
	        port = 80;
	    } 
		else if(portstr[1] != ':' || /* "*:port" */
		    (port = strtoul(portstr+2, &p, 0), *p))
			goto badport;
	    
		ifc = "*";
	    goto ok;
	}
	port = strtoul(portstr, &p, 0);
	if(!*p) { /* "port" */
	    ifc = "*";
	    goto ok;
	}
	addr.sin_addr.s_addr = str2ip(portstr, (const char **)&p);
	if(addr.sin_addr.s_addr != INADDR_NONE) {
		if(*p=='\0') { /* "ip" */
		    port = 80;
		} 
		else if(*p!=':' || /* "ip:port" */
			(port=strtoul(p+1, &p, 0), *p))
		    goto badport;
		
		goto ok;
	}

	if((p = strrchr(portstr, ':'))) {
	    /* "intf:port" */
	    i2 = p - portstr;
	    port = strtoul(p+1, &p, 0);
	    if(*p) 
			goto badport;
	} 
	else { /* "intf" */
	    i2 = strlen(portstr);
	    port=80;
	}
	if(i2 > IFNAMSIZ) 
		goto badport;
	for(i1 = 0; i1 < nifr; i1++) {
	    if(ifr[i1].ifr_addr.sa_family != AF_INET) 
			continue;
	    if((i2 == IFNAMSIZ || ifr[i1].ifr_name[i2] == '\0') &&
		    !strncmp(ifr[i1].ifr_name, portstr, i2)) {
			ifc = ifr[i1].ifr_name;
			addr.sin_addr.s_addr = ((struct sockaddr_in *)&ifr[i1].ifr_addr)->sin_addr.s_addr;
	    }
	}
	if(ifc == NULL) 
		goto badport;

ok:
	if(port <= 0 || port >= 65536) 
		goto badport;
	addr.sin_port = htons(port);

	if(ifc == NULL) {
	    for(i1 = 0; i1 < nifr; i1++) {
			if(ifr[i1].ifr_addr.sa_family != AF_INET) 
				continue;
			if(addr.sin_addr.s_addr == ((struct sockaddr_in *)&ifr[i1].ifr_addr)->sin_addr.s_addr)
		    	ifc = ifr[i1].ifr_name;
	    }
	    if(ifc == NULL && *(char *)&addr.sin_addr == 127)
		    ifc = "lo";
	    if(ifc == NULL) {
			printf("Warning: no interface matching port %s\n", portstr);
			ifc = "?";
	    }
	}

	path = alloca(22);
	p = ip2str(path, addr.sin_addr.s_addr);
	*p++ = ':';
	uint2str(p, ntohs(addr.sin_port))[0] = '\0';
	if(already_listen(path))
		return 0;

	if(nlistening == 1024) {
	    printf("Too much listening port defined, port %s:%d (%s) ignored\n", ifc, port, portstr);
	    return -1;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0) {
		printf("socket(AF_INET, SOCK_STREAM, 0): %m\n");
		return -1;
	}

	optval = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	struct linger linger = {0, 0};
	setsockopt(fd, SOL_SOCKET, SO_LINGER, (int *)&linger, sizeof(linger));

	if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind(%s): %m\n", portstr);
		return -1;
	}

	if(listen(fd, myconfig_get_intval("listen_queue_backlog", 10000)) < 0) {
	    printf("listen(%s): %m\n", portstr);
	    return -1;
	}
	printf("Listening at port: %s:%d (%s)\n", ifc, port, portstr);
	
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	optval = myconfig_get_intval("defer_accept", 10);
	if(optval) 
		setsockopt(fd, SOL_TCP, TCP_DEFER_ACCEPT, &optval, sizeof(optval));

	optval = myconfig_get_intval("tcp_nodelay", 0);
	if(optval) {
		setsockopt(fd, SOL_TCP, TCP_NODELAY, &optval, sizeof(optval));
		optval = 0;
		setsockopt(fd, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
	}
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &so_sndbuf, sizeof(int));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &so_rcvbuf, sizeof(int));

	add_listener(fd, NULL, path);
	
	return 0;
badport:
	printf("Unrecognized port: %s\n", portstr);
	return -1;
}
int epoll_add_listening(int epfd) {
	struct epoll_event ev;
	int i;

	ev.events = POLLIN;
	for(i = 0; i < nlistening; i++) {
	    ev.data.fd = listener[i].fd;
	    epoll_ctl(epfd, EPOLL_CTL_ADD, listener[i].fd, &ev);
	}
	return nlistening;
}
int epoll_stop_listening(int epfd) {
	struct epoll_event ev;
	int i;

	for(i = 0; i < nlistening; i++) {
	    /* 2.4 epoll patch require unused ev */
	    epoll_ctl(epfd, EPOLL_CTL_DEL, listener[i].fd, &ev);
	}
	return nlistening;
}
int init_listen_socket(void){
	struct ifconf ifc;
	struct ifreq *ifr = NULL;
	int nifr = 0;
	int i;
	char *p;

	so_sndbuf = myconfig_get_intval("send_socket_buffer", 65536);
	so_rcvbuf = myconfig_get_intval("recv_socket_buffer", 65536);

	i = socket(AF_INET, SOCK_STREAM, 0);
	if(i < 0) {
	    printf("init_listen_socket: socket fail, %m\n");
	    return -1;
	}
	ifc.ifc_len = 0;
	ifc.ifc_req = NULL;
	if(ioctl(i, SIOCGIFCONF, &ifc) == 0) {
	    ifr = alloca(ifc.ifc_len > 128 ? ifc.ifc_len : 128);
	    ifc.ifc_req = ifr;
	    if(ioctl(i, SIOCGIFCONF, &ifc) == 0)
			nifr = ifc.ifc_len / sizeof(struct ifreq);
	}
	close(i);

	i = 0;
	while(myconfig_get_multivalue("listen_port", i))
	    i++;
	if(i == 0) 
		i = 1;

	listener = (struct listener*)malloc(sizeof(struct listener) * i);
	if(listener == NULL) {
		printf("init_listen_socket: malloc listener fail, i=%d, %m\n", i);
		return -ENOMEM;
	}	

	for(i = 0; (p = myconfig_get_multivalue("listen_port", i)); i++) {
		init_socket_tcp(p, ifr, nifr);
	}

	/* at least one listener, default */
	if(i == 0) 
		init_socket_tcp("*:80", ifr, nifr);

	if(nlistening == 0) {
		printf("No listen-port available\n");
		return -1;
	}

	return 0;
}
void fini_listen_sockets(void){
	while(--nlistening >= 0) {
	    close(listener[nlistening].fd);
	    if(listener[nlistening].path)
	        free(listener[nlistening].path);
	}
}
int make_connect_socket(uint32_t ip, uint16_t port, int* fd) {
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0)
		return -1;
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
	
	struct sockaddr_in addr;
	memset(&addr, 0x0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = htons(port);
	
	int n = connect(s, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in));
	if(n == 0) {
		*fd = s;
		return 0;	
	}
	else if(errno == EINPROGRESS) {
		*fd = s;
		return 1;
	}
	else
		return -1;
}
