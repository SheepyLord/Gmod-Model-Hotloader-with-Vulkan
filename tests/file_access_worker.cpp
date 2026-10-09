// Test-only worker for tests/file_access_tests.cpp: native/file_access_dialog.cpp built
// with MMDHL_FILE_ACCESS_TESTING answers from MMDHL_FA_TEST_ANSWER instead of showing
// windows. It is never packaged; the shipped worker has no such path.
#include "file_access.hpp"
#include <string>
int wmain(int argc,wchar_t** argv){
 try{if(argc==3&&(std::wstring(argv[1])==L"--fa-pick"||std::wstring(argv[1])==L"--fa-consent"))return mmd::fileAccessDialog(std::wstring(argv[1])==L"--fa-pick",argv[2]);}catch(...){return 1;}
 return 2;
}
