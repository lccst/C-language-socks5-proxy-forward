// 参考资料：http://www.rfc-editor.org/rfc/rfc1928.txt
//          http://www.rfc-editor.org/rfc/rfc1929.txt


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

#include "proxy.h"

#define USER_NAME "test"
#define PASS_WORD  "test"
#define MAX_USER 10
#define BUFF_SIZE 10240

#define AUTH_CODE 0x02

#define TIME_OUT 5   // seconds
#define TIME_OUT_MS 5000

#define WHITE_IP_LIST       "white_ip_list.txt"
#define TOUCH_WHITE_IP_LIST 

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

#include <sys/time.h>
long get_cur_ms()
{
    struct timeval tv;
    struct timezone tz;

    gettimeofday(&tv, &tz);

    return (long)(tv.tv_sec*1000 + tv.tv_usec/1000);
}

#define MAX_IP_LEN 18
struct ip_list_s {
	char		ip[MAX_IP_LEN];
	struct ip_list_s *next;
};
struct ip_root_s {
	struct ip_list_s *head;
	struct ip_list_s *tail;
};

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

#include <sys/stat.h>
long get_last_modified_time(const char *filename)
{
    struct stat s;
    if (0!=stat(filename, &s))
        return -1;
    return (long)s.st_mtime;
}

void load_white_ips(struct ip_root_s *o_ip_root, const char *filename)
{
	assert(o_ip_root);
	assert(filename);
	o_ip_root->head = NULL;
	o_ip_root->tail = NULL;

	FILE *fp = fopen(filename, "r");
	assert(fp);

	char *ip = NULL;
	size_t cnt = 0;
	ssize_t len;
	while ((len=getline(&ip, &cnt, fp))!=-1) {
		// delete '\n'
		if (ip[strlen(ip)-1] == '\n')
			ip[strlen(ip)-1] = '\0';
		add_ip(o_ip_root, ip);
	}
	free(ip);
	fclose(fp);
}


// Select auth method, return 0 if success, -1 if failed
static int select_method(int sock)
{
    char recv_buffer[BUFF_SIZE] = {0};
    char reply_buffer[2] = {0};

    METHOD_SELECT_REQUEST *method_request;
    METHOD_SELECT_RESPONSE *method_response;

    // recv METHOD_SELECT_REQUEST
    int ret = recv(sock, recv_buffer, BUFF_SIZE, 0);
    if (ret <=0) {
        debug("sock %d recv method error, %m\n", sock);
        return -1;
    }
    // if client request a wrong version or a wrong number_method
    method_request = (METHOD_SELECT_REQUEST *)recv_buffer;
    method_response = (METHOD_SELECT_RESPONSE *)reply_buffer;

    method_response->version = VERSION;

    // if not socks5
    if ((int)method_request->version != VERSION) {
        method_response->select_method = 0xff;
        send(sock, method_response, sizeof(METHOD_SELECT_RESPONSE), 0);
        debug("sock %d is not socks5 proxy\n", sock);
        return -1;
    }

    method_response->select_method = 0; // none
    if (-1 == send(sock, method_response, sizeof(METHOD_SELECT_RESPONSE), 0)) {
        debug("send selected method to sock %d failed\n", sock);
        return -1;
    }

    return 0;
}

// test password, return 0 for success.
static int auth_password(int sock)
{
    char recv_buffer[BUFF_SIZE] = { 0 };
    char reply_buffer[BUFF_SIZE] = { 0 };

    AUTH_REQUEST *auth_request;
    AUTH_RESPONSE *auth_response;

    // auth username and password
    int ret = recv(sock, recv_buffer, BUFF_SIZE, 0);
    if (ret <= 0)
    {
    perror("recv username and password error");
    close(sock);
    return -1;
    }
    //printf("AuthPass: recv %d bytes\n", ret);

    auth_request = (AUTH_REQUEST *)recv_buffer;

    memset(reply_buffer, 0, BUFF_SIZE);
    auth_response = (AUTH_RESPONSE *)reply_buffer;
    auth_response->version = 0x01;

    char recv_name[256] = { 0 };
    char recv_pass[256] = { 0 };

    // auth_request->name_len is a char, max number is 0xff
    char pwd_str[2] = { 0 };
    strncpy(pwd_str, auth_request->name + auth_request->name_len, 1);
    int pwd_len = (int)pwd_str[0];

    strncpy(recv_name, auth_request->name, auth_request->name_len);
    strncpy(recv_pass, auth_request->name + auth_request->name_len + sizeof(auth_request->pwd_len), pwd_len);

    //printf("username: %s\npassword: %s\n", recv_name, recv_pass);
    // check username and password
    if ((strncmp(recv_name, USER_NAME, strlen(USER_NAME)) == 0) &&
    (strncmp(recv_pass, PASS_WORD, strlen(PASS_WORD)) == 0)) {
        auth_response->result = 0x00;
        if (-1 == send(sock, auth_response, sizeof(AUTH_RESPONSE), 0)) {
            close(sock);
            return -1;
        }else{
            return 0;
        }
    }else{
        auth_response->result = 0x01;
        send(sock, auth_response, sizeof(AUTH_RESPONSE), 0);

        close(sock);
        return -1;
    }
}

// parse command, and try to connect real server.
// return socket for success, -1 for failed.
static int parse_cmd(int sock)
{
    char recv_buffer[BUFF_SIZE] = { 0 };
    char reply_buffer[BUFF_SIZE] = { 0 };

    SOCKS5_REQUEST *socks5_request;
    SOCKS5_RESPONSE *socks5_response;

    // recv command
    int ret = recv(sock, recv_buffer, BUFF_SIZE, 0);
    if (ret <= 0) {
        debug("sock %d recv connect command error, %m\n", sock);
        return -1;
    }

    socks5_request = (SOCKS5_REQUEST *)recv_buffer;
    if ((socks5_request->version != VERSION) || (socks5_request->cmd != CONNECT) ||
    (socks5_request->address_type == IPV6)) {
        debug("socks5 request of sock %d info check failed\n", sock);
        return -1;
    }
    // begain process connect request
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;

    // get real server's ip address
    if (socks5_request->address_type == IPV4) {
        memcpy(&sin.sin_addr.s_addr, &socks5_request->address_type + 
            sizeof(socks5_request->address_type) , 4);
        memcpy(&sin.sin_port, &socks5_request->address_type + 
            sizeof(socks5_request->address_type) + 4, 2);
    } else if (socks5_request->address_type == DOMAIN) {
        char domain_length = *(&socks5_request->address_type + 
            sizeof(socks5_request->address_type));
        char target_domain[512] = {0};

        strncpy(target_domain, &socks5_request->address_type + 2, (size_t)domain_length);

        //long cur = get_cur_ms();
        struct hostent *phost = gethostbyname(target_domain);
        //debug("\t get host by name spend: %ld ms\n\n", get_cur_ms()-cur);
        if (phost == NULL) {
            debug("resolve %s failed\n" , target_domain);
            return -1;
        }
        memcpy(&sin.sin_addr , phost->h_addr_list[0] , phost->h_length);
        memcpy(&sin.sin_port, 
            &socks5_request->address_type + sizeof(socks5_request->address_type) +
            sizeof(domain_length) + domain_length, 2);
    }

    // try to connect to real server
    int real_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (real_server_sock < 0) {
        debug("create real server sock failed, %m\n");
        return -1;
    }
    memset(reply_buffer, 0, sizeof(BUFF_SIZE));

    socks5_response = (SOCKS5_RESPONSE *)reply_buffer;
    socks5_response->version = VERSION;
    socks5_response->reserved = 0x00;
    socks5_response->address_type = 0x01;
    memset(socks5_response + 4, 0, 6);

    ret = connect(real_server_sock, (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
    if (ret == 0) {
        socks5_response->reply = 0x00;
        if (-1 == send(sock, socks5_response, 10, 0)) {
            debug("send res to sock %d failed, %m\n", sock);
            return -1;
        }
    } else {
        socks5_response->reply = 0x01;
        send(sock, socks5_response, 10, 0);
        debug("sock %d connect to real server error, %m\n", sock);
        return -1;
    }
    return real_server_sock;
}

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

void *socks5_proxy_thread(void *client_sock)
{
    threads_cnt_inc();

    int sock = *(int *)client_sock;

    thread_created(sock);

    if (select_method(sock) == -1) {
        debug("sock %d select method failed\n", sock);
        goto error;
    }
#if 0
    if (auth_password(sock) == -1) {
        //printf("auth password error\n");
        goto error;
    }
#endif
    int real_server_sock = parse_cmd(sock);
    if (real_server_sock == -1) {
        debug("sock %d parse command failed.\n", sock);
        goto error;
    }
    forward_data(sock, real_server_sock);

    close(sock);
    close(real_server_sock);

    debug("sock %d exit, threads cnt: %d\n", sock, threads_cnt());
    
    threads_cnt_dec();
    return NULL;
error:
    close(sock);
    threads_cnt_dec();
    return (void *)-1;
}

static int check_ip_passed(const struct ip_root_s *root, const char *ip)
{
    struct ip_list_s *node = root->head;

    while (node) {
        if (0==strncmp(ip, node->ip, sizeof(ip))) {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        printf("\n\t Usage: \n\t   %s <proxy_port>\n\n", argv[0]);
        return 1;
    }

    system("touch "WHITE_IP_LIST);

    struct ip_root_s ips_root = {NULL, NULL};
    load_white_ips(&ips_root, WHITE_IP_LIST);

    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(atoi(argv[1]));
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        debug("socket creation failed, %m\n");
        goto error;
    }

    int opt = SO_REUSEADDR;
    if (0!=setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        debug("set sock opt failed, %m\n");
        goto error;
    }

    if (bind(listen_sock, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)) < 0) {
        debug("bind failed, %m\n");
        goto error;
    }

    if (listen(listen_sock, MAX_USER) < 0) {
        debug("listen failed, %m\n");
        goto error;
    }

    struct sockaddr_in cin;
    int client_sock;
    int client_len = sizeof(struct sockaddr_in);

    long white_ips_file_time = get_last_modified_time(WHITE_IP_LIST);

    while(1) {
        // if modified white-ip-list, reload it
        if (white_ips_file_time != get_last_modified_time(WHITE_IP_LIST)) {
            debug("white ip list modified, reloading...\n");
            free_ip_list(&ips_root);
            load_white_ips(&ips_root, WHITE_IP_LIST);
            white_ips_file_time = get_last_modified_time(WHITE_IP_LIST);
        }

        client_sock = accept(listen_sock, (struct sockaddr *)&cin, (socklen_t *)&client_len);
        //debug("client sock: %d, Connected from %s, processing...\n", client_sock, inet_ntoa(cin.sin_addr));
        if (!check_ip_passed(&ips_root, inet_ntoa(cin.sin_addr))) {
            debug("illegal ip[%s], refused, check your \""WHITE_IP_LIST"\".\n",
                     inet_ntoa(cin.sin_addr));
            close(client_sock);
            continue;
        }
        if (threads_cnt() > 100) {
            debug("too many threads, waiting...\n");
            usleep(500000);
            continue;
        }
        create_thread_start(client_sock);
        //socks5_proxy_thread(&client_sock);
        pthread_t work_thread;
        if (pthread_create(&work_thread, NULL, socks5_proxy_thread, (void *)&client_sock)) {
            debug("create thread for sock %d failed, %m\n", client_sock);
            close(client_sock);
        }else{
            pthread_detach(work_thread);
        }
        while(!is_thread_created())
            usleep(1000);
    }
    return 0;
error:
    free_ip_list(&ips_root);
    if(listen_sock>=0)
        close(listen_sock);
    return -1;
}