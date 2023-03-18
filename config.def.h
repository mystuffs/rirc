/* rirc configuration header
 *
 * Colours can be set [0, 255], Any other value (e.g. -1) will set
 * the default terminal foreground/background */

/* Comma separated set of default nicks to try on connection
 *   String
 *   ("": defaults to effective user id name)
 */
#define DEFAULT_NICKS ""

/* Default Username and Realname sent during connection
 *   String
 *   ("": defaults to effective user id name)
 */
#define DEFAULT_USERNAME ""
#define DEFAULT_REALNAME ""

/* User count in channel before filtering message types
 *   Integer
 *   (0: no filtering) */
#define FILTER_THRESHOLD_JOIN    0
#define FILTER_THRESHOLD_PART    0
#define FILTER_THRESHOLD_NICK    0
#define FILTER_THRESHOLD_QUIT    0
#define FILTER_THRESHOLD_ACCOUNT 0
#define FILTER_THRESHOLD_AWAY    0
#define FILTER_THRESHOLD_CHGHOST 0

/* Message sent for PART and QUIT by default */
#define DEFAULT_QUIT_MESG "rirc v" STR(VERSION)
#define DEFAULT_PART_MESG "rirc v" STR(VERSION)

/* Buffer colours */
#define BUFFER_LINE_HEADER_FG         239
#define BUFFER_LINE_HEADER_BG         -1
#define BUFFER_LINE_HEADER_FG_PINGED  250
#define BUFFER_LINE_HEADER_BG_PINGED  1

#define BUFFER_TEXT_FG -1;
#define BUFFER_TEXT_BG -1;

/* Number of buffer lines to keep in history, must be power of 2 */
#define BUFFER_LINES_MAX (1 << 10)

/* Colours used for nicks */
#define NICK_COLOURS {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

/* Colours for nav channel names in response to activity, in order of precedence */
#define ACTIVITY_COLOURS {    \
    239, /* Default */        \
    242, /* Join/Part/Quit */ \
    247, /* Chat */           \
    3    /* Ping */           \
};

#define NAV_CURRENT_CHAN 255

/* Separator characters */
#define SEP_HORZ "─"

/* Separator colours */
#define SEP_FG 239
#define SEP_BG -1

/* Status bar colours */
#define STATUS_FG -1
#define STATUS_BG -1

/* Prefix string for the input line and colours */
#define INPUT_PREFIX " >>> "
#define INPUT_PREFIX_FG 239
#define INPUT_PREFIX_BG -1

/* Action message */
#define ACTION_FG -1
#define ACTION_BG 239

/* Input line text colours */
#define INPUT_FG -1
#define INPUT_BG -1

/* Buffer text quoting
 *   ("": no text quoting) */
#define QUOTE_LEADER ">"
#define QUOTE_TEXT_FG 2
#define QUOTE_TEXT_BG -1

/* Control character print colour */
#define CNTRL_FG 0
#define CNTRL_BG 9

/* BUFFER_PADDING:
 * How the buffer line headers will be padded [0, 1]
 *
 * 0 (Unpadded):
 *   12:34 alice ~ hello
 *   12:34 bob ~ is there anybody in there?
 *   12:34 charlie ~ just nod if you can hear me
 *
 * 1 (Padded):
 *   12:34   alice ~ hello
 *   12:34     bob ~ is there anybody in there?
 *   12:34 charlie ~ just nod if you can hear me
 */
#define BUFFER_PADDING 1

/* Raise terminal bell when pinged in chat */
#define BELL_ON_PINGED 1

/* [NETWORK] */

/* Default CA certificate file path
 *   ("": a list of known paths is checked) */
#define CA_CERT_FILE ""

/* Default CA certificate directory path
 *   ("": a list of known paths is checked) */
#define CA_CERT_PATH ""

/* Seconds before displaying ping
 *   Integer, [0, 150, 86400]
 *   (0: no ping handling) */
#define IO_PING_MIN 150

/* Seconds between refreshing ping display
 *   Integer, [0, 5, 86400]
 *   (0: no ping handling) */
#define IO_PING_REFRESH 5

/* Seconds before timeout reconnect
 *   Integer, [0, 300, 86400]
 *   (0: no ping timeout reconnect) */
#define IO_PING_MAX 300

/* Reconnect backoff base delay
 *   Integer, [1, 4, 86400] */
#define IO_RECONNECT_BACKOFF_BASE 4

/* Reconnect backoff growth factor
 *   Integer, [1, 2, 32] */
#define IO_RECONNECT_BACKOFF_FACTOR 2

/* Reconnect backoff maximum
 *   Integer, [1, 86400, 86400] */
#define IO_RECONNECT_BACKOFF_MAX 86400
