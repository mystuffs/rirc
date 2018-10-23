#include "test/test.h"

/* Preclude definitions for testing */
#define INPUT_LEN_MAX 10
#define INPUT_HIST_MAX 4

#include "src/components/input2.c"

static void
test_input(void)
{
	char buf[INPUT_LEN_MAX + 1];
	struct input2 inp;

	input2(&inp);

	assert_eq(input2_text_iszero(&inp), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "");

	input2_free(&inp);
}

static void
test_input_clear(void)
{
	/* TODO
	 * test that the working input is cleared
	 * test that scrolled back input history is reset to head */
}

static void
test_input_ins(void)
{
	char buf[INPUT_LEN_MAX + 1];
	struct input2 inp;

	input2(&inp);

	/* Valid */
	assert_eq(input2_insert(&inp, "a", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "a");
	assert_eq(input2_insert(&inp, "bc", 2), 1);
	assert_eq(input2_insert(&inp, "de", 2), 1);
	assert_eq(input2_insert(&inp, "fgh", 3), 1);
	assert_eq(input2_insert(&inp, "i", 1), 1);
	assert_eq(input2_insert(&inp, "j", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "abcdefghij");

	/* Full */
	assert_eq(input2_insert(&inp, "z", 1), 0);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "abcdefghij");

	input2_free(&inp);
}

static void
test_input_del(void)
{
	char buf[INPUT_LEN_MAX + 1];
	struct input2 inp;

	input2(&inp);

	/* Deleting back/forw on empty input */
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "");
	assert_eq(input2_delete(&inp, 0), 0);
	assert_eq(input2_delete(&inp, 1), 0);

	assert_eq(input2_insert(&inp, "abcefg", 6), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);

	/* Delete left */
	assert_eq(input2_delete(&inp, 0), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "acefg");
	assert_eq(input2_delete(&inp, 0), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "cefg");
	assert_eq(input2_delete(&inp, 0), 0);

	/* Delete right */
	assert_eq(input2_delete(&inp, 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "efg");
	assert_eq(input2_delete(&inp, 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "fg");
	assert_eq(input2_delete(&inp, 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "g");
	assert_eq(input2_delete(&inp, 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "");
	assert_eq(input2_delete(&inp, 1), 0);

	input2_free(&inp);
}

static void
test_input_hist(void)
{
	char buf[INPUT_LEN_MAX + 1];
	struct input2 inp;

	input2(&inp);

	assert_eq(input2_hist_push(&inp), 0);

	/* Test pushing clears the working input */
	assert_eq(input2_insert(&inp, "abc", 3), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "abc");
	assert_eq(input2_hist_push(&inp), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "");

	/* Test pushing up to INPUT_HIST_MAX */
	assert_eq(input2_insert(&inp, "def", 3), 1);
	assert_eq(input2_hist_push(&inp), 1);
	assert_eq(input2_insert(&inp, "ghi", 3), 1);
	assert_eq(input2_hist_push(&inp), 1);
	assert_eq(input2_insert(&inp, "jkl", 3), 1);
	assert_eq(input2_hist_push(&inp), 1);

#define INP_HIST_HEAD(I) ((I).hist.buf[MASK((I).hist.head - 1)])
#define INP_HIST_TAIL(I) ((I).hist.buf[MASK((I).hist.tail)])

	assert_strcmp(INP_HIST_HEAD(inp), "jkl");
	assert_strcmp(INP_HIST_TAIL(inp), "abc");

	/* Test pushing after INPUT_HIST_MAX frees the tail */
	assert_eq(input2_insert(&inp, "mno", 3), 1);
	assert_eq(input2_hist_push(&inp), 1);

	assert_strcmp(INP_HIST_HEAD(inp), "mno");
	assert_strcmp(INP_HIST_TAIL(inp), "def");

#undef INP_HIST_HEAD
#undef INP_HIST_TAIL

	/* TODO:
	 * push some inputs, test that tails are cleaned up
	 *
	 * scroll back and all the way forward
	 * replay last
	 * replay middle
	 * replay first history
	 */

	input2_free(&inp);
}

static void
test_input_move(void)
{
	char buf[INPUT_LEN_MAX + 1];
	struct input2 inp;

	input2(&inp);

	/* Test move back */
	assert_eq(input2_insert(&inp, "ab", 2), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "ab");
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "ab");
	assert_eq(input2_insert(&inp, "c", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "acb");
	assert_eq(input2_insert(&inp, "d", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "acdb");
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 0);
	assert_eq(input2_insert(&inp, "e", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "eacdb");

	/* Test move forward */
	assert_eq(input2_cursor(&inp, 1), 1);
	assert_eq(input2_cursor(&inp, 1), 1);
	assert_eq(input2_cursor(&inp, 1), 1);
	assert_eq(input2_insert(&inp, "f", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "eacdfb");
	assert_eq(input2_cursor(&inp, 1), 1);
	assert_eq(input2_cursor(&inp, 1), 0);
	assert_eq(input2_insert(&inp, "g", 1), 1);
	assert_strcmp(input2_write(&inp, buf, sizeof(buf)), "eacdfbg");

	input2_free(&inp);
}

static void
test_input_write(void)
{
	char buf1[INPUT_LEN_MAX + 1];
	char buf2[INPUT_LEN_MAX / 2];
	struct input2 inp;

	input2(&inp);

	/* Test output is written correctly regardless of cursor position */
	assert_eq(input2_insert(&inp, "abcde", 5), 1);
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf1)), "abcde");
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf1)), "abcde");
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf1)), "abcde");
	assert_eq(input2_cursor(&inp, 0), 1);
	assert_eq(input2_cursor(&inp, 0), 0);
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf1)), "abcde");

	/* Test output is always null terminated */
	assert_eq(input2_insert(&inp, "fghij", 5), 1);
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf1)), "fghijabcde");
	assert_strcmp(input2_write(&inp, buf1, sizeof(buf2)), "fghi");

	input2_free(&inp);
}

static void
test_input_complete(void)
{
	/* TODO */
}

int
main(void)
{
	testcase tests[] = {
		TESTCASE(test_input),
		TESTCASE(test_input_clear),
		TESTCASE(test_input_ins),
		TESTCASE(test_input_del),
		TESTCASE(test_input_hist),
		TESTCASE(test_input_move),
		TESTCASE(test_input_write),
		TESTCASE(test_input_complete)
	};

	return run_tests(tests);
}
