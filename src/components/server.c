#include "src/components/server.h"

#include "src/state.h"
#include "src/utils/utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define HANDLED_005 \
	X(CASEMAPPING)  \
	X(CHANMODES)    \
	X(MODES)        \
	X(PREFIX)

struct opt
{
	char *arg;
	char *val;
};

static int parse_005(struct opt*, char**);
static int server_cmp(const struct server*, const char*, const char*);

#define X(cmd) static int server_set_##cmd(struct server*, char*);
HANDLED_005
#undef X

struct server*
server(
	const char *host,
	const char *port,
	const char *pass,
	const char *username,
	const char *realname,
	const char *mode)
{
	struct server *s;

	if ((s = calloc(1, sizeof(*s))) == NULL)
		fatal("calloc: %s", strerror(errno));

	s->host     = strdup(host);
	s->port     = strdup(port);
	s->username = strdup(username);
	s->realname = strdup(realname);

	s->pass = (pass ? strdup(pass) : NULL);
	s->mode = (mode ? strdup(mode) : NULL);

	s->casemapping = CASEMAPPING_RFC1459;
	s->mode_str.type = MODE_STR_USERMODE;
	ircv3_caps(&(s->ircv3_caps));
	mode_cfg(&(s->mode_cfg), NULL, MODE_CFG_DEFAULTS);

	s->channel = channel(host, CHANNEL_T_SERVER);
	s->channel->server = s;
	channel_list_add(&(s->clist), s->channel);

	return s;
}

struct server*
server_list_get(struct server_list *sl, const char *host, const char *port)
{
	struct server *tmp;

	if ((tmp = sl->head) == NULL)
		return NULL;

	if (!server_cmp(sl->head, host, port))
		return sl->head;

	while ((tmp = tmp->next) != sl->head) {

		if (!server_cmp(tmp, host, port))
			return tmp;
	}

	return NULL;
}

struct server*
server_list_add(struct server_list *sl, struct server *s)
{
	if (server_list_get(sl, s->host, s->port) != NULL)
		return s;

	if (sl->head == NULL) {
		sl->head = s->next = s;
		sl->tail = s->prev = s;
	} else {
		s->next = sl->tail->next;
		s->prev = sl->tail;
		sl->head->prev = s;
		sl->tail->next = s;
		sl->tail = s;
	}

	return NULL;
}

struct server*
server_list_del(struct server_list *sl, struct server *s)
{
	struct server *tmp_h,
	              *tmp_t;

	if (sl->head == s && sl->tail == s) {
		/* Removing last server */
		sl->head = NULL;
		sl->tail = NULL;
	} else if ((tmp_h = sl->head) == s) {
		/* Removing head */
		sl->head = sl->tail->next = sl->head->next;
		sl->head->prev = sl->tail;
	} else if ((tmp_t = sl->tail) == s) {
		/* Removing tail */
		sl->tail = sl->head->prev = sl->tail->prev;
		sl->tail->next = sl->head;
	} else {
		/* Removing some server (head, tail) */
		while ((tmp_h = tmp_h->next) != s) {
			if (tmp_h == tmp_t)
				return NULL;
		}
		s->next->prev = s->prev;
		s->prev->next = s->next;
	}

	s->next = NULL;
	s->prev = NULL;

	return s;
}

void
server_reset(struct server *s)
{
	ircv3_caps_reset(&(s->ircv3_caps));
	mode_reset(&(s->usermodes), &(s->mode_str));
	s->ping = 0;
	s->quitting = 0;
	s->registered = 0;
	s->nicks.next = 0;
}

void
server_free(struct server *s)
{
	channel_list_free(&(s->clist));

	user_list_free(&(s->ignore));

	free((void *)s->host);
	free((void *)s->port);
	free((void *)s->pass);
	free((void *)s->username);
	free((void *)s->realname);
	free((void *)s->mode);
	free((void *)s->nick);
	free((void *)s->nicks.base);
	free((void *)s->nicks.set);
	free(s);
}

int
server_set_chans(struct server *s, const char *str)
{
	char *dup;
	char *p1;
	char *p2;
	size_t n_chans = 0;

	p2 = dup = strdup(str);

	do {
		n_chans++;

		p1 = p2;
		p2 = strchr(p2, ',');

		if (p2)
			*p2++ = 0;

		if (!irc_ischan(p1) && !irc_isnick(p1)) {
			free(dup);
			return -1;
		}
	} while (p2);

	for (const char *chan = dup; n_chans; n_chans--) {

		struct channel *c;

		if (channel_list_get(&s->clist, chan, s->casemapping))
			continue;

		if (irc_ischan(chan))
			c = channel(chan, CHANNEL_T_CHANNEL);
		else
			c = channel(chan, CHANNEL_T_PRIVMSG);

		c->server = s;
		channel_list_add(&s->clist, c);

		chan = strchr(chan, 0) + 1;
	}

	free(dup);

	return 0;
}

int
server_set_nicks(struct server *s, const char *str)
{
	char *dup;
	char *p1;
	char *p2;
	size_t n_nicks = 0;

	p2 = dup = strdup(str);

	do {
		n_nicks++;

		p1 = p2;
		p2 = strchr(p2, ',');

		if (p2)
			*p2++ = 0;

		if (!irc_isnick(p1)) {
			free(dup);
			return -1;
		}
	} while (p2);

	free((void *)s->nicks.base);
	free((void *)s->nicks.set);

	s->nicks.next = 0;
	s->nicks.size = n_nicks;
	s->nicks.base = dup;

	if ((s->nicks.set = malloc(sizeof(*s->nicks.set) * n_nicks)) == NULL)
		fatal("malloc: %s", strerror(errno));

	for (const char **set = s->nicks.set; n_nicks; n_nicks--, set++) {
		*set = dup;
		dup = strchr(dup, 0) + 1;
	}

	return 0;
}

void
server_set_004(struct server *s, char *str)
{
	/* <server_name> <version> <user_modes> <chan_modes> */

	const char *user_modes;
	const char *chan_modes;

	/* Not used */
	if (!irc_strsep(&str))
		server_error(s, "invalid numeric 004: server_name is null");

	/* Not used */
	if (!irc_strsep(&str))
		server_error(s, "invalid numeric 004: version is null");

	if (!(user_modes = irc_strsep(&str)))
		server_error(s, "invalid numeric 004: user_modes is null");

	if (!(chan_modes = irc_strsep(&str)))
		server_error(s, "invalid numeric 004: chan_modes is null");

	if (user_modes) {
		if (mode_cfg(&(s->mode_cfg), user_modes, MODE_CFG_USERMODES) == MODE_ERR_NONE)
			debug("Setting numeric 004 user_modes: %s", user_modes);
		else
			server_error(s, "invalid numeric 004 user_modes: %s", user_modes);
	}

	if (chan_modes) {
		if (mode_cfg(&(s->mode_cfg), chan_modes, MODE_CFG_CHANMODES) == MODE_ERR_NONE)
			debug("Setting numeric 004 chan_modes: %s", chan_modes);
		else
			server_error(s, "invalid numeric 004 chan_modes: %s", chan_modes);
	}
}

void
server_set_005(struct server *s, char *str)
{
	/* Iterate over options parsed from str and set for server s */

	struct opt opt;

	while (parse_005(&opt, &str)) {

		int (*server_set)(struct server*, char*) = NULL;

		#define X(cmd) \
		if (!strcmp(opt.arg, #cmd)) \
			server_set = server_set_##cmd;
		HANDLED_005
		#undef X

		if (server_set) {
			if (opt.val == NULL) {
				server_error(s, "invalid numeric 005 %s: value is NULL", opt.arg);
			} else if ((*server_set)(s, opt.val)) {
				server_error(s, "invalid numeric 005 %s: %s", opt.arg, opt.val);
			} else {
				debug("Setting numeric 005 %s: %s", opt.arg, opt.val);
			}
		}
	}
}

static int
server_cmp(const struct server *s, const char *host, const char *port)
{
	int cmp;

	if ((cmp = strcmp(s->host, host)))
		return cmp;

	if ((cmp = strcmp(s->port, port)))
		return cmp;

	return 0;
}

static int
parse_005(struct opt *opt, char **str)
{
	/* Parse a single argument from numeric 005 (ISUPPORT)
	 *
	 * docs/ISUPPORT.txt, section 2
	 *
	 * ":" servername SP "005" SP nickname SP 1*13( token SP ) ":are supported by this server"
	 *
	 * token     =  *1"-" parameter / parameter *1( "=" value )
	 * parameter =  1*20letter
	 * value     =  *letpun
	 * letter    =  ALPHA / DIGIT
	 * punct     =  %d33-47 / %d58-64 / %d91-96 / %d123-126
	 * letpun    =  letter / punct
	 */

	/* FIXME: (see docs)
	 *
	 * '-PARAMETER' is valid and negates a previously set parameter to its default
	 * 'PARAMETER', 'PARAMTER=' are equivalent
	 *
	 * The parameter's value may contain sequences of the form "\xHH", where
	 * HH is a two-digit hexadecimal number.  Each such sequence is
	 * considered identical to the equivalent octet after parsing of the
	 * reply into separate tokens has occurred.
	 *
	 * [Example: X=A\x20B defines one token, "X", with the value "A B",
	 * rather than two tokens "X" and "B".]
	 * [Note: The literal string "\x" must therefore be encoded as
	 * "\x5Cx".]
	 *
	 * If the server has not advertised a CHARSET parameter, it MUST not use
	 * such sequences with a value outside those permitted by the above ABNF
	 * grammar, with the exception of "\x20";  if it has advertised CHARSET,
	 * then it may in addition send any printable character defined in that
	 * encoding. Characters in multibyte encodings such as UTF-8 should be
	 * sent as a series of \x sequences.
	 */

	char *t, *p = *str;

	opt->arg = NULL;
	opt->val = NULL;

	if (!irc_strtrim(&p))
		return 0;

	if (!isalnum(*p))
		return 0;

	opt->arg = p;

	if ((t = strchr(p, ' '))) {
		*t++ = 0;
		*str = t;
	} else {
		*str = strchr(p, 0);
	}

	if ((p = strchr(opt->arg, '='))) {
		*p++ = 0;

		if (*p)
			opt->val = p;
	}

	return 1;
}

static int
server_set_CASEMAPPING(struct server *s, char *val)
{
	if (!strcmp(val, "ascii")) {
		s->casemapping = CASEMAPPING_ASCII;
		return 0;
	}

	if (!strcmp(val, "rfc1459")) {
		s->casemapping = CASEMAPPING_RFC1459;
		return 0;
	}

	if (!strcmp(val, "strict-rfc1459")) {
		s->casemapping = CASEMAPPING_STRICT_RFC1459;
		return 0;
	}

	return 1;
}

static int
server_set_CHANMODES(struct server *s, char *val)
{
	return mode_cfg(&(s->mode_cfg), val, MODE_CFG_SUBTYPES) != MODE_ERR_NONE;
}

static int
server_set_MODES(struct server *s, char *val)
{
	return mode_cfg(&(s->mode_cfg), val, MODE_CFG_MODES) != MODE_ERR_NONE;
}

static int
server_set_PREFIX(struct server *s, char *val)
{
	return mode_cfg(&(s->mode_cfg), val, MODE_CFG_PREFIX) != MODE_ERR_NONE;
}

void
server_nick_set(struct server *s, const char *nick)
{
	debug("Setting server nick: %s", nick);

	if (s->nick)
		free((void *)s->nick);

	s->nick = strdup(nick);
}

void
server_nicks_next(struct server *s)
{
	if (s->nicks.size && s->nicks.next < s->nicks.size) {
		server_nick_set(s, s->nicks.set[s->nicks.next++]);
	} else {
		/* Default to random nick, length 9 (RFC2912, section 1.2.1) */

		char nick_cset[] = "0123456789ABCDEF";
		char nick_rand[] = "rirc*****";

		for (char *p = strchr(nick_rand, '*'); p && *p; p++) {
			/* coverity[dont_call] Acceptable use of insecure rand() function */
			*p = nick_cset[rand() % strlen(nick_cset)];
		}

		server_nick_set(s, nick_rand);
	}
}
