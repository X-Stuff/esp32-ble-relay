#include "IdfLogLine.h"

#include <cstring>

namespace w2b
{

namespace
{

bool levelOf(char letter, LogLevel& level)
{
	switch (letter)
	{
		case 'E':
		{
			level = LogLevel::Error;
			return true;
		}
		case 'W':
		{
			level = LogLevel::Warn;
			return true;
		}
		case 'I':
		{
			level = LogLevel::Info;
			return true;
		}
		case 'D':
		case 'V':
		{
			level = LogLevel::Debug;
			return true;
		}
	}

	return false;
}

void trimLineEnd(char* text)
{
	size_t len = std::strlen(text);

	while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
	{
		text[--len] = '\0';
	}
}

// Returns the position after "X (digits) ", or nullptr when the line does not start that way.
char* afterPrefix(char* text)
{
	if (text[0] == '\0' || text[1] != ' ' || text[2] != '(')
	{
		return nullptr;
	}

	char* p = text + 3;

	while (*p >= '0' && *p <= '9')
	{
		++p;
	}

	if (p == text + 3 || p[0] != ')' || p[1] != ' ')
	{
		return nullptr;
	}

	return p + 2;
}

}  // namespace

void splitIdfLogLine(char* text, LogLevel& level, const char*& tag, const char*& message)
{
	trimLineEnd(text);

	level = LogLevel::Info;
	tag = "idf";
	message = text;

	char* rest = afterPrefix(text);
	char* colon = rest != nullptr ? std::strchr(rest, ':') : nullptr;

	if (colon == nullptr || !levelOf(text[0], level))
	{
		return;
	}

	*colon = '\0';
	tag = rest;
	message = colon + 1;

	while (*message == ' ')
	{
		++message;
	}
}

}  // namespace w2b
