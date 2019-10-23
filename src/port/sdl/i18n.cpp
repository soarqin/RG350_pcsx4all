#include "i18n.h"

#include <libintl.h>
#include <libgen.h>
#include <unistd.h>

std::string& ltrim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
{
    str.erase(0, str.find_first_not_of(chars));
    return str;
}
 
std::string& rtrim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
{
    str.erase(str.find_last_not_of(chars) + 1);
    return str;
}
 
std::string& trim(std::string& str, const std::string& chars = "\t\n\v\f\r ")
{
    return ltrim(rtrim(str, chars), chars);
}

void I18n::init()
{
	// setenv("LC_MESSAGES", "zh_CN.UTF-8", 1);
	setlocale(LC_ALL, "");

#if defined(WIN32)
	bindtextdomain("pcsx4all", "locale");
	textdomain("pcsx4all");
#else
	char exepath[256];
	ssize_t count = readlink("/proc/self/exe", exepath, 256);
	if (count != -1) {
		char filepath[256];
		const char *exedir = dirname(exepath);
		sprintf(filepath, "%s/locale", exedir);
		bindtextdomain("pcsx4all", filepath);
		bind_textdomain_codeset("pcsx4all", "utf-8");
		textdomain("pcsx4all");

		languages_.push_back({"en_US", "English"});
		sprintf(filepath, "%s/locales", exedir);
		FILE *f = fopen(filepath, "rt");
		if (f == NULL) return;
		char line[256];
		while(fgets(line, 256, f)) {
			std::string s = line;
			trim(s);
			auto pos = s.find_first_of(" \t");
			if (pos == std::string::npos) continue;
			languages_.push_back({s.substr(0, pos), s.substr(pos + 1)});
		}
		fclose(f);
	}
#endif
}

void I18n::apply(const std::string &lang)
{
	std::string locale = lang + ".UTF-8";
	setenv("LC_MESSAGES", locale.c_str(), 1);
	setlocale(LC_ALL, "");
}
