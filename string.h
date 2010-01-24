
#ifndef _STRING_H_
#define _STRING_H_

#include <vector>
#include <string>
#include <sstream>

typedef std::vector<std::string> vecstr;

template <class T> std::string to_str(T a){
	std::stringstream out;
	out << a;
	return out.str();
}

void trim(std::string & str);

vecstr explode(const std::string & str, const std::string & sep);
std::string implode(const vecstr & vec, const std::string & sep);

#endif
