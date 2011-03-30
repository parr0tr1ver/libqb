/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "os_base.h"

#include <qb/qbrb.h>
#include "log_int.h"


static void _blackbox_reload(struct qb_log_target *t)
{
	qb_rb_close(t->instance);
	t->instance = qb_rb_open(t->name, t->size,
				 QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
}

/* <u32> file lineno
 * <u32> function name length
 * <string> function name
 * <u32> buffer lenght
 * <string> buffer
 */
static void _blackbox_logger(struct qb_log_target *t,
				 struct qb_log_callsite *cs,
				 const char* timestamp_str,
				 const char *buffer)
{
	size_t size = sizeof(uint32_t);
	size_t fn_size;
	size_t buf_size;
	size_t time_size;
	char *chunk;

	if (t->instance == NULL) {
		return;
	}

	fn_size = strlen(cs->function) + 1;
	buf_size = strlen(buffer) + 1;
	time_size = strlen(timestamp_str) + 1;

	size += 3 * sizeof(uint32_t) + fn_size + buf_size + time_size;

	chunk = qb_rb_chunk_alloc(t->instance, size);

	/* line number */
	memcpy(chunk, &cs->lineno, sizeof(uint32_t));
	chunk += sizeof(uint32_t);

	/* function name */
	memcpy(chunk, &fn_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, cs->function, fn_size);
	chunk += fn_size;

	/* timestamp */
	memcpy(chunk, &time_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, timestamp_str, time_size);
	chunk += time_size;

	/* log message */
	memcpy(chunk, &buf_size, sizeof(uint32_t));
	chunk += sizeof(uint32_t);
	memcpy(chunk, buffer, buf_size);

	(void)qb_rb_chunk_commit(t->instance, size);
}

static void _blackbox_close(struct qb_log_target *t)
{
	qb_rb_close(t->instance);
}

int32_t qb_log_blackbox_open(struct qb_log_target *t)
{
	if (t->size < 1024) {
		return -EINVAL;
	}

	t->instance = qb_rb_open(t->name, t->size,
				 QB_RB_FLAG_CREATE | QB_RB_FLAG_OVERWRITE, 0);
	if (t->instance == NULL) {
		return -errno;
	}
	t->logger = _blackbox_logger;
	t->reload = _blackbox_reload;
	t->close = _blackbox_close;
	return 0;
}

ssize_t qb_log_blackbox_write_to_file(const char *filename)
{
	ssize_t written_size = 0;
	struct qb_log_target *t;
	int fd = open (filename, O_CREAT|O_RDWR, 0700);

	if (fd < 0) {
		return -errno;
	}
	t = qb_log_target_get(QB_LOG_BLACKBOX);
	written_size = qb_rb_write_to_file(t->instance, fd);
	close (fd);

	return written_size;
}

void qb_log_blackbox_print_from_file(const char* bb_filename)
{
	qb_ringbuffer_t * instance;
	ssize_t bytes_read;
	char chunk[512];
	int fd;

	fd = open(bb_filename, O_CREAT|O_RDWR, 0700);
	if (fd < 0) {
		perror("qb_log_blackbox_print_from_file");
		return;
	}
	instance = qb_rb_create_from_file(fd, 0);

	do {
		char     *ptr;
		uint32_t *lineno;
		uint32_t *fn_size;
		char     *function;
		uint32_t *time_size;
		char     *timestamp;
		uint32_t *log_size;
		char     *logmsg;

		bytes_read = qb_rb_chunk_read(instance, chunk, 512, 0);
		ptr = chunk;
		if (bytes_read > 0) {
			/* lineno */
			lineno = (uint32_t*)ptr;
			ptr  += sizeof(uint32_t);

			/* function size & name */
			fn_size = (uint32_t*)ptr;
			ptr += sizeof(uint32_t);

			function = ptr;
			ptr += *fn_size;

			/* timestamp size & content */
			time_size = (uint32_t*)ptr;
			ptr += sizeof(uint32_t);

			timestamp = ptr;
			ptr += *time_size;

			/* message size & content */
			log_size = (uint32_t*)ptr;
			ptr += sizeof(uint32_t);
			logmsg = ptr;
			printf("%s %s():%d %s\n", timestamp, function, *lineno, logmsg);
		}
	} while (bytes_read > 0);
}

