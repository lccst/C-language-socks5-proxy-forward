#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>

#define	TIME_OUT_MS		3000

#define		debug(fmt, ...)			printf((fmt), ##__VA_ARGS__)

static pthread_mutex_t s_mutex_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()      pthread_mutex_lock(&s_mutex_lock)
#define UNLOCK()    pthread_mutex_unlock(&s_mutex_lock)

/** 统计当前存活线程的数目 */
static int s_thread_cnt = 0;
#define     threads_cnt_inc()   do{LOCK();s_thread_cnt++;UNLOCK();}while(0)
#define     threads_cnt_dec()   do{LOCK();s_thread_cnt--;UNLOCK();}while(0)
int threads_cnt()   
{
    LOCK();
    int ret = s_thread_cnt;
    UNLOCK();
    return ret;
}

static char s_listen_port[32] = {0};
static char s_dest_addr[32] = {0};
static char s_dest_port[32] = {0};

/** 用于判断线程是否已经创建完毕，防止地址传的参数被修改 */
static int s_create_thread_flag = 0;
static void create_thread_start(int sock)
{
    struct timeval timeout={1,0};//3s
    if (0!=setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))) {
        debug("set sock %d opt failed, %m\n", sock);
    }
    if (0!=setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))) {
        debug("set sock %d opt failed, %m\n", sock);
    }
    debug("create thread for sock %d\n", sock);
    LOCK();
    s_create_thread_flag = 0;
    UNLOCK();
}
static void thread_created(int sock)
{
    LOCK();
    s_create_thread_flag = 1;
    UNLOCK();
    debug("thread for sock %d already created\n", sock);
}
static int is_thread_created()
{
    LOCK();
    int ret = s_create_thread_flag;
    UNLOCK();
    return ret;
}

long get_cur_ms()
{
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);

    return (long)(tv.tv_sec*1000 + tv.tv_usec/1000);
}

#define		debug(fmt, ...)			printf((fmt), ##__VA_ARGS__)

int is_sock_closed(int sock)
{
	int optval, optlen = sizeof(int);
	getsockopt(sock, SOL_SOCKET, SO_ERROR,(char*) &optval, &optlen);
	// optval is 0 if connecting
	debug("sock %d closed: %d\n", sock, 0!=optval);
	return (0!=optval);
}

#define BUFF_SIZE	10204
#define TIME_OUT	3
static int forward_data(int sock, int real_server_sock)
{
    char recv_buffer[BUFF_SIZE] = {0};

    fd_set fd_read;
    struct timeval time_out;

    time_out.tv_sec = TIME_OUT;
    time_out.tv_usec = 0;

    int ret = 0;
    long cur = get_cur_ms();
    while(1) {
        FD_ZERO(&fd_read);
        FD_SET(sock, &fd_read);
        FD_SET(real_server_sock, &fd_read);

        if (get_cur_ms()-cur >TIME_OUT_MS) {
            //debug("sock %d timeout\n", sock);
            break;
        } 
        ret = select((sock > real_server_sock ? sock : real_server_sock) + 1, &fd_read, NULL, NULL, &time_out);
        if (-1 == ret) {
            debug("%d select socket[%d, %d] error, %m\n", (unsigned char)pthread_self(), sock, real_server_sock);
            break;
        } else if (0 == ret) {
            continue;
        }
        if (FD_ISSET(sock, &fd_read)) {
            //printf("client can read!\n");
            memset(recv_buffer, 0, BUFF_SIZE);
            ret = recv(sock, recv_buffer, BUFF_SIZE, 0);
            if (ret <= 0) {
                debug("sock %d recv from client error, %m\n", sock);
                break;
            }
            ret = send(real_server_sock, recv_buffer, ret, 0);
            if (ret == -1) {
                debug("sock %d send data to real server failed, %m\n", sock);
                break;
            }
        } else if (FD_ISSET(real_server_sock, &fd_read)) {
            //printf("real server can read!\n");
            memset(recv_buffer, 0, BUFF_SIZE);
            ret = recv(real_server_sock, recv_buffer, BUFF_SIZE, 0);
            if (ret <= 0) {
                debug("real server sock recv data failed, %m\n");
                break;
            }
            ret = send(sock, recv_buffer, ret, 0);
            if (ret == -1) {
                perror("send data to client error");
                break;
            }
        }
    }

    return 0;
}

void *forward_data_thread(void *param)
{
	threads_cnt_inc();
	// client sock
	int ct_sock = *(int *)param;

	thread_created(ct_sock);

	int fd_sock = -1, size, ret;
	struct sockaddr_in saddr = {0};
	size = sizeof(struct sockaddr_in);
	saddr.sin_family = AF_INET;
	saddr.sin_port   = htons(atoi(s_dest_port));
	saddr.sin_addr.s_addr = inet_addr(s_dest_addr);
	// forword socket
	fd_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (fd_sock<0) {
		debug("create forward-sock for sock %d failed, %m\n", ct_sock);
		goto error;
	}
	ret = connect(fd_sock, (struct sockaddr*)&saddr, size);
	if (ret < 0) {
		debug("sock %d connect to forward-server failed, %m\n", ct_sock);
		goto error;
	}

	forward_data(ct_sock, fd_sock);


	close(ct_sock);
	close(fd_sock);

	debug("sock %d exit, threads cnt: %d\n", ct_sock, threads_cnt());

	threads_cnt_dec();
	return NULL;
error:
	debug("sock %d exit, threads cnt: %d\n", ct_sock, threads_cnt());
	if (ct_sock > 0)
		close(ct_sock);
	if (fd_sock > 0)
		close(fd_sock);
	threads_cnt_dec();
	return (void *)-1;
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        printf("\n\t usage:\n\t"
                "   %s  <local_port>  <dest_addr>  <dest_port>\n\n", argv[0]);
        return -1;
    }
    strncpy(s_listen_port, argv[1], sizeof(s_listen_port));
    strncpy(s_dest_addr, argv[2], sizeof(s_dest_addr));
    strncpy(s_dest_port, argv[3], sizeof(s_dest_port));
    debug("forward sever listen port: %s, dest: %s:%s\n", s_listen_port, s_dest_addr, s_dest_port);

	int sockfd, client_sockfd, ret;
	struct sockaddr_in saddr = {}, caddr = {};
	int size = sizeof(struct sockaddr_in);

	saddr.sin_family = AF_INET;			/** IPv4 */
	saddr.sin_port   = htons(atoi(s_listen_port));
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(-1==sockfd){
		printf("create socket failed\n");
		goto error;
	}
	ret = bind(sockfd, (struct sockaddr*)&saddr, size);
	if(-1==ret){
		printf("bind failed\n");
		goto error;
	}
	ret = listen(sockfd, 1);	/** listen num is 1 */
	if(-1==ret)
		goto error;
	
	while (1) {
		client_sockfd = accept(sockfd, (struct sockaddr*)&caddr, &size);
		if(-1==client_sockfd)
			continue;

		if (threads_cnt() > 100) {
            debug("too many threads, waiting...\n");
            usleep(500000);
            continue;
        }
        create_thread_start(client_sockfd);

        //forward_data_thread(&client_sockfd);
        pthread_t work_thread;
        if (pthread_create(&work_thread, NULL, forward_data_thread, (void *)&client_sockfd)) {
            debug("create thread for sock %d failed, %m\n", client_sockfd);
            close(client_sockfd);
        }else{
            pthread_detach(work_thread);
        }
        while(!is_thread_created())
            usleep(1000);
	}
	

	return 0;

error:
	printf("tcp server failed: %s\n", strerror(errno));
	return 110;
}


