// The worker's half of file access: the windows the player answers. They run in the
// worker process, which Lua cannot drive, and every sentence is compiled in; only the
// requester, script, purpose and title fields come from the addon, shown in quotes or
// marked as not verified (cleanLabel turned their own quotation marks into apostrophes).
#include "file_access.hpp"
#include "file_access_picker.hpp"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <atomic>
#include <functional>
#include <thread>
namespace mmd {
namespace {
struct Text {
 const wchar_t *title,*askFile,*askFolder,*addon,*script,*purpose,*file,*folder,*folderNote,*warning,*once,*onceNote,*always,*alwaysNote,*deny,*close,
  *notFound,*link,*denied,*unreadable,*notFile,*notFolder,*network,*broad,*pickFile,*pickFiles,*pickFolder,*pickNote,*pickOk,*allFiles,*enableAsk,*enableText,*enableYes,*enableNo;
 const wchar_t* units[4];
};
const std::pair<const char*,Text> texts[]={
 {"en",{.title=L"Model Hotloader: file access",.askFile=L"An addon asks to read a file",.askFolder=L"An addon asks to read a folder",
  .addon=L"Addon: “{requester}” (the name its script reports; not verified)",.script=L"Script (reported by the addon; not verified): {script}",.purpose=L"The addon says: “{purpose}”",
  .file=L"File: {path} ({size})",.folder=L"Folder: {path}",.folderNote=L"Allowing a folder lets the addon read every file in it and in its subfolders.",
  .warning=L"An addon can use whatever it reads as it likes, including sending it to a server or a website. Allow only addons you trust.",
  .once=L"Allow once",.onceNote=L"Until the map changes.",.always=L"Always allow this addon here",.alwaysNote=L"Without asking again, for files in {folder}",.deny=L"Deny",.close=L"Close",
  .notFound=L"This file or folder does not exist.",.link=L"This is a link to another place. Addons cannot read through links.",.denied=L"Model Hotloader never lets addons read this location.",
  .unreadable=L"Windows does not allow this to be read.",.notFile=L"This is a folder, not a file.",.notFolder=L"This is a file, not a folder.",.network=L"Files on other computers cannot be requested by path.",
  .broad=L"This folder is too broad to allow permanently.",
  .pickFile=L"“{requester}” asks you to choose a file to read",.pickFiles=L"“{requester}” asks you to choose files to read",.pickFolder=L"“{requester}” asks you to choose a folder to read",
  .pickNote=L"Model Hotloader shows this window for an addon. Its name is what its script reports and is not verified. The addon can use what you choose as it likes, including sending it to a server or a website.",
  .pickOk=L"Allow reading",.allFiles=L"All files",.enableAsk=L"Let addons ask to read your files?",
  .enableText=L"Addons will be able to ask you to choose files, or to allow a file or folder. Nothing is read until you answer in a window like this one.",.enableYes=L"Turn on",.enableNo=L"Keep off",
  .units={L"bytes",L"KB",L"MB",L"GB"}}},
 {"fr",{.title=L"Model Hotloader : accès aux fichiers",.askFile=L"Un addon demande à lire un fichier",.askFolder=L"Un addon demande à lire un dossier",
  .addon=L"Addon : « {requester} » (nom indiqué par son script ; non vérifié)",.script=L"Script (indiqué par l’addon ; non vérifié) : {script}",.purpose=L"L’addon indique : « {purpose} »",
  .file=L"Fichier : {path} ({size})",.folder=L"Dossier : {path}",.folderNote=L"Autoriser un dossier permet à l’addon de lire tous les fichiers qu’il contient, sous-dossiers compris.",
  .warning=L"Un addon peut faire ce qu’il veut de ce qu’il lit, y compris l’envoyer à un serveur ou à un site web. N’autorisez que les addons auxquels vous faites confiance.",
  .once=L"Autoriser une fois",.onceNote=L"Jusqu’au changement de carte.",.always=L"Toujours autoriser cet addon ici",.alwaysNote=L"Sans redemander, pour les fichiers de {folder}",.deny=L"Refuser",.close=L"Fermer",
  .notFound=L"Ce fichier ou dossier n’existe pas.",.link=L"Ceci est un lien vers un autre emplacement. Les addons ne peuvent pas lire à travers les liens.",.denied=L"Model Hotloader ne laisse jamais les addons lire cet emplacement.",
  .unreadable=L"Windows ne permet pas de lire cet élément.",.notFile=L"Ceci est un dossier, pas un fichier.",.notFolder=L"Ceci est un fichier, pas un dossier.",.network=L"Les fichiers d’autres ordinateurs ne peuvent pas être demandés par chemin.",
  .broad=L"Ce dossier est trop vaste pour être autorisé de façon permanente.",
  .pickFile=L"« {requester} » vous demande de choisir un fichier à lire",.pickFiles=L"« {requester} » vous demande de choisir des fichiers à lire",.pickFolder=L"« {requester} » vous demande de choisir un dossier à lire",
  .pickNote=L"Model Hotloader affiche cette fenêtre pour un addon. Son nom est celui qu’indique son script et n’est pas vérifié. L’addon peut faire ce qu’il veut de ce que vous choisissez, y compris l’envoyer à un serveur ou à un site web.",
  .pickOk=L"Autoriser la lecture",.allFiles=L"Tous les fichiers",.enableAsk=L"Permettre aux addons de demander à lire vos fichiers ?",
  .enableText=L"Les addons pourront vous demander de choisir des fichiers, ou d’autoriser un fichier ou un dossier. Rien n’est lu tant que vous n’avez pas répondu dans une fenêtre comme celle-ci.",.enableYes=L"Activer",.enableNo=L"Laisser désactivé",
  .units={L"octets",L"Ko",L"Mo",L"Go"}}},
 {"ja",{.title=L"Model Hotloader：ファイルアクセス",.askFile=L"アドオンがファイルの読み取りを求めています",.askFolder=L"アドオンがフォルダーの読み取りを求めています",
  .addon=L"アドオン：「{requester}」（スクリプトが名乗っている名前で、確認されていません）",.script=L"スクリプト（アドオンの自己申告で、確認されていません）：{script}",.purpose=L"アドオンの説明：「{purpose}」",
  .file=L"ファイル：{path}（{size}）",.folder=L"フォルダー：{path}",.folderNote=L"フォルダーを許可すると、アドオンはその中とサブフォルダー内のすべてのファイルを読み取れます。",
  .warning=L"アドオンは読み取った内容を自由に使えます。サーバーやウェブサイトへの送信も可能です。信頼できるアドオンだけに許可してください。",
  .once=L"今回だけ許可",.onceNote=L"マップが変わるまで有効です。",.always=L"このアドオンにここを常に許可",.alwaysNote=L"{folder} 内のファイルは今後確認しません",.deny=L"拒否",.close=L"閉じる",
  .notFound=L"このファイルまたはフォルダーは存在しません。",.link=L"これは別の場所へのリンクです。アドオンはリンクを経由して読み取ることはできません。",.denied=L"Model Hotloader は、この場所をアドオンに読み取らせることはありません。",
  .unreadable=L"Windows がこの項目の読み取りを許可していません。",.notFile=L"これはファイルではなくフォルダーです。",.notFolder=L"これはフォルダーではなくファイルです。",.network=L"他のコンピューター上のファイルは、パスを指定して要求することはできません。",
  .broad=L"このフォルダーは範囲が広すぎるため、常に許可することはできません。",
  .pickFile=L"「{requester}」が読み取るファイルを選ぶよう求めています",.pickFiles=L"「{requester}」が読み取るファイル（複数可）を選ぶよう求めています",.pickFolder=L"「{requester}」が読み取るフォルダーを選ぶよう求めています",
  .pickNote=L"このウィンドウは Model Hotloader がアドオンのために表示しています。アドオン名はスクリプトが名乗っているもので、確認されていません。アドオンは選んだ内容を自由に使えます。サーバーやウェブサイトへの送信も可能です。",
  .pickOk=L"読み取りを許可",.allFiles=L"すべてのファイル",.enableAsk=L"アドオンがファイルの読み取りを求められるようにしますか？",
  .enableText=L"アドオンが、ファイルの選択や、ファイル・フォルダーの読み取り許可を求められるようになります。このようなウィンドウで応答するまで、何も読み取られません。",.enableYes=L"オンにする",.enableNo=L"オフのままにする",
  .units={L"バイト",L"KB",L"MB",L"GB"}}},
 {"ko",{.title=L"Model Hotloader: 파일 접근",.askFile=L"애드온이 파일 읽기를 요청합니다",.askFolder=L"애드온이 폴더 읽기를 요청합니다",
  .addon=L"애드온: “{requester}” (스크립트가 밝힌 이름이며 확인되지 않았습니다)",.script=L"스크립트(애드온이 밝힌 것이며 확인되지 않았습니다): {script}",.purpose=L"애드온이 밝힌 이유: “{purpose}”",
  .file=L"파일: {path} ({size})",.folder=L"폴더: {path}",.folderNote=L"폴더를 허용하면 애드온이 그 안과 하위 폴더의 모든 파일을 읽을 수 있습니다.",
  .warning=L"애드온은 읽은 내용을 마음대로 사용할 수 있으며, 서버나 웹사이트로 보낼 수도 있습니다. 신뢰하는 애드온만 허용하세요.",
  .once=L"이번만 허용",.onceNote=L"맵이 바뀔 때까지 유효합니다.",.always=L"이 애드온에 여기를 항상 허용",.alwaysNote=L"{folder} 안의 파일은 다시 묻지 않습니다",.deny=L"거부",.close=L"닫기",
  .notFound=L"이 파일이나 폴더가 없습니다.",.link=L"다른 위치로 연결되는 링크입니다. 애드온은 링크를 통해 읽을 수 없습니다.",.denied=L"Model Hotloader는 애드온이 이 위치를 읽도록 절대 허용하지 않습니다.",
  .unreadable=L"Windows에서 이 항목을 읽을 수 없도록 막고 있습니다.",.notFile=L"파일이 아니라 폴더입니다.",.notFolder=L"폴더가 아니라 파일입니다.",.network=L"다른 컴퓨터의 파일은 경로로 요청할 수 없습니다.",
  .broad=L"이 폴더는 범위가 너무 넓어 항상 허용할 수 없습니다.",
  .pickFile=L"“{requester}”이(가) 읽을 파일을 선택하도록 요청합니다",.pickFiles=L"“{requester}”이(가) 읽을 파일들을 선택하도록 요청합니다",.pickFolder=L"“{requester}”이(가) 읽을 폴더를 선택하도록 요청합니다",
  .pickNote=L"이 창은 Model Hotloader가 애드온을 위해 표시합니다. 애드온 이름은 스크립트가 밝힌 것이며 확인되지 않았습니다. 애드온은 선택한 내용을 마음대로 사용할 수 있으며, 서버나 웹사이트로 보낼 수도 있습니다.",
  .pickOk=L"읽기 허용",.allFiles=L"모든 파일",.enableAsk=L"애드온이 파일 읽기를 요청할 수 있게 할까요?",
  .enableText=L"애드온이 파일을 선택하거나 파일 또는 폴더를 허용해 달라고 요청할 수 있게 됩니다. 이런 창에서 응답하기 전에는 아무것도 읽히지 않습니다.",.enableYes=L"켜기",.enableNo=L"끈 상태로 두기",
  .units={L"바이트",L"KB",L"MB",L"GB"}}},
 {"ru",{.title=L"Model Hotloader: доступ к файлам",.askFile=L"Дополнение просит прочитать файл",.askFolder=L"Дополнение просит прочитать папку",
  .addon=L"Дополнение: «{requester}» (имя, которое сообщает его скрипт; не проверено)",.script=L"Скрипт (со слов дополнения; не проверено): {script}",.purpose=L"Дополнение сообщает: «{purpose}»",
  .file=L"Файл: {path} ({size})",.folder=L"Папка: {path}",.folderNote=L"Разрешив папку, вы позволите дополнению читать все файлы в ней и во вложенных папках.",
  .warning=L"Дополнение может использовать прочитанное как угодно, в том числе отправить на сервер или сайт. Разрешайте только дополнениям, которым доверяете.",
  .once=L"Разрешить один раз",.onceNote=L"До смены карты.",.always=L"Всегда разрешать этому дополнению здесь",.alwaysNote=L"Без повторных вопросов для файлов в {folder}",.deny=L"Запретить",.close=L"Закрыть",
  .notFound=L"Такого файла или папки нет.",.link=L"Это ссылка на другое место. Дополнения не могут читать через ссылки.",.denied=L"Model Hotloader никогда не позволяет дополнениям читать это место.",
  .unreadable=L"Windows не разрешает читать этот объект.",.notFile=L"Это папка, а не файл.",.notFolder=L"Это файл, а не папка.",.network=L"Файлы на других компьютерах нельзя запрашивать по пути.",
  .broad=L"Эта папка слишком общая, чтобы разрешить её навсегда.",
  .pickFile=L"«{requester}» просит выбрать файл для чтения",.pickFiles=L"«{requester}» просит выбрать файлы для чтения",.pickFolder=L"«{requester}» просит выбрать папку для чтения",
  .pickNote=L"Model Hotloader показывает это окно по просьбе дополнения. Его имя сообщает сам скрипт, оно не проверено. Дополнение может использовать выбранное как угодно, в том числе отправить на сервер или сайт.",
  .pickOk=L"Разрешить чтение",.allFiles=L"Все файлы",.enableAsk=L"Разрешить дополнениям просить доступ к вашим файлам?",
  .enableText=L"Дополнения смогут просить вас выбрать файлы или разрешить файл или папку. Ничего не читается, пока вы не ответите в таком окне.",.enableYes=L"Включить",.enableNo=L"Оставить выключенным",
  .units={L"байт",L"КБ",L"МБ",L"ГБ"}}},
 {"zh-cn",{.title=L"Model Hotloader：文件访问",.askFile=L"有插件请求读取文件",.askFolder=L"有插件请求读取文件夹",
  .addon=L"插件：“{requester}”（由其脚本自称，未经验证）",.script=L"脚本（由插件自称，未经验证）：{script}",.purpose=L"插件给出的理由：“{purpose}”",
  .file=L"文件：{path}（{size}）",.folder=L"文件夹：{path}",.folderNote=L"允许访问文件夹后，插件可以读取其中及其子文件夹中的所有文件。",
  .warning=L"插件可以随意使用读取到的内容，包括发送到服务器或网站。请只允许你信任的插件。",
  .once=L"仅允许这一次",.onceNote=L"在切换地图前有效。",.always=L"始终允许此插件读取这里",.alwaysNote=L"读取 {folder} 中的文件时不再询问",.deny=L"拒绝",.close=L"关闭",
  .notFound=L"此文件或文件夹不存在。",.link=L"这是指向其他位置的链接。插件不能通过链接读取。",.denied=L"Model Hotloader 绝不允许插件读取此位置。",
  .unreadable=L"Windows 不允许读取此项。",.notFile=L"这是文件夹，不是文件。",.notFolder=L"这是文件，不是文件夹。",.network=L"不能通过路径请求其他计算机上的文件。",
  .broad=L"此文件夹范围过大，无法永久允许。",
  .pickFile=L"“{requester}”请你选择一个要读取的文件",.pickFiles=L"“{requester}”请你选择要读取的文件",.pickFolder=L"“{requester}”请你选择一个要读取的文件夹",
  .pickNote=L"此窗口由 Model Hotloader 代插件显示。插件名称由其脚本自称，未经验证。插件可以随意使用你选择的内容，包括发送到服务器或网站。",
  .pickOk=L"允许读取",.allFiles=L"所有文件",.enableAsk=L"允许插件请求读取你的文件吗？",
  .enableText=L"插件将可以请你选择文件，或请求允许读取某个文件或文件夹。在你于此类窗口中作出回应之前，不会读取任何内容。",.enableYes=L"开启",.enableNo=L"保持关闭",
  .units={L"字节",L"KB",L"MB",L"GB"}}},
 {"zh-tw",{.title=L"Model Hotloader：檔案存取",.askFile=L"有附加元件要求讀取檔案",.askFolder=L"有附加元件要求讀取資料夾",
  .addon=L"附加元件：「{requester}」（由其指令碼自稱，未經驗證）",.script=L"指令碼（由附加元件自稱，未經驗證）：{script}",.purpose=L"附加元件提供的理由：「{purpose}」",
  .file=L"檔案：{path}（{size}）",.folder=L"資料夾：{path}",.folderNote=L"允許存取資料夾後，附加元件可以讀取其中及其子資料夾內的所有檔案。",
  .warning=L"附加元件可以任意使用讀取到的內容，包括傳送到伺服器或網站。請只允許你信任的附加元件。",
  .once=L"僅允許這一次",.onceNote=L"在切換地圖前有效。",.always=L"一律允許此附加元件讀取這裡",.alwaysNote=L"讀取 {folder} 中的檔案時不再詢問",.deny=L"拒絕",.close=L"關閉",
  .notFound=L"此檔案或資料夾不存在。",.link=L"這是指向其他位置的連結。附加元件無法透過連結讀取。",.denied=L"Model Hotloader 絕不允許附加元件讀取此位置。",
  .unreadable=L"Windows 不允許讀取此項目。",.notFile=L"這是資料夾，不是檔案。",.notFolder=L"這是檔案，不是資料夾。",.network=L"無法透過路徑要求其他電腦上的檔案。",
  .broad=L"此資料夾範圍過大，無法永久允許。",
  .pickFile=L"「{requester}」請你選擇一個要讀取的檔案",.pickFiles=L"「{requester}」請你選擇要讀取的檔案",.pickFolder=L"「{requester}」請你選擇一個要讀取的資料夾",
  .pickNote=L"此視窗由 Model Hotloader 代附加元件顯示。附加元件名稱由其指令碼自稱，未經驗證。附加元件可以任意使用你選擇的內容，包括傳送到伺服器或網站。",
  .pickOk=L"允許讀取",.allFiles=L"所有檔案",.enableAsk=L"允許附加元件要求讀取你的檔案嗎？",
  .enableText=L"附加元件將可以請你選擇檔案，或要求允許讀取某個檔案或資料夾。在你於此類視窗中回應之前，不會讀取任何內容。",.enableYes=L"開啟",.enableNo=L"保持關閉",
  .units={L"位元組",L"KB",L"MB",L"GB"}}},
};
const Text& textFor(const std::string& language){for(auto& [code,t]:texts)if(language==code)return t;return texts[0].second;}
std::wstring fill(std::wstring pattern,std::initializer_list<std::pair<std::wstring_view,std::wstring>> values){
 for(auto& [key,value]:values){std::wstring token=L"{"+std::wstring(key)+L"}";for(size_t at=pattern.find(token);at!=pattern.npos;at=pattern.find(token,at+value.size()))pattern.replace(at,token.size(),value);}
 return pattern;
}
std::wstring field(const Json& request,const char* key){auto it=request.find(key);return it!=request.end()&&it->is_string()?wide(it->get<std::string>()):std::wstring();}
std::wstring size(uint64_t bytes,const Text& t){
 if(bytes<1024)return std::to_wstring(bytes)+L" "+t.units[0];
 double value=double(bytes);int unit=0;while(value>=1024&&unit<3){value/=1024;unit++;}
 wchar_t buffer[32];swprintf_s(buffer,L"%.1f ",value);return buffer+std::wstring(t.units[unit]);
}
// No owner window: an owned dialog would disable the game window and leave it disabled
// if this process is killed. The game started this process, so it may take the foreground.
void raise(const std::wstring& title,std::atomic<bool>& shown,const std::function<void()>& raised){
 for(int i=0;i<60&&!shown.load();i++){if(HWND h=FindWindowExW(nullptr,nullptr,L"#32770",title.c_str())){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId()){ShowWindow(h,SW_SHOW);SetForegroundWindow(h);raised();return;}}Sleep(50);}
}
constexpr int AllowOnce=1001,AllowAlways=1002,Deny=1003;
// The allow buttons wake up a moment after the window appears, so a click or Enter
// meant for the game cannot answer it; Deny is the default button.
HRESULT CALLBACK dialogEvents(HWND window,UINT event,WPARAM wParam,LPARAM,LONG_PTR data){
 auto armed=reinterpret_cast<bool*>(data);
 if(event==TDN_CREATED){SetForegroundWindow(window);for(int id:{AllowOnce,AllowAlways})SendMessageW(window,TDM_ENABLE_BUTTON,id,FALSE);}
 if(event==TDN_TIMER&&!*armed&&wParam>=1200){*armed=true;for(int id:{AllowOnce,AllowAlways})SendMessageW(window,TDM_ENABLE_BUTTON,id,TRUE);}
 return S_OK;
}
using TaskDialogFunction=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
// Returns the pressed button; the worker's manifest selects Common Controls 6, which has
// TaskDialogIndirect. Without it a plain message box offers Allow once or Deny.
int ask(const std::wstring& instruction,const std::wstring& content,const std::wstring& footer,const std::vector<std::pair<int,std::wstring>>& choices,const Text& t){
 auto function=reinterpret_cast<TaskDialogFunction>(GetProcAddress(LoadLibraryW(L"comctl32.dll"),"TaskDialogIndirect"));
 if(!function){
  auto text=instruction+L"\n\n"+content+L"\n\n"+footer;
  if(choices.empty()){MessageBoxW(nullptr,text.c_str(),t.title,MB_OK|MB_ICONWARNING|MB_SETFOREGROUND);return Deny;}
  return MessageBoxW(nullptr,(text+L"\n\n"+choices.front().second.substr(0,choices.front().second.find(L'\n'))+L"?").c_str(),t.title,MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING|MB_SETFOREGROUND)==IDYES?choices.front().first:Deny;
 }
 std::vector<TASKDIALOG_BUTTON> buttons;for(auto& [id,label]:choices)buttons.push_back({id,label.c_str()});
 bool armed=false;TASKDIALOGCONFIG config{};config.cbSize=sizeof(config);
 config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_CALLBACK_TIMER|TDF_SIZE_TO_CONTENT|(buttons.empty()?0:TDF_USE_COMMAND_LINKS);
 config.pszWindowTitle=t.title;config.pszMainIcon=TD_WARNING_ICON;config.pszMainInstruction=instruction.c_str();config.pszContent=content.c_str();
 config.pszFooter=footer.c_str();config.pszFooterIcon=TD_INFORMATION_ICON;config.pButtons=buttons.data();config.cButtons=UINT(buttons.size());
 config.dwCommonButtons=buttons.empty()?TDCBF_CLOSE_BUTTON:0;config.nDefaultButton=buttons.empty()?IDCLOSE:Deny;
 config.pfCallback=dialogEvents;config.lpCallbackData=reinterpret_cast<LONG_PTR>(&armed);
 int pressed=Deny;if(FAILED(function(&config,&pressed,nullptr,nullptr)))return Deny;
 return pressed;
}
Json consent(const Json& request,const Text& t){
 if(request.value("kind",std::string())=="enable"){
  int pressed=ask(t.enableAsk,t.enableText,t.warning,{{AllowOnce,t.enableYes},{Deny,t.enableNo}},t);
  return {{"answer",pressed==AllowOnce?"yes":"no"}};
 }
 bool folder=request.value("folder",false);auto problem=request.value("problem",std::string());
 auto requester=field(request,"requester"),script=field(request,"script"),purpose=field(request,"purpose"),path=field(request,"path");
 std::wstring content=fill(t.addon,{{L"requester",requester}});
 if(!script.empty())content+=L"\n"+fill(t.script,{{L"script",script}});
 if(!purpose.empty())content+=L"\n"+fill(t.purpose,{{L"purpose",purpose}});
 // No size for a file that was not opened (a refused or missing one).
 std::wstring file=t.file;if(!request.contains("size"))for(auto cut:{L" ({size})",L"（{size}）"})if(auto at=file.find(cut);at!=file.npos)file.erase(at,wcslen(cut));
 content+=L"\n\n"+(folder?fill(t.folder,{{L"path",path}}):fill(file,{{L"path",path},{L"size",size(request.value("size",uint64_t(0)),t)}}));
 std::vector<std::pair<int,std::wstring>> choices;std::wstring footer=t.warning;
 if(!problem.empty()){
  const wchar_t* why=problem=="not_found"?t.notFound:problem=="link"?t.link:problem=="denied_location"?t.denied:problem=="not_a_file"?t.notFile:problem=="not_a_folder"?t.notFolder:problem=="network"||problem=="remote_drive"?t.network:t.unreadable;
  content+=L"\n\n"+std::wstring(why);
 }else{
  if(folder)content+=L"\n"+std::wstring(t.folderNote);
  choices.push_back({AllowOnce,std::wstring(t.once)+L"\n"+t.onceNote});
  if(request.value("remember",false))choices.push_back({AllowAlways,std::wstring(t.always)+L"\n"+fill(t.alwaysNote,{{L"folder",field(request,"rememberFolder")}})});
  else footer+=L"\n"+std::wstring(t.broad);
  choices.push_back({Deny,t.deny});
 }
 int pressed=ask(folder?t.askFolder:t.askFile,content,footer,choices,t);
 return {{"answer",problem.empty()&&pressed==AllowOnce?"once":problem.empty()&&pressed==AllowAlways&&choices.size()==3?"always":"deny"}};
}
Json pick(const Json& request,const Text& t){
 bool folder=request.value("folder",false),multiple=request.value("multiple",false);auto requester=field(request,"requester");
 auto title=fill(folder?t.pickFolder:multiple?t.pickFiles:t.pickFile,{{L"requester",requester}})+L" — Model Hotloader";
 auto purpose=field(request,"purpose");if(purpose.empty())purpose=field(request,"title");
 struct Com{Com(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);}~Com(){CoUninitialize();}} com;
 IFileOpenDialog* dialog=nullptr;
 if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))throw std::runtime_error("Cannot open the file picker");
 struct Release{IUnknown* p;~Release(){p->Release();}} release{dialog};
 // Patterns were checked natively before they got here (*.ext lists).
 std::vector<std::wstring> labels,patterns;
 for(auto& f:request.value("filters",Json::array()))if(f.is_array()&&f.size()==2&&f[0].is_string()&&f[1].is_string()){labels.push_back(wide(f[0].get<std::string>()));patterns.push_back(wide(f[1].get<std::string>()));}
 labels.push_back(t.allFiles);patterns.push_back(L"*.*");
 std::vector<COMDLG_FILTERSPEC> specs;for(size_t i=0;i<labels.size();i++)specs.push_back({labels[i].c_str(),patterns[i].c_str()});
 if(!folder)dialog->SetFileTypes(UINT(specs.size()),specs.data());
 FILEOPENDIALOGOPTIONS options=FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST)|(multiple?FOS_ALLOWMULTISELECT:0);
 dialog->SetOptions(options);dialog->SetTitle(title.c_str());dialog->SetOkButtonLabel(t.pickOk);
 // Its own remembered folder: an addon's picker does not open where models were imported from.
#ifndef MMDHL_FILE_ACCESS_TESTING
 static const GUID client={0x5b0e6c1d,0x3f4a,0x4d8e,{0x9a,0x61,0x2c,0x7e,0x14,0xb3,0x58,0xf2}};
#else
 // The test worker remembers nothing for the real picker and starts in the folder it is given.
 static const GUID client={0x5b0e6c1d,0x3f4a,0x4d8e,{0x9a,0x61,0x2c,0x7e,0x14,0xb3,0x58,0xf3}};
 if(auto start=field(request,"testFolder");!start.empty()){IShellItem* item=nullptr;if(SUCCEEDED(SHCreateItemFromParsingName(start.c_str(),nullptr,IID_PPV_ARGS(&item)))){dialog->SetFolder(item);item->Release();}}
#endif
 dialog->SetClientGuid(client);
 IShellItem* documents=nullptr;if(SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Documents,KF_FLAG_DEFAULT,nullptr,IID_PPV_ARGS(&documents)))){dialog->SetDefaultFolder(documents);documents->Release();}
 IFileDialogCustomize* custom=nullptr;
 if(SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&custom)))){
  // The dialog lays its texts out in narrow columns: the addon's words, then Model Hotloader's.
  // A script path has no spaces to wrap at: let it break after each folder.
  std::wstring said,script;for(auto c:field(request,"script")){script.push_back(c);if(c==L'/'||c==L'\\')script.push_back(0x200B);}
  if(!purpose.empty())said=fill(t.purpose,{{L"purpose",purpose}});
  if(!script.empty())said+=(said.empty()?L"":L"\n")+fill(t.script,{{L"script",script}});
  if(!said.empty())custom->AddText(1,said.c_str());
  custom->AddText(2,t.pickNote);custom->Release();
 }
 // Never shown unarmed (file_access_picker.hpp): a picker that would take an OK at once fails instead.
 PickerArming arming;auto cookie=armPicker(*dialog,arming);
 struct Unadvise{IFileDialog* dialog;DWORD cookie;~Unadvise(){if(cookie)dialog->Unadvise(cookie);}} unadvise{dialog,cookie};
 std::atomic<bool> shown{false};std::thread raising([&]{raise(title,shown,[&]{arming.restart();});});
 auto hr=dialog->Show(nullptr);shown=true;raising.join();
 if(FAILED(hr))return {{"answer","cancelled"}};
 IShellItemArray* results=nullptr;if(FAILED(dialog->GetResults(&results)))return {{"answer","cancelled"}};
 Json paths=Json::array();DWORD count=0;results->GetCount(&count);
 for(DWORD i=0;i<count&&i<64;i++){IShellItem* item=nullptr;if(SUCCEEDED(results->GetItemAt(i,&item))){PWSTR p=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){paths.push_back(utf8(p));CoTaskMemFree(p);}item->Release();}}
 results->Release();
 return {{"answer","selected"},{"paths",paths}};
}
#ifdef MMDHL_FILE_ACCESS_TESTING
// Test worker only (tests/file_access_worker.cpp, never packaged): the answer comes from
// MMDHL_FA_TEST_ANSWER instead of a window, so CTest runs without a desktop ("ui-pick"
// shows the real picker; tests ask for it only when MMDHL_FA_UI_TESTS is set).
std::wstring environment(const wchar_t* name){wchar_t buffer[4096]{};auto n=GetEnvironmentVariableW(name,buffer,4096);return n&&n<4096?std::wstring(buffer,n):std::wstring();}
void testLog(const Json& entry){
 if(auto log=environment(L"MMDHL_FA_TEST_LOG");!log.empty()){auto line=entry.dump()+"\n";if(HANDLE f=CreateFileW(log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);f!=INVALID_HANDLE_VALUE){DWORD n=0;WriteFile(f,line.data(),DWORD(line.size()),&n,nullptr);CloseHandle(f);}}
}
int testAnswer(bool picker,const fs::path& folder,const Json& request){
 testLog(request);
 auto answer=environment(L"MMDHL_FA_TEST_ANSWER");
 if(answer==L"crash")return 3;
 if(answer==L"wait"){Sleep(120000);return 0;}
 // The real picker in MMDHL_FA_TEST_FOLDER, answered by posted OK clicks: one at once, like a
 // stray Enter meant for the game, and one after its button woke up. The log notes whether
 // the window was still open after the first.
 if(picker&&answer==L"ui-pick"){
  auto shown=[]{HWND found=nullptr;EnumWindows([](HWND h,LPARAM out)->BOOL{DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t name[16]{};GetClassNameW(h,name,16);
   if(pid!=GetCurrentProcessId()||!IsWindowVisible(h)||std::wstring(name)!=L"#32770")return TRUE;*reinterpret_cast<HWND*>(out)=h;return FALSE;},reinterpret_cast<LPARAM>(&found));return found;};
  bool refusedAtOnce=false;
  std::thread clicker([&]{
   HWND window=nullptr;for(int i=0;i<500&&!window;i++){window=shown();if(!window)Sleep(10);}
   if(!window)return;
   PostMessageW(window,WM_COMMAND,IDOK,0);Sleep(500);refusedAtOnce=IsWindow(window)!=FALSE;
   Sleep(1200);PostMessageW(window,WM_COMMAND,IDOK,0);
   for(int i=0;i<300&&IsWindow(window);i++)Sleep(10);if(IsWindow(window))PostMessageW(window,WM_COMMAND,IDCANCEL,0);
  });
  auto shownRequest=request;shownRequest["testFolder"]=utf8(environment(L"MMDHL_FA_TEST_FOLDER"));
  auto result=pick(shownRequest,textFor("en"));clicker.join();
  testLog({{"uiPick",{{"refusedAtOnce",refusedAtOnce}}}});
  writeJson(folder/L"result.json",result);return 0;
 }
 Json result={{"answer",utf8(answer)}};
 if(picker&&answer.starts_with(L"pick:")){Json paths=Json::array();auto list=answer.substr(5);for(size_t start=0;start<=list.size();){auto end=list.find(L'|',start);if(end==list.npos)end=list.size();paths.push_back(utf8(list.substr(start,end-start)));start=end+1;}result={{"answer","selected"},{"paths",paths}};}
 writeJson(folder/L"result.json",result);return 0;
}
#endif
}
int fileAccessDialog(bool picker,const fs::path& folder){
 auto request=readJson(folder/L"request.json");
#ifdef MMDHL_FILE_ACCESS_TESTING
 return testAnswer(picker,folder,request);
#else
 auto& t=textFor(request.value("language",std::string("en")));
 writeJson(folder/L"result.json",picker?pick(request,t):consent(request,t));return 0;
#endif
}
}
