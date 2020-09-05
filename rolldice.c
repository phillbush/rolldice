#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* default value for a dice structure */
#define DEFROLLS      1
#define DEFDICE       1
#define DEFFACES      6
#define DEFMULTIPLIER 1
#define DEFMODIFIER   0
#define DEFDROP       0

/* structure of a dice string, see default values above */
struct dice {
	int rolls;      /* how many times to roll */
	int dice;       /* number of dice to roll in each roll */
	int faces;      /* number of sides the dice have */
	int multiplier; /* value to multiply the results of each roll to */
	int modifier;   /* value to add to the results of each roll */
	int drop;       /* how many lowest dice rolls to drop*/
};

/* flag for whether to separate the the result of individual dice */
static bool separate;

static int runinteractive(void);
static int runarguments(int, char *[]);
static void rolldice(struct dice);
static struct dice getdice(char *);
static int getnumber(char *, char **);
static void usage(void);

/* roll virtual dice */
int
main(int argc, char *argv[])
{
	int c, exitval;

	separate = false;
	while ((c = getopt(argc, argv, "s")) != -1) {
		switch (c) {
		case 's':
			separate = true;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)      /* no arguments, run interactivelly */
		exitval = runinteractive();
	else                /* run parsing the arguments */
		exitval = runarguments(argc, argv);

	if (ferror(stdin))
		err(1, "stdin");
	if (ferror(stdout))
		err(1, "stdout");

	return exitval;
}

/* run on stdin */
static int
runinteractive(void)
{
	struct dice d;
	char *line = NULL;
	size_t linesize = 0;
	ssize_t linelen;
	int exitval;

	exitval = EXIT_SUCCESS;
	while ((linelen = getline(&line, &linesize, stdin)) != -1) {
		d = getdice(line);
		if (d.rolls == 0) {
			warnx("%s: malformed dice string", line);
			exitval = EXIT_FAILURE;
		} else {
			rolldice(d);
			exitval = EXIT_SUCCESS;
		}
	}
	free(line);
	return exitval;
}

/* run on arguments */
static int
runarguments(int argc, char *argv[])
{
	struct dice *d;
	int i;

	if ((d = calloc(argc, sizeof(*d))) == NULL)
		err(1, NULL);
	for (i = 0; i < argc; i++) {
		d[i] = getdice(argv[i]);
		if ((d[i]).rolls == 0) {
			warnx("%s: malformed dice string", argv[i]);
			return EXIT_FAILURE;
		}
	}
	for (i = 0; i < argc; i++)
		rolldice(d[i]);
	free(d);
	return EXIT_SUCCESS;
}

/* get a random roll given a dice structure */
static void
rolldice(struct dice d)
{
	int i;
	int dropcount;  /* number of dice rolls to drop in current full roll*/
	int rollcount;  /* count the number of the current full roll */
	int *dieresult; /* array containing the result of each die roll */
	int rollresult; /* result of the current full roll */

	/*
	 * NOTE: each "full roll" is composed by a number of dice rolls
	 * subject to the same multiplier, modifier and drop values.
	 *
	 * The number of "full rolls" is defined by d.rolls.
	 * The number of dice rolls in each "full roll" is defined by d.dice.
	 *
	 * The result of a "full roll" is the sum of each die roll with the
	 * multiplier, modifier and drop values applied.
	 */

	if ((dieresult = calloc(d.dice, sizeof(*dieresult))) == NULL)
		err(1, NULL);

	for (rollcount = 1 ; rollcount <= d.rolls; rollcount++) {
		rollresult = 0;     /* reset the result of current full roll */

		/*
		 * If 'separate' is false, print only the result of each "full roll".
		 * If 'separate' is true, print the dice roll in each "full roll"
		 * along with the modifier, multiplier and drop values.
		 */
		if (separate)
			printf("Roll #%d: (", rollcount);

		/* get random values */
		for (i = 0; i < d.dice; i++) {
			dieresult[i] = 1 + arc4random_uniform(d.faces);
			rollresult += dieresult[i];
			if (separate)
				printf("%s%d", (i == 0) ? "" : " ", dieresult[i]);
		}

		/* drop smallest values */
		for (dropcount = 0; dropcount < d.drop; dropcount++) {
			size_t j = 0;
			int min = INT_MAX;

			for (i = 0; i < d.dice; i++) {
				if (dieresult[i] != 0 && dieresult[i] < min) {
					min = dieresult[i];
					j = i;
				}
			}
			rollresult -= dieresult[j];
			if (separate)
				printf(" -%d", dieresult[j]);
			dieresult[j] = 0;
		}

		/* sum rolls, apply multiplier and modifier */
		rollresult = rollresult * d.multiplier + d.modifier;

		/* print multiplier and modifier, if separate is set */
		if (separate) {
			printf(")");
			if (d.multiplier != 1)
				printf(" *%d", d.multiplier);
			if (d.modifier != 0)
				printf(" %+d", d.modifier);
			printf(" = ");
		}

		/* print final roll */
		printf("%d%c", rollresult, (rollcount == d.rolls || separate) ? '\n' : ' ');
	}

	free(dieresult);
}

/* get dice string in format [#x][#]d[#|%][*#][+#|-#][s#], where # is a number */
static struct dice
getdice(char *s)
{
	struct dice d;
	int n, sign;
	char *endp;

	endp = s;
	/* set number of rolls */
	if ((n = getnumber(s, &endp)) < 0)
		goto error;
	s = endp;
	d.rolls = DEFROLLS;
	if (*s == 'x') {
		if (n > 0)
			d.rolls = n;
		else
			goto error;
		s++;
		if ((n = getnumber(s, &endp)) < 0)
			goto error;
		s = endp;
	}

	/* set number of dice */
	if (*s != 'd')
		goto error;
	d.dice = (n == 0) ? DEFDICE : n;
	n = 0;
	s++;

	/* set number of faces */
	if (*s == '%') {
		n = 100;
		s++;
	} else {
		if ((n = getnumber(s, &endp)) < 0)
			goto error;
		if (n == 0 && endp > s) /* test wether the user entered d0 */
			goto error;
		s = endp;
	}
	d.faces = (n == 0) ? DEFFACES : n;
	n = 0;

	/* set multiplier */
	if (*s == '*') {
		s++;
		if ((n = getnumber(s, &endp)) < 1)
			goto error;
		s = endp;
	}
	d.multiplier = (n == 0) ? DEFMULTIPLIER : n;
	n = 0;

	/* set modifier */
	if (*s == '+' || *s == '-') {
		sign = (*s++ == '-') ? -1 : 1;
		if ((n = getnumber(s, &endp)) < 1)
			goto error;
		s = endp;
	}
	d.modifier = (n == 0) ? DEFMODIFIER : sign * n;
	n = 0;

	/* set number of drops */
	if (*s == 's') {
		s++;
		if ((n = getnumber(s, &endp)) < 1)
			goto error;
		s = endp;
	}
	d.drop = (n == 0) ? DEFDROP : n;
	if (d.drop >= d.dice)
		goto error;

	if (*s != '\0' && *s != '\n')
		goto error;

	return d;

error:
	return (struct dice) {0, 0, 0, 0, 0, 0};
}

/* get number from *s; return -1 in case of overflow, return 0 by default */
static int
getnumber(char *s, char **endp)
{
	long n;

	if (!isdigit(*s)) {
		*endp = s;
		return 0;
	}
	n = strtol(s, endp, 10);
	if (errno == ERANGE || n > INT_MAX || n < 0 || *endp == s)
		return -1;
	return (int)n;
}

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-s] [dice-string...]\n", getprogname());
	exit(EXIT_FAILURE);
}
