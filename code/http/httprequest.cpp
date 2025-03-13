#include"httprequest.h"

using namespace std;


const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML=
{
    "/index","/register","/login",
    "/welcome","/vedio","/pucture",
};

const std::unordered_map<std::string,int> HttpRequest::DEFAULT_HTML_TAG=
{
    {"/register,html",0},{"/login.html",1},
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

void HttpRequest::ParseHeader_(const std::string& line)
{
    regex pattern("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line,subMatch,pattern))
        header_[subMatch[1]]=subMatch[2];
    else 
        state_=BODY;
}

void HttpRequest::ParseBody_(const std::string& line)
{
    body_=line;
    ParsePost_();
    state_=FINISH;
    LOG_DEBUG("Body:%s, len:%d",line.c_str(),line.size());
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
    if(method_=="POST"&&header_["Content-Type"]=="application/x-www-form-urlencoded")
    {
        ParseFromUrlencoded_();
        if(DEFAULT_HTML_TAG.count(path_))
        {
            int tag=DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d",tag);
            if(tag==0||tag==1)
            {
                bool isLogin=(tag==1);
                if(UserVerify(post_["username"],post_["password"],isLogin))
                    path_="/welcome.html";
                else
                    path_="/error.html";
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
    MYSQL* sql;
    SqlConnRAII(&sql,SqlConnPool::Instance());
    assert(sql);

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
    SqlConnPool::Instance()->FreeConn(sql);
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
}

bool HttpRequest::parse(Buffer& buff)
{
    const char CRLF[]="\r\n";
    if(buff.ReadableBytes()<=0) return 0;
    while(buff.ReadableBytes()&&state_!=FINISH)
    {
        const char* lineEnd=search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
        string line(buff.Peek(),lineEnd);
        switch(state_)
        {
            case REQUEST_LINE:
                if(!ParseRequestLine_(line))
                    return false;
                ParsePath_();
                break;    
            case HEADERS:
                ParseHeader_(line);
                if(buff.ReadableBytes()<=2)
                    state_=FINISH;
                break;
            case BODY:
                ParseBody_(line);
                break;
            default:
                break;
        }
        if(lineEnd==buff.BeginWrite()) break;
        buff.RetrieveUntil(lineEnd+2);
    }
    LOG_DEBUG("[%s], [%s], [%s]",method_.c_str(),path_.c_str(),version_.c_str());
    return 1;
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
    if(header_.count("Connection"))
        return header_.find("Connection")->second=="keep-alive"&&version_=="1.1";
    return 0;
}