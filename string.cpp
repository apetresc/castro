
#include "string.h"

using namespace std;


void trim(string & str){
	const char * space = " \t\r\n";
    str.erase(0,  str.find_first_not_of(space));
    str.erase(1 + str.find_last_not_of(space));
}

vecstr explode(const string & str, const string & sep){
	vecstr ret;
	unsigned int old = 0, pos = 0;
	while((pos = str.find_first_of(sep, old)) != string::npos){
		ret.push_back(str.substr(old, pos - old));
		old = pos + sep.length();
	}
	ret.push_back(str.substr(old));
	return ret;
}

string implode(const vecstr & vec, const string & sep){
	string ret;
	if(vec.size() == 0)
		return ret;
	ret += vec[0];
	for(unsigned int i = 1; i < vec.size(); i++){
		ret += sep;
		ret += vec[i];
	}
	return ret;
}

