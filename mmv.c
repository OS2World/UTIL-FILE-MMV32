/* 
  	Heavily modified by Kai Uwe Rommel for OS/2. This mostly means
	removal of code not needed for OS/2 to get a cleaner source
	and system dependent additions.
	It can no longer be compiled for the original platforms from
	this modified source code.
*/

/*
	mmv 1.01b
	Copyright (c) 1990 Vladimir Lanin.
	This program may be freely used and copied on a non-commercial basis.
	The author assumes no responsibility for any damage or data loss that 
	may result from the use of this program.

	Author may be reached at:

	lanin@csd4.cs.nyu.edu

	Vladimir Lanin
	330 Wadsworth Ave, Apt 6F,
	New York, NY 10040

	Many thanks to those who have to contributed to the design
	and/or coding of this program:

	Tom Albrecht:	initial Sys V adaptation, consultation, and testing
	Carl Mascott:	V7 adaptation
	Mark Lewis:	-n flag idea, consultation.
	Dave Bernhold:	upper/lowercase conversion idea.
	Paul Stodghill:	copy option, argv[0] checking.
	Frank Fiamingo:	consultation and testing.
	Tom Jordahl:	bug reports and testing.
	John Lukas, Hugh Redelmeyer, Barry Nelson, John Sauter,
	Phil Dench, John Nelson:
			bug reports.
*/


static char USAGE[] = "Usage: \
%s [-m|x|r|c|o|a|l|s] [-h] [-d|p] [-g|t] [-v|n] [from to]\n\
\n\
Use =[l|u]N in the ``to'' pattern to get the [lowercase|uppercase of the]\n\
string matched by the N'th ``from'' pattern wildcard.\n";

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>
#include <signal.h>

#define INCL_NOPM
#include <os2.h>

#define ESC '\''
#define SLASH '\\'
#define OTHERSLASH '/'

typedef int DIRID;
typedef int DEVID;

#define MAXPATH _MAX_PATH
static char TTY[] = "con";


#define mylower(c) (isupper(c) ? (c)-'A'+'a' : (c))
#define myupper(c) (islower(c) ? (c)-'a'+'A' : (c))
#define STRLEN(s) (sizeof(s) - 1)
#define mydup(s) (strcpy((char *)challoc(strlen(s) + 1, 0), (s)))


#define DFLT 0x001
#define NORMCOPY 0x002
#define OVERWRITE 0x004
#define NORMMOVE 0x008
#define XMOVE 0x010
#define DIRMOVE 0x020
#define NORMAPPEND 0x040
#define ZAPPEND 0x080
#define HARDLINK 0x100
#define SYMLINK 0x200

#define COPY (NORMCOPY | OVERWRITE)
#define MOVE (NORMMOVE | XMOVE | DIRMOVE)
#define APPEND (NORMAPPEND | ZAPPEND)
#define LINK (HARDLINK | SYMLINK)

static char MOVENAME[] = "mmv";
static char COPYNAME[] = "mcp";
static char APPENDNAME[] = "mad";
static char LINKNAME[] = "mln";

#define ASKDEL 0
#define ALLDEL 1
#define NODEL 2

#define ASKBAD 0
#define SKIPBAD 1
#define ABORTBAD 2

#define STAY 0
#define LOWER 1
#define UPPER 2

#define MAXWILD 20
#define MAXPATLEN MAXPATH
#define INITROOM 10
#define CHUNKSIZE 2048
#define BUFSIZE 4096

#define FI_STTAKEN 0x01
#define FI_LINKERR 0x02
#define FI_INSTICKY 0x04
#define FI_NODEL 0x08
#define FI_KNOWWRITE 0x010
#define FI_CANWRITE 0x20
#define FI_ISDIR 0x40
#define FI_ISLNK 0x80

typedef struct {
	char *fi_name;
	struct rep *fi_rep;
	char fi_attrib;
} FILEINFO;

#define DI_KNOWWRITE 0x01
#define DI_CANWRITE 0x02
#define DI_CLEANED 0x04

typedef struct {
	DEVID di_vid;
	DIRID di_did;
	unsigned di_nfils;
	FILEINFO **di_fils;
	char di_flags;
} DIRINFO;

#define H_NODIR 1
#define H_NOREADDIR 2

typedef struct {
	char *h_name;
	DIRINFO *h_di;
	char h_err;
} HANDLE;

#define R_ISX 0x01
#define R_SKIP 0x02
#define R_DELOK 0x04
#define R_ISALIASED 0x08
#define R_ISCYCLE 0x10
#define R_ONEDIRLINK 0x20

typedef struct rep {
	HANDLE *r_hfrom;
	FILEINFO *r_ffrom;
	HANDLE *r_hto;
	char *r_nto;			/* non-path part of new name */
	FILEINFO *r_fdel;
	struct rep *r_first;
	struct rep *r_thendo;
	struct rep *r_next;
	char r_flags;
} REP;

typedef struct {
	REP *rd_p;
	DIRINFO *rd_dto;
	char *rd_nto;
	unsigned rd_i;
} REPDICT;

typedef struct chunk {
	struct chunk *ch_next;
	unsigned ch_len;
} CHUNK;

typedef struct {
	CHUNK *sl_first;
	char *sl_unused;
	int sl_len;
} SLICER;


static void init(void);
static void procargs(int argc, char **argv,
		     char **pfrompat, char **ptopat);
static void domatch(char *cfrom, char *cto);
static int getpat(void);
static int getword(char *buf);
static void matchpat(void);
static int parsepat(void);
static int dostage(char *lastend, char *pathend,
		   char **start1, int *len1, int stage, int anylev);
static int trymatch(FILEINFO *ffrom, char *pat);
static int keepmatch(FILEINFO *ffrom, char *pathend,
		     int *pk, int needslash, int dirs, int fils);
static int badrep(HANDLE *hfrom, FILEINFO *ffrom,
		  HANDLE **phto, char **pnto, FILEINFO **pfdel, int *pflags);
static int checkto(HANDLE *hfrom, char *f,
		   HANDLE **phto, char **pnto, FILEINFO **pfdel);
static char *getpath(char *tpath);
static int badname(char *s);
static FILEINFO *fsearch(char *s, DIRINFO *d);
static int ffirst(char *s, int n, DIRINFO *d);
static HANDLE *checkdir(char *p, char *pathend, int which);
static void takedir(FILEFINDBUF3 *pff, DIRINFO *di);
static int fcmp(FILEINFO **pf1, FILEINFO **pf2);
static HANDLE *hadd(char *n);
static int hsearch(char *n, int which, HANDLE **ph);
static DIRINFO *dadd(DEVID v, DIRID d);
static DIRINFO *dsearch(DEVID v, DIRID d);
static int match(char *pat, char *s, char **start1, int *len1);
static void makerep(void);
static void checkcollisions(void);
static int rdcmp(REPDICT *rd1, REPDICT *rd2);
static void findorder(void);
static void scandeletes(int (*pkilldel)(REP *p));
static int baddel(REP *p);
static int skipdel(REP *p);
static void nochains(void);
static void printchain(REP *p);
static void goonordie(void);
static void doreps(void);
static long appendalias(REP *first, REP *p, int *pprintaliased);
static int movealias(REP *first, REP *p, int *pprintaliased);
static int snap(REP *first, REP *p);
static void showdone(REP *fin);
static void breakout(void);
static void breakrep(void);
static void breakstat(void);
static void quit(void);
static int copymove(REP *p);
static int copy(FILEINFO *f, long len);
static int myunlink(char *n, FILEINFO *f);
static int getreply(char *m, int failact);
static void *myalloc(unsigned k);
static void *challoc(int k, int which);
static void chgive(void *p, unsigned k);
static int mygetc(void);
static char *mygets(char *s, int l);
static int leave(void);
static void cleanup(void);

static int op, badstyle, delstyle, verbose, noex, matchall;
static int patflags;

static unsigned ndirs = 0, dirroom;
static DIRINFO **dirs;
static unsigned nhandles = 0, handleroom;
static HANDLE **handles;
static HANDLE badhandle = {"\200", NULL, 0};
static HANDLE *(lasthandle[2]) = {&badhandle, &badhandle};
static unsigned nreps = 0;
static REP hrep, *lastrep = &hrep;
static CHUNK *freechunks = NULL;
static SLICER slicer[2] = {{NULL, NULL, 0}, {NULL, NULL, 0}};

static int badreps = 0, paterr = 0, direrr, failed = 0, gotsig = 0, repbad;
static FILE *outfile = stdout;

static char IDF[] = "$$mmvdid.";
static char TEMP[] = "$$mmvtmp.";
static char TOOLONG[] = "(too long)";
static char EMPTY[] = "(empty)";

static char SLASHSTR[] = {SLASH, '\0'};

static char PATLONG[] = "%.40s... : pattern too long.\n";

char from[MAXPATLEN], to[MAXPATLEN];
static int fromlen, tolen;
static char *(stagel[MAXWILD]), *(firstwild[MAXWILD]), *(stager[MAXWILD]);
static int nwilds[MAXWILD];
static int nstages;
char pathbuf[MAXPATH];
char fullrep[MAXPATH + 1];
static char *(start[MAXWILD]);
static int len[MAXWILD];
static REP mistake;
#define MISTAKE (&mistake)

static int olddevflag, curdisk, maxdisk;

static struct {
	char ph_banner[30];
	char ph_name[9];
	int ph_dfltop;
	int ph_safeid;
	int ph_clustoff;
	int ph_driveoff;
	int ph_drivea;
} patch = {"mmv 1.0 patchable flags", "mmv", XMOVE, 1, 0};

#define DFLTOP (patch.ph_dfltop)
#define CLUSTNO(pff) (*(int *)(((char *)(pff)) + patch.ph_clustoff))
#define DRIVENO(pff) (*(((char *)(pff)) + patch.ph_driveoff) - patch.ph_drivea)

static char *home;
static int homelen;
static int uid, euid, oldumask;

static int dostdin = 0;
static char *cmdname;
#define CMDNAME cmdname


int main(argc, argv)
	int argc;
	char *(argv[]);
{
	char *frompat, *topat;

	init();
	procargs(argc, argv, &frompat, &topat);
	domatch(frompat, topat);
	if (!(op & APPEND))
		checkcollisions();
	findorder();
	if (op & (COPY | LINK))
		nochains();
	scandeletes(baddel);
	goonordie();
	if (!(op & APPEND) && delstyle == ASKDEL)
		scandeletes(skipdel);
	doreps();
	return(failed ? 2 : nreps == 0 && (paterr || badreps));
}


static void init()
{
        ULONG drivemap;

        DosQueryCurrentDisk(&curdisk, &drivemap);
        for ( maxdisk = 0; drivemap; maxdisk++, drivemap >>= 1);

        atexit(cleanup);
        signal(SIGINT, breakout);

	if ((home = getenv("HOME")) == NULL || strcmp(home, SLASHSTR) == 0)
		home = "";
	oldumask = umask(0);
	euid = geteuid();
	uid = getuid();

	dirroom = handleroom = INITROOM;
	dirs = (DIRINFO **)myalloc(dirroom * sizeof(DIRINFO *));
	handles = (HANDLE **)myalloc(handleroom * sizeof(HANDLE *));
	ndirs = nhandles = 0;
}


static void usage()
{
        printf(USAGE, CMDNAME);

        printf(
"\n  -m -x -r -c -o -a         (default -x)"
"\n         move / move-crossdev / rename / copy / overwrite / append mode"
"\n  -z     append mode with special care to ^Z (end-of-file character)\n"

"\n  -h     match . and .. and hidden or system files\n"

"\n  -d     overwrite silently existing target files"
"\n  -p     do not overwrite any existing files\n"
        );

        printf(
"\n  -g     continue on errors with other actions"
"\n  -t     abort when the first error is encountered\n"

"\n  -v     be verbose while working"
"\n  -n     do not execute actions, only show them on stdout\n"

"\n  -s     read pattern pairs from stdin (not needed when redirected)\n"
        );

        exit(1);
}


static void procargs(argc, argv, pfrompat, ptopat)
	int argc;
	char **argv;
	char **pfrompat, **ptopat;
{
	char *p, c;

        cmdname = argv[0];

        if ( (p = strrchr(cmdname, '\\')) != NULL )
          cmdname = p + 1;
        else if ( (p = strrchr(cmdname, '/')) != NULL )
          cmdname = p + 1;
        if ( (p = strchr(cmdname, '.')) != NULL )
          *p = 0;
        strlwr(cmdname);

	op = DFLT;
	verbose = noex = matchall = 0;
	delstyle = ASKDEL;
	badstyle = ASKBAD;
	for (argc--, argv++; argc > 0 && **argv == '-'; argc--, argv++)
		for (p = *argv + 1; *p != '\0'; p++) {
			c = mylower(*p);
			if (c == 'v' && !noex)
				verbose = 1;
			else if (c == 'n' && !verbose)
				noex = 1;
                        else if (c == 's')
                                dostdin = 1;
			else if (c == 'h')
				matchall = 1;
			else if (c == 'd' && delstyle == ASKDEL)
				delstyle = ALLDEL;
			else if (c == 'p' && delstyle == ASKDEL)
				delstyle = NODEL;
			else if (c == 'g' && badstyle == ASKBAD)
				badstyle = SKIPBAD;
			else if (c == 't' && badstyle == ASKBAD)
				badstyle = ABORTBAD;
			else if (c == 'm' && op == DFLT)
				op = NORMMOVE;
			else if (c == 'x' && op == DFLT)
				op = XMOVE;
			else if (c == 'r' && op == DFLT)
				op = DIRMOVE;
			else if (c == 'c' && op == DFLT)
				op = NORMCOPY;
			else if (c == 'o' && op == DFLT)
				op = OVERWRITE;
			else if (c == 'a' && op == DFLT)
				op = NORMAPPEND;
			else if (c == 'z' && op == DFLT)
				op = ZAPPEND;
			else if (c == 'l' && op == DFLT)
				op = HARDLINK;
			else if (c == 's' && op == DFLT)
				op = SYMLINK;
                        else
                                usage();
		}

	if (op == DFLT)
		if (strcmp(cmdname, MOVENAME) == 0)
			op = XMOVE;
		else if (strcmp(cmdname, COPYNAME) == 0)
			op = NORMCOPY;
		else if (strcmp(cmdname, APPENDNAME) == 0)
			op = NORMAPPEND;
		else if (strcmp(cmdname, LINKNAME) == 0)
			op = HARDLINK;
		else
			op = DFLTOP;

	if (badstyle != ASKBAD && delstyle == ASKDEL)
		delstyle = NODEL;

	if (argc == 0)
		*pfrompat = NULL;
	else if (argc == 2) {
		*pfrompat = *(argv++);
		*ptopat = *(argv++);
	}
        else
                usage();
}


static void domatch(cfrom, cto)
	char *cfrom, *cto;
{
	if (cfrom == NULL)
        {
                if ( dostdin || !isatty(fileno(stdin)) )
		while (getpat())
			matchpat();
                else
                        usage();
        }
	else if ((fromlen = strlen(cfrom)) >= MAXPATLEN) {
		printf(PATLONG, cfrom);
		paterr = 1;
	}
	else if ((tolen = strlen(cto)) >= MAXPATLEN) {
		printf(PATLONG, cto);
		paterr = 1;
	}
	else {
		strcpy(from, cfrom);
		strcpy(to, cto);
		matchpat();
	}
}


static int getpat()
{
	int c, gotit = 0;
	char extra[MAXPATLEN];

	patflags = 0;
	do {
		if ((fromlen = getword(from)) == 0 || fromlen == -1)
			goto nextline;

		do {
			if ((tolen = getword(to)) == 0) {
				printf("%s -> ? : missing replacement pattern.\n", from);
				goto nextline;
			}
			if (tolen == -1)
				goto nextline;
		} while (
			tolen == 2 &&
			(to[0] == '-' || to[0] == '=') &&
			(to[1] == '>' || to[1] == '^')
		);
		if (getword(extra) == 0)
			gotit = 1;
		else if (strcmp(extra, "(*)") == 0) {
			patflags |= R_DELOK;
            gotit = (getword(extra) == 0);
		}

nextline:
		while ((c = mygetc()) != '\n' && c != EOF)
			;
		if (c == EOF)
			return(0);
	} while (!gotit);

	return(1);
}


static int getword(buf)
	char *buf;
{
	int c, prevc, n;
	char *p;

	p = buf;
	prevc = ' ';
	n = 0;
	while ((c = mygetc()) != EOF && (prevc == ESC || !isspace(c))) {
		if (n == -1)
			continue;
		if (n == MAXPATLEN - 1) {
			*p = '\0';
			printf(PATLONG, buf);
			n = -1;
		}
		*(p++) = c;
		n++;
		prevc = c;
	}
	*p = '\0';
	while (c != EOF && isspace(c) && c != '\n')
		c = mygetc();
	if (c != EOF)
		ungetc(c, stdin);
	return(n);
}


static void matchpat()
{
	if (parsepat())
		paterr = 1;
	else if (dostage(from, pathbuf, start, len, 0, 0)) {
		printf("%s -> %s : no match.\n", from, to);
		paterr = 1;
	}
}


static int parsepat()
{
	char *p, *lastname, c;
	int totwilds, instage, x;
	static char TRAILESC[] = "%s -> %s : trailing %c is superfluous.\n";

	lastname = from;
	if (from[0] != '\0' && from[1] == ':')
		lastname += 2;
	if (from[0] == '~' && from[1] == SLASH) {
		if ((homelen = strlen(home)) + fromlen > MAXPATLEN) {
			printf(PATLONG, from);
			return(-1);
		}
		memmove(from + homelen, from + 1, fromlen);
		memmove(from, home, homelen);
		lastname += homelen + 1;
	}

	totwilds = nstages = instage = 0;
	for (p = lastname; (c = *p) != '\0'; p++)
		switch (c) {
		case OTHERSLASH:
			*p = SLASH;
 		case SLASH:
			lastname = p + 1;
			if (instage) {
				if (firstwild[nstages] == NULL)
					firstwild[nstages] = p;
				stager[nstages++] = p;
				instage = 0;
			}
			break;
		case ';':
			if (lastname != p) {
				printf("%s -> %s : badly placed ;.\n", from, to);
				return(-1);
			}
		case '!':
		case '*':
		case '?':
		case '[':
			if (totwilds++ == MAXWILD) {
				printf("%s -> %s : too many wildcards.\n", from, to);
				return(-1);
			}
			if (instage) {
				nwilds[nstages]++;
				if (firstwild[nstages] == NULL)
					firstwild[nstages] = p;
			}
			else {
				stagel[nstages] = lastname;
				firstwild[nstages] = (c == ';' ? NULL : p);
				nwilds[nstages] = 1;
				instage = 1;
			}
			if (c != '[')
				break;
			while ((c = *(++p)) != ']') {
				switch (c) {
				case '\0':
					printf("%s -> %s : missing ].\n", from, to);
					return(-1);
				case ':':
				case OTHERSLASH:
				case SLASH:
					printf("%s -> %s : '%c' can not be part of [].\n",
						from, to, c);
					return(-1);
				case ESC:
					if ((c = *(++p)) == '\0') {
						printf(TRAILESC, from, to, ESC);
						return(-1);
					}
				}
			}
			break;
		case ESC:
			if ((c = *(++p)) == '\0') {
				printf(TRAILESC, from, to, ESC);
				return(-1);
			}
		}

	if (instage) {
		if (firstwild[nstages] == NULL)
			firstwild[nstages] = p;
		stager[nstages++] = p;
	}
	else {
		stagel[nstages] = lastname;
		nwilds[nstages] = 0;
		firstwild[nstages] = p;
		stager[nstages++] = p;
	}

	lastname = to;
	if (to[0] != '\0' && to[1] == ':')
		lastname += 2;

	if (to[0] == '~' && to[1] == SLASH) {
		if ((homelen = strlen(home)) + tolen > MAXPATLEN) {
			printf(PATLONG, to);
				return(-1);
		}
		memmove(to + homelen, to + 1, tolen);
		memmove(to, home, homelen);
		lastname += homelen + 1;
	}

	for (p = lastname; (c = *p) != '\0'; p++)
		switch (c) {
		case OTHERSLASH:
			*p = SLASH;
		case SLASH:
			if (op & DIRMOVE) {
				printf("%s -> %s : no path allowed in target under -r.\n",
					from, to);
				return(-1);
			}
			lastname = p + 1;
			break;
		case '=':
			c = *(++p);
			if (c == 'l' || c == 'u') {
				c = *(++p);
			}
			if (!isdigit(c)) {
				printf("%s -> %s : expected digit (not '%c') after =.\n",
					from, to, c);
				return(-1);
			}
			for(x = 0; ;x *= 10) {
				x += c - '0';
				c = *(p+1);
				if (!isdigit(c))
					break;
				p++;
			}
			if (x < 1 || x > totwilds) {
				printf("%s -> %s : wildcard %d does not exist.\n",
					from, to, x);
				return(-1);
			}
			break;
		case ESC:
			if ((c = *(++p)) == '\0') {
				printf(TRAILESC, from, to, ESC);
				return(-1);
			}
		default:
			if (c <= ' ' || c == 127 || c == 255) {
				printf("%s -> %s : illegal character '%c' (0x%02X).\n",
					from, to, c, c);
				return(-1);
			}
		}

	return(0);
}


static int dostage(lastend, pathend, start1, len1, stage, anylev)
	char *lastend, *pathend;
	char **start1;
	int *len1;
	int stage;
	int anylev;
{
	DIRINFO *di;
	HANDLE *h, *hto;
	int prelen, litlen, nfils, i, k, flags, try;
	FILEINFO **pf, *fdel;
	char *nto, *firstesc;
	REP *p;
	int wantdirs, ret = 1, laststage = (stage + 1 == nstages);

	wantdirs = !laststage ||
		(op & (DIRMOVE | SYMLINK)) ||
		(nwilds[nstages - 1] == 0);

	if (!anylev) {
		prelen = stagel[stage] - lastend;
		if (pathend - pathbuf + prelen >= MAXPATH) {
			printf("%s -> %s : search path after %s too long.\n",
				from, to, pathbuf);
			paterr = 1;
			return(1);
		}
		memmove(pathend, lastend, prelen);
		pathend += prelen;
		*pathend = '\0';
		lastend = stagel[stage];
	}

	if ((h = checkdir(pathbuf, pathend, 0)) == NULL) {
		if (stage == 0 || direrr == H_NOREADDIR) {
			printf("%s -> %s : directory %s does not %s.\n",
				from, to, pathbuf, direrr == H_NOREADDIR ?
				"allow reads/searches" : "exist");
			paterr = 1;
		}
		return(stage);
	}
	di = h->h_di;

	if (*lastend == ';') {
		anylev = 1;
		*start1 = pathend;
		*len1 = 0;
		lastend++;
	}

	nfils = di->di_nfils;

	firstesc = strchr(lastend, ESC);
	if (firstesc == NULL || firstesc > firstwild[stage])
		firstesc = firstwild[stage];
	litlen = firstesc - lastend;
	pf = di->di_fils + (i = ffirst(lastend, litlen, di));
	if (i < nfils)
	do {
		if (
			(try = trymatch(*pf, lastend)) != 0 &&
			(
				try == 1 ||
				match(lastend + litlen, (*pf)->fi_name + litlen,
					start1 + anylev, len1 + anylev)
			) &&
			keepmatch(*pf, pathend, &k, 0, wantdirs, laststage)
		) {
			if (!laststage)
				ret &= dostage(stager[stage], pathend + k,
					start1 + nwilds[stage], len1 + nwilds[stage],
					stage + 1, 0);
			else {
				ret = 0;
				makerep();
				if (badrep(h, *pf, &hto, &nto, &fdel, &flags))
					(*pf)->fi_rep = MISTAKE;
				else {
					(*pf)->fi_rep = p = (REP *)challoc(sizeof(REP), 1);
					p->r_flags = flags | patflags;
					p->r_hfrom = h;
					p->r_ffrom = *pf;
					p->r_hto = hto;
					p->r_nto = nto;
					p->r_fdel = fdel;
					p->r_first = p;
					p->r_thendo = NULL;
					p->r_next = NULL;
					lastrep->r_next = p;
					lastrep = p;
					nreps++;
				}
			}
		}
		i++, pf++;
	} while (i < nfils && strncmp(lastend, (*pf)->fi_name, litlen) == 0);

skiplev:
	if (anylev)
		for (pf = di->di_fils, i = 0; i < nfils; i++, pf++)
			if (
				*((*pf)->fi_name) != '.' &&
                                ((*pf)->fi_attrib & FILE_DIRECTORY) &&
				keepmatch(*pf, pathend, &k, 1, 1, 0)
			) {
				*len1 = pathend - *start1 + k;
				ret &= dostage(lastend, pathend + k, start1, len1, stage, 1);
			}

	return(ret);
}


static int trymatch(ffrom, pat)
	FILEINFO *ffrom;
	char *pat;
{
	char *p;

	if (ffrom->fi_rep != NULL)
		return(0);
	p = ffrom->fi_name;
	if (*p == '.')
		if (p[1] == '\0' || (p[1] == '.' && p[2] == '\0'))
			return(strcmp(pat, p) == 0);
        else if (!matchall && ffrom->fi_attrib & (FILE_HIDDEN | FILE_SYSTEM))
		return(strcmp(pat, p) == 0);

	return(-1);
}


static int keepmatch(ffrom, pathend, pk, needslash, dirs, fils)
	FILEINFO *ffrom;
	char *pathend;
	int *pk;
	int needslash;
	int dirs, fils;
{
	*pk = strlen(ffrom->fi_name);
	if (pathend - pathbuf + *pk + needslash >= MAXPATH) {
		*pathend = '\0';
		printf("%s -> %s : search path %s%s too long.\n",
			from, to, pathbuf, ffrom->fi_name);
		paterr = 1;
		return(0);
	}
	strcpy(pathend, ffrom->fi_name);
        if ((ffrom->fi_attrib & FILE_DIRECTORY) ? !dirs : !fils)
		return(0);
	if (needslash) {
		strcpy(pathend + *pk, SLASHSTR);
		(*pk)++;
	}
	return(1);
}


static int badrep(hfrom, ffrom, phto, pnto, pfdel, pflags)
	HANDLE *hfrom;
	FILEINFO *ffrom;
	HANDLE **phto;
	char **pnto;
	FILEINFO **pfdel;
	int *pflags;
{
	char *f = ffrom->fi_name;

	*pflags = 0;
	if (    (ffrom->fi_attrib & FILE_DIRECTORY) &&
		!(op & (DIRMOVE | SYMLINK)))
		printf("%s -> %s : source file is a directory.\n", pathbuf, fullrep);
	else if ((op & (COPY | APPEND)) && access(pathbuf, R_OK))
		printf("%s -> %s : no read permission for source file.\n",
			pathbuf, fullrep);
	else if (
		*f == '.' &&
		(f[1] == '\0' || strcmp(f, "..") == 0) &&
		!(op & SYMLINK)
	)
		printf("%s -> %s : . and .. can't be renamed.\n", pathbuf, fullrep);
	else if (repbad || checkto(hfrom, f, phto, pnto, pfdel) || badname(*pnto))
		printf("%s -> %s : bad new name.\n", pathbuf, fullrep);
	else if (*phto == NULL)
		printf("%s -> %s : %s.\n", pathbuf, fullrep,
			direrr == H_NOREADDIR ?
			"no read or search permission for target directory" :
			"target directory does not exist");
	else if (
		(*phto)->h_di->di_vid != hfrom->h_di->di_vid &&
		(*pflags = R_ISX, (op & (NORMMOVE | HARDLINK)))
	)
		printf("%s -> %s : cross-device move.\n",
			pathbuf, fullrep);
	else if (*pflags && (op & MOVE) && access(pathbuf, R_OK))
		printf("%s -> %s : no read permission for source file.\n",
			pathbuf, fullrep);
	else if ((op & SYMLINK) &&
		!(
			*(hfrom->h_name) == SLASH ||
			(*pflags |= R_ONEDIRLINK, hfrom->h_di == (*phto)->h_di)
		)
	)
		printf("%s -> %s : symbolic link would be badly aimed.\n",
			pathbuf, fullrep);
	else
		return(0);
	badreps++;
	return(-1);
}


static int checkto(hfrom, f, phto, pnto, pfdel)
	HANDLE *hfrom;
	char *f;
	HANDLE **phto;
	char **pnto;
	FILEINFO **pfdel;
{
	char tpath[MAXPATH + 1];
	char *pathend;
	FILEINFO *fdel;
	int hlen, tlen;

	if (op & DIRMOVE) {
		*phto = hfrom;
		hlen = strlen(hfrom->h_name);
		pathend = fullrep + hlen;
		memmove(pathend, fullrep, strlen(fullrep) + 1);
		memmove(fullrep, hfrom->h_name, hlen);
		if ((fdel = *pfdel = fsearch(pathend, hfrom->h_di)) != NULL) {
			*pnto = fdel->fi_name;
		}
		else
			*pnto = mydup(pathend);
	}
	else {
		pathend = getpath(tpath);
		hlen = pathend - fullrep;
		*phto = checkdir(tpath, tpath + hlen, 1);
		if (
			*phto != NULL &&
			*pathend != '\0' &&
			(fdel = *pfdel = fsearch(pathend, (*phto)->h_di)) != NULL &&
                        (fdel->fi_attrib & FILE_DIRECTORY)
		) {
			tlen = strlen(pathend);
			strcpy(pathend + tlen, SLASHSTR);
			tlen++;
			strcpy(tpath + hlen, pathend);
			pathend += tlen;
			hlen += tlen;
			*phto = checkdir(tpath, tpath + hlen, 1);
		}

		if (*pathend == '\0') {
			*pnto = f;
			if (pathend - fullrep + strlen(f) >= MAXPATH) {
				strcpy(fullrep, TOOLONG);
				return(-1);
			}
			strcat(pathend, f);
			if (*phto != NULL) {
				fdel = *pfdel = fsearch(f, (*phto)->h_di);
			}
		}
		else if (fdel != NULL)
			*pnto = fdel->fi_name;
		else
			*pnto = mydup(pathend);
	}
	return(0);
}


static char *getpath(tpath)
	char *tpath;
{
	char *pathstart, *pathend, c;

	if (*fullrep != '\0' && fullrep[1] == ':')
		pathstart = fullrep + 2;
	else
		pathstart = fullrep;

	pathend = pathstart + strlen(pathstart) - 1;
	while (pathend >= pathstart && *pathend != SLASH)
		--pathend;
	pathend++;

	c = *pathend;
	*pathend = '\0';
	strcpy(tpath, fullrep);
	*pathend = c;
	return(pathend);
}


static int badname(s)
	char *s;
{
	char *ext;

	return ((*s == '.' && (s[1] == '\0' || strcmp(s, "..") == 0)) ||
		strlen(s) > MAXPATH || strncmp(s, IDF, STRLEN(IDF)) == 0);
}


static FILEINFO *fsearch(s, d)
	char *s;
	DIRINFO *d;
{
	FILEINFO **fils = d->di_fils;
	int nfils = d->di_nfils;
	int first, k, last, res;

	for(first = 0, last = nfils - 1;;) {
		if (last < first)
			return(NULL);
		k = (first + last) >> 1;
		if ((res = strcmp(s, fils[k]->fi_name)) == 0)
			return(fils[k]);
		if (res < 0)
			last = k - 1;
		else
			first = k + 1;
	}
}


static int ffirst(s, n, d)
	char *s;
	int n;
	DIRINFO *d;
{
	int first, k, last, res;
	FILEINFO **fils = d->di_fils;
	int nfils = d->di_nfils;

	if (nfils == 0 || n == 0)
		return(0);
	first = 0;
	last = nfils - 1;
	for(;;) {
		k = (first + last) >> 1;
		res = strncmp(s, fils[k]->fi_name, n);
		if (first == last)
			return(res == 0 ? k : nfils);
		else if (res > 0)
			first = k + 1;
		else
			last = k;
	}
}


static HDIR hdir;
static ULONG count;

static HANDLE *checkdir(p, pathend, which)
	char *p, *pathend;
	int which;
{
        FILEFINDBUF3 de;
	DIRID d;
	DEVID v;
	HANDLE *h;
	char *dirstart = p;
	int fd;
	int firstfound;
	DIRINFO *di;

	if (hsearch(p, which, &h))
		if (h->h_di == NULL) {
			direrr = h->h_err;
			return(NULL);
		}
		else
			return(h);

	if (*p == '\0' || p[1] != ':')
		v = curdisk;
	else {
		dirstart += 2;
		v = mylower(p[0]) - 'a';
		if (v < 0 || v >= maxdisk)
			return(NULL);
	}

	if (patch.ph_safeid) {
		strcpy(pathend, IDF);
		strcpy(pathend + STRLEN(IDF), "*");
                hdir = HDIR_SYSTEM;
		count = 1;
                if (DosFindFirst(p, &hdir, 0, &de, sizeof(de), &count, 1)) {
			if ((d = ndirs) == 1000) {
				fprintf(stderr, "Too many different directories.\n");
				quit();
			}
			sprintf(pathend + STRLEN(IDF), "%03d", d);
                        if ((fd = creat(p, S_IREAD | S_IWRITE)) < 0) {
				direrr = h->h_err = H_NODIR;
				return(NULL);
			}
                        close(fd);
			strcpy(pathend, "*");
			hdir = HDIR_SYSTEM;
                        count = 1;
                        if (DosFindFirst(p, &hdir, 
				   FILE_DIRECTORY | FILE_SYSTEM | FILE_HIDDEN, 
				   &de, sizeof(de), &count, 1))
				h->h_di = dadd(v, d);
			else
				takedir(&de, h->h_di = dadd(v, d));
		}
                else if ((d = atoi(de.achName + STRLEN(IDF))) < ndirs)
			h->h_di = dirs[d];
		else {
                        strcpy(pathend, de.achName);
			fprintf(stderr, "Strange dir-id file encountered: %s.\n", p);
			quit();
		}
		*pathend = '\0';
	}
	else {
		strcpy(pathend, "*");
                hdir = HDIR_SYSTEM;
                count = 1;
                firstfound = !DosFindFirst(p, &hdir, 
		                 FILE_DIRECTORY | FILE_SYSTEM | FILE_HIDDEN, 
				 &de, sizeof(de), &count, 1);
		*pathend = '\0';
		if (firstfound) {
			v = DRIVENO(&de);
			d = CLUSTNO(&de);
		}
		else {
			strcpy(pathend, "T.D");
			if (mkdir(p)) {
				*pathend = '\0';
				direrr = h->h_err = H_NODIR;
				return(NULL);
			}
			strcpy(pathend, "*");
			hdir = HDIR_SYSTEM;
                        count = 1;
                        firstfound = !DosFindFirst(p, &hdir, 
				   FILE_DIRECTORY | FILE_SYSTEM | FILE_HIDDEN, 
				   &de, sizeof(de), &count, 1);
			*pathend = '\0';
			v = DRIVENO(&de);
			d = CLUSTNO(&de);
			rmdir(p);
			if (!firstfound || d != 0) {
				fprintf(stderr,
					"Strange, %s does not seem to be a root dir.\n",
					p);
				quit();
			}
		}

		if ((di = dsearch(v, d)) == NULL)
			if (firstfound)
				takedir(&de, h->h_di = dadd(v, d));
			else
				h->h_di = dadd(v, d);
		else
			h->h_di = di;
	}

	return(h);
}


static void takedir(pff, di)
        FILEFINDBUF3 *pff;
	DIRINFO *di;
{
	int cnt, room;
	FILEINFO **fils, *f;
	char c, *p, *p1;

	room = INITROOM;
	di->di_fils = fils = (FILEINFO **)myalloc(room * sizeof(FILEINFO *));
	cnt = 0;
	do {
                if (strnicmp(pff->achName, IDF, STRLEN(IDF)) == 0)
			continue;
		if (cnt == room) {
			room *= 2;
			fils = (FILEINFO **)myalloc(room * sizeof(FILEINFO *));
			memcpy(fils, di->di_fils, cnt * sizeof(FILEINFO *));
			chgive(di->di_fils, cnt * sizeof(FILEINFO *));
			di->di_fils = fils;
			fils = di->di_fils + cnt;
		}
		*fils = f = (FILEINFO *)challoc(sizeof(FILEINFO), 1);
		f->fi_name = p = (char *)challoc(pff->cchName + 1, 0);
		strcpy(p, pff->achName);
                f->fi_attrib = (char) pff->attrFile;
		f->fi_rep = NULL;
		cnt++;
		fils++;
        } while (DosFindNext(hdir, pff, sizeof(*pff), &count) == 0);
	qsort(di->di_fils, cnt, sizeof(FILEINFO *), fcmp);
	di->di_nfils = cnt;
}


static int fcmp(pf1, pf2)
	FILEINFO **pf1, **pf2;
{
        return(strcmp((*pf1)->fi_name, (*pf2)->fi_name));
}


static HANDLE *hadd(n)
	char *n;
{
	HANDLE **newhandles, *h;

	if (nhandles == handleroom) {
		handleroom *= 2;
		newhandles = (HANDLE **)myalloc(handleroom * sizeof(HANDLE *));
		memcpy(newhandles, handles, nhandles * sizeof(HANDLE *));
		chgive(handles, nhandles * sizeof(HANDLE *));
		handles = newhandles;
	}
	handles[nhandles++] = h = (HANDLE *)challoc(sizeof(HANDLE), 1);
	h->h_name = (char *)challoc(strlen(n) + 1, 0);
	strcpy(h->h_name, n);
	h->h_di = NULL;
	return(h);
}


static int hsearch(n, which, pret)
	char *n;
	int which;
	HANDLE **pret;
{
	int i;
	HANDLE **ph;

	if (strcmp(n, lasthandle[which]->h_name) == 0) {
		*pret = lasthandle[which];
		return(1);
	}

	for(i = 0, ph = handles; i < nhandles; i++, ph++)
		if (strcmp(n, (*ph)->h_name) == 0) {
			lasthandle[which] = *pret = *ph;
			return(1);
		}

	lasthandle[which] = *pret = hadd(n);
	return(0);
}


static DIRINFO *dadd(v, d)
	DEVID v;
	DIRID d;
{
	DIRINFO *di;
	DIRINFO **newdirs;

	if (ndirs == dirroom) {
		dirroom *= 2;
		newdirs = (DIRINFO **)myalloc(dirroom * sizeof(DIRINFO *));
		memcpy(newdirs, dirs, ndirs * sizeof(DIRINFO *));
		chgive(dirs, ndirs * sizeof(DIRINFO *));
		dirs = newdirs;
	}
	dirs[ndirs++] = di = (DIRINFO *)challoc(sizeof(DIRINFO), 1);
	di->di_vid = v;
	di->di_did = d;
	di->di_nfils = 0;
	di->di_fils = NULL;
	di->di_flags = 0;
	return(di);
}


static DIRINFO *dsearch(v, d)
	DEVID v;
	DIRID d;
{
	int i;
	DIRINFO *di;

	for(i = 0, di = *dirs; i < ndirs; i++, di++)
		if (v == di->di_vid && d == di->di_did)
			return(di);
	return(NULL);
}


static int match(pat, s, start1, len1)
	char *pat, *s, **start1;
	int *len1;
{
	char c, *olds;

	*start1 = 0;
	for(;;)
		switch (c = *pat) {
		case '\0':
		case SLASH:
			return(*s == '\0');
		case '*':
			*start1 = s;
			if ((c = *(++pat)) == '\0') {
				*len1 = strlen(s);
				return(1);
			}
			else {
				for (*len1=0; !match(pat, s, start1+1, len1+1); (*len1)++, s++)
					if (*s == '\0')
						return(0);
				return(1);
			}
		case '?':
			if (*s == '\0')
				return(0);
			*(start1++) = s;
			*(len1++) = 1;
			pat++;
			s++;
			break;
		case '[':
			{
				int matched = 0, notin = 0, inrange = 0;
				char prevc = '\0';

				if ((c = *(++pat)) == '^') {
					notin = 1;
					c = *(++pat);
				}
				while (c != ']') {
					if (c == '-' && !inrange)
						inrange = 1;
					else {
						if (c == ESC) {
							c = *(++pat);
						}
						if (inrange) {
							if (*s >= prevc && *s <= c)
								matched = 1;
							inrange = 0;
						}
						else if (c == *s)
							matched = 1;
						prevc = c;
					}
					c = *(++pat);
				}
				if (inrange && *s >= prevc)
					matched = 1;
				if (!(matched ^ notin))
					return(0);
				*(start1++) = s;
				*(len1++) = 1;
				pat++;
				s++;
			}
			break;
		case ESC:
			c = *(++pat);
		default:
			if (c == *s) {
 				pat++;
				s++;
			}
			else
				return(0);
		}
}


static void makerep()
{
	int l, x;
	int i, cnv;
	char *q;
	char *p, *pat, c, pc;

	repbad = 0;
	p = fullrep;
	for (pat = to, l = 0; (c = *pat) != '\0'; pat++, l++) {
		if (c == '=') {
			c = *(++pat);
			if (c == 'l') {
				cnv = LOWER;
				c = *(++pat);
			}
			else if (c == 'u') {
				cnv = UPPER;
				c = *(++pat);
			}
			else
				cnv = STAY;
			for(x = 0; ;x *= 10) {
				x += c - '0';
				c = *(pat+1);
				if (!isdigit(c))
					break;
				pat++;
			}
			--x;
			if (l + len[x] >= MAXPATH)
				goto toolong;
			switch (cnv) {
			case STAY:
				memmove(p, start[x], len[x]);
				p += len[x];
				break;
			case LOWER:
				for (i = len[x], q = start[x]; i > 0; i--, p++, q++)
					*p = mylower(*q);
				break;
			case UPPER:
				for (i = len[x], q = start[x]; i > 0; i--, p++, q++)
					*p = myupper(*q);
			}
		}
		else {
			if (c == ESC)
				c = *(++pat);
			if (l == MAXPATH)
				goto toolong;
			if (
				(c == SLASH) &&
				(
					p == fullrep ? pat != to :
					(
						(
							(pc = *(p - 1)) == SLASH
						) &&
					 	*(pat - 1) != pc
					)
				)
			) {
				repbad = 1;
				if (l + STRLEN(EMPTY) >= MAXPATH)
					goto toolong;
				strcpy(p, EMPTY);
				p += STRLEN(EMPTY);
				l += STRLEN(EMPTY);
			}
			*(p++)= c;
		}
	}
	if (p == fullrep) {
		strcpy(fullrep, EMPTY);
		repbad = 1;
	}
	*(p++) = '\0';
	return;

toolong:
	repbad = 1;
	strcpy(fullrep, TOOLONG);
}


static void checkcollisions()
{
	REPDICT *rd, *prd;
	REP *p, *q;
	int i, mult, oldnreps;

	if (nreps == 0)
		return;
	rd = (REPDICT *)myalloc(nreps * sizeof(REPDICT));
	for (
		q = &hrep, p = q->r_next, prd = rd, i = 0;
		p != NULL;
		q = p, p = p->r_next, prd++, i++
	) {
		prd->rd_p = p;
		prd->rd_dto = p->r_hto->h_di;
		prd->rd_nto = p->r_nto;
		prd->rd_i = i;
	}
	qsort(rd, nreps, sizeof(REPDICT), rdcmp);
	mult = 0;
	for (i = 0, prd = rd, oldnreps = nreps; i < oldnreps; i++, prd++)
		if (
			i < oldnreps - 1 &&
			prd->rd_dto == (prd + 1)->rd_dto &&
			strcmp(prd->rd_nto, (prd + 1)->rd_nto) == 0
		) {
			if (!mult)
				mult = 1;
			else
				printf(" , ");
			printf("%s%s", prd->rd_p->r_hfrom->h_name,
				prd->rd_p->r_ffrom->fi_name);
			prd->rd_p->r_flags |= R_SKIP;
			prd->rd_p->r_ffrom->fi_rep = MISTAKE;
			nreps--;
			badreps++;
		}
		else if (mult) {
			prd->rd_p->r_flags |= R_SKIP;
			prd->rd_p->r_ffrom->fi_rep = MISTAKE;
			nreps--;
			badreps++;
			printf(" , %s%s -> %s%s : collision.\n",
				prd->rd_p->r_hfrom->h_name, prd->rd_p->r_ffrom->fi_name,
				prd->rd_p->r_hto->h_name, prd->rd_nto);
			mult = 0;
		}
	chgive(rd, oldnreps * sizeof(REPDICT));
}


static int rdcmp(rd1, rd2)
	REPDICT *rd1, *rd2;
{
	int ret;

	if (
		(ret = rd1->rd_dto - rd2->rd_dto) == 0 &&
		(ret = strcmp(rd1->rd_nto, rd2->rd_nto)) == 0
	)
		ret = rd1->rd_i - rd2->rd_i;
	return(ret);
}


static void findorder()
{
	REP *p, *q, *t, *first, *pred;
	FILEINFO *fi;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next)
		if (p->r_flags & R_SKIP) {
			q->r_next = p->r_next;
			p = q;
		}
		else if (
			(fi = p->r_fdel) == NULL ||
			(pred = fi->fi_rep) == NULL ||
			pred == MISTAKE
		)
			continue;
		else if ((first = pred->r_first) == p) {
			p->r_flags |= R_ISCYCLE;
			pred->r_flags |= R_ISALIASED;
			if (op & MOVE)
				p->r_fdel = NULL;
		}
		else {
			if (op & MOVE)
				p->r_fdel = NULL;
			while (pred->r_thendo != NULL)
				pred = pred->r_thendo;
			pred->r_thendo = p;
			for (t = p; t != NULL; t = t->r_thendo)
				t->r_first = first;
			q->r_next = p->r_next;
			p = q;
		}
}


static void nochains()
{
	REP *p, *q;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next)
		if (p->r_flags & R_ISCYCLE || p->r_thendo != NULL) {
			printchain(p);
			printf("%s%s : no chain copies allowed.\n",
				p->r_hto->h_name, p->r_nto);
			q->r_next = p->r_next;
			p = q;
		}
}


static void printchain(p)
	REP *p;
{
	if (p->r_thendo != NULL)
		printchain(p->r_thendo);
	printf("%s%s -> ", p->r_hfrom->h_name, p->r_ffrom->fi_name);
	badreps++;
	nreps--;
	p->r_ffrom->fi_rep = MISTAKE;
}


static void scandeletes(pkilldel)
	int (*pkilldel)();
{
	REP *p, *q, *n;

	for (q = &hrep, p = q->r_next; p != NULL; q = p, p = p->r_next) {
		if (p->r_fdel != NULL)
			while ((*pkilldel)(p)) {
				nreps--;
				p->r_ffrom->fi_rep = MISTAKE;
				if ((n = p->r_thendo) != NULL) {
					if (op & MOVE)
						n->r_fdel = p->r_ffrom;
					n->r_next = p->r_next;
					q->r_next = p = n;
				}
				else {
					q->r_next = p->r_next;
					p = q;
					break;
				}
			}
	}
}


static int baddel(p)
	REP *p;
{
	HANDLE *hfrom = p->r_hfrom, *hto = p->r_hto;
	FILEINFO *fto = p->r_fdel;
	char *t = fto->fi_name, *f = p->r_ffrom->fi_name;
	char *hnf = hfrom->h_name, *hnt = hto->h_name;

	if (delstyle == NODEL && !(p->r_flags & R_DELOK) && !(op & APPEND))
		printf("%s%s -> %s%s : old %s%s would have to be %s.\n",
			hnf, f, hnt, t, hnt, t,
			(op & OVERWRITE) ? "overwritten" : "deleted");
	else if (fto->fi_rep == MISTAKE)
		printf("%s%s -> %s%s : old %s%s was to be done first.\n",
			hnf, f, hnt, t, hnt, t);
	else if (fto->fi_attrib & FILE_DIRECTORY)
		printf("%s%s -> %s%s : %s%s%s is a directory.\n",
			hnf, f, hnt, t, (op & APPEND) ? "" : "old ", hnt, t);
	else if (
		(op & (APPEND | OVERWRITE)) &&
                fto->fi_attrib & FILE_READONLY
	) {
		printf("%s%s -> %s%s : %s%s %s.\n",
			hnf, f, hnt, t, hnt, t,
			"lacks write permission");
	}
	else
		return(0);
	badreps++;
	return(1);
}


static int skipdel(p)
	REP *p;
{
	if (p->r_flags & R_DELOK)
		return(0);
	fprintf(stderr, "%s%s -> %s%s : ",
		p->r_hfrom->h_name, p->r_ffrom->fi_name,
		p->r_hto->h_name, p->r_nto);
	if (p->r_fdel->fi_attrib & FILE_READONLY)
		fprintf(stderr, "old %s%s lacks write permission. delete it",
			p->r_hto->h_name, p->r_nto);
	else
		fprintf(stderr, "%s old %s%s",
			(op & OVERWRITE) ? "overwrite" : "delete",
			p->r_hto->h_name, p->r_nto);
	return(!getreply("? ", -1));
}


static void goonordie()
{
	if ((paterr || badreps) && nreps > 0) {
		fprintf(stderr, "Not everything specified can be done.");
		if (badstyle == ABORTBAD) {
			fprintf(stderr, " Aborting.\n");
			exit(1);
		}
		else if (badstyle == SKIPBAD)
			fprintf(stderr, " Proceeding with the rest.\n");
		else if (!getreply(" Proceed with the rest? ", -1))
			exit(1);
	}
}


static void doreps()
{
	char *fstart;
	int k, printaliased = 0, alias;
	REP *first, *p;
	long aliaslen;

	signal(SIGINT, breakrep);

	for (first = hrep.r_next, k = 0; first != NULL; first = first->r_next) {
		for (p = first; p != NULL; p = p->r_thendo, k++) {
			if (gotsig) {
				fflush(stdout);
				fprintf(stderr, "User break.\n");
				printaliased = snap(first, p);
				gotsig = 0;
			}
			strcpy(fullrep, p->r_hto->h_name);
			strcat(fullrep, p->r_nto);
			if (!noex && (p->r_flags & R_ISCYCLE))
				if (op & APPEND)
					aliaslen = appendalias(first, p, &printaliased);
				else
					alias = movealias(first, p, &printaliased);
			strcpy(pathbuf, p->r_hfrom->h_name);
			fstart = pathbuf + strlen(pathbuf);
			if ((p->r_flags & R_ISALIASED) && !(op & APPEND))
				sprintf(fstart, "%s%03d", TEMP, alias);
			else
				strcpy(fstart, p->r_ffrom->fi_name);
			if (!noex) {
				if (p->r_fdel != NULL && !(op & (APPEND | OVERWRITE)))
					myunlink(fullrep, p->r_fdel);
				if (
					(op & (COPY | APPEND)) ?
						copy(p->r_ffrom,
							p->r_flags & R_ISALIASED ? aliaslen : -1L) :
#ifndef OS2
					(op & HARDLINK) ?
						link(pathbuf, fullrep) :
#ifndef SYSV
					(op & SYMLINK) ?
						symlink((p->r_flags & R_ONEDIRLINK) ? fstart : pathbuf,
							fullrep) :
#endif
#endif
					p->r_flags & R_ISX ?
						copymove(p) : /* move */
						rename(pathbuf, fullrep)
				) {
					fprintf(stderr,
						"%s -> %s has failed.\n", pathbuf, fullrep);
					printaliased = snap(first, p);
				}
			}
			if (verbose || noex) {
				if (p->r_flags & R_ISALIASED && !printaliased)
					strcpy(fstart, p->r_ffrom->fi_name);
				fprintf(outfile, "%s %c%c %s%s%s\n",
					pathbuf,
					p->r_flags & R_ISALIASED ? '=' : '-',
					p->r_flags & R_ISCYCLE ? '^' : '>',
					fullrep,
					(p->r_fdel != NULL && !(op & APPEND)) ? " (*)" : "",
					noex ? "" : " : done");
			}
		}
		printaliased = 0;
	}
	if (k != nreps)
		fprintf(stderr, "Strange, did %d reps; %d were expected.\n",
			k, nreps);
	if (k == 0)
		fprintf(stderr, "Nothing done.\n");
}


static long appendalias(first, p, pprintaliased)
	REP *first, *p;
	int *pprintaliased;
{
	long ret;

	struct stat fstat;

	if (stat(fullrep, &fstat)) {
		fprintf(stderr, "append cycle stat on %s has failed.\n", fullrep);
		*pprintaliased = snap(first, p);
	}
	else
		ret = fstat.st_size;

	return(ret);
}


static int movealias(first, p, pprintaliased)
	REP *first, *p;
	int *pprintaliased;
{
	char *fstart;
	int ret;

	strcpy(pathbuf, p->r_hto->h_name);
	fstart = pathbuf + strlen(pathbuf);
	strcpy(fstart, TEMP);
	for (
		ret = 0;
		sprintf(fstart + STRLEN(TEMP), "%03d", ret),
		fsearch(fstart, p->r_hto->h_di) != NULL;
		ret++
	)
		;
	if (rename(fullrep, pathbuf)) {
		fprintf(stderr,
			"%s -> %s has failed.\n", fullrep, pathbuf);
		*pprintaliased = snap(first, p);
	}
	return(ret);
}


static int snap(first, p)
	REP *first, *p;
{
	char fname[80];
	int redirected = 0;

	if (noex)
		exit(1);

	failed = 1;
	signal(SIGINT, breakstat);
	if (
		badstyle == ASKBAD &&
		isatty(fileno(stdout)) &&
		getreply("Redirect standard output to file? ", 0)
	) {
		redirected = 1;
		umask(oldumask);
		while (
			fprintf(stderr, "File name> "),
			(outfile = fopen(mygets(fname, 80), "w")) == NULL
		)
			fprintf(stderr, "Can't open %s.\n", fname);
	}
	if (redirected || !verbose)
		showdone(p);
	fprintf(outfile, "The following left undone:\n");
	noex = 1;
	return(first != p);
}


static void showdone(fin)
	REP *fin;
{
	REP *first, *p;

	for (first = hrep.r_next; ; first = first->r_next)
		for (p = first; p != NULL; p = p->r_thendo) {
			if (p == fin)
				return;
			fprintf(outfile, "%s%s %c%c %s%s : done%s\n",
				p->r_hfrom->h_name, p->r_ffrom->fi_name,
				p->r_flags & R_ISALIASED ? '=' : '-',
				p->r_flags & R_ISCYCLE ? '^' : '>',
				p->r_hto->h_name, p->r_nto,
				(p->r_fdel != NULL && !(op & APPEND)) ? " (*)" : "");
		}
}


static void breakout()
{
	fflush(stdout);
	fprintf(stderr, "Aborting, nothing done.\n");
	exit(1);
}


static void breakrep()
{
	gotsig = 1;
}


static void breakstat()
{
	exit(1);
}


static void quit()
{
	fprintf(stderr, "Aborting, nothing done.\n");
	exit(1);
}


static int copymove(p)
	REP *p;
{
#if 0
	int llen;
	char linkbuf[MAXPATH];

	if ((llen = readlink(pathbuf, linkbuf, MAXPATH - 1)) >= 0) {
		linkbuf[llen] = '\0';
		return(symlink(linkbuf, fullrep) || 
		       myunlink(pathbuf, p->r_ffrom));
	}
#endif
	return(copy(p->r_ffrom, -1L) || myunlink(pathbuf, p->r_ffrom));
}



#define IRWMASK (S_IREAD | S_IWRITE)
#define RWMASK (IRWMASK | (IRWMASK >> 3) | (IRWMASK >> 6))

static int copy(ff, len)
	FILEINFO *ff;
	long len;
{
	char buf[BUFSIZE], c;
	int f, t, k, mode, perm;
        FILESTATUS fs;

	if ((f = open(pathbuf, O_RDONLY | O_BINARY, 0)) < 0)
		return(-1);
	perm = IRWMASK;
	mode = O_CREAT | (op & APPEND ? 0 : O_TRUNC) |
		O_BINARY | (op & ZAPPEND ? O_RDWR : O_WRONLY);
	t = open(fullrep, mode, perm);
	if (t < 0) {
		close(f);
		return(-1);
	}
	if (op & APPEND)
		lseek(t, 0L, 2);
	if (op & ZAPPEND && filelength(t) != 0) {
		if (lseek(t, -1L, 1) == -1L || read(t, &c, 1) != 1) {
			close(f);
			close(t);
			return(-1);
		}
		if (c == 26)
			lseek(t, -1L, 1);
	}
	if ((op & APPEND) && len != -1L) {
		while (
			len != 0 &&
			(k = read(f, buf, len > BUFSIZE ? BUFSIZE : (unsigned)len)) > 0 &&
			write(t, buf, k) == k
		)
			len -= k;
		if (len == 0)
			k = 0;
	}
	else
		while ((k = read(f, buf, BUFSIZE)) > 0 && write(t, buf, k) == k)
			;
	if (!(op & (APPEND | OVERWRITE)))
		if (DosQueryFileInfo(f, 1, &fs, sizeof(fs)) ||
		    DosSetFileInfo(t, 1, &fs, sizeof(fs)))
			fprintf(stderr, "Strange, couldn't transfer time from %s to %s.\n",
				pathbuf, fullrep);

	close(f);
	close(t);
	if (k != 0) {
		if (!(op & APPEND))
			unlink(fullrep);
		return(-1);
	}
	return(0);
}


static int myunlink(n, f)
	char *n;
	FILEINFO *f;
{
        if ((f->fi_attrib & FILE_READONLY) && chmod(n, 0666)) {
		fprintf(stderr, "Strange, can not chmod %s.\n", f);
		return(-1);
	}
	if (unlink(n)) {
		fprintf(stderr, "Strange, can not unlink %s.\n", n);
		return(-1);
	}
	return(0);
}


static int getreply(m, failact)
	char *m;
	int failact;
{
	static FILE *tty = NULL;
	int c, r;

	fprintf(stderr, m);
	if (tty == NULL && (tty = fopen(TTY, "r")) == NULL) {
		fprintf(stderr, "Can not open %s to get reply.\n", TTY);
		if (failact == -1)
			quit();
		else
			return(failact);
	}
	for (;;) {
		r = fgetc(tty);
		if (r == EOF) {
			fprintf(stderr, "Can not get reply.\n");
			if (failact == -1)
				quit();
			else
				return(failact);
		}
		if (r != '\n')
			while ((c = fgetc(tty)) != '\n' && c != EOF)
				;
		r = mylower(r);
		if (r == 'y' || r == 'n')
			return(r == 'y');
		fprintf(stderr, "Yes or No? ");
	}
}


static void *myalloc(k)
	unsigned k;
{
	void *ret;

	if (k == 0)
		return(NULL);
	if ((ret = (void *)malloc(k)) == NULL) {
		fprintf(stderr, "Insufficient memory.\n");
		quit();
	}
	return(ret);
}


static void *challoc(k, which)
	int which;
	int k;
{
	void *ret;
	CHUNK *p, *q;
	SLICER *sl = &(slicer[which]);

	if (k > sl->sl_len) {
		for (
			q = NULL, p = freechunks;
			p != NULL && (sl->sl_len = p->ch_len) < k;
			q = p, p = p->ch_next
		)
			;
		if (p == NULL) {
			sl->sl_len = CHUNKSIZE - sizeof(CHUNK *);
			p = (CHUNK *)myalloc(CHUNKSIZE);
		}
		else if (q == NULL)
			freechunks = p->ch_next;
		else
			q->ch_next = p->ch_next;
		p->ch_next = sl->sl_first;
		sl->sl_first = p;
		sl->sl_unused = (char *)&(p->ch_len);
	}
	sl->sl_len -= k;
	ret = (void *)sl->sl_unused;
	sl->sl_unused += k;
	return(ret);
}


static void chgive(p, k)
	void *p;
	unsigned k;
{
	((CHUNK *)p)->ch_len = k - sizeof(CHUNK *);
	((CHUNK *)p)->ch_next = freechunks;
	freechunks = (CHUNK *)p;
}


static int mygetc()
{
	static int lastc = 0;

	if (lastc == EOF)
		return(EOF);
	return(lastc = getchar());
}


static char *mygets(s, l)
	char *s;
	int l;
{
	char *nl;

	for (;;) {
		if (fgets(s, l, stdin) == NULL)
			return(NULL);
		if ((nl = strchr(s, '\n')) != NULL)
			break;
		fprintf(stderr, "Input string too long. Try again> ");
	}
	*nl = '\0';
	return(s);
}


static int leave()
{
	return(0);
}

static void cleanup()
{
	int i;

	if (patch.ph_safeid) {
		for (i = 0; i < nhandles; i++) {
			if (!(handles[i]->h_di->di_flags & DI_CLEANED)) {
				sprintf(pathbuf, "%s%s%03d",
					handles[i]->h_name, IDF, handles[i]->h_di->di_did);
				if (unlink(pathbuf))
					fprintf(stderr, "Strange, couldn't unlink %s.\n", pathbuf);
				handles[i]->h_di->di_flags |= DI_CLEANED;
			}
		}
	}
}
