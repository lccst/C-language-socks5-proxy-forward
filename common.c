#include "common.h"

#define random(x) (rand()%x)

#define PWD_DIC_SEED	7758
#define MAX_DIC_CNT		255


static int s_pwd_dic[MAX_DIC_CNT] = {0};
static int s_dic_pwd[MAX_DIC_CNT] = {0};

static void generate_pwd_dic()
{
	int cnt = 0;
	unsigned char rad;

	for (cnt=0; cnt<MAX_DIC_CNT; cnt ++) {
		s_pwd_dic[cnt] = -1;
		s_dic_pwd[cnt] = -1;
	}
	cnt = 0;

	srand(PWD_DIC_SEED);
	while (cnt<MAX_DIC_CNT) {
		rad = random(MAX_DIC_CNT);

		/** we already have such a num */
		if (-1!=s_dic_pwd[rad]) 
			continue;

		s_pwd_dic[cnt] = rad;
		s_dic_pwd[rad] = cnt;

		cnt ++;
	}
}

static void pwd_dic_data(const unsigned char *data, int len, unsigned char *o_result)
{
	int i = 0;
	for (i=0; i<len; i++) {
		o_result[i] = (unsigned char)s_pwd_dic[data[i] ];
	}
}

static void dic_pwd_data(const unsigned char *data, int len, unsigned char *o_result)
{
	int i=0;
	for (i=0; i<len; i++) {
		o_result[i] = (unsigned char)s_dic_pwd[data[i] ];
	}
}

long x_send(int sock, const void *buf, size_t n, int flag)
{
    unsigned char *res = malloc(n);
    assert(res);
    memset(res, 0, n);

    pwd_dic_data((const unsigned char *)buf, n, res);

    long ret = send(sock, res, n, flag);
    free(res);

    return ret;
}
long x_recv(int sock, void *buf, size_t n, int flag)
{
    unsigned char *raw = malloc(n);
    assert(raw);
    memset(raw, 0, n);

    long ret = recv(sock, raw, n, flag);
    if (ret>0){
        memset(buf, 0, n);
        dic_pwd_data(raw, ret, buf);
    }
    free(raw);
    return ret;
}

void x_send_recv_init()
{
    generate_pwd_dic();
}








void add_ip(struct ip_root_s *root, const char *ip)
{
	struct ip_list_s *node = NULL;

	if (NULL==root->head) {
		root->head = malloc(sizeof(struct ip_list_s));
		assert(root->head);
		root->head->next = NULL;
		/** now head is also tail */
		root->tail = root->head;
	} else {
		node = malloc(sizeof(struct ip_list_s));
		assert(node);
		node->next = NULL;
		/** connect node to list tail */
		root->tail->next = node;
		/** now node is tail */
		root->tail = node;		
	}
    debug(" -- add ip: \"%s\"\n", ip);
	strncpy(root->tail->ip, ip, MAX_IP_LEN);
}

void free_ip_list(struct ip_root_s *root)
{
	struct ip_list_s *node = root->head;
	while (node) {
		free (node);
		node = node->next;
	}
	root->head = NULL;
	root->tail = NULL;
}



#include <sys/time.h>
long get_cur_ms()
{
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);

    return (long)(tv.tv_sec*1000 + tv.tv_usec/1000);
}



int forward_data(int sock, int real_server_sock)
{
    unsigned char recv_buffer[BUFF_SIZE] = {0};

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
                debug("sock %d x_recv from client error, %m\n", sock);
                break;
            }
        printf("(%d) %x %x %x %x %x %x\n", ret, recv_buffer[0], recv_buffer[1],
                recv_buffer[2], recv_buffer[3], recv_buffer[4], recv_buffer[5]);
            ret = x_send(real_server_sock, recv_buffer, ret, 0);
            if (ret == -1) {
                debug("sock %d x_send data to real server failed, %m\n", sock);
                break;
            }
        } else if (FD_ISSET(real_server_sock, &fd_read)) {
            //printf("real server can read!\n");
            memset(recv_buffer, 0, BUFF_SIZE);
            ret = x_recv(real_server_sock, recv_buffer, BUFF_SIZE, 0);
            if (ret <= 0) {
                debug("real server sock x_recv data failed, %m\n");
                break;
            }
            ret = send(sock, recv_buffer, ret, 0);
            if (ret == -1) {
                perror("x_send data to client error");
                break;
            }
        }
    }

    return 0;
}

int forward_proxy_data(int sock, int real_server_sock)
{
    unsigned char recv_buffer[BUFF_SIZE] = {0};

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
            ret = x_recv(sock, recv_buffer, BUFF_SIZE, 0);
            if (ret <= 0) {
                debug("sock %d x_recv from client error, %m\n", sock);
                break;
            }
            ret = send(real_server_sock, recv_buffer, ret, 0);
            if (ret == -1) {
                debug("sock %d x_send data to real server failed, %m\n", sock);
                break;
            }
        } else if (FD_ISSET(real_server_sock, &fd_read)) {
            //printf("real server can read!\n");
            memset(recv_buffer, 0, BUFF_SIZE);
            ret = recv(real_server_sock, recv_buffer, BUFF_SIZE, 0);
            if (ret <= 0) {
                debug("real server sock x_recv data failed, %m\n");
                break;
            }
            ret = x_send(sock, recv_buffer, ret, 0);
            if (ret == -1) {
                perror("x_send data to client error");
                break;
            }
        }
    }

    return 0;
}
