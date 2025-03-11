#include"httpresponse.h"

using namespace std;

const unordered_map<std::string,std::string> SUFFIX_TYPE=
{
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

const unordered_map<int,std::string> CODE_STATUS=
{
    {200,"OK"},
    {400,"Bad Request"},
    {403,"Forbidden"},
    {404,"Not Found"},
};

const unordered_map<int,std::string> CODE_PATH=
{
    {400,"/400.html"},
    {403,"/403.html"},
    {404,"/404.html"},
};

void HttpResponse::AddStateLine_(Buffer& buff){}

void HttpResponse::AddHeader_(Buffer& buff){}

void HttpResponse::AddContent_(Buffer &buff){}

void HttpResponse::ErrorHtml_(){}

std::string HttpResponse::GetFileType_(){}

HttpResponse::HttpResponse()
{
    
}
HttpResponse::~HttpResponse(){}
void HttpResponse::Init(const std::string& srcDir,std::string& path,bool isKeepAlive,int code){}
void HttpResponse::MakeResponse(Buffer& buff){}
void HttpResponse::UnmapFile(){}
char* HttpResponse::File(){}
size_t HttpResponse::FileLen() const{}
void HttpResponse::ErrorContent(Buffer& buff,std::string message){}