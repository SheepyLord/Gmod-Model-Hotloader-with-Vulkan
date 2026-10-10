#pragma once
// The tests take wide arguments (wmain) as Windows passes them; elsewhere main converts
// its UTF-8 arguments and calls the same wmain.
#ifndef _WIN32
#include <filesystem>
#include <string>
#include <vector>
int wmain(int argc,wchar_t** argv);
int main(int argc,char** argv){
 std::vector<std::wstring> text;for(int i=0;i<argc;i++)text.push_back(std::filesystem::path(argv[i]).wstring());
 std::vector<wchar_t*> pointers;for(auto& t:text)pointers.push_back(t.data());pointers.push_back(nullptr);
 return wmain(argc,pointers.data());
}
#endif
