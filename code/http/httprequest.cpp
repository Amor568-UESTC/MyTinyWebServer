#include"httprequest.h"

#include <charconv>

using namespace std;

namespace {
string ToLowerAscii(string txt) {
    for (char& c : txt) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return txt;
}

//去掉字符串两端的空格和制表符
string TrimOWS(const string& txt) {
    const auto fst = txt.find_first_not_of(" \t");
    if (fst == string::npos) {
        return {};
    }
    const auto lst = txt.find_last_not_of(" \t");
    return txt.substr(fst, lst - fst + 1);
}

//检查Connection中是否包含某个完整选项
//如Connection: Upgrade, Keep-Alive
bool HasConnectionToken(const string& val, const string& wanted) {
    size_t begin = 0;
    while (begin < val.size()) {
        const auto comma = val.find(',', begin);
        const auto token = ToLowerAscii(TrimOWS(val.substr(
            begin, comma == string::npos ? string::npos : comma - begin)));
        if (token == wanted) {
            return true;
        }
        if (comma == string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return false;
}
}

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML=
{
    "/index","/register","/login",
    "/welcome","/video","/picture",
};

const std::unordered_map<std::string,int> HttpRequest::DEFAULT_HTML_TAG=
{
    {"/register.html",0},{"/login.html",1},
};

bool HttpRequest::ParseRequestLine_(const std::string& line)
{
    regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line,subMatch,pattern))
    {
        method_=subMatch[1];
        path_=subMatch[2];
        version_=subMatch[3];
        state_=HEADERS;
        return 1;
    }
    LOG_ERROR("RequestLine Error");
    return 0;
}

bool HttpRequest::ParseHeader_(const std::string& line)
{
    static const regex pattern(R"(^([!#$%&'*+\-.^_\x60|~0-9A-Za-z]+):[ \t]*(.*)$)");
    smatch subMatch;
    if (!regex_match(line, subMatch, pattern)) {
        return false;
    }

    string key = ToLowerAscii(subMatch[1].str());
    string val = TrimOWS(subMatch[2].str());
    for (unsigned char c : val) {
        //c小于普通空格0x20只能为\t,拒绝DEL控制符0x7f
        if ((c < 0x20 && c != '\t') || c == 0x7f) {
            return false;
        }
    }

    //重复Content-Length一律拒绝
    if (key == "content-length" && header_.contains(key)) {
        return false;
    }
    if (key == "connection" && header_.contains(key)) {
        header_[key] += "," + val;
    } else {
        header_[key] = val;
    }
    return true;
}

bool HttpRequest::FinishHeadrs_() {
    //当前暂不支持Transfer-Encoding,明确拒绝
    if (header_.count("transfer-encoding")) {
        return false;
    }

    contentLength_ = 0;
    auto it = header_.find("content-length");
    if (it == header_.end()) {
        return true;
    }

    const string& text = it->second;
    const char* begin = text.data();
    const char* end = begin + text.size();
    //使用from_chars解析content-length
    auto result = from_chars(begin, end, contentLength_);
    return result.ec == errc{} && result.ptr == end;
}


void HttpRequest::ParseBody_(const std::string& line)
{
    body_=line;
    ParsePost_();
    state_=FINISH;
    LOG_DEBUG("Body:%s, len:%zu",line.c_str(),line.size());
}

void HttpRequest::ParsePath_()
{
    if(path_=="/") path_="/index.html";
    else 
        for(auto& item:DEFAULT_HTML)
            if(item==path_)
            {
                path_+=".html";
                break;
            }
}

void HttpRequest::ParsePost_()
{
    const auto type = header_.find("content-type");
    if (method_ == "POST" && type != header_.end() &&
        ToLowerAscii(TrimOWS(type->second.substr(0, type->second.find(';')))) ==
        "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_();
        if (DEFAULT_HTML_TAG.contains(path_)) {
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag: %d", tag);
            if (tag == 0 || tag == 1) {
                bool isLogin = (tag == 1);
                if (UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                } else {
                    path_ = "/error.html";
                }
            }
        }
    }
}

void HttpRequest::ParseFromUrlencoded_()
{
    if(body_.size()==0) return ;
    string key,val;
    int num=0;
    int n=body_.size();
    int i=0,j=0;
    for(;i<n;i++)
    {
        char ch=body_[i];
        switch(ch)
        {
            case '=':
                key=body_.substr(j,i-j);
                j=i+1;
                break;
            case '+':
                body_[i]=' ';
                break;
            case '%':
                num = ConverHex(body_[i+1])*16+ConverHex(body_[i+2]);
                body_[i+2]=num%10+'0';
                body_[i+1]=num/10+'0';
                i+=2;
                break;
            case '&':
                val=body_.substr(j,i-j);
                j=i+1;
                post_[key]=val;
                LOG_DEBUG("%s=%s",key.c_str(),val.c_str());
                break;
            default:
                break;
        }
    }
    assert(j<=i);
    if(!post_.count(key)&&j<i)
    {
        val=body_.substr(j,i-j);
        post_[key]=val;
    }
}

bool HttpRequest::UserVerify(const std::string& name,const std::string& pwd,bool isLogin)
{
    if(name==""||pwd=="") return 0;
    LOG_INFO("Verify name:%s pwd:%s",name.c_str(),pwd.c_str());
    //获取不到连接就直接返回失败
    MYSQL* sql = nullptr;
    SqlConnRAII sqlGuard(&sql, SqlConnPool::Instance());
    if (sql == nullptr) {
        LOG_ERROR("No database connection available");
        return false;
    }

    bool flag=0;
    char order[256]={0};
    MYSQL_RES* res=nullptr;

    if(!isLogin) flag=1;
    snprintf(order,256,"select username,password from user where username='%s' limit 1",name.c_str());
    LOG_DEBUG("%s",order);

    if(mysql_query(sql,order))
    {
        mysql_free_result(res);
        return 0;
    }
    res=mysql_store_result(sql);


    while(MYSQL_ROW row=mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL ROW: %s %s",row[0],row[1]);
        string password(row[1]);
        if(isLogin)
        {
            if(pwd==password) flag=1;
            else
            {
                flag=0;
                LOG_DEBUG("pwd error!");
            }
        }
        else 
        {
            flag=0;
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    if(!isLogin&&flag==1)
    {
        LOG_DEBUG("register!");
        bzero(order,256);
        snprintf(order,256,"insert into user(username,password) values('%s','%s')",name.c_str(),pwd.c_str());
        LOG_DEBUG("%s",order);
        if(mysql_query(sql,order))
        {
            LOG_DEBUG("Insert error!");
            flag=0;
        }
        flag=1;
    }
    LOG_DEBUG("UserVerify success!!");
    return flag;
}

int HttpRequest::ConverHex(char ch)
{
    if(ch>='A'&&ch<='F') return ch-'A'+10;
    if(ch>='a'&&ch<='f') return ch-'a'+10;
    return ch;
}

void HttpRequest::Init()
{
    method_=path_=version_=body_="";
    state_=REQUEST_LINE;
    header_.clear();
    post_.clear();
    contentLength_ = 0;
}

HttpRequest::ParseRes HttpRequest::parse(Buffer& buff)
{
    const char* CRLF = "\r\n";
    using res = HttpRequest::ParseRes;
    
    while (state_ != FINISH) {
        //先解析BODY
        if (state_ == BODY) {
            if (buff.ReadableBytes() < contentLength_) {
                return res::NeedMore;
            }

            string body(buff.Peek(), contentLength_);
            buff.Retrieve(contentLength_);
            ParseBody_(body);
            return res::Complete;
        }
        //请求行和请求头按行解析
        const char* end = buff.BeginWriteConst();
        const char* lineEnd = search(buff.Peek(), end, CRLF, CRLF + 2);
        if (lineEnd == end) { //不以CRLF结尾
            return res::NeedMore;
        }
        string line(buff.Peek(), lineEnd);
        buff.RetrieveUntil(lineEnd + 2);

        if (state_ == REQUEST_LINE) {
            if (!ParseRequestLine_(line)) {
                return res::BadRequest;
            }
            ParsePath_();
        } else if (state_ == HEADERS) {
            if (line.empty()) {
                if (!FinishHeadrs_()) {
                    return res::BadRequest;
                }
                state_ = BODY;
            } else if (!ParseHeader_(line)) {
                return res::BadRequest;
            }
        }
    }
    
    LOG_DEBUG("[%s], [%s], [%s]",method_.c_str(),path_.c_str(),version_.c_str());
    return res::Complete;
}

std::string HttpRequest::path() const { return path_;}

std::string& HttpRequest::path() { return path_;}

std::string HttpRequest::method() const { return method_;}

std::string HttpRequest::version() const { return version_;}

std::string HttpRequest::GetPost(const std::string& key) const 
{
    assert(!key.empty());
    if(post_.count(key))
        return post_.find(key)->second;
    return "";
}

std::string HttpRequest::GetPost(const char* key) const
{
    assert(key!=nullptr);
    if(post_.count(key))
        return post_.find(key)->second;
    return "";
}

bool HttpRequest::IsKeepAlive() const
{
    const auto it = header_.find("connection");
    return version_ == "1.1" && it != header_.end() &&
        HasConnectionToken(it->second, "keep-alive") &&
        !HasConnectionToken(it->second, "close");
}
