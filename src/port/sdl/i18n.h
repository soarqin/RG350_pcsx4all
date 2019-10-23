#pragma once

#include <string>
#include <vector>

struct Lang {
	std::string locale;
	std::string name;
};

class I18n {
public:
	void init();
	void apply(const std::string &lang);
    inline const std::vector<Lang> &getList() { return languages_; }

private:
	std::vector<Lang> languages_;
};
