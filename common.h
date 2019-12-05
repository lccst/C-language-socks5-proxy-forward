#ifndef __COMMON_H__
#define __COMMON_H__

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

#define BUFF_SIZE 10240

#define TIME_OUT 5   // seconds
#define TIME_OUT_MS 3000

#define WHITE_IP_LIST       "white_ip_list.txt"

#define		debug(fmt, ...)			printf((fmt), ##__VA_ARGS__)

static pthread_mutex_t s_mutex_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()      pthread_mutex_lock(&s_mutex_lock)
#define UNLOCK()    pthread_mutex_unlock(&s_mutex_lock)


#define MAX_IP_LEN 18
struct ip_list_s {
	char		ip[MAX_IP_LEN];
	struct ip_list_s *next;
};
struct ip_root_s {
	struct ip_list_s *head;
	struct ip_list_s *tail;
};




/** 统计当前存活线程的数目 */
static int s_thread_cnt = 0;
#define     threads_cnt_inc()   do{LOCK();s_thread_cnt++;UNLOCK();}while(0)
#define     threads_cnt_dec()   do{LOCK();s_thread_cnt--;UNLOCK();}while(0)
static int threads_cnt()   
{
    LOCK();
    int ret = s_thread_cnt;
    UNLOCK();
    return ret;
}

/** 用于判断线程是否已经创建完毕，防止地址传的参数被修改 */
static int s_create_thread_flag = 0;
static void create_thread_start(int sock)
{
    struct timeval timeout={3,0};//3s
    if (0!=setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))) {
        debug("set sock %d opt failed, %m\n", sock);
    }
    if (0!=setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))) {
        debug("set sock %d opt failed, %m\n", sock);
    }
    //debug("create thread for sock %d\n", sock);
    LOCK();
    s_create_thread_flag = 0;
    UNLOCK();
}
static void thread_created(int sock)
{
    LOCK();
    s_create_thread_flag = 1;
    UNLOCK();
    //debug("thread for sock %d already created\n", sock);
}
static int is_thread_created()
{
    LOCK();
    int ret = s_create_thread_flag;
    UNLOCK();
    return ret;
}

long get_cur_ms();

void x_send_recv_init();
long x_recv(int sock, void *buf, size_t n, int flag);
long x_send(int sock, const void *buf, size_t n, int flag);

void add_ip(struct ip_root_s *root, const char *ip);
void free_ip_list(struct ip_root_s *root);

/** x_sock using xsend-xrecv, sock using send-recv */
int forward_data(int x_sock, int sock);

#endif
