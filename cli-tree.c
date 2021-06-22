/*
 * Command Tree Parsing Engine
 * 
 * Copyright (c) 2021 Jiajia Liu <liujia6264@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "cli-term.h"
#include "stream.h"
#include "hashtable.h"

#include "libregexp.h"

enum token_type {
	TOKEN_LITERAL,
	TOKEN_OPTION,
	TOKEN_VARIABLE,
	TOKEN_VARARG,
};

struct token {
	char *key;
	char *desc;
	int type;
};

struct cmd_node {
	struct cmd_node *sibling;
	struct cmd_node *children;
	struct cmd_node *parent;

	struct token *tokens;
	size_t nr_tokens;

	struct cmd_node *keyword;
	struct cmdoptattr *optattr;

	int (*func)(struct term *term, struct cmdopt *opt);
};

struct parser_state {
	const char *cp;
	const char *desc;

	int in_multiple;
	int in_keyword;

	int just_read_word;

	struct token *token;
	size_t token_count;
	const struct cmd_elem *elem;
	struct cmd_node *parent;
	struct cmd_node *save_parent;
};

#define for_each_node(node, tree)	\
	for (node = tree; node; node = node->sibling)

#define for_each_token(node, i, token)	\
	for (token = node->tokens, i = 0; i < node->nr_tokens; i++, token++)

#define for_each_node_token(head, node, i, token)	\
	for_each_node(node, head) 			\
	for_each_token(node, i, token)

static const char *next_key(const char **endptr)
{
	const char *key = NULL;
	const char *ptr = *endptr;

	while (*ptr && isspace(*ptr))
		ptr++;

	*endptr = ptr;
	if (*ptr == '\0' || *ptr == '\n')
		return NULL;
	else if (*ptr == '|') {
		*endptr = ptr + strlen(ptr);
		return ptr;
	}

	key = ptr;
	while (*ptr && !strchr("|", *ptr) && !isspace(*ptr))
		ptr++;

	*endptr = ptr;

	return key;
}

static const char *next_help(const char **desc)
{
	const char *line;
	const char *ptr;

	for (line = *desc; *line && isspace(*line); line++)
		;

	*desc = line;
	if (*line == '\0')
		return NULL;

	for (ptr = line; *ptr && *ptr != '\n'; ptr++)
		;

	*desc = ptr;

	return line;
}

static struct cmd_node *cmd_node_find(struct cmd_node *parent,
				      struct token *token, size_t count,
				      int *ret)
{
	struct cmd_node *node;

	if (count == 1) {
		const char *key = token->key;

		if (ret)
			*ret = 1;
		for (node = parent->children; node; node = node->sibling) {
			int i;

			for (i = 0; i < node->nr_tokens; i++) {
				if (!strcmp(key, node->tokens[i].key))
					return node;
			}
		}
		if (ret)
			*ret = 0;

		return NULL;
	} else {
		int c, i, found;
		for (node = parent->children; node; node = node->sibling) {
			if (node->nr_tokens == 1)
				continue;

			found = 0;
			for (c = 0; c < count; c++) {
				const char *key = token[c].key;
				for (i = 0; i < node->nr_tokens; i++) {
					if (!strcmp(key, node->tokens[i].key)) {
						found++;
						break;
					}
				}
			}

			if (found) {
				if (ret)
					*ret = found;
				return node;
			}
		}

		if (ret)
			*ret = 0;

		return NULL;
	}
}

static struct cmd_node *cmd_node_new(struct token *token, size_t count)
{
	struct cmd_node *node;

	node = calloc(1, sizeof(struct cmd_node));
	if (node == NULL)
		return NULL;

	node->tokens = token;
	node->nr_tokens = count;

	return node;
}

static void cmd_node_insert(struct cmd_node *parent, struct cmd_node *new, int keyword)
{
	struct cmd_node **pp;
	int count = 0;

	pp = !keyword ? &parent->children : &parent->keyword;

	for (; *pp; pp = &(*pp)->sibling) {
		count++;
	}

	*pp = new;
	new->parent = parent;
}

static int token_record(struct token *token,
			const char *cp, const char *cp_end,
			const char *dp, const char *dp_end)
{
	size_t cp_len = cp_end - cp;
	size_t dp_len = dp_end - dp;

	if (*cp ==  '[') {
		token->type = TOKEN_OPTION;
	} else if (*cp == '.') {
		token->type = TOKEN_VARARG;
	} else if (*cp >= 'A' && *cp <= 'Z') {
		token->type = TOKEN_VARIABLE;
	} else {
		token->type = TOKEN_LITERAL;
	}

	token->key = malloc(cp_len + 1);
	if (token->key == NULL)
		return -ENOMEM;
	token->desc = malloc(dp_len + 1);
	if (token->desc == NULL)
		return -ENOMEM;

	memcpy(token->key, cp, cp_len);
	token->key[cp_len] = '\0';

	memcpy(token->desc, dp, dp_len);
	token->desc[dp_len] = '\0';

	return 0;
}

static int parser_read_word(struct parser_state *state)
{
	const char *start;
	const char *line;
	struct cmd_node *new;

	start = state->cp;

	while (*state->cp && !strchr("\r\n{}()|", *state->cp) &&
		!isspace(*state->cp))
		state->cp++;

	line = next_help(&state->desc);
	if (line == NULL)
		return -EINVAL;

	if (!state->in_multiple) {
		struct token *token;

		token = calloc(1, sizeof(struct token));
		if (token == NULL)
			return -ENOMEM;

		if (token_record(token, start, state->cp, line, state->desc))
			return -ENOMEM;

		new = cmd_node_find(state->parent, token, 1, NULL);
		if (new) {
			free(token->key);
			free(token->desc);
			free(token);
			state->parent = new;
		} else {
			new = cmd_node_new(token, 1);
			if (new == NULL)
				return -ENOMEM;

			if (state->in_keyword == 1) {
				cmd_node_insert(state->parent, new, 1);
				state->in_keyword = 2;
			} else {
				cmd_node_insert(state->parent, new, 0);
			}

			state->parent = new;
		}
	} else {
		int index = state->token_count;
		size_t size = (index + 1) * sizeof(struct token);

		state->token = realloc(state->token, size);
		if (state->token == NULL)
			return -ENOMEM;

		state->token_count++;

		if (token_record(&state->token[index], start, state->cp,
				 line, state->desc)) {
			int i;

			for (i = 0; i < index; i++) {
				free(state->token[i].key);
				free(state->token[i].desc);
			}
			free(state->token);
			state->token = NULL;
			state->token_count = 0;
			return -ENOMEM;
		}
	}

	state->just_read_word = 1;

	return 0;
}

static int parser_begin_multiple(struct parser_state *state)
{
	if (state->in_keyword == 1)
		return -EINVAL;
	if (state->in_multiple)
		return -EINVAL;

	state->cp++;
	state->in_multiple = 1;
	state->just_read_word = 0;

	return 0;
}

static int parser_end_multiple(struct parser_state *state)
{
	struct cmd_node *new;
	int ret = 0;

	if (!state->in_multiple)
		return -EINVAL;

	if (!state->token_count)
		return -EINVAL;

	state->cp++;
	state->in_multiple = 0;

	new = cmd_node_find(state->parent, state->token,
			    state->token_count, &ret);
	if (new) {
		int i;

		if (ret != state->token_count)
			return -EINVAL;

		for (i = 0; i < state->token_count; i++) {
			free(state->token[i].key);
			free(state->token[i].desc);
		}
		free(state->token);
	} else {
		new = cmd_node_new(state->token, state->token_count);
		if (new == NULL)
			return -ENOMEM;
		cmd_node_insert(state->parent, new, 0);
	}

	state->parent = new;
	state->token = NULL;
	state->token_count = 0;

	return 0;
}

static int parser_begin_keyword(struct parser_state *state)
{
	if (state->in_keyword || state->in_multiple)
		return -EINVAL;

	state->cp++;
	state->in_keyword = 1;
	state->save_parent = state->parent;

	return 0;
}

static int parser_end_keyword(struct parser_state *state)
{
	if (state->in_multiple || !state->in_keyword)
		return -EINVAL;
	if (state->in_keyword == 1)
		return -EINVAL;

	state->cp++;
	state->in_keyword = 0;
	// state->parent->func = state->elem->func;
	state->parent = state->save_parent;


	return 0;
}

static int parser_handle_pipe(struct parser_state *state)
{
	if (state->in_multiple) {
		state->cp++;
		state->just_read_word = 0;
	} else if (state->in_keyword) {
		state->cp++;
		state->in_keyword = 1;
		// state->parent->func = state->elem->func;
		state->parent = state->save_parent;
	} else
		return -EINVAL;

	return 0;
}

static int parser_end(struct parser_state *state)
{
	if (state->in_multiple || state->in_keyword)
		return -EINVAL;

	state->parent->func = state->elem->func;
	state->parent->optattr = state->elem->optattr;
	if (state->parent->tokens[0].type == TOKEN_VARARG) {
		state->parent->parent->func = state->elem->func;
		state->parent->parent->optattr = state->elem->optattr;
	}

	return 0;
}

static int cmd_add_elem(struct cmd_node *tree, const struct cmd_elem *elem)
{
	struct parser_state state;
	int ret;

	memset(&state, 0, sizeof(struct parser_state));
	state.cp = elem->line;
	state.desc = elem->desc;
	state.elem = elem;
	state.parent = tree;

	for (;;) {
		while (*state.cp && *state.cp == ' ')
			state.cp++;

		switch (*state.cp) {
		case '\0':
			ret = parser_end(&state);
			return ret;
		case '{':
			ret = parser_begin_keyword(&state);
			break;
		case '(':
			ret = parser_begin_multiple(&state);
			break;
		case '}':
			ret = parser_end_keyword(&state);
			break;
		case ')':
			ret = parser_end_multiple(&state);
			break;
		case '|':
			ret = parser_handle_pipe(&state);
			break;
		default:
			ret = parser_read_word(&state);
			break;
		}

		if (ret) {
			return ret;
		}
	}
}

static void free_node(struct cmd_node *node)
{
	int i;

	for (i = 0; i < node->nr_tokens; i++) {
		free(node->tokens[i].key);
		free(node->tokens[i].desc);
	}
	if (node->tokens)
		free(node->tokens);

	free(node);
}

void _cmd_tree_travel(struct cmd_node *tree, struct stream *out)
{
	struct cmd_node *node;

	for (node = tree; node; node = node->sibling) {
		int i;
		struct token *t;

		stream_puts(out, "node %p, func %p, children %p\r\n",
			    node, node->func, node->children);
		for (i = 0; i < node->nr_tokens; i++) {
			t = &node->tokens[i];
			stream_puts(out, "  cmd %p - %s\r\n", t->key, t->key);
			stream_puts(out, "  des %p - %s\r\n", t->desc, t->desc);
		}

		if (node->keyword) {
			stream_puts(out, "Keyword:\r\n");
			_cmd_tree_travel(node->keyword, out);
		}

		if (node->children) {
			stream_puts(out, "Children:\r\n");
			_cmd_tree_travel(node->children, out);
		}
	}
}

void cmd_tree_dump(struct cmd_node *tree, struct stream *out)
{
	int i, count = 0;
	struct cmd_node *node;
	struct token *token;

	for_each_node_token(tree->children, node, i, token) {
		count++;
		if (count == 1) {
			stream_puts(out, "tree-+-%s\n", token->key);
		} else if (node->sibling != NULL){
			stream_puts(out, "     |-%s\n", token->key);
		} else {
			stream_puts(out, "     `-%s\n", token->key);
		}
	}
}

struct ls {
	int width[32];
	int more[32];
};

void _cmd_tree_dump(struct cmd_node *tree, struct stream *out,
			struct ls *ls, int level, int first, int last, int opt)
{
	int i;
	struct cmd_node *node;
	int len = 0;
	int count = 0;

	if (!tree)
		return;

	if (!first) {
		for (i = 0; i < level; i++) {
			int m;
			for (m = 0; m < ls->width[i] + 1; m++)
				stream_puts(out, " ");

			if (i == level - 1) {
				if (opt)
					stream_puts(out, "*-");
				else if (last)
					stream_puts(out, "`-");
				else
					stream_puts(out, "|-");
			} else {
				if (ls->more[i + 1])
					stream_puts(out, "| ");
				else if (opt)
					stream_puts(out, "| ");
				else
					stream_puts(out, "  ");
			}
		}
	}

	if (level == 0) {
		len = stream_puts(out, "cmd");
		ls->width[0] = 3;
		ls->more[0] = 0;
	} else {
		int m;
		struct token *token;

		for_each_token(tree, m, token) {
			if (m == 0)
				if (tree->func) {
					// len = strlen(token->key);
					// stream_puts(out, "%s%c%s%s", "\x1b[34;1m", token->key[0], "\x1b[0m", token->key+1);
					len += stream_puts(out, "%s", token->key);
				} else
					len += stream_puts(out, "%s", token->key);
			else
				len += stream_puts(out, "|%s", token->key);
		}
	}

	ls->width[level] = len;
	ls->more[level] = !last;

	if (tree->children == NULL && tree->keyword == NULL)
		stream_puts(out, "\r\n");

	for_each_node(node, tree->children) {
		count++;
		if (node == tree->children) {
			if (node->sibling)
				stream_puts(out, "-+-");
			else
				stream_puts(out, "---");
		}
		_cmd_tree_dump(node, out, ls, level + 1, node == tree->children, node->sibling == NULL, 0);
	}

	for_each_node(node, tree->keyword) {
		if (count == 0 && node == tree->keyword) {
			stream_puts(out, "-*-");
		}

		_cmd_tree_dump(node, out, ls, level + 1, count ? 0 : node == tree->keyword, node->sibling == NULL, 1);
	}
}

void cmd_tree_travel(struct cmd_node *tree, struct stream *out)
{
	struct ls ls;

	_cmd_tree_dump(tree, out, &ls, 0, 1, 1, 0);
	// return cmd_tree_dump(tree, out);
	// return _cmd_tree_travel(tree->children, out);
}

void cmd_tree_delete(struct cmd_node *tree)
{
	if (tree->children)
		cmd_tree_delete(tree->children);
	if (tree->sibling)
		cmd_tree_delete(tree->sibling);
	if (tree->keyword)
		cmd_tree_delete(tree->keyword);

	free_node(tree);
}

static size_t token_count(struct cmd_node *head, const char *str, size_t len)
{
	int i;
	struct token *token;
	struct cmd_node *node;
	size_t count = 0;

	for_each_node_token(head, node, i, token) {
		if (!len || strncmp(token->key, str, len) == 0)
			count++;
	}

	return count;
}

enum match_type {
	no_match,
	extend_match,
	vararg_match,
	exact_match
};

static int match_word(struct token *token, const char *word)
{
	if (token->type == TOKEN_LITERAL) {
		if (!strcmp(token->key, word))
			return exact_match;
	} else if (token->type == TOKEN_VARIABLE || token->type == TOKEN_OPTION)
		return extend_match;
	else if (token->type == TOKEN_VARARG)
		return vararg_match;

	return no_match;
}

static struct cmd_node *find_best_node(struct cmd_node *head,
				       const char *word, struct token **ret,
				       int *ret_node_pos)
{
	int i, pos = 0;
	int match, best_match = no_match;
	struct token *token, *target_token = NULL;
	struct cmd_node *node, *target_node = NULL;
	int target_node_pos = 0;

	for_each_node(node, head) {
		for_each_token(node, i, token) {
			match = match_word(token, word);
			if (best_match < match) {
				best_match = match;
				target_node = node;
				target_token = token;
				target_node_pos = pos;
			}
		}
		pos++;
	}

	if (target_token) {
		if (ret)
			*ret = target_token;
		if (ret_node_pos)
			*ret_node_pos = target_node_pos;
	}

	return target_node;
}

int cmd_search(struct cmd_node *head, struct cmd_node **ret, int wordc,
	       char **words, int *wordi, int argc, char **argv, int *argi,
	       struct hashtable *h)
{
	struct token *token = NULL;
	struct cmd_node *target = NULL;

	target = find_best_node(head, words[*wordi], &token, NULL);
	if (target == NULL)
		return CMD_ERR_NO_MATCH;

	*ret = target;

	if (target->nr_tokens > 1) {
		argv[*argi] = words[*wordi];
		++(*argi);
		++(*wordi);
	} else if (token->type != TOKEN_LITERAL) {
		argv[*argi] = words[*wordi];
		++(*argi);
		++(*wordi);

		if (token->type == TOKEN_VARARG) {
			while (*wordi < wordc) {
				argv[*argi] = words[*wordi];
				++(*argi);
				++(*wordi);
			}
		}
	} else {
		++(*wordi);
	}

	if (*wordi == wordc)
		return 0;
	if (target->keyword) {
		int matched = 0;
		struct token *_token;
		struct cmd_node *_target;

		do {
			char *key;
			_target = find_best_node(target->keyword, words[*wordi], &_token, NULL);
			if (_target == NULL)
				break;

			matched++;

			if (h) {
				key = words[*wordi];
				hashtable_set(h, key, "1");
			}

			++(*wordi);

			if (_target->children) {
				if (*wordi == wordc) {
					*ret = _target;
					return 0;
				}
				_target = find_best_node(_target->children, words[*wordi], &_token, NULL);
				if (_target == NULL)
					return CMD_ERR_NO_MATCH;
				if (h) {
					hashtable_set(h, key, words[*wordi]);
				}
				++(*wordi);
			}
		} while (*wordi < wordc);

		if (*wordi == wordc) {
			*ret = target;
			return 0;
		}
	}

	if (target->children)
		return cmd_search(target->children, ret, wordc, words, wordi, argc, argv, argi, h);
	else
		return CMD_ERR_NO_MATCH;
}

static int line_get_args(const char *line, char ***argv)
{
	size_t len = 0;
	int count = 0;
	const char *start, *next = line;

	for (;;) {
		start = next_key(&next);
		if (start == NULL)
			break;
		count++;
	}

	if (count == 0)
		return 0;

	next = line;
	*argv = malloc(sizeof(char *) * (count + 1));
	if (*argv == NULL)
		return -ENOMEM;
	(*argv)[count] = NULL;
	count = 0;

	for (;;) {
		char *arg;

		start = next_key(&next);
		if (start == NULL)
			break;

		len = next - start;
		arg = malloc(len + 1);
		if (arg == NULL) {
			int i;
			for (i = 0; i < count; i++)
				free((*argv)[i]);
			free(*argv);
			return -ENOMEM;
		}

		memcpy(arg, start, len);
		arg[len] = '\0';

		(*argv)[count++] = arg;
	}

	return count;
}

static void line_free_args(int wordc, char **wordv)
{
	int i;
	for (i = 0; i < wordc; i++)
		free(wordv[i]);
	if (wordc)
		free(wordv);
}

int cmd_pipe(struct term *term, char *cmd)
{
	int pfd[2][2];
	pid_t pid;

	if (pipe(pfd[0]) < 0) {
		term_print(term, "error pipe %s\r\n", strerror(errno));
		return CMD_ERR_SYSTEM;
	}

	if (pipe(pfd[1]) < 0) {
		term_print(term, "error pipe %s\r\n", strerror(errno));
		return CMD_ERR_SYSTEM;
	}

	pid = fork();
	if (pid < 0) {
		term_print(term, "fork %s\r\n", strerror(errno));
		return CMD_ERR_SYSTEM;
	} else if (pid == 0) {
		char *args[4] = {
			"/bin/sh",
			"-c",
			cmd,
			NULL
		};

		dup2(pfd[0][0], 0);
		dup2(pfd[1][1], 1);
		dup2(pfd[1][1], 2);

		close(pfd[0][1]);
		close(pfd[1][0]);

		if (execv(args[0], args) < 0) {
			printf("execvp %s\n", strerror(errno));
			exit(CMD_ERR_SYSTEM);
		}
	} else {
		char buf[128];
		ssize_t r, c = 0;

		close(pfd[0][0]);
		close(pfd[1][1]);
		stream_flush(term_ostream(term), pfd[0][1]);
		close(pfd[0][1]);

		for (;;) {
			r = read(pfd[1][0], buf, sizeof(buf));
			if (r < 0) {
				if (errno == EINTR)
					continue;
				term_print(term, "error execvp %s\r\n", strerror(errno));
				return CMD_ERR_SYSTEM;
			} else if (r == 0) {
				waitpid(pid, NULL, 0);
				break;
			} else {
				stream_put(term_ostream(term), buf, r);
				c += r;
			}
		}
	}

	return 0;
}

static void strip_head_blank(char **str)
{
	char *ptr = *str;

	while (*ptr && *ptr == ' ')
		ptr++;

	*str = ptr;
}

static void strip_tail_blank(const char *str, char **end)
{
	char *ptr = *end;

	while (ptr > str && *(ptr - 1) == ' ')
		ptr--;

	*end = ptr;
}

int cmdopt_parse(struct term *term, struct cmdopt *opt, struct cmdoptattr *optattr)
{
	int kpcount = hashtable_count(opt->kpairs);
	int i;
	struct optattr *attr;

	if (!optattr)
		return 0;

	if (optattr->init)
		optattr->init(optattr->buf, optattr->bufsize);
	else
		memset(optattr->buf, 0, optattr->bufsize);

	if (!kpcount && !opt->argc)
		return 0;

	for (i = 0; i < optattr->size; i++) {
		attr = &optattr->attrs[i];
		if (attr->index >= 0 && attr->index < opt->argc) {
			int r;
			r = attr->set(opt->argv[attr->index], optattr->buf + attr->offset);
			if (r < 0) {
				term_print(term, "invalid option %s.\r\n", opt->argv[attr->index]);
				return -1;
			}
		} else if (attr->key) {
			int r;
			void *value;

			value = hashtable_get(opt->kpairs, attr->key);
			if (!value)
				continue;

			r = attr->set(value, optattr->buf + attr->offset);
			if (r < 0) {
				term_print(term, "invalid keyword %s:%s.\r\n", attr->key, value);
				return -1;
			}
		} else {
			term_print(term, "invalid attribute at %d\n", i);
		}
	}

	return 0;
}

int cmd_execute(struct term *term, struct cmd_node *tree, const char *line)
{
	int i, wordc;
	char **words;
	int wordi = 0;
	int ret;
	struct cmd_node *node;
	struct cmdopt *opt = term_cmdopt(term);

	wordc = line_get_args(line, &words);
	if (wordc < 0)
		return CMD_ERR_SYSTEM;
	else if (wordc == 0)
		return CMD_SUCCESS;

	for (i = 0; i < wordc; i++) {
		if (words[i][0] == '|')
			break;
	}

	if (i == 0) {
		term_print(term, "%% Invalid command - %s.\r\n", line);
		return CMD_ERR_NO_MATCH;
	}

	ret = cmd_search(tree->children, &node, i, words, &wordi, 1024, opt->argv, &opt->argc, opt->kpairs);
	if (ret != 0) {
		ret = CMD_ERR_NO_MATCH;
	} else if (!node->func) {
		ret = CMD_ERR_INCOMPLETE;
	} else {
		ret = cmdopt_parse(term, opt, node->optattr);
		if (ret == 0)
			ret = node->func(term, opt);
	}

	cmdopt_clear(opt);

	if (ret != CMD_SUCCESS) {
		switch (ret) {
		case CMD_ERR_NO_MATCH:
			term_print(term, "%% Unknown command - %s.\r\n", line);
			break;
		case CMD_ERR_INCOMPLETE:
			term_print(term, "%% Command incomplete.\r\n");
			break;
		default:
			term_print(term, "%% Command return error %d.\r\n", ret);
			break;
		}
	}

	if (i < wordc && ret == CMD_SUCCESS) {
		char *ptr = &words[i][1];
		char *key = NULL, *key_end = NULL;
		char *next = NULL, *next_end = NULL;
		int handled = 0;

		strip_head_blank(&ptr);

		if (*ptr) {
			key = ptr;
			while (*ptr && *ptr != ' ')
				ptr++;
			key_end = ptr;
		}

		if (key && *ptr) {
			strip_head_blank(&ptr);
			if (*ptr) {
				next = ptr;
				next_end = next + strlen(ptr);
				strip_tail_blank(next, &next_end);
			}
		}

		if (key && next) {
			size_t key_len = key_end - key;

			if (key_len == sizeof("include") - 1 && strncmp(key, "include", key_len) == 0) {
				handled = 1;
				*next_end = 0;
				stream_flush_regexp(term_ostream(term), term_fd(term), next, next_end - next);
			}
		}

		if (!handled)
			cmd_pipe(term, &words[i][1]);
	}

	line_free_args(wordc, words);

	return ret;
}

void cmd_complete_free(int ret, char **keys)
{
	if (ret == CMD_COMPLETE_MATCH) {
		free(keys[0]);
		free(keys);
	} else {
		if (keys)
			free(keys);
	}
}

static int cmd_lcd(char **matched)
{
	int i;
	int j;
	int lcd = -1;
	char *s1, *s2;
	char c1, c2;

	if (matched[0] == NULL || matched[1] == NULL)
		return 0;

	for (i = 1; matched[i] != NULL; i++) {
		s1 = matched[i - 1];
		s2 = matched[i];

		for (j = 0; (c1 = s1[j]) && (c2 = s2[j]); j++)
			if (c1 != c2)
				break;

		if (lcd < 0)
			lcd = j;
		else {
			if (lcd > j)
				lcd = j;
		}
	}

	return lcd;
}

static int get_complete(struct cmd_node *tree, const char *word, int *n, char ***keys)
{
	size_t len;
	size_t count;
	int i, index = 0, lcd;
	struct token *token;
	struct cmd_node *node;
	struct cmd_node *head = tree->children;
	struct cmd_node *keyword = tree->keyword;

	len = word ? strlen(word) : 0;

	count = token_count(head, word, len);
	count += token_count(keyword, word, len);
	if (count == 0)
		return CMD_ERR_NO_MATCH;

	*n = count;
	*keys = calloc(count + 1, sizeof(char *));
	if (*keys == NULL)
		return CMD_ERR_SYSTEM;

	for_each_node_token(head, node, i, token) {
		if (!len || strncmp(token->key, word, len) == 0) {
			(*keys)[index++] = token->key;
		}
	}

	for_each_node_token(keyword, node, i, token) {
		if (!len || strncmp(token->key, word, len) == 0) {
			(*keys)[index++] = token->key;
		}
	}

	lcd = cmd_lcd(*keys);
	if (lcd && lcd > len) {
		const char *str = (*keys)[0];
		char *one;

		one = malloc(lcd + 1);
		memcpy(one, str, lcd);
		one[lcd] = '\0';
		(*keys)[0] = one;

		return CMD_COMPLETE_MATCH;
	}

	return count == 1 ? CMD_COMPLETE_FULL_MATCH : CMD_COMPLETE_LIST_MATCH;
}

static int _cmd_complete(struct cmd_node *tree, const char *line,
			 int wordc, char **words, int *n, char ***keys)
{
	struct cmd_node *base = tree;
	char *argv[1000];
	int argi = 0, wordi = 0;
	int ret;
	const char *word;
	int _wordc = wordc;

	if (wordc == 0)
		return CMD_SUCCESS;

	if (wordc && line[strlen(line)-1] != ' ') {
		_wordc = wordc - 1;
	}

	if (_wordc > 0) {
		ret = cmd_search(tree->children, &base, _wordc, words, &wordi, 1024, argv, &argi, NULL);
		if (ret != 0) {
			return CMD_ERR_NO_MATCH;
		}
	}

	word = wordi < wordc ? words[wordi] : NULL;

	return get_complete(base, word, n, keys);
}

int cmd_complete(struct cmd_node *tree, const char *line, int *n, char ***keys)
{
	int ret;
	int i, wordc;
	char **words;

	wordc = line_get_args(line, &words);
	if (wordc < 0)
		return CMD_ERR_SYSTEM;

	for (i = 0; i < wordc; i++) {
		if (words[i][0] == '|')
			return CMD_SUCCESS;
	}

	ret = _cmd_complete(tree, line, wordc, words, n, keys);
	line_free_args(wordc, words);
	return ret;
}

void cmd_describe_free(int ret, char **keys, char **descs)
{
	if (keys)
		free(keys);
	if (descs)
		free(descs);
}

static int alloc_desc(size_t count, char ***keys, char ***descs)
{
	*keys = calloc(count + 1, sizeof(char *));
	if (*keys == NULL)
		return -ENOMEM;
	*descs = calloc(count + 1, sizeof(char *));
	if (*descs == NULL)
		return -ENOMEM;

	return 0;
}

static int get_desc(struct cmd_node *tree, const char *word, int *n,
		    char ***keys, char ***descs)
{
	size_t count;
	size_t len;
	int index = 0;
	struct cmd_node *node;
	struct token *token;
	int i;
	struct cmd_node *head = tree->children;
	struct cmd_node *keyword = tree->keyword;

	len = word ? strlen(word) : 0;

	count  = token_count(head, word, len);
	count += token_count(keyword, word, len);
	if (count <= 0)
		return CMD_ERR_NO_MATCH;

	*n = count;
	if (alloc_desc(count, keys, descs) < 0)
		return CMD_ERR_SYSTEM;

	for_each_node_token(head, node, i, token) {
		if (!len || strncmp(token->key, word, len) == 0) {
			(*keys)[index] = token->key;
			(*descs)[index] = token->desc;
			index++;
		}
	}

	for_each_node_token(keyword, node, i, token) {
		if (!len || strncmp(token->key, word, len) == 0) {
			(*keys)[index] = token->key;
			(*descs)[index] = token->desc;
			index++;
		}
	}

	return count == 1 ? CMD_COMPLETE_FULL_MATCH : CMD_COMPLETE_LIST_MATCH;
}

static int _cmd_describe(struct cmd_node *tree, const char *line, int wordc,
			 char **words, int *n, char ***keys, char ***descs, int *cr)
{
	struct cmd_node *base = tree;
	char *argv[1000];
	int argi = 0, wordi = 0;
	int i, ret;
	const char *word;
	int _wordc = wordc;

	if (wordc && line[strlen(line)-1] != ' ') {
		_wordc = wordc - 1;
	}

	if (_wordc > 0) {
		ret = cmd_search(tree->children, &base, _wordc, words, &wordi, 1024, argv, &argi, NULL);
		if (ret != 0) {
			return CMD_ERR_NO_MATCH;
		}
	}

	word = wordi < wordc ? words[wordi] : NULL;
	*cr = _wordc == wordc && base->func ? 1 : 0;

	ret = get_desc(base, word, n, keys, descs);

	if (word) {
		for (i = 0; i < *n; i++) {
			if (strcmp(word, (*keys)[i]) == 0) {
				if (base->func)
					*cr = 1;
				break;
			}
		}
	}

	return ret;
}

int cmd_describe(struct cmd_node *tree, const char *line, int *n,
		 char ***keys, char ***descs, int *cr)
{
	int ret;
	int i, wordc;
	char **wordv;

	wordc = line_get_args(line, &wordv);
	if (wordc < 0)
		return CMD_ERR_NO_MATCH;

	for (i = 0; i < wordc; i++) {
		if (wordv[i][0] == '|')
			return CMD_SUCCESS;
	}

	ret = _cmd_describe(tree, line, wordc, wordv, n, keys, descs, cr);
	line_free_args(wordc, wordv);

	return ret;
}

extern const struct cmd_elem __start_cmd_section, __stop_cmd_section;

static int elem_compare(const void *a, const void *b)
{
	const struct cmd_elem **ea = (const struct cmd_elem **)a;
	const struct cmd_elem **eb = (const struct cmd_elem **)b;

	return strcmp((*ea)->line, (*eb)->line);
}

/* The command syntax represented by line is from Quagga and gets simplified. */

#define COMMON_COMMAND(func, line, desc)				\
	static int func(struct term *term, struct cmdopt *opt);		\
									\
	struct cmd_elem cmd_common_##func = {				\
		line, desc, func					\
	};								\
									\
	static int func(struct term *term, struct cmdopt *opt)

COMMON_COMMAND(list_commands,
	"list",
	"List all defined commands\n")
{
	cmd_list_elems(term_ostream(term));

	return 0;
}

COMMON_COMMAND(quit_terminal,
	"quit",
	"Exit current terminal\n")
{
	stream_putc(term_ostream(term), 4);
	term_flush(term);
	term_quit(term);

	return 0;
}

COMMON_COMMAND(exit_terminal,
	"exit",
	"Exit current terminal\n")
{
	term_flush(term);
	term_quit(term);

	return 0;
}

static const struct cmd_elem *common_cmds[] = {
	&cmd_common_list_commands,
	&cmd_common_quit_terminal,
	&cmd_common_exit_terminal,
	NULL
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)	sizeof(arr) / sizeof(arr[0])
#endif

void cmd_list_elems(struct stream *out)
{
	const struct cmd_elem *start = &__start_cmd_section;
	const struct cmd_elem *end = &__stop_cmd_section;
	const struct cmd_elem **array;
	size_t i, nr_cmds = end - start;
	size_t nr_comm = ARRAY_SIZE(common_cmds) - 1;
	size_t count = nr_comm + nr_cmds;

	array = malloc(count  * sizeof(struct cmd_elem *));
	if (array == NULL) {
		return;
	}

	for (i = 0; i < nr_comm; i++)
		array[i] = common_cmds[i];

	for (i = 0; i < nr_cmds; i++) {
		array[nr_comm + i] = start + i;
	}

	qsort(array + nr_comm, nr_cmds, sizeof(void *), elem_compare);

	for (i = 0; i < count; i++) {
		stream_puts(out, "  %s\r\n", array[i]->line);
	}
}

struct cmd_node *cmd_tree_build(const struct cmd_elem *start, const struct cmd_elem *end)
{
	const struct cmd_elem *elem;
	struct cmd_node *tree;
	size_t i, nr_comm = ARRAY_SIZE(common_cmds) - 1;

	tree = calloc(1, sizeof(struct cmd_node));
	if (tree == NULL)
		return NULL;

	for (i = 0 ; i < nr_comm; i++) {
		if (cmd_add_elem(tree, common_cmds[i]) < 0) {
			printf("failed to add '%s'\r\n", common_cmds[i]->line);
		}
	}

	for (elem = start; elem < end; elem++) {
		if (cmd_add_elem(tree, elem) < 0) {
			printf("failed to add '%s'\r\n", elem->line);
		}
	}

	return tree;
}

struct cmd_node *cmd_tree_build_default(void)
{
	return cmd_tree_build(&__start_cmd_section, &__stop_cmd_section);
}
