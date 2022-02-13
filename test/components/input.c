#include "test/test.h"

/* Preclude definitions for testing */
#define INPUT_LEN_MAX 16
#define INPUT_HIST_MAX 4

#include "src/components/input.c"

#define CHECK_INPUT_FRAME(I, S, F, C) \
	assert_eq(input_frame((I), buf, (F)), (C)); \
	assert_strcmp(buf, (S));

#define CHECK_INPUT_WRITE(I, S) \
	assert_ueq(input_write((I), buf, sizeof(buf), 0), strlen((S))); \
	assert_strcmp(buf, (S));

static uint16_t completion_l(char*, uint16_t, uint16_t, int);
static uint16_t completion_m(char*, uint16_t, uint16_t, int);
static uint16_t completion_s(char*, uint16_t, uint16_t, int);
static uint16_t completion_rot1(char*, uint16_t, uint16_t, int);

static char buf[INPUT_LEN_MAX + 1];

static uint16_t
completion_l(char *str, uint16_t len, uint16_t max, int first)
{
	/* Completes to word longer than len */

	(void)len;
	(void)first;

	const char longer[] = "xyxyxy";

	if (max < sizeof(longer) - 1)
		return 0;

	memcpy(str, longer, sizeof(longer) - 1);

	return sizeof(longer) - 1;
}

static uint16_t
completion_m(char *str, uint16_t len, uint16_t max, int first)
{
	/* Writes up to max chars */

	(void)first;

	for (uint16_t i = 0; i < (len + max); i++)
		str[i] = 'x';

	return (len + max);
}

static uint16_t
completion_s(char *str, uint16_t len, uint16_t max, int first)
{
	/* Completes to word shorter than len */

	(void)len;
	(void)first;

	const char shorter[] = "z";

	if (max < sizeof(shorter) - 1)
		return 0;

	memcpy(str, shorter, sizeof(shorter) - 1);

	return sizeof(shorter) - 1;
}

static uint16_t
completion_rot1(char *str, uint16_t len, uint16_t max, int first)
{
	/* Completetion function, increments all characters */

	uint16_t i = 0;

	while (i < len && i < max)
		str[i++] += 1;

	if (first) {
		str[i++] = '!';
		str[i++] = '!';
	}

	return i;
}

static void
test_input_init(void)
{
	struct input inp;

	input_init(&inp);
	assert_eq(input_text_iszero(&inp), 1);
	input_free(&inp);
}

static void
test_input_reset(void)
{
	struct input inp;

	input_init(&inp);

	/* Test clearing empty input */
	assert_eq(input_reset(&inp), 0);
	assert_eq(input_text_iszero(&inp), 1);

	/* Test clearing non-empty input */
	assert_eq(input_insert(&inp, "abc", 3), 1);
	assert_eq(input_reset(&inp), 1);
	assert_eq(input_text_iszero(&inp), 1);

	/* Test clearing non-empty input, cursor at start */
	assert_eq(input_insert(&inp, "abc", 3), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 0);
	assert_eq(input_reset(&inp), 1);
	assert_eq(input_text_iszero(&inp), 1);

	input_free(&inp);
}

static void
test_input_ins(void)
{
	struct input inp;

	input_init(&inp);

	/* Valid */
	assert_eq(input_insert(&inp, "a", 1), 1);
	CHECK_INPUT_WRITE(&inp, "a");

	assert_eq(input_insert(&inp, "bc", 2), 1);
	assert_eq(input_insert(&inp, "de", 2), 1);
	assert_eq(input_insert(&inp, "fgh", 3), 1);
	assert_eq(input_insert(&inp, "i", 1), 1);
	assert_eq(input_insert(&inp, "j", 1), 1);
	assert_eq(input_insert(&inp, "klmnop", 6), 1);
	CHECK_INPUT_WRITE(&inp, "abcdefghijklmnop");
	assert_ueq(input_text_size(&inp), INPUT_LEN_MAX);

	/* Full */
	assert_eq(input_insert(&inp, "z", 1), 0);
	CHECK_INPUT_WRITE(&inp, "abcdefghijklmnop");
	assert_ueq(input_text_size(&inp), INPUT_LEN_MAX);

	input_free(&inp);
}

static void
test_input_del(void)
{
	struct input inp;

	input_init(&inp);

	/* Deleting back/forward on empty input */
	CHECK_INPUT_WRITE(&inp, "");
	assert_eq(input_delete_back(&inp), 0);
	assert_eq(input_delete_forw(&inp), 0);

	assert_eq(input_insert(&inp, "abcefg", 6), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);

	/* Delete left */
	assert_eq(input_delete_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "acefg");
	assert_eq(input_delete_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "cefg");
	assert_eq(input_delete_back(&inp), 0);

	/* Delete right */
	assert_eq(input_delete_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "efg");
	assert_eq(input_delete_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "fg");
	assert_eq(input_delete_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "g");
	assert_eq(input_delete_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "");
	assert_eq(input_delete_forw(&inp), 0);

	input_free(&inp);
}

static void
test_input_hist(void)
{
	struct input inp;

	input_init(&inp);

	assert_eq(input_hist_push(&inp), 0);

	/* Test scrolling input fails when no history */
	assert_eq(input_hist_back(&inp), 0);
	assert_eq(input_hist_forw(&inp), 0);

	/* Test pushing clears the working input */
	assert_eq(input_insert(&inp, "111", 3), 1);
	CHECK_INPUT_WRITE(&inp, "111");
	assert_eq(input_hist_push(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "");

	/* Test pushing up to INPUT_HIST_MAX */
	assert_eq(input_insert(&inp, "222", 3), 1);
	assert_eq(input_hist_push(&inp), 1);
	assert_eq(input_insert(&inp, "333", 3), 1);
	assert_eq(input_hist_push(&inp), 1);
	assert_eq(input_insert(&inp, "444", 3), 1);
	assert_eq(input_hist_push(&inp), 1);

	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "444");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.tail), "111");

	/* Test pushing after INPUT_HIST_MAX frees the tail */
	assert_eq(input_insert(&inp, "555", 3), 1);
	assert_eq(input_hist_push(&inp), 1);

	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.tail), "222");

	/* Test scrolling backwards */
	assert_eq(input_reset(&inp), 0);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 4), "222");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 3), "333");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 2), "444");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555");

	assert_eq(input_hist_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "555");
	assert_eq(input_hist_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "444");
	assert_eq(input_hist_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "333");
	assert_eq(input_hist_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "222");
	assert_eq(input_hist_back(&inp), 0);
	CHECK_INPUT_WRITE(&inp, "222");

	/* Test scrolling forwards */
	assert_eq(input_hist_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "333");
	assert_eq(input_hist_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "444");
	assert_eq(input_hist_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "555");
	assert_eq(input_hist_forw(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "");
	assert_eq(input_hist_forw(&inp), 0);
	CHECK_INPUT_WRITE(&inp, "");

	/* Test replaying history */
	assert_eq(input_reset(&inp), 0);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 4), "222");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 3), "333");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 2), "444");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555");

	/* Test replaying history from head */
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_push(&inp), 1);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 4), "333");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 3), "444");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 2), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555");

	/* Test replaying history from tail */
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 0);
	assert_eq(input_hist_push(&inp), 1);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 4), "444");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 3), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 2), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "333");

	/* Test replaying history with edit */
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_hist_back(&inp), 1);
	assert_eq(input_insert(&inp, "xxx", 3), 1);
	assert_eq(input_hist_push(&inp), 1);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 4), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 3), "555");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 2), "333");
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555xxx");

	/* Test pushing resets scrollback */
	assert_eq(input_hist_back(&inp), 1);
	assert_strcmp(INPUT_HIST_LINE(&inp, inp.hist.head - 1), "555xxx");

	input_free(&inp);
}

static void
test_input_move(void)
{
	struct input inp;

	input_init(&inp);

	/* Test move back */
	assert_eq(input_insert(&inp, "ab", 2), 1);
	CHECK_INPUT_WRITE(&inp, "ab");
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "ab");
	assert_eq(input_insert(&inp, "c", 1), 1);
	CHECK_INPUT_WRITE(&inp, "acb");
	assert_eq(input_insert(&inp, "d", 1), 1);
	CHECK_INPUT_WRITE(&inp, "acdb");
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 0);
	assert_eq(input_insert(&inp, "e", 1), 1);
	CHECK_INPUT_WRITE(&inp, "eacdb");

	/* Test move forward */
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_insert(&inp, "f", 1), 1);
	CHECK_INPUT_WRITE(&inp, "eacdfb");
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 0);
	assert_eq(input_insert(&inp, "g", 1), 1);
	CHECK_INPUT_WRITE(&inp, "eacdfbg");

	input_free(&inp);
}

static void
test_input_frame(void)
{
	struct input inp;

	input_init(&inp);

	assert_eq(input_insert(&inp, "1234567890", 10), 1);

	/* Test cursor fits */
	CHECK_INPUT_FRAME(&inp, "1234567890", 12, 10);

	/* Test cursor doesnt fit */
	CHECK_INPUT_FRAME(&inp, "567890", 11, 6);

	/* Test cursor back keeps cursor in view */
	CHECK_INPUT_FRAME(&inp, "890", 6, 3);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "56789", 6, 3);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "23456", 6, 3);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "12345", 6, 0);

	/* Test cursor forward keeps cursor in view */
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "12345", 6, 3);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "45678", 6, 3);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	assert_eq(input_cursor_forw(&inp), 1);
	CHECK_INPUT_FRAME(&inp, "890", 6, 3);

	input_free(&inp);
}

static void
test_input_write(void)
{
	struct input inp;

	input_init(&inp);

	/* Test output is written correctly regardless of cursor position */
	assert_eq(input_insert(&inp, "abcde", 5), 1);
	CHECK_INPUT_WRITE(&inp, "abcde");
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "abcde");
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	CHECK_INPUT_WRITE(&inp, "abcde");
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 0);
	CHECK_INPUT_WRITE(&inp, "abcde");

	/* Test output is always null terminated */
	assert_eq(input_insert(&inp, "fghijklmno", 10), 1);
	CHECK_INPUT_WRITE(&inp, "fghijklmnoabcde");

	/* Test truncated write */
	assert_eq(input_write(&inp, buf, 5, 0), 4);
	assert_strcmp(buf, "fghi");

	/* Test truncated, offset write */
	assert_eq(input_write(&inp, buf, 5, 3), 4);
	assert_strcmp(buf, "ijkl");

	input_free(&inp);
}

static void
test_input_complete(void)
{
	struct input inp;

	input_init(&inp);

	/* Test empty */
	assert_eq(input_complete(&inp, completion_rot1), 0);
	assert_eq(input_reset(&inp), 0);

	/* Test only space */
	assert_eq(input_insert(&inp, " ", 1), 1);
	assert_eq(input_complete(&inp, completion_rot1), 0);
	assert_eq(input_reset(&inp), 1);

	/* Test: ` abc `
	 *             ^ */
	assert_eq(input_insert(&inp, " abc ", 5), 1);
	assert_eq(input_complete(&inp, completion_rot1), 0);
	CHECK_INPUT_WRITE(&inp, " abc ");

	/* Test: ` abc `
	 *            ^ */
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], ' ');
	assert_eq(input_complete(&inp, completion_rot1), 1);
	assert_eq(inp.buf[inp.head], ' ');
	CHECK_INPUT_WRITE(&inp, " bcd ");

	/* Test: ` bcd `
	 *           ^ */
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'd');
	assert_eq(input_complete(&inp, completion_rot1), 1);
	assert_eq(inp.buf[inp.head], ' ');
	CHECK_INPUT_WRITE(&inp, " cde ");

	/* Test: ` cde `
	 *          ^ */
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'd');
	assert_eq(input_complete(&inp, completion_rot1), 1);
	assert_eq(inp.buf[inp.head], ' ');
	CHECK_INPUT_WRITE(&inp, " def ");

	/* Test: ` def `
	 *         ^ */
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'd');
	assert_eq(input_complete(&inp, completion_rot1), 1);
	assert_eq(inp.buf[inp.head], ' ');
	CHECK_INPUT_WRITE(&inp, " efg ");

	/* Test: ` efg `
	 *        ^ */
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], ' ');
	assert_eq(input_complete(&inp, completion_rot1), 0);
	assert_eq(inp.buf[inp.head], ' ');
	CHECK_INPUT_WRITE(&inp, " efg ");
	assert_eq(input_reset(&inp), 1);

	/* Test start of line */
	assert_eq(input_insert(&inp, "x abc ", 6), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'x');
	assert_eq(input_complete(&inp, completion_rot1), 1);
	assert_eq(inp.buf[inp.tail], ' ');
	CHECK_INPUT_WRITE(&inp, "y!! abc ");
	assert_eq(input_reset(&inp), 1);

	/* Test replacement word longer */
	assert_eq(input_insert(&inp, " abc ab", 7), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'c');
	assert_eq(input_complete(&inp, completion_l), 1);
	CHECK_INPUT_WRITE(&inp, " xyxyxy ab");
	assert_eq(inp.buf[inp.tail], ' '); /* points to 'c' */
	assert_eq(input_reset(&inp), 1);

	/* Test replacement word shorter */
	assert_eq(input_insert(&inp, " abc ab ", 8), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(input_cursor_back(&inp), 1);
	assert_eq(inp.buf[inp.head], 'c');
	assert_eq(input_complete(&inp, completion_s), 1);
	CHECK_INPUT_WRITE(&inp, " z ab ");
	assert_eq(inp.buf[inp.tail], ' ');
	assert_eq(input_reset(&inp), 1);

	/* Test writing up to max chars */
	assert_eq(input_insert(&inp, "a", 1), 1);
	assert_eq(input_complete(&inp, completion_m), 1);
	CHECK_INPUT_WRITE(&inp, "xxxxxxxxxxxxxxxx");
	assert_ueq(input_text_size(&inp), INPUT_LEN_MAX);

	input_free(&inp);
}

static void
test_input_text_size(void)
{
	struct input inp;

	input_init(&inp);

	/* Test size is correct from 0 -> max */
	for (int i = 0; i < INPUT_LEN_MAX; i++) {
		assert_ueq(input_text_size(&inp), i);
		assert_eq(input_insert(&inp, "a", 1), 1);
	}

	assert_ueq(input_text_size(&inp), INPUT_LEN_MAX);

	/* Test size is correct regardless of cursor position */
	for (int i = 0; i < INPUT_LEN_MAX; i++) {
		assert_eq(input_cursor_back(&inp), 1);
		assert_ueq(input_text_size(&inp), INPUT_LEN_MAX);
	}

	input_free(&inp);
}

int
main(void)
{
	struct testcase tests[] = {
		TESTCASE(test_input_init),
		TESTCASE(test_input_reset),
		TESTCASE(test_input_ins),
		TESTCASE(test_input_del),
		TESTCASE(test_input_hist),
		TESTCASE(test_input_move),
		TESTCASE(test_input_frame),
		TESTCASE(test_input_write),
		TESTCASE(test_input_complete),
		TESTCASE(test_input_text_size)
	};

	return run_tests(NULL, NULL, tests);
}
