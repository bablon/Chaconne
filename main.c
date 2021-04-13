#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "cli-term.h"
#include "event-loop.h"

static struct termios new, old;

void atexit_func(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	printf("atexit\n");
}

void handle_signal(int signo)
{
	if (signo == SIGQUIT) {
		printf("Got SIGQUIT\n");
		exit(1);
	} else if (signo == SIGINT) {
		printf("Got SIGINT\n");
		exit(1);
	} else if (signo == SIGCONT) {
		printf("Got SIGCONT\n");
		tcsetattr(STDIN_FILENO, TCSANOW, &new);
		signal(SIGTSTP, handle_signal);
	} else if (signo == SIGTSTP) {
		printf("Got SIGTSTP\n");
		tcsetattr(STDIN_FILENO, TCSANOW, &old);
		signal(SIGTSTP, SIG_DFL);
		kill(getpid(), SIGTSTP);
	}
}

#if 0
__attribute__((constructor))
static void cons(void)
{
	tcgetattr(STDIN_FILENO, &new);
	memcpy(&old, &new, sizeof(old));
}

__attribute__((destructor))
static void dest(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	printf("destructor\n");
}
#endif

int setnonblocking(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		return -errno;

	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
		return -errno;

	return 0;
}

int setreuseaddr(int sock)
{
	int opt = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		perror("setsockopt so_reuseaddr");
		return -errno;
	}

	return 0;
}

#ifdef SO_REUSEPORT
int setreuseport(int sock)
{
	int opt = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
		perror("setsockopt so_reuseport");
		return -errno;
	}

	return 0;
}
#else
int setreuseport(int sock)
{
	return 0;
}
#endif

int server_create(uint16_t port)
{
	int fd;
	struct sockaddr_in sin;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		return -errno;

	setreuseaddr(fd);
	setreuseport(fd);

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		perror("bind");
		close(fd);
		return -errno;
	}

	if (listen(fd, 16) == -1) {
		perror("listen");
		close(fd);
		return -errno;
	}

	return fd;
}

#define MAX_NR_CLIENTS	8

struct zebra_server {
	struct event_source *source;
	struct event_loop *loop;
	int fd;

	struct term *clients[MAX_NR_CLIENTS];
};

static void zebra_server_destroy(struct zebra_server *srv)
{
	int i, fd;

	for (i = 0; i < MAX_NR_CLIENTS; i++) {
		if (srv->clients[i]) {
			fd = term_fd(srv->clients[i]);
			term_destroy(srv->clients[i]);
			close(fd);
		}
	}

	event_source_remove(srv->source);
	close(srv->fd);
}

static int zebra_accept(int fd, uint32_t mask, void *data)
{
	int cfd, i;
	struct sockaddr_in sin;
	socklen_t socklen = sizeof(sin);
	struct term *term;
	struct zebra_server *srv = data;

	event_source_fd_update(srv->source, EVENT_READABLE);

	cfd = accept(fd, (struct sockaddr *)&sin, &socklen);
	if (cfd == -1) {
		perror("accept");
		return -errno;
	}

	for (i = 0; i < MAX_NR_CLIENTS; i++) {
		if (srv->clients[i] == NULL)
			break;
	}

	if (i == MAX_NR_CLIENTS) {
		close(cfd);
		return 0;
	}

	term = term_create(srv->loop, cfd, "remote");
	if (term == NULL) {
		printf("failed to create terminal\n");
		close(cfd);
		return -1;
	}

	srv->clients[i] = term;

	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	struct term *term;
	struct event_loop *loop;
	struct zebra_server zebra;

	memset(&zebra, 0, sizeof(zebra));

	signal(SIGQUIT, handle_signal);
	signal(SIGINT, handle_signal);
	signal(SIGCONT, handle_signal);
	signal(SIGTSTP, handle_signal);
	// atexit(atexit_func);

	if (ttyname(STDIN_FILENO)) {
		tcgetattr(STDIN_FILENO, &new);
		memcpy(&old, &new, sizeof(old));

		new.c_lflag &= ~ICANON;
		new.c_lflag &= ~ECHO;
		tcsetattr(STDIN_FILENO, TCSANOW, &new);
	}

	loop = event_loop_create();
	if (loop == NULL)
		exit(1);

	zebra.fd = server_create(2601);
	if (zebra.fd == -1)
		exit(1);

	zebra.loop = loop;
	zebra.source = event_loop_add_fd(loop, zebra.fd, 1, EVENT_READABLE, zebra_accept, &zebra);

	term = term_create(loop, STDIN_FILENO, NULL);
	if (term == NULL)
		exit(1);

	while (!term_want_exit(term)) {
		event_loop_dispatch(loop, -1);

		for (i = 0; i < MAX_NR_CLIENTS; i++) {
			if (zebra.clients[i] && term_want_exit(zebra.clients[i])) {
				int fd;
				fd = term_fd(zebra.clients[i]);
				term_destroy(zebra.clients[i]);
				close(fd);
				zebra.clients[i] = NULL;
			}
		}
	}

	zebra_server_destroy(&zebra);
	term_destroy(term);
	event_loop_destroy(loop);

	tcsetattr(STDIN_FILENO, TCSANOW, &old);

	return 0;
}
