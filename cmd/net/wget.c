/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "arg.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct Stream {
	int fd;
	char buf[8192];
	size_t len;
	size_t idx;
};

static void
usage(void)
{
	eprintf("usage: %s [-O file] [-p post_data] [-r limit] url\n", argv0);
}

static int
dial(const char *host, const char *port)
{
	struct addrinfo hints, *res, *rp;
	int fd = -1, r;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	r = getaddrinfo(host, port, &hints, &res);
	if (r != 0) {
		weprintf("getaddrinfo %s:%s: %s\n", host, port, gai_strerror(r));
		return -1;
	}

	for (rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);
	return fd;
}

static void
parse_url(char *url, char **host, char **port, char **path)
{
	char *p, *ss;

	if (strncasecmp(url, "http://", 7) == 0) {
		url += 7;
	} else if (strncasecmp(url, "https://", 8) == 0) {
		url += 8;
	} else {
		eprintf("unsupported protocol or invalid url: %s\n", url);
	}

	*host = url;
	p = strchr(url, '/');
	if (p) {
		*p = '\0';
		*path = p + 1;
	} else {
		*path = "";
	}

	/* handle ipv6 brackets or host:port */
	if (**host == '[') {
		(*host)++;
		ss = strchr(*host, ']');
		if (ss) {
			*ss = '\0';
			ss++;
			if (*ss == ':')
				*port = ss + 1;
			else
				*port = "80";
		} else {
			eprintf("invalid ipv6 literal: %s\n", *host);
		}
	} else {
		p = strrchr(*host, ':');
		if (p) {
			*p = '\0';
			*port = p + 1;
		} else {
			*port = "80";
		}
	}
}

static char *
find_header(const char *headers, const char *name)
{
	const char *p;
	size_t len = strlen(name);

	p = headers;
	while (p && *p) {
		if (strncasecmp(p, name, len) == 0) {
			p += len;
			while (*p == ' ' || *p == '\t')
				p++;
			len = strcspn(p, "\r\n");
			return estrndup(p, len);
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return NULL;
}

static int
stream_getc(struct Stream *s)
{
	ssize_t r;

	if (s->idx < s->len) {
		return (unsigned char)s->buf[s->idx++];
	}
	s->idx = 0;
	r = read(s->fd, s->buf, sizeof(s->buf));
	if (r <= 0) {
		s->len = 0;
		return EOF;
	}
	s->len = (size_t)r;
	return (unsigned char)s->buf[s->idx++];
}

static size_t
stream_read(struct Stream *s, void *ptr, size_t size)
{
	size_t total = 0;
	size_t n;
	char *p = ptr;
	ssize_t r;

	while (total < size) {
		if (s->idx < s->len) {
			n = MIN(size - total, s->len - s->idx);
			memcpy(p + total, s->buf + s->idx, n);
			s->idx += n;
			total += n;
		} else {
			s->idx = 0;
			r = read(s->fd, s->buf, sizeof(s->buf));
			if (r <= 0) {
				s->len = 0;
				break;
			}
			s->len = (size_t)r;
		}
	}
	return total;
}

static void
read_chunked(struct Stream *s, int out_fd)
{
	char line[128];
	char chunk_buf[8192];
	size_t line_len, n;
	long long chunk_size, remaining;
	int c;

	for (;;) {
		line_len = 0;
		for (;;) {
			c = stream_getc(s);
			if (c == EOF)
				eprintf("unexpected end of file reading chunk size\n");
			if (c == '\n') {
				line[line_len] = '\0';
				break;
			}
			if (c != '\r' && line_len < sizeof(line) - 1) {
				line[line_len++] = c;
			}
		}

		chunk_size = strtoll(line, NULL, 16);
		if (chunk_size == 0) {
			stream_getc(s);
			stream_getc(s);
			break;
		}

		remaining = chunk_size;
		while (remaining > 0) {
			n = stream_read(s, chunk_buf, MIN(remaining, (long long)sizeof(chunk_buf)));
			if (n == 0)
				eprintf("unexpected end of file in chunk data\n");
			if (writeall(out_fd, chunk_buf, n) < 0)
				eprintf("write output:\n");
			remaining -= n;
		}

		stream_getc(s);
		stream_getc(s);
	}
}

static void
read_non_chunked(struct Stream *s, int out_fd, long long content_len)
{
	char chunk_buf[8192];
	long long remaining = content_len;
	size_t n, to_read;

	while (content_len < 0 || remaining > 0) {
		to_read = sizeof(chunk_buf);
		if (content_len >= 0)
			to_read = (size_t)MIN(remaining, (long long)sizeof(chunk_buf));
		n = stream_read(s, chunk_buf, to_read);
		if (n == 0) {
			if (content_len >= 0)
				eprintf("unexpected end of file\n");
			break;
		}
		if (writeall(out_fd, chunk_buf, n) < 0)
			eprintf("write output:\n");
		if (content_len >= 0)
			remaining -= n;
	}
}

int
main(int argc, char *argv[])
{
	struct Stream s;
	char *Oflag = NULL;
	char *pflag = NULL;
	char *rflag = NULL;
	char *url, *host, *port, *path, *loc;
	char *curr_host, *curr_port, *curr_path;
	char *new_url;
	char *cl_str;
	char *te_str;
	char *header_end;
	char *out_name;
	int redirects = 0;
	int max_redirects = 20;
	int sock = -1;
	int out_fd = 1;
	int chunked;
	int status;
	long long content_len;
	size_t total_read;
	ssize_t n;
	size_t dir_len;
	char *last_slash;

	ARGBEGIN {
	case 'O':
		Oflag = EARGF(usage());
		break;
	case 'p':
		pflag = EARGF(usage());
		break;
	case 'r':
		rflag = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (argc < 1)
		usage();

	if (rflag)
		max_redirects = estrtonum(rflag, 0, 1000);

	url = estrdup(argv[0]);

	while (sock < 0) {
		if (redirects > max_redirects)
			eprintf("too many redirects\n");

		/* parse url and backup original parts for relative redirect resolves */
		curr_host = curr_port = curr_path = NULL;
		parse_url(url, &curr_host, &curr_port, &curr_path);

		host = estrdup(curr_host);
		port = estrdup(curr_port);
		path = estrdup(curr_path);

		sock = dial(host, port);
		if (sock < 0)
			eprintf("failed to connect to %s:%s\n", host, port);

		if (pflag) {
			dprintf(sock, "POST /%s HTTP/1.1\r\n"
			              "Host: %s\r\n"
			              "User-Agent: wget/aruu\r\n"
#ifdef STD_NON_POSIX
			              "Accept: */*\r\n"
#endif
			              "Content-Length: %zu\r\n"
			              "Connection: close\r\n\r\n",
			              path, host, strlen(pflag));
			writeall(sock, pflag, strlen(pflag));
		} else {
			dprintf(sock, "GET /%s HTTP/1.1\r\n"
			              "Host: %s\r\n"
			              "User-Agent: wget/aruu\r\n"
#ifdef STD_NON_POSIX
			              "Accept: */*\r\n"
#endif
			              "Connection: close\r\n\r\n",
			              path, host);
		}

		/* read headers */
		total_read = 0;
		header_end = NULL;
		memset(s.buf, 0, sizeof(s.buf));
		while (total_read < sizeof(s.buf) - 1) {
			n = read(sock, s.buf + total_read, sizeof(s.buf) - 1 - total_read);
			if (n <= 0) {
				if (n < 0)
					eprintf("read socket:\n");
				else
					eprintf("connection closed by server\n");
			}
			total_read += n;
			s.buf[total_read] = '\0';
			header_end = strstr(s.buf, "\r\n\r\n");
			if (header_end)
				break;
		}

		if (!header_end)
			eprintf("http header too large or not found\n");

		*header_end = '\0';
		s.fd = sock;
		s.len = total_read;
		s.idx = (header_end + 4) - s.buf;

		/* parse HTTP status code */
		if (strncasecmp(s.buf, "HTTP/1.1 ", 9) != 0 &&
		    strncasecmp(s.buf, "HTTP/1.0 ", 9) != 0) {
			eprintf("invalid http response: %s\n", s.buf);
		}
		status = atoi(s.buf + 9);

		if (status >= 300 && status < 400) {
			loc = find_header(s.buf, "Location:");
			if (!loc)
				eprintf("redirect response without location header\n");

			if (strncasecmp(loc, "http://", 7) == 0 ||
			    strncasecmp(loc, "https://", 8) == 0) {
				new_url = estrdup(loc);
			} else if (loc[0] == '/') {
				new_url = emalloc(8 + strlen(host) + strlen(port) + strlen(loc) + 2);
				sprintf(new_url, "http://%s:%s%s", host, port, loc);
			} else {
				last_slash = strrchr(path, '/');
				dir_len = 0;
				if (last_slash)
					dir_len = last_slash - path + 1;
				new_url = emalloc(8 + strlen(host) + strlen(port) + 1 + dir_len + strlen(loc) + 2);
				sprintf(new_url, "http://%s:%s/", host, port);
				if (dir_len > 0)
					strncat(new_url, path, dir_len);
				strcat(new_url, loc);
			}

			free(loc);
			free(url);
			url = new_url;
			close(sock);
			sock = -1;
			redirects++;
		} else if (status != 200) {
			eprintf("server returned status: %d\n", status);
		}

		free(host);
		free(port);
		free(path);
	}

	/* parse headers for content length and chunked encoding */
	cl_str = find_header(s.buf, "Content-Length:");
	content_len = -1;
	if (cl_str) {
		content_len = strtoll(cl_str, NULL, 10);
		free(cl_str);
	}

	te_str = find_header(s.buf, "Transfer-Encoding:");
	chunked = 0;
	if (te_str) {
		if (strcasecmp(te_str, "chunked") == 0)
			chunked = 1;
		free(te_str);
	}

	/* open output file */
	out_name = NULL;
	if (Oflag) {
		out_name = Oflag;
	} else {
		last_slash = strrchr(url, '/');
		if (last_slash && *(last_slash + 1))
			out_name = last_slash + 1;
		else
			out_name = "index.html";
	}

	if (strcmp(out_name, "-") != 0) {
		out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (out_fd < 0)
			eprintf("open %s:\n", out_name);
	}

	/* read and write body */
	if (chunked)
		read_chunked(&s, out_fd);
	else
		read_non_chunked(&s, out_fd, content_len);

	close(sock);
	if (out_fd != 1)
		close(out_fd);
	free(url);

	return 0;
}
