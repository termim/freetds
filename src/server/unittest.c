/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-1999  Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <stdarg.h>
#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <freetds/tds.h>
#include <freetds/utils/string.h>
#include <freetds/server.h>
#include <freetds/utils.h>

static void dump_login(TDSLOGIN * login);

int
main(int argc, char **argv)
{
	TDSCONTEXT *ctx;
	TDSSOCKET *tds;
	TDSLOGIN *login;
	TDSRESULTINFO *resinfo;
	TDSCOLUMN *col;

	if (argc < 2 || atoi(argv[1]) <= 0) {
		fprintf(stderr, "syntax: %s <port>\n", argv[0]);
		return 1;
	}

	ctx = tds_alloc_context(NULL);
	tds = tds_listen(ctx, atoi(argv[1]));
	if (!tds)
		return 1;
	/* get_incoming(tds->s); */
	login = tds_alloc_read_login(tds);
	if (!login) {
		fprintf(stderr, "Error reading login\n");
		exit(1);
	}
	dump_login(login);
	/* tds->conn->tds_version = 0x702; */
	/* tds->conn->product_version = TDS_MS_VER(10, 0, 6000); */
	if (!strcmp(tds_dstr_cstr(&login->user_name), "guest") && !strcmp(tds_dstr_cstr(&login->password), "sybase")) {
		tds->out_flag = TDS_REPLY;
		tds_env_change(tds, TDS_ENV_DATABASE, "master", "pubs2");
		tds_send_msg(tds, 5701, 2, 10, "Changed database context to 'pubs2'.", "JDBC", "ZZZZZ", 1);
		if (!login->suppress_language) {
			tds_env_change(tds, TDS_ENV_LANG, NULL, "us_english");
			tds_send_msg(tds, 5703, 1, 10, "Changed language setting to 'us_english'.", "JDBC", "ZZZZZ", 1);
		}
		if (IS_TDS50(tds->conn))
			tds_env_change(tds, TDS_ENV_PACKSIZE, NULL, "512");
		/* TODO set mssql if tds7+ */
		tds_send_login_ack(tds, "sql server");
		if (!IS_TDS50(tds->conn))
			tds_env_change(tds, TDS_ENV_PACKSIZE, "4096", "4096");
		if (IS_TDS50(tds->conn))
			tds_send_capabilities_token(tds);
		tds_send_done_token(tds, TDS_DONE_FINAL, 0);
	} else {
		/* send nack before exiting */
		exit(1);
	}
	tds_flush_packet(tds);
	tds_free_login(login);
	login = NULL;
	/* printf("incoming packet %d\n", tds_read_packet(tds)); */
	printf("query : %s\n", tds_get_generic_query(tds));
	tds->out_flag = TDS_REPLY;
	resinfo = tds_alloc_results(1);
	if (!resinfo) {
		fprintf(stderr, "out of memory");
		exit(1);
	}
	col = resinfo->columns[0];
	tds_set_column_type(tds->conn, col, SYBVARCHAR);
	col->column_size = 30;
	col->on_server.column_size = 30;
	if (!tds_dstr_copy(&col->column_name, "name"))
		exit(1);
	col->column_cur_size = 5;
	col->column_data = (void *) "pubs2";
	tds_send_table_header(tds, resinfo);
	tds_send_row(tds, resinfo);
	tds_send_done_token(tds, TDS_DONE_COUNT, 1);
	tds_flush_packet(tds);
	tds_sleep_s(30);

	tds_free_results(resinfo);
	tds_free_socket(tds);
	tds_free_context(ctx);

	return 0;
}

static void
dump_login(TDSLOGIN * login)
{
	printf("host %s\n", tds_dstr_cstr(&login->client_host_name));
	printf("user %s\n", tds_dstr_cstr(&login->user_name));
	printf("pass %s\n", tds_dstr_cstr(&login->password));
	printf("app  %s\n", tds_dstr_cstr(&login->app_name));
	printf("srvr %s\n", tds_dstr_cstr(&login->server_name));
	printf("vers %d.%d\n", TDS_MAJOR(login), TDS_MINOR(login));
	printf("lib  %s\n", tds_dstr_cstr(&login->library));
	printf("lang %s\n", tds_dstr_cstr(&login->language));
	printf("char %s\n", tds_dstr_cstr(&login->server_charset));
	printf("bsiz %d\n", login->block_size);
}
