#include"httpresponse.h"

#include <cerrno>
#include <linux/openat2.h>
#include <sys/syscall.h>

using namespace std;

const unordered_map<std::string,std::string> HttpResponse::SUFFIX_TYPE=
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

const unordered_map<int,std::string> HttpResponse::CODE_STATUS=
{
    {200,"OK"},
    {400,"Bad Request"},
    {403,"Forbidden"},
    {404,"Not Found"},
    {500, "Internal Server Error"},
};

const unordered_map<int,std::string> HttpResponse::CODE_PATH=
{
    {400,"/400.html"},
    {403,"/403.html"},
    {404,"/404.html"},
};

void HttpResponse::AddStateLine_(Buffer& buff)
{
    string status;
    if(CODE_STATUS.count(code_))
        status=CODE_STATUS.find(code_)->second;
    else
    {
        code_=400;
        status=CODE_STATUS.find(400)->second;
    }
    buff.Append("HTTP/1.1 "+to_string(code_)+" "+status+"\r\n");
}

void HttpResponse::AddHeader_(Buffer& buff)
{
    buff.Append("Connection: ");
    if(isKeepAlive_)
    {
        buff.Append("keep-alive\r\n");
    }
    else buff.Append("close\r\n");
    //暂时没有500的html页面
    const string type = code_ >= 400 && !mmFile_ ?
        "text/html; charset=utf-8" : GetFileType_();
    buff.Append("Content-type: " + type + "\r\n");
}

int HttpResponse::OpenFile_() const {
    if (path_.empty() || path_.front() != '/' ||
        path_.find('\0') != string::npos) {
        errno = EINVAL;
        return -1;
    }

    int rootFd = open(srcDir_.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (rootFd < 0) {
        return -1;
    }

    struct open_how how{};
    how.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS;

    //去掉URL开头的"/",相对于资源目录打开
    int fd = static_cast<int>(
        syscall(SYS_openat2, rootFd, path_.c_str() + 1, &how, sizeof(how))
    );

    //先保存错误码,避免后面close覆盖
    int saveErrno = errno;
    close(rootFd);
    if (fd < 0) {
        errno = saveErrno;
    }
    return fd;
}

bool HttpResponse::MapFile_(int srcFd) {
    if (mmFileStat_.st_size == 0) {
        close(srcFd);
        return true;
    }

    //文件映射到内存
    void* mmRet = mmap(nullptr, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    close(srcFd);
    if (mmRet == MAP_FAILED) { //使用MAP_FAILED来判断
        mmFile_ = nullptr;
        mmFileStat_ = {};
        code_ = 500;
        return false;
    }

    mmFile_ = static_cast<char*>(mmRet);
    return true;
}

void HttpResponse::ErrorHtml_()
{
    if (CODE_PATH.count(code_)) {
        path_ = CODE_PATH.find(code_)->second;
    }
}

std::string HttpResponse::GetFileType_()
{
    string::size_type idx=path_.find_last_of('.');
    if(idx==string::npos) return "text/plain";
    string suffix=path_.substr(idx);
    if(SUFFIX_TYPE.count(suffix))
        return SUFFIX_TYPE.find(suffix)->second;
    return "text/plain";
}

HttpResponse::HttpResponse()
{
    code_=-1;
    path_=srcDir_="";
    isKeepAlive_=0;
    mmFile_=nullptr;
    mmFileStat_={0};
}

HttpResponse::~HttpResponse() { UnmapFile();}

void HttpResponse::Init(const std::string& srcDir,std::string& path,bool isKeepAlive,int code)
{
    assert(srcDir!="");
    UnmapFile(); //释放旧映射
    code_=code;
    isKeepAlive_=isKeepAlive;
    path_=path;
    srcDir_=srcDir;
    mmFileStat_ = {};
}

void HttpResponse::MakeResponse(Buffer& buff)
{
    int srcFd =OpenFile_();
    if (srcFd < 0) {
        switch (errno) {
            case EXDEV: //尝试越出资源链接
            case ELOOP: //包含符号链接
            case EACCES:
            case EPERM:
                code_ = 403;
                break;
            case EINVAL:
                code_ = 400;
                break;
            default:
                code_ = 404;
                break;
        }
    } else if (fstat(srcFd, &mmFileStat_) < 0 ||
               !S_ISREG(mmFileStat_.st_mode)) {
        code_ = 404;
    } else if (!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    } else if (code_ == -1) {
        code_ = 200;
    }

    //错误页面也通过OpenFile_()打开,不直接拼路径
    if (CODE_PATH.count(code_)) {
        if (srcFd >= 0) {
            close(srcFd);
        }
        mmFileStat_ = {};
        ErrorHtml_();
        srcFd = OpenFile_();
        if (srcFd >= 0 &&
            (fstat(srcFd, &mmFileStat_) < 0 ||
            !S_ISREG(mmFileStat_.st_mode) ||
            !(mmFileStat_.st_mode & S_IROTH))) {
            close(srcFd);
            srcFd = -1;
        }
    }
    // 在输出状态行之前完成映射，确保状态码与实际响应一致。
    const bool fileReady = srcFd >= 0 && MapFile_(srcFd);
    if (!fileReady) {
        mmFile_ = nullptr;
        mmFileStat_ = {};
    }

    AddStateLine_(buff);
    AddHeader_(buff);
    if (fileReady) {
        buff.Append("Content-length: " + to_string(FileLen()) + "\r\n\r\n");
    } else {
        ErrorContent(buff, code_ == 500 ? "File mapping failed!" : "File unavailable!");
    }
}

void HttpResponse::UnmapFile()
{
    if(mmFile_)
    {
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_=nullptr;
    }
}

char* HttpResponse::File() { return mmFile_;}

size_t HttpResponse::FileLen() const { return mmFileStat_.st_size;}

void HttpResponse::ErrorContent(Buffer& buff,std::string message)
{
    string body;
    string status;
    body+="<html><title>Error</title>";
    body+="<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_))
        status=CODE_STATUS.find(code_)->second;
    else
        status="Bad Request";
    body+=to_string(code_)+" : "+status+"\n";
    body+="<p>"+message+"</p>";
    body+="<hr><em>MyTinyWebServer</em></body></html>";
    buff.Append("Content-length: "+to_string(body.size())+"\r\n\r\n");
    buff.Append(body);
}
