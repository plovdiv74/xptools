#ifndef CmdLine_hpp
#define CmdLine_hpp

/**
 * A very simple (and limited) command line argument parser.
 * Accepts three types of arguments:
 *   - Flags (like --foo)
 *   - Key-value pairs (like --foo="bar baz" or --bang=bop)
 *     Note that the equals here is critical---if you pass in --foo "bar" without the =, it will not work.
 *   - "Short" flags like -a or -b="something" (with limitations---we do not "deduplicate" in the typical
 *     Unix style where we treat -a -b -c as identical to -abc or even -cba).
 *     You're probably better off just using long options.
 */
class CmdLine {
public:
#if LIN || APL
	CmdLine(int argc, char const * const * argv);
#else // Windows gets all its args as a single std::string
	CmdLine(const char * arg);
#endif

	// If your option is just a flag, get_value() will return an empty std::string; instead, check has_option()
	bool	has_option(const std::string & option) const;
	std::string	get_value(const std::string & option) const;

	using storage_type = std::unordered_map<std::string, std::string>;
private:
	const storage_type m_options;
};

#endif
