#include "src/handlers/irc_recv.h"

#include "config.h"
#include "src/components/server.h"
#include "src/draw.h"
#include "src/handlers/irc_ctcp.h"
#include "src/handlers/irc_recv.gperf.out"
#include "src/handlers/ircv3.h"
#include "src/io.h"
#include "src/state.h"
#include "src/utils/utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#define failf(S, ...) \
	do { server_error((S), __VA_ARGS__); \
	     return 1; \
	} while (0)

#define sendf(S, ...) \
	do { int ret; \
	     if ((ret = io_sendf((S)->connection, __VA_ARGS__))) \
	         failf((S), "Send fail: %s", io_err(ret)); \
	} while (0)

/* Generic message handler */
static int irc_generic(struct server*, struct irc_message*, const char*, const char*);

/* Generic message handler subtypes */
static int irc_generic_error(struct server*, struct irc_message*);
static int irc_generic_ignore(struct server*, struct irc_message*);
static int irc_generic_info(struct server*, struct irc_message*);
static int irc_generic_unknown(struct server*, struct irc_message*);

/* Numeric handlers */
static int irc_numeric_001(struct server*, struct irc_message*);
static int irc_numeric_004(struct server*, struct irc_message*);
static int irc_numeric_005(struct server*, struct irc_message*);
static int irc_numeric_221(struct server*, struct irc_message*);
static int irc_numeric_324(struct server*, struct irc_message*);
static int irc_numeric_328(struct server*, struct irc_message*);
static int irc_numeric_329(struct server*, struct irc_message*);
static int irc_numeric_332(struct server*, struct irc_message*);
static int irc_numeric_333(struct server*, struct irc_message*);
static int irc_numeric_353(struct server*, struct irc_message*);
static int irc_numeric_401(struct server*, struct irc_message*);
static int irc_numeric_403(struct server*, struct irc_message*);
static int irc_numeric_433(struct server*, struct irc_message*);

static int irc_recv_numeric(struct server*, struct irc_message*);
static int recv_mode_chanmodes(struct irc_message*, const struct mode_cfg*, struct server*, struct channel*);
static int recv_mode_usermodes(struct irc_message*, const struct mode_cfg*, struct server*);

static unsigned threshold_account = FILTER_THRESHOLD_ACCOUNT;
static unsigned threshold_away    = FILTER_THRESHOLD_AWAY;
static unsigned threshold_chghost = FILTER_THRESHOLD_CHGHOST;
static unsigned threshold_join    = FILTER_THRESHOLD_JOIN;
static unsigned threshold_part    = FILTER_THRESHOLD_PART;
static unsigned threshold_quit    = FILTER_THRESHOLD_QUIT;

static const irc_recv_f irc_numerics[] = {
	  [1] = irc_numeric_001,    /* RPL_WELCOME */
	  [2] = irc_generic_info,   /* RPL_YOURHOST */
	  [3] = irc_generic_info,   /* RPL_CREATED */
	  [4] = irc_numeric_004,    /* RPL_MYINFO */
	  [5] = irc_numeric_005,    /* RPL_ISUPPORT */
	[200] = irc_generic_info,   /* RPL_TRACELINK */
	[201] = irc_generic_info,   /* RPL_TRACECONNECTING */
	[202] = irc_generic_info,   /* RPL_TRACEHANDSHAKE */
	[203] = irc_generic_info,   /* RPL_TRACEUNKNOWN */
	[204] = irc_generic_info,   /* RPL_TRACEOPERATOR */
	[205] = irc_generic_info,   /* RPL_TRACEUSER */
	[206] = irc_generic_info,   /* RPL_TRACESERVER */
	[207] = irc_generic_info,   /* RPL_TRACESERVICE */
	[208] = irc_generic_info,   /* RPL_TRACENEWTYPE */
	[209] = irc_generic_info,   /* RPL_TRACECLASS */
	[210] = irc_generic_info,   /* RPL_TRACELOG */
	[211] = irc_generic_info,   /* RPL_STATSLINKINFO */
	[212] = irc_generic_info,   /* RPL_STATSCOMMANDS */
	[213] = irc_generic_info,   /* RPL_STATSCLINE */
	[214] = irc_generic_info,   /* RPL_STATSNLINE */
	[215] = irc_generic_info,   /* RPL_STATSILINE */
	[216] = irc_generic_info,   /* RPL_STATSKLINE */
	[217] = irc_generic_info,   /* RPL_STATSQLINE */
	[218] = irc_generic_info,   /* RPL_STATSYLINE */
	[219] = irc_generic_ignore, /* RPL_ENDOFSTATS */
	[221] = irc_numeric_221,    /* RPL_UMODEIS */
	[234] = irc_generic_info,   /* RPL_SERVLIST */
	[235] = irc_generic_ignore, /* RPL_SERVLISTEND */
	[240] = irc_generic_info,   /* RPL_STATSVLINE */
	[241] = irc_generic_info,   /* RPL_STATSLLINE */
	[242] = irc_generic_info,   /* RPL_STATSUPTIME */
	[243] = irc_generic_info,   /* RPL_STATSOLINE */
	[244] = irc_generic_info,   /* RPL_STATSHLINE */
	[245] = irc_generic_info,   /* RPL_STATSSLINE */
	[246] = irc_generic_info,   /* RPL_STATSPING */
	[247] = irc_generic_info,   /* RPL_STATSBLINE */
	[250] = irc_generic_info,   /* RPL_STATSCONN */
	[251] = irc_generic_info,   /* RPL_LUSERCLIENT */
	[252] = irc_generic_info,   /* RPL_LUSEROP */
	[253] = irc_generic_info,   /* RPL_LUSERUNKNOWN */
	[254] = irc_generic_info,   /* RPL_LUSERCHANNELS */
	[255] = irc_generic_info,   /* RPL_LUSERME */
	[256] = irc_generic_info,   /* RPL_ADMINME */
	[257] = irc_generic_info,   /* RPL_ADMINLOC1 */
	[258] = irc_generic_info,   /* RPL_ADMINLOC2 */
	[259] = irc_generic_info,   /* RPL_ADMINEMAIL */
	[262] = irc_generic_info,   /* RPL_TRACEEND */
	[263] = irc_generic_info,   /* RPL_TRYAGAIN */
	[265] = irc_generic_info,   /* RPL_LOCALUSERS */
	[266] = irc_generic_info,   /* RPL_GLOBALUSERS */
	[301] = irc_generic_info,   /* RPL_AWAY */
	[302] = irc_generic_info,   /* ERR_USERHOST */
	[303] = irc_generic_info,   /* RPL_ISON */
	[305] = irc_generic_info,   /* RPL_UNAWAY */
	[306] = irc_generic_info,   /* RPL_NOWAWAY */
	[311] = irc_generic_info,   /* RPL_WHOISUSER */
	[312] = irc_generic_info,   /* RPL_WHOISSERVER */
	[313] = irc_generic_info,   /* RPL_WHOISOPERATOR */
	[314] = irc_generic_info,   /* RPL_WHOWASUSER */
	[315] = irc_generic_ignore, /* RPL_ENDOFWHO */
	[317] = irc_generic_info,   /* RPL_WHOISIDLE */
	[318] = irc_generic_ignore, /* RPL_ENDOFWHOIS */
	[319] = irc_generic_info,   /* RPL_WHOISCHANNELS */
	[322] = irc_generic_info,   /* RPL_LIST */
	[323] = irc_generic_ignore, /* RPL_LISTEND */
	[324] = irc_numeric_324,    /* RPL_CHANNELMODEIS */
	[325] = irc_generic_info,   /* RPL_UNIQOPIS */
	[328] = irc_numeric_328,    /* RPL_CHANNEL_URL */
	[329] = irc_numeric_329,    /* RPL_CREATIONTIME */
	[331] = irc_generic_ignore, /* RPL_NOTOPIC */
	[332] = irc_numeric_332,    /* RPL_TOPIC */
	[333] = irc_numeric_333,    /* RPL_TOPICWHOTIME */
	[341] = irc_generic_info,   /* RPL_INVITING */
	[346] = irc_generic_info,   /* RPL_INVITELIST */
	[347] = irc_generic_ignore, /* RPL_ENDOFINVITELIST */
	[348] = irc_generic_info,   /* RPL_EXCEPTLIST */
	[349] = irc_generic_ignore, /* RPL_ENDOFEXCEPTLIST */
	[351] = irc_generic_info,   /* RPL_VERSION */
	[352] = irc_generic_info,   /* RPL_WHOREPLY */
	[353] = irc_numeric_353,    /* RPL_NAMEREPLY */
	[364] = irc_generic_info,   /* RPL_LINKS */
	[365] = irc_generic_ignore, /* RPL_ENDOFLINKS */
	[366] = irc_generic_ignore, /* RPL_ENDOFNAMES */
	[367] = irc_generic_info,   /* RPL_BANLIST */
	[368] = irc_generic_ignore, /* RPL_ENDOFBANLIST */
	[369] = irc_generic_ignore, /* RPL_ENDOFWHOWAS */
	[371] = irc_generic_info,   /* RPL_INFO */
	[372] = irc_generic_info,   /* RPL_MOTD */
	[374] = irc_generic_ignore, /* RPL_ENDOFINFO */
	[375] = irc_generic_ignore, /* RPL_MOTDSTART */
	[376] = irc_generic_ignore, /* RPL_ENDOFMOTD */
	[381] = irc_generic_info,   /* RPL_YOUREOPER */
	[391] = irc_generic_info,   /* RPL_TIME */
	[396] = irc_generic_info,   /* RPL_VISIBLEHOST */
	[401] = irc_numeric_401,    /* ERR_NOSUCHNICK */
	[402] = irc_generic_error,  /* ERR_NOSUCHSERVER */
	[403] = irc_numeric_403,    /* ERR_NOSUCHCHANNEL */
	[404] = irc_generic_error,  /* ERR_CANNOTSENDTOCHAN */
	[405] = irc_generic_error,  /* ERR_TOOMANYCHANNELS */
	[406] = irc_generic_error,  /* ERR_WASNOSUCHNICK */
	[407] = irc_generic_error,  /* ERR_TOOMANYTARGETS */
	[408] = irc_generic_error,  /* ERR_NOSUCHSERVICE */
	[409] = irc_generic_error,  /* ERR_NOORIGIN */
	[410] = irc_generic_error,  /* ERR_INVALIDCAPCMD */
	[411] = irc_generic_error,  /* ERR_NORECIPIENT */
	[412] = irc_generic_error,  /* ERR_NOTEXTTOSEND */
	[413] = irc_generic_error,  /* ERR_NOTOPLEVEL */
	[414] = irc_generic_error,  /* ERR_WILDTOPLEVEL */
	[415] = irc_generic_error,  /* ERR_BADMASK */
	[416] = irc_generic_error,  /* ERR_TOOMANYMATCHES */
	[421] = irc_generic_error,  /* ERR_UNKNOWNCOMMAND */
	[422] = irc_generic_error,  /* ERR_NOMOTD */
	[423] = irc_generic_error,  /* ERR_NOADMININFO */
	[431] = irc_generic_error,  /* ERR_NONICKNAMEGIVEN */
	[432] = irc_generic_error,  /* ERR_ERRONEUSNICKNAME */
	[433] = irc_numeric_433,    /* ERR_NICKNAMEINUSE */
	[436] = irc_generic_error,  /* ERR_NICKCOLLISION */
	[437] = irc_generic_error,  /* ERR_UNAVAILRESOURCE */
	[441] = irc_generic_error,  /* ERR_USERNOTINCHANNEL */
	[442] = irc_generic_error,  /* ERR_NOTONCHANNEL */
	[443] = irc_generic_error,  /* ERR_USERONCHANNEL */
	[451] = irc_generic_error,  /* ERR_NOTREGISTERED */
	[461] = irc_generic_error,  /* ERR_NEEDMOREPARAMS */
	[462] = irc_generic_error,  /* ERR_ALREADYREGISTRED */
	[463] = irc_generic_error,  /* ERR_NOPERMFORHOST */
	[464] = irc_generic_error,  /* ERR_PASSWDMISMATCH */
	[465] = irc_generic_error,  /* ERR_YOUREBANNEDCREEP */
	[466] = irc_generic_error,  /* ERR_YOUWILLBEBANNED */
	[467] = irc_generic_error,  /* ERR_KEYSET */
	[471] = irc_generic_error,  /* ERR_CHANNELISFULL */
	[472] = irc_generic_error,  /* ERR_UNKNOWNMODE */
	[473] = irc_generic_error,  /* ERR_INVITEONLYCHAN */
	[474] = irc_generic_error,  /* ERR_BANNEDFROMCHAN */
	[475] = irc_generic_error,  /* ERR_BADCHANNELKEY */
	[476] = irc_generic_error,  /* ERR_BADCHANMASK */
	[477] = irc_generic_error,  /* ERR_NOCHANMODES */
	[478] = irc_generic_error,  /* ERR_BANLISTFULL */
	[481] = irc_generic_error,  /* ERR_NOPRIVILEGES */
	[482] = irc_generic_error,  /* ERR_CHANOPRIVSNEEDED */
	[483] = irc_generic_error,  /* ERR_CANTKILLSERVER */
	[484] = irc_generic_error,  /* ERR_RESTRICTED */
	[485] = irc_generic_error,  /* ERR_UNIQOPPRIVSNEEDED */
	[491] = irc_generic_error,  /* ERR_NOOPERHOST */
	[501] = irc_generic_error,  /* ERR_UMODEUNKNOWNFLAG */
	[502] = irc_generic_error,  /* ERR_USERSDONTMATCH */
	[704] = irc_generic_info,   /* RPL_HELPSTART */
	[705] = irc_generic_info,   /* RPL_HELP */
	[706] = irc_generic_ignore, /* RPL_ENDOFHELP */
	[900] = ircv3_numeric_900,  /* IRCv3 RPL_LOGGEDIN */
	[901] = ircv3_numeric_901,  /* IRCv3 RPL_LOGGEDOUT */
	[902] = ircv3_numeric_902,  /* IRCv3 ERR_NICKLOCKED */
	[903] = ircv3_numeric_903,  /* IRCv3 RPL_SASLSUCCESS */
	[904] = ircv3_numeric_904,  /* IRCv3 ERR_SASLFAIL */
	[905] = ircv3_numeric_905,  /* IRCv3 ERR_SASLTOOLONG */
	[906] = ircv3_numeric_906,  /* IRCv3 ERR_SASLABORTED */
	[907] = ircv3_numeric_907,  /* IRCv3 ERR_SASLALREADY */
	[908] = ircv3_numeric_908,  /* IRCv3 RPL_SASLMECHS */
	[1000] = NULL               /* Out of range */
};

int
irc_recv(struct server *s, struct irc_message *m)
{
	const struct recv_handler* handler;

	if (isdigit(*m->command))
		return irc_recv_numeric(s, m);

	if ((handler = recv_handler_lookup(m->command, m->len_command)))
		return handler->f(s, m);

	return irc_generic_unknown(s, m);
}

static int
irc_generic(struct server *s, struct irc_message *m, const char *command, const char *from)
{
	/* Generic handling of messages, in the form:
	 *
	 *   [command] [params] ~ trailing
	 */

	const char *params       = NULL;
	const char *params_sep   = NULL;
	const char *trailing     = NULL;
	const char *trailing_sep = NULL;

	if (!command && !irc_strtrim(&m->params))
		return 1;

	irc_message_split(m, &params, &trailing);

	if (command && params)
		params_sep = " ";

	if ((command || params) && trailing)
		trailing_sep = " ~ ";

	newlinef(s->channel, 0, from,
		"%s%s%s" "%s%s%s%s" "%s%s",
		(!command      ? "" : "["),
		(!command      ? "" : command),
		(!command      ? "" : "]"),
		(!params_sep   ? "" : params_sep),
		(!params       ? "" : "["),
		(!params       ? "" : params),
		(!params       ? "" : "]"),
		(!trailing_sep ? "" : trailing_sep),
		(!trailing     ? "" : trailing)
	);

	return 0;
}

static int
irc_generic_error(struct server *s, struct irc_message *m)
{
	/* Generic error message handling */

	return irc_generic(s, m, NULL, FROM_ERROR);
}

static int
irc_generic_ignore(struct server *s, struct irc_message *m)
{
	/* Generic handling for ignored message types */

	UNUSED(s);
	UNUSED(m);

	return 0;
}

static int
irc_generic_info(struct server *s, struct irc_message *m)
{
	/* Generic info message handling */

	return irc_generic(s, m, NULL, FROM_INFO);
}

static int
irc_generic_unknown(struct server *s, struct irc_message *m)
{
	/* Generic handling of unknown commands */

	return irc_generic(s, m, m->command, FROM_UNKNOWN);
}

static int
irc_numeric_001(struct server *s, struct irc_message *m)
{
	/* 001 :<Welcome message> */

	const char *params;
	const char *trailing;
	struct channel *c = s->channel;

	s->registered = 1;

	if (irc_message_split(m, &params, &trailing))
		server_info(s, "%s", trailing);

	server_info(s, "You are known as %s", s->nick);

	if (s->mode)
		sendf(s, "MODE %s +%s", s->nick, s->mode);

	do {
		if (c->type == CHANNEL_T_CHANNEL && !c->parted)
			sendf(s, "JOIN %s", c->name);
	} while ((c = c->next) != s->channel);

	return 0;
}

static int
irc_numeric_004(struct server *s, struct irc_message *m)
{
	/* 004 1*<params> [:message] */

	const char *params;
	const char *trailing;

	if (irc_message_split(m, &params, &trailing))
		server_info(s, "%s ~ %s", params, trailing);
	else
		server_info(s, "%s", params);

	server_set_004(s, m->params);

	return 0;
}

static int
irc_numeric_005(struct server *s, struct irc_message *m)
{
	/* 005 1*<params> [:message] */

	const char *params;
	const char *trailing;

	if (irc_message_split(m, &params, &trailing))
		server_info(s, "%s ~ %s", params, trailing);
	else
		server_info(s, "%s ~ are supported by this server", params);

	server_set_005(s, m->params);

	return 0;
}

static int
irc_numeric_221(struct server *s, struct irc_message *m)
{
	/* 221 <modestring> */

	return recv_mode_usermodes(m, &(s->mode_cfg), s);
}

static int
irc_numeric_324(struct server *s, struct irc_message *m)
{
	/* 324 <channel> 1*[<modestring> [<mode arguments>]] */

	char *chan;
	struct channel *c;

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_CHANNELMODEIS: channel is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_CHANNELMODEIS: channel '%s' not found", chan);

	return recv_mode_chanmodes(m, &(s->mode_cfg), s, c);
}

static int
irc_numeric_328(struct server *s, struct irc_message *m)
{
	/* 328 <channel> <url> */

	char *chan;
	char *url;
	struct channel *c;

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_CHANNEL_URL: channel is null");

	if (!irc_message_param(m, &url))
		failf(s, "RPL_CHANNEL_URL: url is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_CHANNEL_URL: channel '%s' not found", chan);

	newlinef(c, 0, FROM_INFO, "URL for %s is: \"%s\"", chan, url);

	return 0;
}

static int
irc_numeric_329(struct server *s, struct irc_message *m)
{
	char buf[sizeof("1970-01-01T00:00:00")];
	char *chan;
	char *time_str;
	struct channel *c;
	struct tm tm;
	time_t t = 0;

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_CREATIONTIME: channel is null");

	if (!irc_message_param(m, &time_str))
		failf(s, "RPL_CREATIONTIME: time is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_CREATIONTIME: channel '%s' not found", chan);

	errno = 0;
	t = strtoul(time_str, NULL, 0);

	if (errno)
		failf(s, "RPL_CREATIONTIME: strtoul error: %s", strerror(errno));

	if (gmtime_r(&t, &tm) == NULL)
		failf(s, "RPL_CREATIONTIME: gmtime_r error: %s", strerror(errno));

	if ((strftime(buf, sizeof(buf), "%FT%T", &tm)) == 0)
		failf(s, "RPL_CREATIONTIME: strftime error");

	newlinef(c, 0, FROM_INFO, "Channel created %s", buf);

	return 0;
}

static int
irc_numeric_332(struct server *s, struct irc_message *m)
{
	/* 332 <channel> :<topic> */

	char *chan;
	char *topic;
	struct channel *c;

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_TOPIC: channel is null");

	if (!irc_message_param(m, &topic))
		failf(s, "RPL_TOPIC: topic is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_TOPIC: channel '%s' not found", chan);

	newlinef(c, 0, FROM_INFO, "Topic for %s is \"%s\"", chan, topic);

	return 0;
}

static int
irc_numeric_333(struct server *s, struct irc_message *m)
{
	/* 333 <channel> <nick> <time> */

	char buf[sizeof("1970-01-01T00:00:00")];
	char *chan;
	char *nick;
	char *time_str;
	struct channel *c;
	struct tm tm;
	time_t t = 0;

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_TOPICWHOTIME: channel is null");

	if (!irc_message_param(m, &nick))
		failf(s, "RPL_TOPICWHOTIME: nick is null");

	if (!irc_message_param(m, &time_str))
		failf(s, "RPL_TOPICWHOTIME: time is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_TOPICWHOTIME: channel '%s' not found", chan);

	errno = 0;
	t = strtoul(time_str, NULL, 0);

	if (errno)
		failf(s, "RPL_TOPICWHOTIME: strtoul error: %s", strerror(errno));

	if (gmtime_r(&t, &tm) == NULL)
		failf(s, "RPL_TOPICWHOTIME: gmtime_r error: %s", strerror(errno));

	if ((strftime(buf, sizeof(buf), "%FT%T", &tm)) == 0)
		failf(s, "RPL_TOPICWHOTIME: strftime error");

	newlinef(c, 0, FROM_INFO, "Topic set by %s, %s", nick, buf);

	return 0;
}

static int
irc_numeric_353(struct server *s, struct irc_message *m)
{
	/* <type> <channel> 1*(<modes><nick>) */

	char *chan;
	char *nick;
	char *nicks;
	char *prefix;
	struct channel *c;

	if (!irc_message_param(m, &prefix))
		failf(s, "RPL_NAMEREPLY: type is null");

	if (!irc_message_param(m, &chan))
		failf(s, "RPL_NAMEREPLY: channel is null");

	if (!irc_message_param(m, &nicks))
		failf(s, "RPL_NAMEREPLY: nicks is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "RPL_NAMEREPLY: channel '%s' not found", chan);

	if (*prefix != '@' && *prefix != '*' && *prefix != '=')
		failf(s, "RPL_NAMEREPLY: invalid channel type: '%c'", *prefix);

	if (*prefix == '@')
		(void) mode_chanmode_set(&(c->chanmodes), &(s->mode_cfg), 's', 1);

	if (*prefix == '*')
		(void) mode_chanmode_set(&(c->chanmodes), &(s->mode_cfg), 'p', 1);

	c->chanmodes.prefix = *prefix;

	while ((prefix = nick = irc_strsep(&nicks))) {

		struct mode m = MODE_EMPTY;

		while (mode_prfxmode_set(&m, &(s->mode_cfg), *nick, 1) == MODE_ERR_NONE)
			nick++;

		if (*nick == 0)
			failf(s, "RPL_NAMEREPLY: invalid nick: '%s'", prefix);

		if (user_list_add(&(c->users), s->casemapping, nick, m) == USER_ERR_DUPLICATE)
			failf(s, "RPL_NAMEREPLY: duplicate nick: '%s'", nick);
	}

	draw(DRAW_STATUS);

	return 0;
}

static int
irc_numeric_401(struct server *s, struct irc_message *m)
{
	/* <nick> :No such nick/channel */

	char *message;
	char *nick;
	struct channel *c;

	if (!irc_message_param(m, &nick))
		failf(s, "ERR_NOSUCHNICK: nick is null");

	if (!(c = channel_list_get(&(s->clist), nick, s->casemapping)))
		c = s->channel;

	irc_message_param(m, &message);

	if (message && *message)
		newlinef(c, 0, FROM_ERROR, "[%s] %s", nick, message);
	else
		newlinef(c, 0, FROM_ERROR, "[%s] No such nick/channel", nick);

	return 0;
}

static int
irc_numeric_403(struct server *s, struct irc_message *m)
{
	/* <chan> :No such channel */

	char *message;
	char *chan;
	struct channel *c;

	if (!irc_message_param(m, &chan))
		failf(s, "ERR_NOSUCHCHANNEL: chan is null");

	if (!(c = channel_list_get(&(s->clist), chan, s->casemapping)))
		c = s->channel;

	irc_message_param(m, &message);

	if (message && *message)
		newlinef(c, 0, FROM_ERROR, "[%s] %s", chan, message);
	else
		newlinef(c, 0, FROM_ERROR, "[%s] No such channel", chan);

	return 0;
}

static int
irc_numeric_433(struct server *s, struct irc_message *m)
{
	/* 433 <nick> :Nickname is already in use */

	char *nick;

	if (!irc_message_param(m, &nick))
		failf(s, "ERR_NICKNAMEINUSE: nick is null");

	server_error(s, "Nick '%s' in use", nick);

	if (!strcmp(nick, s->nick)) {
		server_nicks_next(s);
		server_error(s, "Trying again with '%s'", s->nick);
		sendf(s, "NICK %s", s->nick);
	}

	return 0;
}

static int
irc_recv_numeric(struct server *s, struct irc_message *m)
{
	/* :server <code> <target> [args] */

	char *targ;
	unsigned code = 0;

	if ((m->command[0] && isdigit(m->command[0]))
	 && (m->command[1] && isdigit(m->command[1]))
	 && (m->command[2] && isdigit(m->command[2]))
	 && (m->command[3] == 0))
	{
		code += (m->command[0] - '0') * 100;
		code += (m->command[1] - '0') * 10;
		code += (m->command[2] - '0');
	}

	if (!code)
		failf(s, "NUMERIC: '%s' invalid", m->command);

	if (!(irc_message_param(m, &targ)))
		failf(s, "NUMERIC: target is null");

	if (strcmp(targ, s->nick) && strcmp(targ, "*"))
		failf(s, "NUMERIC: target '%s' is invalid", targ);

	if (!irc_numerics[code])
		return irc_generic_unknown(s, m);

	return (*irc_numerics[code])(s, m);
}

static int
recv_error(struct server *s, struct irc_message *m)
{
	/* ERROR :<message> */

	char *message;

	if (!irc_message_param(m, &message))
		failf(s, "ERROR: message is null");

	newlinef(s->channel, 0, (s->quitting ? FROM_INFO : "ERROR"), "%s", message);

	return 0;
}

static int
recv_invite(struct server *s, struct irc_message *m)
{
	/* :nick!user@host INVITE <nick> <channel> */

	char *chan;
	char *nick;
	struct channel *c;

	if (!m->from)
		failf(s, "INVITE: sender's nick is null");

	if (!irc_message_param(m, &nick))
		failf(s, "INVITE: nick is null");

	if (!irc_message_param(m, &chan))
		failf(s, "INVITE: channel is null");

	if (!strcmp(nick, s->nick)) {
		server_info(s, "%s invited you to %s", m->from, chan);
		return 0;
	}

	/* IRCv3 CAP invite-notify, sent to all users on the target channel */
	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "INVITE: channel '%s' not found", chan);

	newlinef(c, 0, FROM_INFO, "%s invited %s to %s", m->from, nick, chan);

	return 0;
}

static int
recv_join(struct server *s, struct irc_message *m)
{
	/* :nick!user@host JOIN <channel>
	 * :nick!user@host JOIN <channel> <account> :<realname> */

	char *chan;
	struct channel *c;

	if (!m->from)
		failf(s, "JOIN: sender's nick is null");

	if (!irc_message_param(m, &chan))
		failf(s, "JOIN: channel is null");

	if (!strcmp(m->from, s->nick)) {
		if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL) {
			c = channel(chan, CHANNEL_T_CHANNEL);
			c->server = s;
			channel_list_add(&s->clist, c);
			channel_set_current(c);
		}
		c->joined = 1;
		c->parted = 0;
		newlinef(c, BUFFER_LINE_JOIN, FROM_JOIN, "Joined %s", chan);
		sendf(s, "MODE %s", chan);
		draw(DRAW_ALL);
		return 0;
	}

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "JOIN: channel '%s' not found", chan);

	if (user_list_add(&(c->users), s->casemapping, m->from, MODE_EMPTY) == USER_ERR_DUPLICATE)
		failf(s, "JOIN: user '%s' already on channel '%s'", m->from, chan);

	if (!threshold_join || threshold_join > c->users.count) {

		if (s->ircv3_caps.extended_join.set) {

			char *account;
			char *realname;

			if (!irc_message_param(m, &account))
				failf(s, "JOIN: account is null");

			if (!irc_message_param(m, &realname))
				failf(s, "JOIN: realname is null");

			newlinef(c, BUFFER_LINE_JOIN, FROM_JOIN, "%s!%s has joined [%s - %s]",
				m->from, m->host, account, realname);
		} else {
			newlinef(c, BUFFER_LINE_JOIN, FROM_JOIN, "%s!%s has joined", m->from, m->host);
		}
	}

	draw(DRAW_STATUS);

	return 0;
}

static int
recv_kick(struct server *s, struct irc_message *m)
{
	/* :nick!user@host KICK <channel> <user> [:message] */

	char *chan;
	char *message;
	char *user;
	struct channel *c;

	if (!m->from)
		failf(s, "KICK: sender's nick is null");

	if (!irc_message_param(m, &chan))
		failf(s, "KICK: channel is null");

	if (!irc_message_param(m, &user))
		failf(s, "KICK: user is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "KICK: channel '%s' not found", chan);

	irc_message_param(m, &message);

	/* RFC 2812, section 3.2.8:
	 *
	 * If a "comment" is given, this will be sent instead of the
	 * default message, the nickname of the user issuing the KICK */
	if (message && !strcmp(m->from, message))
		message = NULL;

	if (!strcmp(user, s->nick)) {

		channel_part(c);

		if (message && *message)
			newlinef(c, 0, FROM_INFO, "Kicked by %s (%s)", m->from, message);
		else
			newlinef(c, 0, FROM_INFO, "Kicked by %s", m->from);

	} else {

		if (user_list_del(&(c->users), s->casemapping, user) == USER_ERR_NOT_FOUND)
			failf(s, "KICK: nick '%s' not found in '%s'", user, chan);

		if (message && *message)
			newlinef(c, 0, FROM_INFO, "%s has kicked %s (%s)", m->from, user, message);
		else
			newlinef(c, 0, FROM_INFO, "%s has kicked %s", m->from, user);
	}

	draw(DRAW_STATUS);

	return 0;
}

static int
recv_mode(struct server *s, struct irc_message *m)
{
	/* MODE <targ> 1*[<modestring> [<mode arguments>]]
	 *
	 * modestring  =  1*(modeset)
	 * modeset     =  plusminus *(modechar)
	 * plusminus   =  %x53 / %x55            ; '+' / '-'
	 * modechar    =  ALPHA
	 *
	 * Any number of mode flags can be set or unset in a MODE message, but
	 * the maximum number of modes with parameters is given by the server's
	 * MODES configuration.
	 *
	 * Mode flags that require a parameter are configured as the server's
	 * CHANMODE subtypes; A,B,C,D
	 *
	 * The following formats are equivalent, if e.g.:
	 *  - 'a' and 'c' require parameters
	 *  - 'b' has no parameter
	 *
	 *   MODE <channel> +ab  <param a> +c <param c>
	 *   MODE <channel> +abc <param a>    <param c>
	 */

	char *targ;
	struct channel *c;

	if (!irc_message_param(m, &targ))
		failf(s, "NICK: new nick is null");

	if (!strcmp(targ, s->nick))
		return recv_mode_usermodes(m, &(s->mode_cfg), s);

	if ((c = channel_list_get(&s->clist, targ, s->casemapping)))
		return recv_mode_chanmodes(m, &(s->mode_cfg), s, c);

	failf(s, "MODE: target '%s' not found", targ);
}

static int
recv_mode_chanmodes(struct irc_message *m, const struct mode_cfg *cfg, struct server *s, struct channel *c)
{
	char flag;
	char *modestring;
	char *modearg;
	enum mode_err mode_err;
	struct mode *chanmodes = &(c->chanmodes);
	struct user *user;

	// TODO: mode string segfaults if args out of order

	if (!irc_message_param(m, &modestring)) {
		newlinef(c, 0, FROM_ERROR, "MODE: modestring is null");
		return 1;
	}

	do {
		int set = -1;
		mode_err = MODE_ERR_NONE;

		while ((flag = *modestring++)) {

			if (flag == '-') {
				set = 0;
				continue;
			}

			if (flag == '+') {
				set = 1;
				continue;
			}

			if (set == -1) {
				newlinef(c, 0, FROM_ERROR, "MODE: missing '+'/'-'");
				continue;
			}

			modearg = NULL;

			switch (mode_type(cfg, flag, set)) {

				/* Doesn't consume an argument */
				case MODE_FLAG_CHANMODE:

					mode_err = mode_chanmode_set(chanmodes, cfg, flag, set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, FROM_INFO, "%s%s%s mode: %c%c",
								(m->from ? m->from : ""),
								(m->from ? " set " : ""),
								c->name,
								(set ? '+' : '-'),
								flag);
					}
					break;

				/* Consumes an argument */
				case MODE_FLAG_CHANMODE_PARAM:

					if (!irc_message_param(m, &modearg)) {
						newlinef(c, 0, FROM_ERROR, "MODE: flag '%c' expected argument", flag);
						continue;
					}

					mode_err = mode_chanmode_set(chanmodes, cfg, flag, set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, FROM_INFO, "%s%s%s mode: %c%c %s",
								(m->from ? m->from : ""),
								(m->from ? " set " : ""),
								c->name,
								(set ? '+' : '-'),
								flag,
								modearg);
					}
					break;

				/* Consumes an argument and sets a usermode */
				case MODE_FLAG_PREFIX:

					if (!irc_message_param(m, &modearg)) {
						newlinef(c, 0, FROM_ERROR, "MODE: flag '%c' argument is null", flag);
						continue;
					}

					if (!(user = user_list_get(&(c->users), s->casemapping, modearg, 0))) {
						newlinef(c, 0, FROM_ERROR, "MODE: flag '%c' user '%s' not found", flag, modearg);
						continue;
					}

					mode_prfxmode_set(&(user->prfxmodes), cfg, flag, set);

					if (mode_err == MODE_ERR_NONE) {
						newlinef(c, 0, FROM_INFO, "%s%suser %s mode: %c%c",
								(m->from ? m->from : ""),
								(m->from ? " set " : ""),
								modearg,
								(set ? '+' : '-'),
								flag);
					}
					break;

				case MODE_FLAG_INVALID_FLAG:
					newlinef(c, 0, FROM_ERROR, "MODE: invalid flag '%c'", flag);
					break;

				default:
					newlinef(c, 0, FROM_ERROR, "MODE: unhandled error, flag '%c'", flag);
					continue;
			}
		}
	} while (irc_message_param(m, &modestring));

	mode_str(&(c->chanmodes), &(c->chanmodes_str), MODE_STR_CHANMODE);
	draw(DRAW_STATUS);

	return 0;
}

static int
recv_mode_usermodes(struct irc_message *m, const struct mode_cfg *cfg, struct server *s)
{
	char flag;
	char *modestring;
	enum mode_err mode_err;
	struct mode *usermodes = &(s->usermodes);

	if (!irc_message_param(m, &modestring))
		failf(s, "MODE: modestring is null");

	do {
		int set = -1;

		while ((flag = *modestring++)) {

			if (flag == '-') {
				set = 0;
				continue;
			}

			if (flag == '+') {
				set = 1;
				continue;
			}

			if (set == -1) {
				server_error(s, "MODE: missing '+'/'-'");
				continue;
			}

			mode_err = mode_usermode_set(usermodes, cfg, flag, set);

			if (mode_err == MODE_ERR_NONE)
				server_info(s, "%s%smode: %c%c",
						(m->from ? m->from : ""),
						(m->from ? " set " : ""),
						(set ? '+' : '-'),
						flag);

			else if (mode_err == MODE_ERR_INVALID_FLAG)
				server_error(s, "MODE: invalid flag '%c'", flag);
		}
	} while (irc_message_param(m, &modestring));

	mode_str(usermodes, &(s->mode_str), MODE_STR_USERMODE);
	draw(DRAW_STATUS);

	return 0;
}

static int
recv_nick(struct server *s, struct irc_message *m)
{
	/* :nick!user@host NICK <nick> */

	char *nick;
	struct channel *c = s->channel;

	if (!m->from)
		failf(s, "NICK: old nick is null");

	if (!irc_message_param(m, &nick))
		failf(s, "NICK: new nick is null");

	if (!strcmp(m->from, s->nick)) {
		server_nick_set(s, nick);
		newlinef(s->channel, BUFFER_LINE_NICK, FROM_INFO, "Your nick is now '%s'", nick);
	}

	do {
		enum user_err ret;

		if ((ret = user_list_rpl(&(c->users), s->casemapping, m->from, nick)) == USER_ERR_NONE)
			newlinef(c, BUFFER_LINE_NICK, FROM_INFO, "%s  >>  %s", m->from, nick);

		else if (ret == USER_ERR_DUPLICATE)
			server_error(s, "NICK: user '%s' already on channel '%s'", nick, c->name);

	} while ((c = c->next) != s->channel);

	return 0;
}

static int
recv_notice(struct server *s, struct irc_message *m)
{
	/* :nick!user@host NOTICE <target> :<message> */

	char *message;
	char *target;
	struct channel *c;

	if (!m->from)
		failf(s, "NOTICE: sender's nick is null");

	if (!irc_message_param(m, &target))
		failf(s, "NOTICE: target is null");

	if (!irc_message_param(m, &message))
		failf(s, "NOTICE: message is null");

	if (IS_CTCP(message))
		return ctcp_response(s, m->from, target, message);

	if (!(c = channel_list_get(&(s->clist), m->from, s->casemapping)))
		c = s->channel;

	newlinef(c, BUFFER_LINE_CHAT, m->from, "%s", message);

	return 0;
}

static int
recv_part(struct server *s, struct irc_message *m)
{
	/* :nick!user@host PART <channel> [:message] */

	char *chan;
	char *message;
	struct channel *c;

	if (!m->from)
		failf(s, "PART: sender's nick is null");

	if (!irc_message_param(m, &chan))
		failf(s, "PART: channel is null");

	irc_message_param(m, &message);

	if (!strcmp(m->from, s->nick)) {

		/* If not found, assume channel was closed */
		if ((c = channel_list_get(&s->clist, chan, s->casemapping)) != NULL) {

			if (message && *message)
				newlinef(c, BUFFER_LINE_PART, FROM_PART, "you have parted (%s)", message);
			else
				newlinef(c, BUFFER_LINE_PART, FROM_PART, "you have parted");

			channel_part(c);
		}
	} else {

		if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
			failf(s, "PART: channel '%s' not found", chan);

		if (user_list_del(&(c->users), s->casemapping, m->from) == USER_ERR_NOT_FOUND)
			failf(s, "PART: nick '%s' not found in '%s'", m->from, chan);

		if (!threshold_part || threshold_part > c->users.count) {

			if (message && *message)
				newlinef(c, 0, FROM_PART, "%s!%s has parted (%s)", m->from, m->host, message);
			else
				newlinef(c, 0, FROM_PART, "%s!%s has parted", m->from, m->host);
		}
	}

	draw(DRAW_STATUS);

	return 0;
}

static int
recv_ping(struct server *s, struct irc_message *m)
{
	/* PING <server> */

	char *server;

	if (!irc_message_param(m, &server))
		failf(s, "PING: server is null");

	sendf(s, "PONG %s", server);

	return 0;
}

static int
recv_pong(struct server *s, struct irc_message *m)
{
	/* PONG <server> [<server2>] */

	UNUSED(s);
	UNUSED(m);

	return 0;
}

static int
recv_privmsg(struct server *s, struct irc_message *m)
{
	/* :nick!user@host PRIVMSG <target> :<message> */

	char *message;
	char *target;
	int urgent = 0;
	struct channel *c;

	if (!m->from)
		failf(s, "PRIVMSG: sender's nick is null");

	if (!irc_message_param(m, &target))
		failf(s, "PRIVMSG: target is null");

	if (!irc_message_param(m, &message))
		failf(s, "PRIVMSG: message is null");

	if (IS_CTCP(message))
		return ctcp_request(s, m->from, target, message);

	if (!strcmp(target, s->nick)) {

		if ((c = channel_list_get(&s->clist, m->from, s->casemapping)) == NULL) {
			c = channel(m->from, CHANNEL_T_PRIVMSG);
			c->server = s;
			channel_list_add(&s->clist, c);
		}

		if (c != current_channel())
			urgent = 1;

	} else if ((c = channel_list_get(&s->clist, target, s->casemapping)) == NULL) {
		failf(s, "PRIVMSG: channel '%s' not found", target);
	}

	if (irc_pinged(s->casemapping, message, s->nick)) {

		if (c != current_channel())
			urgent = 1;

		newlinef(c, BUFFER_LINE_PINGED, m->from, "%s", message);
	} else {
		newlinef(c, BUFFER_LINE_CHAT, m->from, "%s", message);
	}

	if (urgent) {
		c->activity = ACTIVITY_PINGED;
		draw(DRAW_BELL);
		draw(DRAW_NAV);
	}

	return 0;
}

static int
recv_quit(struct server *s, struct irc_message *m)
{
	/* :nick!user@host QUIT [:message] */

	char *message;
	struct channel *c = s->channel;

	if (!m->from)
		failf(s, "QUIT: sender's nick is null");

	irc_message_param(m, &message);

	do {
		if (user_list_del(&(c->users), s->casemapping, m->from) == USER_ERR_NONE) {

			if (threshold_quit && threshold_quit <= c->users.count)
				continue;

			if (message && *message)
				newlinef(c, BUFFER_LINE_QUIT, FROM_QUIT, "%s!%s has quit (%s)",
					m->from, m->host, message);
			else
				newlinef(c, BUFFER_LINE_QUIT, FROM_QUIT, "%s!%s has quit",
					m->from, m->host);
		}
	} while ((c = c->next) != s->channel);

	draw(DRAW_STATUS);

	return 0;
}

static int
recv_topic(struct server *s, struct irc_message *m)
{
	/* :nick!user@host TOPIC <channel> [:topic] */

	char *chan;
	char *topic;
	struct channel *c;

	if (!m->from)
		failf(s, "TOPIC: sender's nick is null");

	if (!irc_message_param(m, &chan))
		failf(s, "TOPIC: channel is null");

	if (!irc_message_param(m, &topic))
		failf(s, "TOPIC: topic is null");

	if ((c = channel_list_get(&s->clist, chan, s->casemapping)) == NULL)
		failf(s, "TOPIC: channel '%s' not found", chan);

	if (*topic) {
		newlinef(c, 0, FROM_INFO, "%s has set the topic:", m->from);
		newlinef(c, 0, FROM_INFO, "\"%s\"", topic);
	} else {
		newlinef(c, 0, FROM_INFO, "%s has unset the topic", m->from);
	}

	return 0;
}

static int
recv_ircv3_cap(struct server *s, struct irc_message *m)
{
	int ret;

	if ((ret = ircv3_recv_CAP(s, m)) && !s->registered)
		io_dx(s->connection);

	return ret;
}

static int
recv_ircv3_authenticate(struct server *s, struct irc_message *m)
{
	int ret;

	if ((ret = ircv3_recv_AUTHENTICATE(s, m)) && !s->registered)
		io_dx(s->connection);

	return ret;
}

static int
recv_ircv3_account(struct server *s, struct irc_message *m)
{
	/* :nick!user@host ACCOUNT <account> */

	char *account;
	struct channel *c = s->channel;

	if (!m->from)
		failf(s, "ACCOUNT: sender's nick is null");

	if (!irc_message_param(m, &account))
		failf(s, "ACCOUNT: account is null");

	do {
		if (!user_list_get(&(c->users), s->casemapping, m->from, 0))
			continue;

		if (threshold_account && threshold_account <= c->users.count)
			continue;

		if (!strcmp(account, "*"))
			newlinef(c, 0, FROM_INFO, "%s has logged out", m->from);
		else
			newlinef(c, 0, FROM_INFO, "%s has logged in as %s", m->from, account);

	} while ((c = c->next) != s->channel);

	return 0;
}

static int
recv_ircv3_away(struct server *s, struct irc_message *m)
{
	/* :nick!user@host AWAY [:message] */

	char *message;
	struct channel *c = s->channel;

	if (!m->from)
		failf(s, "AWAY: sender's nick is null");

	irc_message_param(m, &message);

	do {
		if (!user_list_get(&(c->users), s->casemapping, m->from, 0))
			continue;

		if (threshold_away && threshold_away <= c->users.count)
			continue;

		if (message)
			newlinef(c, 0, FROM_INFO, "%s is now away: %s", m->from, message);
		else
			newlinef(c, 0, FROM_INFO, "%s is no longer away", m->from);

	} while ((c = c->next) != s->channel);

	return 0;
}

static int
recv_ircv3_chghost(struct server *s, struct irc_message *m)
{
	/* :nick!user@host CHGHOST new_user new_host */

	char *user;
	char *host;
	struct channel *c = s->channel;

	if (!m->from)
		failf(s, "CHGHOST: sender's nick is null");

	if (!irc_message_param(m, &user))
		failf(s, "CHGHOST: user is null");

	if (!irc_message_param(m, &host))
		failf(s, "CHGHOST: host is null");

	do {
		if (!user_list_get(&(c->users), s->casemapping, m->from, 0))
			continue;

		if (threshold_chghost && threshold_chghost <= c->users.count)
			continue;

		newlinef(c, 0, FROM_INFO, "%s has changed user/host: %s/%s", m->from, user, host);

	} while ((c = c->next) != s->channel);

	return 0;
}

#undef failf
#undef sendf
