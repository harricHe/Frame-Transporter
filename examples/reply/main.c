#include "all.h"

struct context context;
struct exiting_watcher watcher;

///

static size_t on_read_advise(struct established_socket * established_sock, struct frame * frame)
{
	return frame_currect_dvec_size(frame);
}

bool on_read(struct established_socket * established_sock, struct frame * frame)
{
//	frame_print(frame);	
	frame_flip(frame);
	established_socket_write(established_sock, frame);
	return true;
}

void on_close(struct established_socket * established_sock)
{
	printf("on_close()\n");
}

struct established_socket_cb sock_est_sock_cb = 
{
	.read_advise = on_read_advise,
	.read = on_read,
	.close = on_close,
};

///
struct listening_socket_chain * lsocks = NULL;
struct established_socket established_sock; //TODO: Replace by some kind of list

static bool on_accept_cb(struct listening_socket * listening_socket, int fd, const struct sockaddr * client_addr, socklen_t client_addr_len)
{
	bool ok;
	ok = established_socket_init_accept(&established_sock, &sock_est_sock_cb, listening_socket, fd, client_addr, client_addr_len);
	if (!ok) return false;

	established_socket_read_start(&established_sock);

	return true;
}

static void on_exiting_cb(struct exiting_watcher * watcher, struct context * context)
{
	listening_socket_chain_stop(lsocks);
}

int main(int argc, char const *argv[])
{
	bool ok;
	int rc;
	
	logging_set_verbose(true);
	libsccmn_init();
	
	ok = context_init(&context);
	if (!ok) return EXIT_FAILURE;

	// Start listening
	rc = listening_socket_chain_extend_getaddrinfo(&lsocks, &context, AF_INET, SOCK_STREAM, "127.0.0.1", "12345", on_accept_cb);
	if (rc < 0) return EXIT_FAILURE;

	rc = listening_socket_chain_extend_getaddrinfo(&lsocks, &context, AF_UNIX, SOCK_STREAM, "/tmp/sctext.tmp", NULL, on_accept_cb);
	if (rc < 0) return EXIT_FAILURE;

	listening_socket_chain_start(lsocks);

	context_exiting_watcher_add(&context, &watcher, on_exiting_cb);

	context_evloop_run(&context);

	listening_socket_chain_del(lsocks);

	context_fini(&context);

	return EXIT_SUCCESS;
}
