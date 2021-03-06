#include "../_ft_internal.h"

/*
See:

http://ftp.icm.edu.pl/packages/socks/socks4/SOCKS4.protocol
https://www.openssh.com/txt/socks4a.protocol

https://tools.ietf.org/html/rfc1928

*/

static bool ft_proto_socks_5_on_read(struct ft_proto_socks * this, struct ft_stream * stream, struct ft_frame * frame, bool * ok);

///

bool ft_proto_socks_init(struct ft_proto_socks * this, struct ft_proto_socks_delegate * delegate, struct ft_stream * stream)
{
	assert(this != NULL);
	assert(delegate != NULL);
	assert(stream != NULL);

	stream->base.socket.protocol = this;
	this->delegate = delegate;

	this->VN = 0;
	this->CD = 0;
	this->DSTPORT = 0;
	this->DSTIP = 0;

	this->authorized = false;
	this->SELMETHOD = 0xFF;

	return true;
}

void ft_proto_socks_fini(struct ft_proto_socks * this)
{
	assert(this != NULL);
}


static struct ft_frame * ft_proto_socks_stream_delegate_get_read_frame(struct ft_stream * stream)
{
	assert(stream != NULL);

	struct ft_frame * frame = ft_pool_borrow(&stream->base.socket.context->frame_pool, FT_FRAME_TYPE_RAW_DATA);
	if (frame == NULL) return NULL;

	ft_frame_format_empty(frame);
	ft_frame_append_vec(frame, 1); // Read  a VN into vector #1

	return frame;	
}


static void ft_proto_socks_on_request(struct ft_proto_socks * this, struct ft_stream * stream, struct ft_frame * frame)
{
	bool ok;

	char type;
	uint16_t port;
	static char host[256];

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN");

	assert(this != NULL);
	host[0] = '\0';

	if (this->CD != 1)
	{
		FT_ERROR_P("Invalid SOCKS command code (CD: %u)", this->CD);
		goto exit_send_error_response;
	}

	if ((this->VN == 4) && (this->DSTIP > 0) && (this->DSTIP <= 255))
	{
		// SOCKS4A
		port = this->DSTPORT;
		strncpy(host, ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 3)), sizeof(host)-1);
		host[sizeof(host)-1] = '\0';
		type = 'D';
	}

	else if (this->VN == 4)
	{
		// SOCKS4
		port = this->DSTPORT;
		snprintf(host, sizeof(host), "%u.%u.%u.%u", this->DSTIP >> 24 & 0xFF, this->DSTIP >> 16 & 0xFF, this->DSTIP >> 8 & 0xFF, this->DSTIP & 0xFF);
		type = '4';
	}

	else if (this->VN == 5)
	{
		port = this->DSTPORT;

		// SOCKS5
		if (this->ATYP == 1)
		{
			uint8_t * cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 2));
			cursor = ft_load_u32(cursor, &this->DSTIP);
			snprintf(host, sizeof(host), "%u.%u.%u.%u", this->DSTIP >> 24 & 0xFF, this->DSTIP >> 16 & 0xFF, this->DSTIP >> 8 & 0xFF, this->DSTIP & 0xFF);
			type = '4';
		}

		else if (this->ATYP == 3)
		{
			uint8_t len;
			uint8_t * cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 2));
			cursor = ft_load_u8(cursor, &len);
			snprintf(host, sizeof(host)-1, "%.*s", len, cursor);
			type = 'D';
		}

		else if (this->ATYP == 4)
		{
			uint8_t * cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 2));
			uint8_t ipv6[16];
			for (int i=0; i<16; i++)
				cursor = ft_load_u8(cursor, &ipv6[i]);

			snprintf(host, sizeof(host)-1, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
				ipv6[0], ipv6[1],
				ipv6[2], ipv6[3],
				ipv6[4], ipv6[5],
				ipv6[6], ipv6[7],
				ipv6[8], ipv6[9],
				ipv6[10], ipv6[11],
				ipv6[12], ipv6[13],
				ipv6[14], ipv6[15]
			);

			type = '6';
		}

		else
		{
			FT_ERROR_P("Unknown/unsupported SOCKS5 ATYP:%u", this->ATYP);
			goto exit_send_error_response;
		}
	}

	else
	{
		FT_ERROR_P("Unknown SOCKS protocol");
		goto exit_send_error_response;
	}

	if (host[0] == '\0')
	{
		FT_ERROR("Cannot resolve target host");
		goto exit_send_error_response;
	}

	//Call delegate method
	ok = this->delegate->connect(stream, type, host, port);
	if (!ok) goto exit_send_error_response;

	ft_frame_return(frame);


	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END OK");
	return;

exit_send_error_response:

	// Prepare response frame
	ft_frame_format_simple(frame);
	struct ft_vec * vec = ft_frame_get_vec(frame);
	uint8_t * cursor = ft_vec_ptr(vec);
	cursor = ft_store_u8(cursor, 0);  // VN
	cursor = ft_store_u8(cursor, 91); // CD
	cursor = ft_store_u16(cursor, 0); // DSTPORT
	cursor = ft_store_u32(cursor, 0); // DSTIP

	vec->limit = 8;
	vec->position = 8;

	ft_frame_flip(frame);

	ft_stream_write(stream, frame);
	ft_stream_cntl(stream, FT_STREAM_WRITE_SHUTDOWN);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END ERROR");
	return;
}


static bool ft_proto_socks_4_on_read(struct ft_proto_socks * this, struct ft_stream * stream, struct ft_frame * frame, bool * ok)
{
	uint8_t * cursor;
	struct ft_vec * vec;

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN");

	switch (frame->vec_limit)
	{

		case 2:
			if (this->VN == 4) // SOCKS4
			{
				cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 1));
				cursor = ft_load_u8(cursor, &this->CD);
				cursor = ft_load_u16(cursor, &this->DSTPORT);
				cursor = ft_load_u32(cursor, &this->DSTIP);

				ft_frame_append_vec(frame, 1);
				FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 2");
				return false; //Continue reading
			}

			FT_ERROR("Unknown SOCKS protocol version (VN: %u, vl: %d)", this->VN, frame->vec_limit);
			break;


		case 3:
			if (this->VN == 4) // SOCKS4
			{
				// Parse USERID
				vec = ft_frame_get_vec_at(frame, 2);
				char * p = ft_vec_ptr(vec);	

				if (p[-1] == '\0')
				{
					if ((this->DSTIP > 0) && (this->DSTIP <= 255)) // SOCK4A
					{
						ft_frame_append_vec(frame, 1);
						FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 3");
						return false; //Continue reading
					}

					else
					{
						ft_proto_socks_on_request(this, stream, frame);
						FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END req4");
						return true;
					}
				}

				// TODO: Ensure that capacity is sane
				// Keep filling this frame ...
				ft_frame_prev_vec(frame);
				vec->capacity += 1;
				vec->limit = vec->capacity;
				FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont");
				return false;
			}

			FT_ERROR("Unknown SOCKS protocol version (VN: %u, vl: %d)", this->VN, frame->vec_limit);
			break;


		case 4:
			if ((this->VN == 4) && (this->DSTIP > 0) && (this->DSTIP <= 255)) // SOCK4A
			{
				// Parse HOST

				vec = ft_frame_get_vec_at(frame, 3);
				char * p = ft_vec_ptr(vec);

				if (p[-1] == '\0')
				{
					ft_proto_socks_on_request(this, stream, frame);
					FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont req4A");
					return true;
				}

				// TODO: Ensure that capacity is sane
				// Keep filling this frame ...
				ft_frame_prev_vec(frame);
				vec->capacity += 1;
				vec->limit = vec->capacity;
				return false;
			}

			FT_ERROR("Unknown SOCKS protocol version (VN: %u, vl: %d)", this->VN, frame->vec_limit);
			break;
	}

	*ok = false;
	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END err");
	return false;
}


static bool ft_proto_socks_stream_delegate_read(struct ft_stream * stream, struct ft_frame * frame)
{
	bool ok;
	assert(stream != NULL);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN");

	struct ft_proto_socks * this = (struct ft_proto_socks *)stream->base.socket.protocol;
	assert(this != NULL);

	if (frame->type == FT_FRAME_TYPE_END_OF_STREAM)
	{
		if (!stream->flags.write_open)
			ft_frame_return(frame);
		else
			ft_stream_write(stream, frame); // Close write end as well
		FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END eos");
		return true;
	}

	if ((this->VN == 0) && (frame->vec_limit == 1))
	{
		// Parsing a initial VN
		uint8_t * cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 0));
		cursor = ft_load_u8(cursor, &this->VN);

		if (this->VN == 4) // SOCKS4
		{
			ft_frame_append_vec(frame, 7); // Read additional 7 bytes to the next vec
			FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 1");
			return false; //Continue reading
		}

		else if (this->VN == 5) // SOCKS5
		{
			ft_frame_append_vec(frame, 1); // Read additional 1 byte to the next vec
			FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 2");
			return false; //Continue reading	
		}

		FT_ERROR("Unknown SOCKS protocol version (VN: %u, vl:%d)", this->VN, frame->vec_limit);
	}

	else if (this->VN == 4)
	{
		ok = true;
		bool ret = ft_proto_socks_4_on_read(this, stream, frame, &ok);
		if (ok)
		{
			FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END ok4");
			return ret;
		}
	}

	else if (this->VN == 5)
	{
		ok = true;
		bool ret = ft_proto_socks_5_on_read(this, stream, frame, &ok);
		if (ok)
		{
			FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END ok5");
			return ret;
		}

	}

	FT_WARN("Failure to parse SOCKS request, closing the connection");

	ft_frame_return(frame);
	ft_stream_cntl(stream, FT_STREAM_WRITE_SHUTDOWN | FT_STREAM_READ_STOP);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END err");
	return true;
}


static void ft_proto_socks_stream_on_error(struct ft_stream * stream)
{
	assert(stream != NULL);
	struct ft_proto_socks * this = (struct ft_proto_socks *)stream->base.socket.protocol;
	assert(this != NULL);

	// Bubble the error up
	if (this->delegate->error != NULL) this->delegate->error(stream);
}

struct ft_stream_delegate ft_stream_proto_socks_delegate = {
	.get_read_frame = ft_proto_socks_stream_delegate_get_read_frame,
	.read = ft_proto_socks_stream_delegate_read,
	.error = ft_proto_socks_stream_on_error,
};


bool ft_proto_socks_stream_send_final_response(struct ft_stream * stream, int reply)
{
	bool ok;
	struct ft_proto_socks * this = (struct ft_proto_socks *)stream->base.socket.protocol;
	assert(this != NULL);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN reply:%d", reply);

	struct ft_frame * frame = ft_pool_borrow(&stream->base.socket.context->frame_pool, FT_FRAME_TYPE_RAW_DATA);
	if (frame == NULL)
	{
		FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END no-frame");
		return false;
	}
	ft_frame_format_simple(frame);

	if (this->VN == 4)
	{
		struct ft_vec * vec = ft_frame_get_vec(frame);
		uint8_t * cursor = ft_vec_ptr(vec);
		cursor = ft_store_u8(cursor, 0);  // VN
		cursor = ft_store_u8(cursor, (reply == 0) ? 90: 91); // CD
		cursor = ft_store_u16(cursor, 0); // DSTPORT (value is ignored based on protocol specs)
		cursor = ft_store_u32(cursor, 0); // DSTIP (value is ignored based on protocol specs)

		vec->limit = 8;
		vec->position = 8;
	}

	else if (this->VN == 5)
	{
		struct ft_vec * vec = ft_frame_get_vec(frame);
		uint8_t * cursor = ft_vec_ptr(vec);
		uint8_t * initial = cursor;
		cursor = ft_store_u8(cursor, 5);  // VER protocol version: X'05'
		cursor = ft_store_u8(cursor, reply); //  REP Reply field
		cursor = ft_store_u8(cursor, 0); // RSV RESERVED
		cursor = ft_store_u8(cursor, 1); // ATYP
		cursor = ft_store_u32(cursor, 0); // BND.ADDR
		cursor = ft_store_u16(cursor, 0); // BND.PORT

		vec->limit = cursor - initial;
		vec->position = cursor - initial;
	}

	else
	{
		FT_ERROR_P("Unimplemented protocol version: %u", this->VN);
		ft_frame_return(frame);
		FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END err-proto");
		return false;
	}

	ft_frame_flip(frame);

	ok = ft_stream_write(stream, frame);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END ok:%c", ok ? 'y' : 'N');
	return ok;
}

// SOCKS5

static bool sock_relay_on_socks5_method_selection_request(struct ft_proto_socks * this, struct ft_stream * stream, struct ft_frame * frame)
{
	struct ft_vec * vec = ft_frame_get_vec_at(frame, 2);
	uint8_t * p = ft_vec_begin_ptr(vec);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN");

	assert(this->SELMETHOD == 0xFF);

	// Evaluate methods
	for(int i=0; i<vec->position; i++)
	{
		if (p[i] == 0) // NO AUTHENTICATION REQUIRED
		{
			this->SELMETHOD = 0;
		}
	}

	// Prepare response frame
	ft_frame_format_simple(frame);
	vec = ft_frame_get_vec(frame);
	uint8_t * cursor = ft_vec_ptr(vec);
	cursor = ft_store_u8(cursor, 0x05);  // VER
	cursor = ft_store_u8(cursor, this->SELMETHOD); // CD
	vec->limit = 2;
	vec->position = 2;
	ft_frame_flip(frame);
	ft_stream_write(stream, frame);
	if (this->SELMETHOD == 0xFF) ft_stream_cntl(stream, FT_STREAM_WRITE_SHUTDOWN);

	if (this->SELMETHOD == 0) this->authorized = true;

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END");

	return true;
}


static bool ft_proto_socks_5_on_read(struct ft_proto_socks * this, struct ft_stream * stream, struct ft_frame * frame, bool * ok)
{
	assert(this != NULL);
	assert(stream != NULL);
	assert(frame != NULL);

	uint8_t * cursor;
	uint8_t b;

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "BEGIN");

	if (!this->authorized)
	{
		if (this->SELMETHOD == 0xFF)
		{
			switch (frame->vec_limit)
			{
				case 2:
					cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 1));
					cursor = ft_load_u8(cursor, &b);

					if (b == 0) break;

					ft_frame_append_vec(frame, b);
					FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 2");
					return false; //Continue reading

				case 3:
					sock_relay_on_socks5_method_selection_request(this, stream, frame);
					FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont 3");
					return true;
			}
		}
	}

	else
	{
		switch (frame->vec_limit)
		{
			case 1:
				ft_frame_append_vec(frame, 3);
				FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n1");
				return false; //Continue reading

			case 2:
				{
					cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 1));
					cursor = ft_load_u8(cursor, &this->CD);
					cursor = ft_load_u8(cursor, &b);
					cursor = ft_load_u8(cursor, &this->ATYP);

					switch (this->ATYP)
					{
						case 1:
							ft_frame_append_vec(frame, 4); // Read 4 bytes of IPv4 into vector #2
							FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n21");
							return false; //Continue reading

						case 3:
							ft_frame_append_vec(frame, 1); // Read Hostname into vector #2
							FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n23");
							return false; //Continue reading

						case 4:
							ft_frame_append_vec(frame, 16);  // Read 16 bytes of IPv6 into vector #2
							FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n24");
							return false; //Continue reading

						default:
							FT_WARN("Unknown ATYP: %u", this->ATYP);
					}
				}
				break;

			case 3:
				if (this->ATYP == 3) // DOMAINNAME: X'03'
				{
					struct ft_vec * vec = ft_frame_get_vec_at(frame, 2);
					if (vec->capacity == 1)
					{
						uint8_t *len = ft_vec_ptr(vec);

						ft_frame_prev_vec(frame);
						vec->capacity += len[-1];
						vec->limit = vec->capacity;
						FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n3");
						return false; //Continue reading
					}

				}
				
				ft_frame_append_vec(frame, 2); // Read DST.PORT into vector #3
				FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END cont n31");
				return false; //Continue reading

			case 4:
				cursor = ft_vec_begin_ptr(ft_frame_get_vec_at(frame, 3));
				cursor = ft_load_u16(cursor, &this->DSTPORT);

				ft_proto_socks_on_request(this, stream, frame);
				FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END ok");
				return true;
		}
	}

	FT_WARN("Failure to parse SOCKS request, closing the connection (vl: %d)", frame->vec_limit);

	ft_frame_return(frame);
	ft_stream_cntl(stream, FT_STREAM_WRITE_SHUTDOWN | FT_STREAM_READ_STOP);

	FT_TRACE(FT_TRACE_ID_PROTO_SOCK, "END error");
	return true;
}
