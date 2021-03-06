#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <array>
#include <type_traits>
#include <limits>
#include <numeric>
#include <algorithm>

#include <misc/util/util.hpp>
#include <misc/warnings.hpp>
#include <frontend/ast.hpp>
#include <structures/exception.hpp>
#include <frontend/parser/parser.hpp>
#include <frontend/parser/ast_nodes.hpp>

#include <backend/eval/eval.hpp>


namespace wpp {
	std::string intrinsic_assert(
		wpp::node_t expr,
		wpp::node_t a, wpp::node_t b,
		const wpp::Position& pos,
		wpp::Environment& env,
		wpp::Arguments* args
	) {
		// auto& [base, functions, tree, warnings] = env;

		// Check if strings are equal.
		const auto str_a = eval_ast(a, env, args);
		const auto str_b = eval_ast(b, env, args);

		if (str_a != str_b)
			throw wpp::Exception{
				// pos, "assertion failed: ", reconstruct_source(node_id, tree)
				pos, "assertion failed!"
			};

		return "";
	}


	std::string intrinsic_error(wpp::node_t expr, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		const auto msg = eval_ast(expr, env, args);
		throw wpp::Exception{ pos, msg };
		return "";
	}


	std::string intrinsic_file(wpp::node_t expr, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		const auto fname = eval_ast(expr, env, args);

		try {
			return wpp::read_file(std::filesystem::relative(std::filesystem::path{fname}));
		}

		catch (...) {
			throw wpp::Exception{ pos, "failed reading file '", fname, "'" };
		}
	}


	std::string intrinsic_source(wpp::node_t expr, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		const auto fname = eval_ast(expr, env, args);

		// Store current path and get the path of the new file.
		const auto old_path = std::filesystem::current_path();
		const auto new_path = old_path / std::filesystem::path{fname};

		// Read the new file.
		std::string file;

		try {
			file = wpp::read_file(fname);
		}

		catch (const std::filesystem::filesystem_error& e) {
			throw wpp::Exception{pos, "file '", fname, "' not found."};
		}


		// Create lexer, passing the new path relative to base path.
		wpp::Lexer lex{std::filesystem::relative(new_path, env.base), file.c_str()};
		wpp::node_t root = document(lex, env.tree);


		std::filesystem::current_path(new_path.parent_path());

		const std::string str = wpp::eval_ast(root, env, args);

		std::filesystem::current_path(old_path);

		return str;
	}


	std::string intrinsic_log(wpp::node_t expr, const wpp::Position&, wpp::Environment& env, wpp::Arguments* args) {
		std::cerr << eval_ast(expr, env, args);
		return "";
	}


	std::string intrinsic_escape(wpp::node_t expr, const wpp::Position&, wpp::Environment& env, wpp::Arguments* args) {
		// Escape escape chars in a string.
		std::string str;
		const auto input = eval_ast(expr, env, args);
		str.reserve(input.size());

		for (const char c: input) {
			switch (c) {
				case '"':  str += "\\\""; break;
				case '\'': str += "\\'"; break;
				case '\n': str += "\\n"; break;
				case '\t': str += "\\t"; break;
				case '\r': str += "\\r"; break;
				default:   str += c; break;
			}
		}

		return str;
	}

	std::string intrinsic_slice(
		wpp::node_t string_expr,
		wpp::node_t start_expr,
		wpp::node_t end_expr,
		const wpp::Position& pos,
		wpp::Environment& env,
		wpp::Arguments* args
	) {
		// Evaluate arguments
		const auto string = eval_ast(string_expr, env, args);
		const auto start_raw = eval_ast(start_expr, env, args);
		const auto end_raw = eval_ast(end_expr, env, args);

		// Parse the start and end arguments
		int start;
		int end;

		try {
			start = std::stoi(start_raw);
			end = std::stoi(end_raw);
		}

		catch (...) {
			throw wpp::Exception { pos, "slice range must be numerical." };
		}

		const int len = string.length();

		// Work out the start and length of the slice
		int begin;
		int count;

		if (start < 0)
			begin = len + start;
		else
			begin = start;

		if (end < 0)
			count = (len + end) - begin + 1;
		else
			count = end - begin + 1;

		// Make sure the range is valid
		if (count <= 0)
			throw wpp::Exception{ pos, "end of slice cannot be before the start." };

		else if (len < begin + count)
			throw wpp::Exception{ pos, "slice extends outside of string bounds." };

		else if (start < 0 && end >= 0)
			throw wpp::Exception{ pos, "start cannot be negative where end is positive." };

		// Return the string slice
		else
			return string.substr(begin, count);
	}

	std::string intrinsic_find(
		wpp::node_t string_expr,
		wpp::node_t pattern_expr,
		wpp::Environment& env,
		wpp::Arguments* args
	) {
		// Evaluate arguments
		const auto string = eval_ast(string_expr, env, args);
		const auto pattern = eval_ast(pattern_expr, env, args);

		// Search in string. Returns the index of a match.
		if (auto position = string.find(pattern); position != std::string::npos)
			return std::to_string(position);

		return "";
	}

	std::string intrinsic_length(wpp::node_t string_expr, wpp::Environment& env, wpp::Arguments* args) {
		// Evaluate argument
		const auto string = eval_ast(string_expr, env, args);
		return std::to_string(string.size());
	}

	std::string intrinsic_eval(wpp::node_t expr, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		auto& [base, functions, tree, warnings] = env;

		const auto code = eval_ast(expr, env, args);

		wpp::Lexer lex{"<eval>", code.c_str()};
		wpp::node_t root;

		try {
			root = document(lex, tree);
			return wpp::eval_ast(root, env, args);
		}

		catch (const wpp::Exception& e) {
			throw wpp::Exception{ pos, "inside eval: ", e.what() };
		}
	}


	std::string intrinsic_run(wpp::node_t expr, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		#if defined(WPP_DISABLE_RUN)
			throw wpp::Exception{ pos, "run not available." };
		#endif

		const auto cmd = eval_ast(expr, env, args);

		int rc = 0;
		std::string str = wpp::exec(cmd, rc);

		// trim trailing newline.
		if (str.back() == '\n')
			str.erase(str.end() - 1, str.end());

		if (rc)
			throw wpp::Exception{ pos, "subprocess exited with non-zero status." };

		return str;
	}


	std::string intrinsic_pipe(wpp::node_t cmd, wpp::node_t data, const wpp::Position& pos, wpp::Environment& env, wpp::Arguments* args) {
		#if defined(WPP_DISABLE_RUN)
			throw wpp::Exception{ pos, "pipe not available." };
		#endif

		std::string str;

		const auto cmd_str = eval_ast(cmd, env, args);
		const auto data_str = eval_ast(data, env, args);

		int rc = 0;
		std::string out = wpp::exec(cmd_str, data_str, rc);

		// trim trailing newline.
		if (out.back() == '\n')
			out.erase(out.end() - 1, out.end());

		if (rc)
			throw wpp::Exception{ pos, "subprocess exited with non-zero status." };

		return out;
	}







	// The core of the evaluator.
	std::string eval_ast(const wpp::node_t node_id, wpp::Environment& env, wpp::Arguments* args) {
		const auto& variant = env.tree[node_id];
		std::string str;

		wpp::visit(variant,
			[&] (const Intrinsic& fn) {
				const auto& [type, name, exprs, pos] = fn;

				// Make sure that intrinsic is called with the correct number of arguments.
				constexpr std::array intrinsic_arg_n = [&] {
					std::array<size_t, TOKEN_TOTAL> lookup{};

					lookup[TOKEN_SLICE]  = 3;
					lookup[TOKEN_FIND]   = 2;
					lookup[TOKEN_ASSERT] = 2;
					lookup[TOKEN_PIPE]   = 2;
					lookup[TOKEN_ERROR]  = 1;
					lookup[TOKEN_FILE]   = 1;
					lookup[TOKEN_ESCAPE] = 1;
					lookup[TOKEN_EVAL]   = 1;
					lookup[TOKEN_RUN]    = 1;
					lookup[TOKEN_SOURCE] = 1;
					lookup[TOKEN_LENGTH] = 1;
					lookup[TOKEN_LOG]    = 1;

					return lookup;
				} ();


				const auto n_args = intrinsic_arg_n[type];
				if (n_args != exprs.size())
					throw wpp::Exception{pos, name, " takes exactly ", n_args, " arguments."};


				// Dispatch to instrinsics.
				if (type == TOKEN_ASSERT)
					str = wpp::intrinsic_assert(node_id, exprs[0], exprs[1], pos, env, args);

				else if (type == TOKEN_ERROR)
					str = wpp::intrinsic_error(exprs[0], pos, env, args);

				else if (type == TOKEN_FILE)
					str = wpp::intrinsic_file(exprs[0], pos, env, args);

				else if (type == TOKEN_SOURCE)
					str = wpp::intrinsic_source(exprs[0], pos, env, args);

				else if (type == TOKEN_ESCAPE)
					str = wpp::intrinsic_escape(exprs[0], pos, env, args);

				else if (type == TOKEN_EVAL)
					str = wpp::intrinsic_eval(exprs[0], pos, env, args);

				else if (type == TOKEN_RUN)
					str = wpp::intrinsic_run(exprs[0], pos, env, args);

				else if (type == TOKEN_PIPE)
					str = wpp::intrinsic_pipe(exprs[0], exprs[1], pos, env, args);

				else if (type == TOKEN_SLICE)
					str = wpp::intrinsic_slice(exprs[0], exprs[1], exprs[2], pos, env, args);

				else if (type == TOKEN_FIND)
					str = wpp::intrinsic_find(exprs[0], exprs[1], env, args);

				else if (type == TOKEN_LENGTH)
					str = wpp::intrinsic_length(exprs[0], env, args);

				else if (type == TOKEN_LOG)
					str = wpp::intrinsic_log(exprs[0], pos, env, args);
			},

			[&] (const FnInvoke& call) {
				auto& [base, functions, tree, warnings] = env;
				const auto& [caller_name, caller_args, caller_pos] = call;
				std::string caller_mangled_name = wpp::cat(caller_name, caller_args.size());

				// Check if parameter.
				if (args) {
					if (auto it = (*args).find(caller_name); it != (*args).end()) {
						if (caller_args.size() > 0)
							throw wpp::Exception{caller_pos, "calling argument '", caller_name, "' as if it were a function."};

						str = it->second;

						// Check if it's shadowing a function (even this one).
						if (warnings & wpp::WARN_PARAM_SHADOW_FUNC and functions.find(wpp::cat(caller_name, 0)) != functions.end())
							wpp::warn(caller_pos, "parameter ", caller_name, " is shadowing a function.");

						return;
					}
				}

				// If it wasn't a parameter, we fall through to here and check if it's a function.
				auto it = functions.find(caller_mangled_name);
				if (it == functions.end())
					throw wpp::Exception{caller_pos, "func not found: ", caller_name, "."};

				if (it->second.empty())
					throw wpp::Exception{caller_pos, "func not found: ", caller_name, "."};

				const auto func = tree.get<wpp::Fn>(it->second.back());

				// Retrieve function.
				const auto& [callee_name, params, body, callee_pos] = func;

				// Set up Arguments to pass down to function body.
				Arguments env_args;

				if (args) {
					for (const auto& [key, val]: *args)
						env_args.emplace(key, val);
				}

				// Evaluate arguments and store their result.
				for (int i = 0; i < (int)caller_args.size(); i++) {
					const auto result = eval_ast(caller_args[i], env, args);

					if (auto it = env_args.find(params[i]); it != env_args.end()) {
						if (warnings & wpp::WARN_PARAM_SHADOW_PARAM)
							wpp::warn(callee_pos, "parameter '", it->first, "' inside function '", callee_name, "' shadows parameter from parent scope.");

						it->second = result;
					}

					else {
						env_args.insert_or_assign(params[i], eval_ast(caller_args[i], env, args));
					}
				}

				// Call function.
				str = eval_ast(body, env, &env_args);
			},

			[&] (const Fn& func) {
				auto& [base, functions, tree, warnings] = env;
				const auto& [name, params, body, pos] = func;

				auto it = functions.find(wpp::cat(name, params.size()));

				if (it != functions.end()) {
					if (warnings & wpp::WARN_FUNC_REDEFINED)
						wpp::warn(pos, "function '", name, "' redefined.");

					it->second.emplace_back(node_id);
				}

				else
					functions.emplace(wpp::cat(name, params.size()), std::vector{node_id});
			},

			[&] (const Codeify& colby) {
				const auto& [expr, pos] = colby;
				str = intrinsic_eval(expr, pos, env, args);
			},

			[&] (const Var& var) {
				auto& [base, functions, tree, warnings] = env;
				auto [name, body, pos] = var;

				const auto func_name = wpp::cat(name, 0);
				const auto str = eval_ast(body, env, args);

				// Replace body with a string of the evaluation result.
				tree.replace<String>(body, str, pos);

				// Replace Var node with Fn node.
				tree.replace<Fn>(node_id, func_name, std::vector<std::string>{}, body, pos);

				auto it = functions.find(func_name);
				if (it != functions.end()) {
					if (warnings & wpp::WARN_VARFUNC_REDEFINED)
						wpp::warn(tree.get<Fn>(node_id).pos, "function/variable '", name, "' redefined.");

					it->second.emplace_back(node_id);
				}

				else
					functions.emplace(func_name, std::vector{node_id});
			},

			[&] (const Drop& drop) {
				auto& [base, functions, tree, warnings] = env;
				const auto& [func_id, pos] = drop;

				auto* func = std::get_if<FnInvoke>(&tree[func_id]);

				if (not func)
					throw wpp::Exception{pos, "invalid function passed to drop."};

				const auto& [caller_name, caller_args, caller_pos] = *func;

				std::string caller_mangled_name = wpp::cat(caller_name, caller_args.size());

				auto it = functions.find(caller_mangled_name);

				if (it != functions.end()) {
					if (not it->second.empty())
						it->second.pop_back();

					else
						functions.erase(it);
				}

				else {
					throw wpp::Exception{pos, "cannot drop undefined function '", caller_name, "' (", caller_args.size(), " parameters)."};
				}
			},

			[&] (const String& x) {
				str = x.value;
			},

			[&] (const Concat& cat) {
				const auto& [lhs, rhs, pos] = cat;
				str = eval_ast(lhs, env, args) + eval_ast(rhs, env, args);
			},

			[&] (const Block& block) {
				const auto& [stmts, expr, pos] = block;

				for (const wpp::node_t node: stmts)
					str += eval_ast(node, env, args);

				str = eval_ast(expr, env, args);
			},

			[&] (const Map& map) {
				const auto& [test, cases, default_case, pos] = map;

				const auto test_str = eval_ast(test, env, args);

				// Compare test_str with arms of the map.
				auto it = std::find_if(cases.begin(), cases.end(), [&] (const auto& elem) {
					return test_str == eval_ast(elem.first, env, args);
				});

				// If found, evaluate the hand.
				if (it != cases.end())
					str = eval_ast(it->second, env, args);

				// If not found, check for a default arm, otherwise error.
				else {
					if (default_case == wpp::NODE_EMPTY)
						throw wpp::Exception{pos, "no matches found."};

					else
						str = eval_ast(default_case, env, args);
				}
			},

			[&] (const Pre& pre) {
				auto& [base, functions, tree, warnings] = env;
				const auto& [exprs, stmts, pos] = pre;

				for (const wpp::node_t stmt: stmts) {
					if (wpp::Fn* func = std::get_if<wpp::Fn>(&tree[stmt])) {
						std::string name;

						for (auto it = exprs.rbegin(); it != exprs.rend(); ++it)
							name += eval_ast(*it, env, args);

						func->identifier = name + func->identifier;
						str += eval_ast(stmt, env, args);
					}

					else if (wpp::Pre* pre = std::get_if<wpp::Pre>(&tree[stmt])) {
						pre->exprs.insert(pre->exprs.end(), exprs.begin(), exprs.end());
						str += eval_ast(stmt, env, args);
					}

					else {
						str += eval_ast(stmt, env, args);
					}
				}
			},

			[&] (const Document& doc) {
				for (const wpp::node_t node: doc.stmts)
					str += eval_ast(node, env, args);
			}
		);

		return str;
	}
}





namespace wpp {
	int run(const std::string& fname, const wpp::warning_t warning_flags) {
		std::string file;

		try {
			file = wpp::read_file(fname);
		}

		catch (const std::filesystem::filesystem_error& e) {
			std::cerr << "file not found.\n";
			return 1;
		}

		// Set current path to path of file.
		const auto path = std::filesystem::current_path() / std::filesystem::path{fname};
		std::filesystem::current_path(path.parent_path());

		try {
			wpp::Lexer lex{std::filesystem::current_path(), file.c_str()};
			wpp::AST tree;
			wpp::Environment env{std::filesystem::current_path(), tree, warning_flags};

			tree.reserve((1024 * 1024 * 10) / sizeof(decltype(tree)::value_type));

			auto root = wpp::document(lex, tree);
			std::cout << wpp::eval_ast(root, env) << std::flush;
		}

		catch (const wpp::Exception& e) {
			wpp::error(e.pos, e.what());
			return 1;
		}

		return 0;
	}
}


