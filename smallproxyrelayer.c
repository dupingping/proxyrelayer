#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <bits/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>

#include <pthread.h>


/* add by dupingping begin */
#include <libxml/parser.h>
#include <libxml/tree.h>
/*  add by dupingping end  */

#define     MAX_CHAIN 30*1024

typedef enum {HTTP_TYPE,SOCKS4_TYPE,SOCKS5_TYPE} proxy_type;
typedef enum {DYNAMIC_TYPE,STRICT_TYPE,RANDOM_TYPE} chain_type;
typedef enum {PLAY_STATE,DOWN_STATE,BLOCKED_STATE,BUSY_STATE} proxy_state;
typedef enum {RANDOMLY,FIFOLY} select_type;

/*proxy_state ps;*/

typedef struct
{
    char *ip;
    unsigned short port;
    proxy_type pt;
    chain_type ct;
    char *user;
    char *pass;
    int serverfd;
} proxy_data;

typedef struct proxy_list
{
    proxy_data *data;
    struct proxy_list *next;
} proxy_list;

int *bindServers(int *portlist);
int acceptClient(int sd, struct sockaddr_in *cli_in);
void clientProcess(int clifd, unsigned short port);

int newClientConn(int sockaddr, unsigned short port){
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr_in;
    if (sd < 0) return sd;


    memset(&addr_in, 0, sizeof(addr_in));

    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(port);
    addr_in.sin_addr.s_addr = sockaddr;
    printf("connecting\n");
    printf("connect %d %x\n", connect(sd, (const struct sockaddr *)&addr_in, sizeof(addr_in)), sockaddr);
    printf("%d %s\n", errno, strerror(errno));
    return sd;
}

static inline proxy_list *get_proxy_list(int reload)
{
    static proxy_list *pl = 0;

    if (pl && !reload) return pl;

    pl = 0;

    char fnbuf[1024] = {0, };
    int count = 0;

    xmlDoc *doc = NULL;
    xmlNode *root_element = NULL;

    snprintf(fnbuf,256,"%s/.proxychains/servers.xml",getenv("HOME"));

    if (!(doc = xmlReadFile("./servers.xml", NULL, 0)))
    if (!(doc = xmlReadFile(fnbuf, NULL, 0)))
    if (!(doc = xmlReadFile("/etc/servers.xml", NULL, 0))) {
	perror("Can't locate servers.xml");
	exit(1);
    }

    /*Get the root element node */
    root_element = xmlDocGetRootElement(doc);

    xmlNode *servers_node = NULL;

    for (servers_node = root_element; servers_node; servers_node = servers_node->next) {

	if (servers_node->type != XML_ELEMENT_NODE && strcmp(servers_node->name, "servers")) {
	    continue;
	}

	xmlNode *server_node = NULL;

	

	for (server_node = servers_node->children; server_node; server_node = server_node->next) {
	    proxy_data *pd = (proxy_data *)malloc(sizeof(proxy_data));
	    memset(pd, 0, sizeof(proxy_data));

	    if (server_node->type != XML_ELEMENT_NODE && strcmp(server_node->name, "server")) {
		continue;
	    }

	    xmlNode *attr_node = NULL;

	    for (attr_node = server_node->children; attr_node; attr_node = attr_node->next) {

		if (attr_node->type != XML_ELEMENT_NODE) {
		    continue;
		}

		if (!attr_node->children || strcmp(attr_node->children->name, "text") || !attr_node->children->content) {
		    continue;
		}

		if (!strcmp(attr_node->name, "ip")) {
		    pd->ip=strdup(attr_node->children->content);
		}

		if (!strcmp(attr_node->name, "user")) {
		    pd->user=strdup(attr_node->children->content);
		}

		if (!strcmp(attr_node->name, "pass")) {
		    pd->pass=strdup(attr_node->children->content);
		}

		if (!strcmp(attr_node->name, "port")) {
		    pd->port=htons((unsigned short)atoi(attr_node->children->content));
		}

		if (!strcmp(attr_node->name, "protocoltype")) {
		    if (!strcmp(attr_node->children->content, "http"))
			pd->pt=HTTP_TYPE;

		    if (!strcmp(attr_node->name, "socks4"))
			pd->pt=SOCKS4_TYPE;
		    if (!strcmp(attr_node->name, "socks5"))
			pd->pt=SOCKS5_TYPE;
		    if (++count == MAX_CHAIN)
			break;
		}

		pd->ct=RANDOM_TYPE;
		if (!strcmp(attr_node->name, "type")) {
		    if (!strcmp(attr_node->children->content, "random"))
			pd->ct=RANDOM_TYPE;
		    else if (!strcmp(attr_node->children->content, "strict"))
			pd->pt=STRICT_TYPE;
		}

	    }

	    if( pd->ip && pd->port){
		proxy_list *cpl = (proxy_list *)malloc(sizeof(proxy_list));
		memset(cpl, 0, sizeof(proxy_list));

		cpl->data = pd;
		cpl->next = pl;
		pl = cpl;

		if (++count == MAX_CHAIN)
		    break;
	    }
	}
    }
    return pl;
}

int main()
{

    proxy_list *pl = get_proxy_list(1);
    proxy_list *cpl;

    int i; /* loop variable */

    int portlist[65536] = {0, };
    int portcount = 0;

    for (cpl = pl; cpl; cpl = cpl->next){
	for (i=0; i<portcount; i++){
	    if (portlist[i] == cpl->data->port) break;
	}
	if (i != portcount) continue;
	portlist[i] = cpl->data->port;
	portcount++;
    }


    /* create servers */
    int *serverfds = bindServers(portlist);

    for (i=0; serverfds[i]; i++){
	printf("port : %d\n", serverfds[i]);
    }

    fd_set rfds;
    struct timeval tv;
    int retval;

    int clientfds[65536] = {0, };


    while (1){
	int ffd, tfd, maxfd = 0;
	tv.tv_sec = 5;
	tv.tv_usec = 0;

	
	FD_ZERO(&rfds);

	for (i=0; serverfds[i]; i++){
	    FD_SET(serverfds[i], &rfds);
	    if (maxfd < serverfds[i]) maxfd = serverfds[i];
	}

	retval = select(maxfd + 1, &rfds, NULL, NULL, &tv);

	if (retval <= 0){
	    continue;
	}

	for (i=0; serverfds[i]; i++){
	    if (FD_ISSET(serverfds[i], &rfds)){
		FD_CLR(serverfds[i], &rfds);
		struct sockaddr_in cliin;
		int clientfd = acceptClient(serverfds[i], &cliin);
		continue;
	    }
	}
    }
    return 0;
}

/* return server fd */
int *bindServers(int *ports)
{
    int i, port, count;
    static int serverfds[65536];

    count = 0;

    memset(serverfds, 0, sizeof(serverfds));

    for (i=0; (port = ports[i]) != 0; i++){
	int sd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	struct sockaddr_in addr_in, cli_in;
	int on = 1, socklen;

	if (sd < 0)
	    continue;

	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
	memset(&addr_in, 0, sizeof(struct sockaddr));
	memset(&cli_in, 0, sizeof(struct sockaddr));
	addr_in.sin_family = AF_INET;
	printf("%d\n", port);
	addr_in.sin_port = port;
	addr_in.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sd, (const struct sockaddr *)&addr_in, sizeof(addr_in)))
	    continue;

	listen(sd, 10);
	printf("ok\n");
	serverfds[count++] = sd;
//	break;
    }

    return serverfds;
}

int acceptClient(int sd, struct sockaddr_in *cli_in)
{
    int clifd;
    int on = 1, socklen;
    memset(&cli_in, 0, sizeof(struct sockaddr));

    socklen = sizeof(struct sockaddr_in);
    clifd = accept(sd, (struct sockaddr *)&cli_in, &socklen);

    int status, i;

    pid_t pid = 0;
    pid = fork();

    if (pid == 0){
	pid = fork();
	if (pid) exit(0);
	clientProcess(clifd, 80);
	exit(0);
    }

    close(clifd);

    wait(&status);

    return clifd;
}

void clientProcess(int clifd, unsigned short port)
{
    struct hostent *hptr;
    int servaddr;
    int servfd = -1;
    char str[1024];
    char **pptr = 0;


    proxy_list *pl = get_proxy_list(0);
    proxy_list *cpl = 0;

    for (cpl = pl; cpl; cpl = cpl->next){

	if ((port == 80 || port == 8080 || port == 8004) && cpl->data->pt != HTTP_TYPE)
	    continue;

	if (port == 1080 && (cpl->data->pt != SOCKS4_TYPE && cpl->data->pt != SOCKS5_TYPE))
	    continue;

	hptr = gethostbyname(cpl->data->ip);
	pptr = hptr->h_addr_list;

	if (!pptr) continue;

	inet_ntop (hptr->h_addrtype, *pptr, str, sizeof (str));


	printf("ip is %s\n", str);

	inet_pton(AF_INET, str, &servaddr);

	servfd = newClientConn(servaddr, cpl->data->port);

	if (servfd <= 0) continue;

    }

    printf("client fd : %d\n", servfd);

    if (servfd < 0){
	printf("Can not connect\n");
    }

    fd_set rfds;
    struct timeval tv;
    int retval;

    int clientfds[65536] = {0, };

    while (1){
	int ffd, tfd, maxfd = 0;
	tv.tv_sec = 5;
	tv.tv_usec = 0;

	
	FD_ZERO(&rfds);

	FD_SET(clifd, &rfds);
	FD_SET(servfd, &rfds);

	maxfd = servfd > clifd ? servfd : clifd;

	retval = select(maxfd + 1, &rfds, NULL, NULL, &tv);
	printf("retval:%d\n", retval);
	if (retval <= 0){
	    printf("timeout for 1s\n");
	    continue;
	}


	if (FD_ISSET(clifd, &rfds)){
	    FD_CLR(clifd, &rfds);
	    printf("print client --------------------\n");
	    ffd = clifd;
	    tfd = servfd;
	} else if (FD_ISSET(servfd, &rfds)){
	    FD_CLR(servfd, &rfds);
	    printf("print server --------------------\n");
	    ffd = servfd;
	    tfd = clifd;
	} else{
	    printf("- timeout for 5s\n");
	    continue;
	}
	
	char buf[16384];
	int readlen;
	readlen = read(ffd, buf, 16384);

	if (readlen <= 0){
	    printf("Connection closed\n");
	    break;
	}

	printf("%s\n", buf);

	write(tfd, buf, readlen);
	printf("write done\n");
    }
}
