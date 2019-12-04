#include "common.h"

static char s_listen_port[32] = {0};
static char s_dest_addr[32] = {0};
static char s_dest_port[32] = {0};

static int is_sock_closed(int sock)
{
	int optval, optlen = sizeof(int);
	getsockopt(sock, SOL_SOCKET, SO_ERROR,(char*) &optval, &optlen);
	// optval is 0 if connecting
	debug("sock %d closed: %d\n", sock, 0!=optval);
	return (0!=optval);
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

    x_send_recv_init();

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

        // forward_data_thread(&client_sockfd);
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


