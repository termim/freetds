/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 2004-2011  James K. Lowden
 *
 * This program  is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef MicrosoftsDbLib
#ifdef _WIN32
# pragma warning( disable : 4142 )
# include "win32.microsoft/have.h"
# include "../../include/replacements.win32.hacked.h"
int getopt(int argc, const char *argv[], char *optstring);

# ifndef DBNTWIN32
#  define DBNTWIN32

/*
 * As of Visual Studio .NET 2003, define WIN32_LEAN_AND_MEAN to avoid redefining LPCBYTE in sqlfront.h
 * Unless it was already defined, undefine it after windows.h has been included.
 * (windows.h includes a bunch of stuff needed by sqlfront.h.  Bleh.)
 */
#  ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN_DEFINED_HERE
#  endif
#  include <freetds/windows.h>
#  ifdef WIN32_LEAN_AND_MEAN_DEFINED_HERE
#   undef WIN32_LEAN_AND_MEAN_DEFINED_HERE
#   undef WIN32_LEAN_AND_MEAN
#  endif
#  include <process.h>
#  include <sqlfront.h>
#  include <sqldb.h>

#endif /* DBNTWIN32 */
# include "win32.microsoft/syb2ms.h"
#endif
#endif /* MicrosoftsDbLib */

#include <config.h>

#include <stdio.h>
#include <assert.h>

#if HAVE_ERRNO_H
#include <errno.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if HAVE_STRING_H
#include <string.h>
#endif

#if HAVE_LIBGEN_H
#include <libgen.h>
#endif

#if HAVE_LOCALE_H
#include <locale.h>
#endif

#include <sybfront.h>
#include <sybdb.h>
#ifndef MicrosoftsDbLib
#include <freetds/replacements.h>
#else

#ifndef _WIN32
# include <freetds/replacements.h>
#endif
#endif /* MicrosoftsDbLib */

#include <freetds/sysdep_private.h>
#include <freetds/utils.h>
#include <freetds/bool.h>
#include <freetds/macros.h>

#ifndef MicrosoftsDbLib
static int err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, char *dberrstr, char *oserrstr);
static int msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, char *msgtext,
		char *srvname, char *procname, int line);
#else
static int err_handler(DBPROCESS * dbproc, int severity, int dberr, int oserr, const char dberrstr[], const char oserrstr[]);
static int msg_handler(DBPROCESS * dbproc, DBINT msgno, int msgstate, int severity, const char msgtext[],
		const char srvname[], const char procname[], unsigned short int line);
#endif /* MicrosoftsDbLib */

typedef struct _options
{
	int 	optind;
	char 	*servername,
		*database,
		*appname,
		 hostname[128],
		*input_filename,
		*output_filename;
} OPTIONS;

typedef struct _procedure
{
	char 	 name[512], owner[512];
} PROCEDURE;

static int print_ddl(DBPROCESS *dbproc, PROCEDURE *procedure);
static int print_results(DBPROCESS *dbproc);
static LOGINREC* get_login(int argc, char *argv[], OPTIONS *poptions);
static void parse_argument(const char argument[], PROCEDURE* procedure);
static void usage(const char invoked_as[]);
static char *rtrim(char *s);
static char *ltrim(char *s);

/* global variables */
static OPTIONS options;
static char use_statement[512];
/* end global variables */


/**
 * The purpose of this program is to load or extract the text of a stored procedure.
 * This first cut does extract only.
 * TODO: support loading procedures onto the server.
 */
int
main(int argc, char *argv[])
{
	LOGINREC *login;
	DBPROCESS *dbproc;
	PROCEDURE procedure;
	RETCODE erc;
	int i, nrows;

	setlocale(LC_ALL, "");

#ifdef __VMS
        /* Convert VMS-style arguments to Unix-style */
        parse_vms_args(&argc, &argv);
#endif

	/* Initialize db-lib */
#if _WIN32 && defined(MicrosoftsDbLib)
	LPCSTR msg = dbinit();
	if (msg == NULL) {
#else
	erc = dbinit();
	if (erc == FAIL) {
#endif /* MicrosoftsDbLib */
		fprintf(stderr, "%s:%d: dbinit() failed\n", options.appname, __LINE__);
		exit(1);
	}


	memset(&options, 0, sizeof(options));
	login = get_login(argc, argv, &options); /* get command-line parameters and call dblogin() */
	assert(login != NULL);

	/* Install our error and message handlers */
	dberrhandle(err_handler);
	dbmsghandle(msg_handler);

	/*
	 * Override stdin, stdout, and stderr, as required
	 */
	if (options.input_filename) {
		if (freopen(options.input_filename, "rb", stdin) == NULL) {
			fprintf(stderr, "%s: unable to open %s: %s\n", options.appname, options.input_filename, strerror(errno));
			exit(1);
		}
	}

	if (options.output_filename) {
		if (freopen(options.output_filename, "wb", stdout) == NULL) {
			fprintf(stderr, "%s: unable to open %s: %s\n", options.appname, options.output_filename, strerror(errno));
			exit(1);
		}
	}

	/* Select the specified database, if any */
	if (options.database)
		DBSETLDBNAME(login, options.database);

	/*
	 * Connect to the server
	 */
	dbproc = dbopen(login, options.servername);
	if (!dbproc) {
		fprintf(stderr, "There was a problem connecting to the server.\n");
		exit(1);
	}

	/*
	 * Read the procedure names and move their texts.
	 */
	for (i=options.optind; i < argc; i++) {
#ifndef MicrosoftsDbLib
		static const char query[] = " select	c.text"
#else
		static const char query[] = " select	cast(c.text as text)"
#endif /* MicrosoftsDbLib */
					 ", number "
					 " from syscomments c,"
					 "      sysobjects o"
					 " where	o.id = c.id"
					 " and		o.name = '%s'"
					 " and		o.uid = user_id('%s')"
					 " and		o.type not in ('U', 'S')" /* no user or system tables */
					 " order by 	c.number, %sc.colid"
					;
		static const char query_table[] = " execute sp_help '%s.%s' ";

		parse_argument(argv[i], &procedure);

		erc = dbfcmd(dbproc, query, procedure.name, procedure.owner,
			     (DBTDS(dbproc) == DBTDS_5_0) ? "c.colid2, ":"");

		/* Send the query to the server (we could use dbsqlexec(), instead) */
		erc = dbsqlsend(dbproc);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbsqlsend() failed\n", options.appname, __LINE__);
			exit(1);
		}

		/* Wait for it to execute */
		erc = dbsqlok(dbproc);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbsqlok() failed\n", options.appname, __LINE__);
			exit(1);
		}

		/* Write the output */
		nrows = print_results(dbproc);

		if (0 == nrows) {
			erc = dbfcmd(dbproc, query_table, procedure.owner, procedure.name);
			assert(SUCCEED == erc);
			erc = dbsqlexec(dbproc);
			if (erc == FAIL) {
				fprintf(stderr, "%s:%d: dbsqlexec() failed\n", options.appname, __LINE__);
				exit(1);
			}
			nrows = print_ddl(dbproc, &procedure);
		}

		switch (nrows) {
		case -1:
			return 1;
		case 0:
			fprintf(stderr, "%s: error: %s.%s.%s.%s not found\n", options.appname,
					options.servername, options.database, procedure.owner, procedure.name);
			return 2;
		default:
			break;
		}
	}

	return 0;
}

static void
parse_argument(const char argument[], PROCEDURE* procedure)
{
	const char *s = strchr(argument, '.');

	if (s) {
		size_t len = s - argument;
		if (len > sizeof(procedure->owner) - 1)
			len = sizeof(procedure->owner) - 1;
		memcpy(procedure->owner, argument, len);
		procedure->owner[len] = '\0';

		strlcpy(procedure->name, s+1, sizeof(procedure->name));
	} else {
		strcpy(procedure->owner, "dbo");
		strlcpy(procedure->name, argument, sizeof(procedure->name));
	}
}

static char *
rtrim(char *s)
{
	char *p = strchr(s, '\0');

	while (--p >= s && *p == ' ')
		;
	*(p + 1) = '\0';

	return s;
}

static char *
ltrim(char *s)
{
	char *p = s;

	while (*p == ' ')
		++p;
	memmove(s, p, strlen(p) + 1);

	return s;
}

static bool
is_in(const char *item, const char *list)
{
	const size_t item_len = strlen(item);
	for (;;) {
		size_t len = strlen(list);
		if (len == 0)
			return false;
		if (len == item_len && strcasecmp(item, list) == 0)
			return true;
		list += len + 1;
	}
}

/*
 * Get the table information from sp_help, because it's easier to get the index information (eventually).
 * The column descriptions are in resultset #2, which is where we start.
 * As shown below, sp_help returns different columns for resultset #2, so we build a map.
 * 	    Sybase 	   	   Microsoft
 *	    -----------------      ----------------
 *	 1. Column_name            Column_name
 *	 2. Type		   Type
 *       3.                        Computed
 *	 4. Length		   Length
 *	 5. Prec		   Prec
 *	 6. Scale		   Scale
 *	 7. Nulls		   Nullable
 *	 8. Default_name	   TrimTrail
 *	 9. Rule_name              FixedLenNullIn
 *	10. Access_Rule_name       Collation
 *	11. Identity
 */
static int
print_ddl(DBPROCESS *dbproc, PROCEDURE *procedure)
{
 	struct DDL { char *name, *type, *length, *precision, *scale, *nullable; } *ddl = NULL;
	static const char *const colmap_names[6] = {
		"column_name\0",
		"type\0",
		"length\0",
		"prec\0",
		"scale\0",
		"nulls\0nullable\0",
	};
	int colmap[TDS_VECTOR_SIZE(colmap_names)];

	FILE *create_index;
	RETCODE erc;
	int row_code, iresultset, i, ret;
	int maxnamelen = 0, nrows = 0;
	char **p_str;

	create_index = tmpfile();

	assert(dbproc);
	assert(procedure);
	assert(create_index);

	/* sp_help returns several result sets.  We want just the second one, for now */
	for (iresultset=1; (erc = dbresults(dbproc)) != NO_MORE_RESULTS; iresultset++) {
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbresults(), result set %d failed\n", options.appname, __LINE__, iresultset);
			goto cleanup;
		}

		/* Get the data */
		while ((row_code = dbnextrow(dbproc)) != NO_MORE_ROWS) {
			struct DDL *p;
			char **coldesc[sizeof(struct DDL)/sizeof(char*)];	/* an array of pointers to the DDL elements */

			assert(row_code == REG_ROW);

			/* Look for index data */
			if (0 == strcmp("index_name", dbcolname(dbproc, 1))) {
				char *index_name, *index_description, *index_keys, *p, fprimary=0;
				DBINT datlen;

				assert(dbnumcols(dbproc) >=3 );	/* column had better be in range */

				/* name */
				datlen = dbdatlen(dbproc, 1);
				index_name = (char *) calloc(1, 1 + datlen);
				assert(index_name);
				memcpy(index_name, dbdata(dbproc, 1), datlen);

				/* kind */
				datlen = dbdatlen(dbproc, 2);
				index_description = (char *) calloc(1, 1 + datlen);
				assert(index_description);
				memcpy(index_description, dbdata(dbproc, 2), datlen);

				/* columns */
				datlen = dbdatlen(dbproc, 3);
				index_keys = (char *) calloc(1, 1 + datlen);
				assert(index_keys);
				memcpy(index_keys, dbdata(dbproc, 3), datlen);

				/* fix up the index attributes; we're going to use the string verbatim (almost). */
				p = strstr(index_description, "located");
				if (p) {
					*p = '\0'; /* we don't care where it's located */
				}
				/* Microsoft version: [non]clustered[, unique][, primary key] located on PRIMARY */
				p = strstr(index_description, "primary key");
				if (p) {
					fprimary = 1;
					*p = '\0'; /* we don't care where it's located */
					if ((p = strchr(index_description, ',')) != NULL)
						*p = '\0'; /* we use only the first term (clustered/nonclustered) */
				} else {
					/* reorder "unique" and "clustered" */
					char nonclustered[] = "nonclustered", unique[] = "unique";
					char *pclustering = nonclustered;
					if (NULL == strstr(index_description, pclustering)) {
						pclustering += 3;
						if (NULL == strstr(index_description, pclustering))
							*pclustering = '\0';
					}
					if (NULL == strstr(index_description, unique))
						unique[0] = '\0';
					sprintf(index_description, "%s %s", unique, pclustering);
				}
				/* Put it to a temporary file; we'll print it after the CREATE TABLE statement. */
				if (fprimary) {
					fprintf(create_index, "ALTER TABLE %s.%s ADD CONSTRAINT %s PRIMARY KEY %s (%s)\nGO\n\n",
						procedure->owner, procedure->name, index_name, index_description, index_keys);
				} else {
					fprintf(create_index, "CREATE %s INDEX %s on %s.%s(%s)\nGO\n\n",
						index_description, index_name, procedure->owner, procedure->name, index_keys);
				}

				free(index_name);
				free(index_description);
				free(index_keys);

				continue;
			}

			/* skip other resultsets that don't describe the table's columns */
			if (0 != strcasecmp("Column_name", dbcolname(dbproc, 1)))
				continue;

			/* Find the columns we need */
			for (i = 0; i < TDS_VECTOR_SIZE(colmap); ++i)
				colmap[i] = -1;
			for (i = 1; i <= dbnumcols(dbproc); ++i) {
				const char *name = dbcolname(dbproc, i);
				int j;

				for (j = 0; j < TDS_VECTOR_SIZE(colmap); ++j) {
					if (is_in(name, colmap_names[j])) {
						colmap[j] = i;
						break;
					}
				}
			}
			for (i = 0; i < TDS_VECTOR_SIZE(colmap); ++i) {
				if (colmap[i] == -1) {
					fprintf(stderr, "Expected column name %s not found\n", colmap_names[i]);
					exit(1);
				}
			}

			/* Make room for the next row */
			p = (struct DDL *) realloc(ddl, ++nrows * sizeof(struct DDL));
			if (p == NULL) {
				perror("error: insufficient memory for row DDL");
				assert(p !=  NULL);
				exit(1);
			}
			ddl = p;

			/* take the address of each member, so we can loop through them */
			coldesc[0] = &ddl[nrows-1].name;
			coldesc[1] = &ddl[nrows-1].type;
			coldesc[2] = &ddl[nrows-1].length;
			coldesc[3] = &ddl[nrows-1].precision;
			coldesc[4] = &ddl[nrows-1].scale;
			coldesc[5] = &ddl[nrows-1].nullable;

			for( i=0; i < sizeof(struct DDL)/sizeof(char*); i++) {
				const int col_index = colmap[i];
				const DBINT datlen = dbdatlen(dbproc, col_index);
				const int type = dbcoltype(dbproc, col_index);

				assert(datlen >= 0);	/* column had better be in range */

				if (datlen == 0) {
					*coldesc[i] = NULL;
					continue;
				}

				if (type == SYBCHAR || type == SYBVARCHAR) {
					*coldesc[i] = (char *) calloc(1, 1 + datlen); /* calloc will null terminate */
					if (!*coldesc[i]) {
						perror("error: insufficient memory for row detail");
						exit(1);
					}
					memcpy(*coldesc[i], dbdata(dbproc, col_index), datlen);
				} else {
					char buf[256];
					DBINT len = dbconvert(dbproc, type, dbdata(dbproc, col_index), datlen,
						SYBVARCHAR, (BYTE *) buf, -1);
					if (len < 0) {
						fprintf(stderr, "Error converting column to char");
						exit(1);
					}
					*coldesc[i] = strdup(buf);
					if (!*coldesc[i]) {
						perror("error: insufficient memory for row detail");
						exit(1);
					}
				}

				/*
				 * maxnamelen will determine how much room we allow for column names
				 * in the CREATE TABLE statement
				 */
				if (i == 0)
					maxnamelen = (maxnamelen > datlen)? maxnamelen : datlen;
			}
		} /* wend */
	}

	/*
	 * We've collected the description for the columns in the 'ddl' array.
	 * Now we'll print the CREATE TABLE statement in jkl's preferred format.
	 */
	if (nrows == 0)
		goto cleanup;

	printf("%sCREATE TABLE %s.%s\n", use_statement, procedure->owner, procedure->name);
	for (i=0; i < nrows; i++) {
		static const char varytypenames[] =	"char\0"
							"nchar\0"
							"varchar\0"
							"nvarchar\0"
							"unichar\0"
							"univarchar\0"
							"binary\0"
							"varbinary\0"
							;
		char *type = NULL;
		bool is_null;

		/* get size of decimal, numeric, char, and image types */
		ret = 0;
		if (is_in(ddl[i].type, "decimal\0numeric\0")) {
			if (ddl[i].precision && 0 != strcasecmp("NULL", ddl[i].precision)) {
				rtrim(ddl[i].precision);
				rtrim(ddl[i].scale);
				ret = asprintf(&type, "%s(%s,%s)", ddl[i].type, ddl[i].precision, ddl[i].scale);
			}
		} else if (is_in(ddl[i].type, varytypenames)) {
			ltrim(rtrim(ddl[i].length));
			if (strcmp(ddl[i].length, "-1") == 0)
				ret = asprintf(&type, "%s(max)", ddl[i].type);
			else
				ret = asprintf(&type, "%s(%s)", ddl[i].type, ddl[i].length);
		}
		assert(ret >= 0);

		is_null = is_in(ddl[i].nullable, "1\0yes\0");

		/*      {(|,} name type [NOT] NULL */
		printf("\t%c %-*s %-15s %3s NULL\n", (i==0? '(' : ','), maxnamelen, ddl[i].name,
						(type? type : ddl[i].type), (is_null? "" : "NOT"));

		free(type);
	}
	printf("\t)\nGO\n\n");

	/* print the CREATE INDEX statements */
	rewind(create_index);
	while ((i = fgetc(create_index)) != EOF) {
		fputc(i, stdout);
	}

cleanup:
	p_str = (char **) ddl;
	for (i=0; i < nrows * (sizeof(struct DDL)/sizeof(char*)); ++i)
		free(p_str[i]);
	free(ddl);
	fclose(create_index);
	return nrows;
}

static int /* return count of SQL text rows */
print_results(DBPROCESS *dbproc)
{
	RETCODE erc;
	int row_code;
	int iresultset;
	int nrows=0;
	int prior_procedure_number=1;

	/* bound variables */
	enum column_id { ctext=1, number=2 };
	char sql_text[16002];
	int	 sql_text_status;
	int	 procedure_number; /* for create proc abc;2 */
	int	 procedure_number_status;

	/*
	 * Set up each result set with dbresults()
	 */
	for (iresultset=1; (erc = dbresults(dbproc)) != NO_MORE_RESULTS; iresultset++) {
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbresults(), result set %d failed\n", options.appname, __LINE__, iresultset);
			return -1;
		}

		if (SUCCEED != DBROWS(dbproc)) {
			return 0;
		}

		/*
		 * Bind the columns to our variables.
		 */
		if (sizeof(sql_text) <= dbcollen(dbproc, ctext) ) {
			assert(sizeof(sql_text) > dbcollen(dbproc, ctext));
			return 0;
		}
		erc = dbbind(dbproc, ctext, STRINGBIND, 0, (BYTE *) sql_text);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbbind(), column %d failed\n", options.appname, __LINE__, ctext);
			return -1;
		}
		erc = dbnullbind(dbproc, ctext, &sql_text_status);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbnullbind(), column %d failed\n", options.appname, __LINE__, ctext);
			return -1;
		}

		erc = dbbind(dbproc, number, INTBIND, -1, (BYTE *) &procedure_number);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbbind(), column %d failed\n", options.appname, __LINE__, number);
			return -1;
		}
		erc = dbnullbind(dbproc, number, &procedure_number_status);
		if (erc == FAIL) {
			fprintf(stderr, "%s:%d: dbnullbind(), column %d failed\n", options.appname, __LINE__, number);
			return -1;
		}

		/*
		 * Print the data to stdout.
		 */
		printf("%s", use_statement);
		for (;(row_code = dbnextrow(dbproc)) != NO_MORE_ROWS; nrows++, prior_procedure_number = procedure_number) {
			switch (row_code) {
			case REG_ROW:
				if ( -1 == sql_text_status) {
					fprintf(stderr, "defncopy: error: unexpected NULL row in SQL text\n");
				} else {
					if (prior_procedure_number != procedure_number)
						printf("\nGO\n");
					printf("%s", sql_text);
				}
				break;
			case BUF_FULL:
			default:
				fprintf(stderr, "defncopy: error: expected REG_ROW (%d), got %d instead\n", REG_ROW, row_code);
				assert(row_code == REG_ROW);
				break;
			} /* row_code */

		} /* wend dbnextrow */
		printf("\nGO\n");

	} /* wend dbresults */
	return nrows;
}

static void
usage(const char invoked_as[])
{
	fprintf(stderr, "usage:  %s \n"
			"        [-U username] [-P password]\n"
			"        [-S servername] [-D database]\n"
			"        [-i input filename] [-o output filename]\n"
			"        [owner.]object_name [[owner.]object_name...]\n"
			, invoked_as);
/**
defncopy Syntax Error
Usage: defncopy
    [-v]
    [-X]
--  [-a <display_charset>]
--  [-I <interfaces_file>]
--  [-J [<client_charset>]]
--  [-K <keytab_file>]
    [-P <password>]
--  [-R <remote_server_principal>]
    [-S [<server_name>]]
    [-U <user_name>]
--  [-V <security_options>]
--  [-Z <security_mechanism>]
--  [-z <language>]
    { in <file_name> <database_name> |
      out <file_name> <database_name> [<owner>.]<object_name>
          [[<owner>.]<object_name>...] }
**/
}

static LOGINREC *
get_login(int argc, char *argv[], OPTIONS *options)
{
	LOGINREC *login;
	char *password;
	int ch;
	int fdomain = TRUE;

	extern char *optarg;
	extern int optind;

	assert(options && argv);

	options->appname = basename(argv[0]);

	login = dblogin();

	if (!login) {
		fprintf(stderr, "%s: unable to allocate login structure\n", options->appname);
		exit(1);
	}

	DBSETLAPP(login, options->appname);

	if (-1 != gethostname(options->hostname, sizeof(options->hostname))) {
		DBSETLHOST(login, options->hostname);
	}

	while ((ch = getopt(argc, argv, "U:P:S:d:D:i:o:v")) != -1) {
		switch (ch) {
		case 'U':
			DBSETLUSER(login, optarg);
			fdomain = FALSE;
			break;
		case 'P':
			password = tds_getpassarg(optarg);
			DBSETLPWD(login, password);
			memset(password, 0, strlen(password));
			free(password);
			password = NULL;
			fdomain = FALSE;
			break;
		case 'S':
			options->servername = strdup(optarg);
			break;
		case 'd':
		case 'D':
			options->database = strdup(optarg);
			break;
		case 'i':
			options->input_filename = strdup(optarg);
			break;
		case 'o':
			options->output_filename = strdup(optarg);
			break;
		case 'v':
			printf("%s\n\n%s", argv[0],
				"Copyright (C) 2004  James K. Lowden\n"
				"This program  is free software; you can redistribute it and/or\n"
				"modify it under the terms of the GNU General Public\n"
				"License as published by the Free Software Foundation\n");
				exit(1);
			break;
		case '?':
		default:
			usage(options->appname);
			exit(1);
		}
	}

#if defined(MicrosoftsDbLib) && defined(_WIN32)
	if (fdomain)
		DBSETLSECURE(login);
#else
	if (fdomain)
		DBSETLNETWORKAUTH(login, TRUE);
#endif /* MicrosoftsDbLib */
	if (!options->servername) {
		usage(options->appname);
		exit(1);
	}

	options->optind = optind;

	return login;
}

static int
#ifndef MicrosoftsDbLib
err_handler(DBPROCESS * dbproc TDS_UNUSED, int severity, int dberr, int oserr TDS_UNUSED,
	    char *dberrstr, char *oserrstr TDS_UNUSED)
#else
err_handler(DBPROCESS * dbproc TDS_UNUSED, int severity, int dberr, int oserr TDS_UNUSED,
	    const char dberrstr[], const char oserrstr[] TDS_UNUSED)
#endif /* MicrosoftsDbLib */
{

	if (dberr) {
		fprintf(stderr, "%s: Msg %d, Level %d\n", options.appname, dberr, severity);
		fprintf(stderr, "%s\n\n", dberrstr);
	}

	else {
		fprintf(stderr, "%s: DB-LIBRARY error:\n\t", options.appname);
		fprintf(stderr, "%s\n", dberrstr);
	}

	return INT_CANCEL;
}

static int
#ifndef MicrosoftsDbLib
msg_handler(DBPROCESS * dbproc TDS_UNUSED, DBINT msgno, int msgstate TDS_UNUSED, int severity TDS_UNUSED, char *msgtext,
	    char *srvname TDS_UNUSED, char *procname TDS_UNUSED, int line TDS_UNUSED)
#else
msg_handler(DBPROCESS * dbproc TDS_UNUSED, DBINT msgno, int msgstate TDS_UNUSED, int severity TDS_UNUSED, const char msgtext[],
	    const char srvname[] TDS_UNUSED, const char procname[] TDS_UNUSED, unsigned short int line TDS_UNUSED)
#endif /* MicrosoftsDbLib */
{
	char *dbname, *endquote;

	switch (msgno) {
	case 5701: /* Print "USE dbname" for "Changed database context to 'dbname'" */
		dbname = strchr(msgtext, '\'');
		if (!dbname)
			break;
		endquote = strchr(++dbname, '\'');
		if (!endquote)
			break;
		*endquote = '\0';
		sprintf(use_statement, "USE %s\nGO\n\n", dbname);
		return 0;

	case 0:	/* Ignore print messages */
	case 5703:	/* Ignore "Changed language setting to <language>". */
		return 0;

	default:
		break;
	}

#if 0
	printf("Msg %ld, Severity %d, State %d\n", (long) msgno, severity, msgstate);

	if (strlen(srvname) > 0)
		printf("Server '%s', ", srvname);
	if (strlen(procname) > 0)
		printf("Procedure '%s', ", procname);
	if (line > 0)
		printf("Line %d", line);
#endif
	printf("\t/* %s */\n", msgtext);

	return 0;
}
