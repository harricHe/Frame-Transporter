#include "testutils.h"

////

struct established_socket established_sock;

///

// This test is about incoming stream that 'hangs open' after all data are uploaded

struct frame * sock_est_1_on_get_read_frame(struct established_socket * established_sock)
{
	struct frame_dvec * dvec;

	struct frame * frame = frame_pool_borrow(&established_sock->context->frame_pool, frame_type_RAW_DATA);
	ck_assert_ptr_ne(frame, NULL);
	ck_assert_int_eq(frame->dvec_position, 0);
	ck_assert_int_eq(frame->dvec_limit, 0);

	dvec = frame_add_dvec(frame, 0, 5);
	ck_assert_ptr_ne(dvec, NULL);
	ck_assert_int_eq(dvec->position, 0);
	ck_assert_int_eq(dvec->limit, 5);
	ck_assert_int_eq(dvec->capacity, 5);

	ck_assert_int_eq(frame->dvec_position, 0);
	ck_assert_int_eq(frame->dvec_limit, 1);

	dvec = frame_add_dvec(frame, 5, 6);
	ck_assert_ptr_ne(dvec, NULL);
	ck_assert_int_eq(dvec->position, 0);
	ck_assert_int_eq(dvec->limit, 6);
	ck_assert_int_eq(dvec->capacity, 6);

	ck_assert_int_eq(frame->dvec_position, 0);
	ck_assert_int_eq(frame->dvec_limit, 2);

	return frame;
}

bool sock_est_1_on_read(struct established_socket * established_sock, struct frame * frame)
{
	ck_assert_int_ne(frame->type, frame_type_FREE);

	if (frame->type == frame_type_RAW_DATA)
	{
		ck_assert_int_eq(established_sock->syserror, 0);

		ck_assert_int_eq(memcmp(frame->data, "1234\nABCDE\n", 11), 0);
		frame_pool_return(frame);

		established_socket_write_shutdown(established_sock);
		return true;
	}


	if (frame->type == frame_type_STREAM_END)
	{
		ck_assert_int_eq(established_sock->syserror, 0);
		ck_assert_int_eq(established_sock->stats.read_bytes, 11);

		established_socket_write_shutdown(established_sock);
		return false;
	}

	ck_abort();
	return false;
}

struct established_socket_cb sock_est_1_sock_cb = 
{
	.get_read_frame = sock_est_1_on_get_read_frame,
	.read = sock_est_1_on_read,
	.error = NULL
};

bool sock_est_1_on_accept(struct listening_socket * listening_socket, int fd, const struct sockaddr * client_addr, socklen_t client_addr_len)
{
	bool ok;

	ok = established_socket_init_accept(&established_sock, &sock_est_1_sock_cb, listening_socket, fd, client_addr, client_addr_len);
	ck_assert_int_eq(ok, true);

	listening_socket_stop(listening_socket);

	return true;
}

struct listening_socket_cb sock_est_1_listen_cb = 
{
	.accept = sock_est_1_on_accept,
};


START_TEST(sock_est_1_utest)
{
	struct listening_socket listen_sock;
	int rc;
	bool ok;

	struct context context;
	ok = context_init(&context);
	ck_assert_int_eq(ok, true);


	struct addrinfo * rp = NULL;
	ok = resolve(&rp, "127.0.0.1", "12345");
	ck_assert_int_eq(ok, true);
	ck_assert_ptr_ne(rp, NULL);

	ok = listening_socket_init(&listen_sock, &sock_est_1_listen_cb, &context, rp);
	ck_assert_int_eq(ok, true);

	freeaddrinfo(rp);

	ok = listening_socket_start(&listen_sock);
	ck_assert_int_eq(ok, true);

	FILE * p = popen("nc localhost 12345", "w");
	ck_assert_ptr_ne(p, NULL);

	fprintf(p, "1234\nABCDE\n");
	fflush(p);

	context_evloop_run(&context);

	rc = pclose(p);
	if ((rc == -1) && (errno == ECHILD)) rc = 0; // Override too quick execution error
	ck_assert_int_eq(rc, 0);

	ok = listening_socket_stop(&listen_sock);
	ck_assert_int_eq(ok, true);

	listening_socket_fini(&listen_sock);

	//TODO: Temporary
	established_socket_fini(&established_sock);

	context_fini(&context);
}
END_TEST

///

// This test is about incoming stream that terminates after all data are uploaded

bool sock_est_2_on_read(struct established_socket * established_sock, struct frame * frame)
{
	ck_assert_int_ne(frame->type, frame_type_FREE);

	if (frame->type == frame_type_RAW_DATA)
	{
		ck_assert_int_eq(established_sock->syserror, 0);

		return false;
	}


	if (frame->type == frame_type_STREAM_END)
	{
		ck_assert_int_eq(established_sock->syserror, 0);

		established_socket_write_shutdown(established_sock);
		return false;
	}

	ck_abort();
	return false;
}

struct established_socket_cb sock_est_2_sock_cb = 
{
	.get_read_frame = get_read_frame_simple,
	.read = sock_est_2_on_read,
};

bool sock_est_2_on_accept(struct listening_socket * listening_socket, int fd, const struct sockaddr * client_addr, socklen_t client_addr_len)
{
	bool ok;

	ok = established_socket_init_accept(&established_sock, &sock_est_2_sock_cb, listening_socket, fd, client_addr, client_addr_len);
	ck_assert_int_eq(ok, true);

	established_socket_set_read_partial(&established_sock, true);


	// Stop listening
	listening_socket_stop(listening_socket);

	return true;
}

struct listening_socket_cb sock_est_2_listen_cb = 
{
	.accept = sock_est_2_on_accept,
};


START_TEST(sock_est_2_utest)
{
	struct listening_socket listen_sock;
	int rc;
	bool ok;

	libsccmn_config.log_verbose = true;
	//libsccmn_config.log_trace_mask |= L_TRACEID_SOCK_STREAM | L_TRACEID_EVENT_LOOP;

	struct context context;
	ok = context_init(&context);
	ck_assert_int_eq(ok, true);

	struct addrinfo * rp = NULL;
	ok = resolve(&rp, "127.0.0.1", "12345");
	ck_assert_int_eq(ok, true);
	ck_assert_ptr_ne(rp, NULL);

	ok = listening_socket_init(&listen_sock, &sock_est_2_listen_cb, &context, rp);
	ck_assert_int_eq(ok, true);

	freeaddrinfo(rp);

	ok = listening_socket_start(&listen_sock);
	ck_assert_int_eq(ok, true);

	FILE * p = popen("cat /etc/hosts | nc localhost 12345", "w");
	ck_assert_ptr_ne(p, NULL);

	context_evloop_run(&context);

	rc = pclose(p);
	if ((rc == -1) && (errno == ECHILD)) rc = 0; // Override too quick execution error
	ck_assert_int_eq(rc, 0);

	ok = listening_socket_stop(&listen_sock);
	ck_assert_int_eq(ok, true);

	listening_socket_fini(&listen_sock);

	//TODO: Temporary
	established_socket_fini(&established_sock);

	context_fini(&context);
}
END_TEST

///

void sock_est_conn_fail_on_error(struct established_socket * established_sock)
{
	printf("ERROR - NICE!\n");
}


struct established_socket_cb sock_est_conn_fail_cb = 
{
	.connected = NULL,
	.get_read_frame = NULL,
	.read = NULL,
	.error = sock_est_conn_fail_on_error,
};

START_TEST(sock_est_conn_fail_utest)
{
	struct established_socket sock;
	bool ok;

	struct context context;
	ok = context_init(&context);
	ck_assert_int_eq(ok, true);

	struct addrinfo * rp = NULL;
	ok = resolve(&rp, "127.0.0.1", "12346");
	ck_assert_int_eq(ok, true);
	ck_assert_ptr_ne(rp, NULL);

	ok = established_socket_init_connect(&sock, &sock_est_conn_fail_cb, &context, rp);
	ck_assert_int_eq(ok, true);

	freeaddrinfo(rp);

	struct frame * frame = frame_pool_borrow(&context.frame_pool, frame_type_RAW_DATA);
	ck_assert_ptr_ne(frame, NULL);

	frame_format_simple(frame);
	struct frame_dvec * dvec = frame_current_dvec(frame);
	ck_assert_ptr_ne(dvec, NULL);	

	ok = frame_dvec_sprintf(dvec, "Virtually anything, will be lost anyway");

	frame_flip(frame);

	ok = established_socket_write(&sock, frame);
	ck_assert_int_eq(ok, true);

	ok = established_socket_write_shutdown(&sock);
	ck_assert_int_eq(ok, true);

	context_evloop_run(&context);


	established_socket_fini(&sock);

	context_fini(&context);

}
END_TEST

///

ssize_t sock_est_ssl_1_read_counter;
EVP_MD_CTX * sock_est_ssl_1_mdctx;

void sock_est_ssl_1_on_connected(struct established_socket * this)
{
	ck_assert_int_eq(this->flags.ssl_status, 2);
	ck_assert_int_eq(this->flags.connecting, false);

	established_socket_read_start(this);
}

bool sock_est_ssl_1_on_read(struct established_socket * established_sock, struct frame * frame)
{
	ck_assert_int_ne(frame->type, frame_type_FREE);

	if (frame->type == frame_type_RAW_DATA)
	{
		ck_assert_int_eq(established_sock->syserror, 0);

		frame_flip(frame);
		ck_assert_int_gt(frame_total_position_to_limit(frame), 0);
		//frame_print(frame);

		for (struct frame_dvec * dvec = frame_current_dvec(frame); dvec != NULL; dvec = frame_next_dvec(frame))
		{
			int rc = EVP_DigestUpdate(sock_est_ssl_1_mdctx, frame_dvec_ptr(dvec), frame_dvec_len(dvec));
			ck_assert_int_eq(rc, 1);
		}

		sock_est_ssl_1_read_counter += frame_total_position_to_limit(frame);
	}

	frame_pool_return(frame);
	return true;
}

void sock_est_ssl_1_on_error(struct established_socket * established_sock)
{
	ck_assert_int_eq(0,1);
}


struct established_socket_cb sock_est_ssl_1_cb = 
{
	.connected = sock_est_ssl_1_on_connected,
	.get_read_frame = get_read_frame_simple,
	.read = sock_est_ssl_1_on_read,
	.error = sock_est_ssl_1_on_error,
};


START_TEST(sock_est_ssl_1_utest)
{
	struct established_socket sock;
	bool ok;
	int rc;

	generate_random_file("./sock_est_ssl_1_utest.bin", 4096, 100);

	EVP_MD_CTX * mdctx = EVP_MD_CTX_create();
	ck_assert_ptr_ne(mdctx, NULL);

	rc = EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
	ck_assert_int_eq(rc, 1);

	digest_file(mdctx, "./sock_est_ssl_1_utest.bin");

	unsigned char digest1[EVP_MAX_MD_SIZE];
	EVP_DigestFinal(mdctx, digest1, NULL);
    EVP_MD_CTX_cleanup(mdctx);
	
	sock_est_ssl_1_mdctx = EVP_MD_CTX_create();
	ck_assert_ptr_ne(sock_est_ssl_1_mdctx, NULL);
	
	rc = EVP_DigestInit_ex(sock_est_ssl_1_mdctx, EVP_sha256(), NULL);
	ck_assert_int_eq(rc, 1);

	libsccmn_init();

	libsccmn_config.log_trace_mask |= L_TRACEID_SOCK_STREAM | L_TRACEID_EVENT_LOOP;

	sock_est_ssl_1_read_counter = 0;

	SSL_CTX * ssl_ctx = SSL_CTX_new(TLSv1_2_client_method());
	ck_assert_ptr_ne(ssl_ctx, NULL);

	struct context context;
	ok = context_init(&context);
	ck_assert_int_eq(ok, true);

	int inf = 0, outf = 0;
	pid_t pid = popen2("openssl s_server -accept 12345 -tls1_2 -key ./ssl/key.pem -cert ./ssl/cert.pem -msg -HTTP", &inf, &outf);
	ck_assert_int_ge(pid, 0);
	ck_assert_int_ge(inf, 0);
	ck_assert_int_ge(outf, 0);
	usleep(60000);

	struct addrinfo * rp = NULL;
	ok = resolve(&rp, "127.0.0.1", "12345");
	ck_assert_int_eq(ok, true);
	ck_assert_ptr_ne(rp, NULL);

	ok = established_socket_init_connect(&sock, &sock_est_ssl_1_cb, &context, rp);
	ck_assert_int_eq(ok, true);

	freeaddrinfo(rp);

	established_socket_set_read_partial(&sock, true);

	ok = established_socket_ssl_enable(&sock, ssl_ctx);
	ck_assert_int_eq(ok, true);


	struct frame * frame = frame_pool_borrow(&context.frame_pool, frame_type_RAW_DATA);
	ck_assert_ptr_ne(frame, NULL);

	frame_format_simple(frame);
	struct frame_dvec * dvec = frame_current_dvec(frame);
	ck_assert_ptr_ne(dvec, NULL);	

	ok = frame_dvec_sprintf(dvec, "GET /sock_est_ssl_1_utest.bin HTTP/1.0\r\n\r\n");
	ck_assert_int_eq(ok, true);

	frame_flip(frame);

	ok = established_socket_write(&sock, frame);
	ck_assert_int_eq(ok, true);

	ok = established_socket_write_shutdown(&sock);
	ck_assert_int_eq(ok, true);

	context_evloop_run(&context);

	unsigned char digest2[EVP_MAX_MD_SIZE];
	EVP_DigestFinal(sock_est_ssl_1_mdctx, digest2, NULL);
    EVP_MD_CTX_cleanup(sock_est_ssl_1_mdctx);
    ck_assert_int_eq(memcmp(digest1, digest2, EVP_MD_size(EVP_sha256())), 0);

	ck_assert_int_eq(sock_est_ssl_1_read_counter, 4096 * 100);

	rc = kill(pid, SIGINT);
	ck_assert_int_eq(rc, 0);

	char buffer[128*1024];
	ssize_t x = read(outf, buffer, sizeof(buffer));
	ck_assert_int_gt(x, 0);

	close(inf);
	close(outf);

	established_socket_fini(&sock);

	context_fini(&context);

}
END_TEST

///

Suite * sock_est_tsuite(void)
{
	TCase *tc;
	Suite *s = suite_create("sock_est");

	tc = tcase_create("sock_est-core");
	suite_add_tcase(s, tc);
	tcase_add_test(tc, sock_est_1_utest);
	tcase_add_test(tc, sock_est_2_utest);
	tcase_add_test(tc, sock_est_conn_fail_utest);

	tc = tcase_create("sock_est-ssl");
	suite_add_tcase(s, tc);
	tcase_add_test(tc, sock_est_ssl_1_utest);

	return s;
}
