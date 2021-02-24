#include <string>
#include <iostream>
#include <utility>
#include <chrono>

#include <cstdint>
#include <cstring>
#include <ctime>

#include <misc/argp.hpp>
#include <misc/warnings.hpp>
#include <backend/eval/eval.hpp>
#include <misc/repl.hpp>


int main(int argc, const char* argv[]) {
	wpp::ArgResult input, output, sexpr, repl;

	auto usage = "w++ -i INPUT [-o OUTPUT] [-srh]";

	auto argparser = wpp::ArgumentParser(
		"wot++",
		"A small macro language for producing and manipulating strings",
		"alpha-git",
		usage
	)
		.arg(&input,  "File to read input from",               "input",  "i", true)
		.arg(&output, "File to output to (stdout by default)", "output", "o", true)
		.arg(&sexpr,  "Print AST as S-expression",             "sexpr",  "s", false)
		.arg(&repl,   "Start an interactive prompt",           "repl",   "r", false);

	wpp::warning_t warning_flags =
		wpp::WARN_FUNC_REDEFINED |
		wpp::WARN_PARAM_SHADOW_FUNC |
		wpp::WARN_PARAM_SHADOW_PARAM |
		wpp::WARN_VARFUNC_REDEFINED
	;

	if (not argparser.parse(argc, argv))
		return 1;

	else if (repl.is_present)
		return wpp::repl();

	else if (input.is_present)
		return wpp::run(input.value, warning_flags);

	return 1;
}
