### HTTP请求类实现

    //根据HTTP请求的结构，使用状态机实现本类，
    用于逐步解析请求。
    //关于HTTP请求报文，请学习计算机网络相关内容！
    这里给一个实例：
行    GET /index.html HTTP/1.1    
头    Host: www.example.com
头    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36
头    Accept-Language: en-US,en;q=0.5
 
体    [空行]

    //GET方法请求体为空

## 枚举类型

    # PARSE_LINE #
    REQUEST_LINE:请求行
    HEADERS:请求头
    BODY:请求体
    FINISH:完成
    
    # HTTP_CODE #
    NO_REQUEST:没有请求,赋值为0后面依次增加
    GET_REQUEST:Get请求
    BAD_REQUEST:坏请求
    NO_RESOURCE:没有文件
    FORBIDDEN_REQUEST:拒绝的请求
    FILE_REQUEST:文件请求
    INTERNAL_ERROR:服务器错误
    CLOSED_CONNECT:断开连接

## 类内部私有成员和函数

    # ParseRequestLine_(const string& line)
    分析请求行。利用正则表达式分析请求行的模式，
    regex_match函数按照模式捕获line中内容到submatch中，
    成功则返回1,失败返回0.若成功则将内容分别存储到内部成员，
    state_设置为HEADERS(下一个状态)，函数返回1；失败则LOG_ERROR并
    返回0

    # ParseHeader_(connst string& line)
    分析请求行。方法基本同上，使用regex，smatch和
    regex_match获取请求行中内容，并存储到header_

    # ParseBody_(const string& line)
    分析请求体。调用ParsePost_()，将state_转为FINISH
    并将Body内容LOG_DEBUG

    # ParsePath_()
    分析GET请求中的路径(URL)，尝试从DEFAUL_HTML中找到
    对应的选项，添加后缀

    # ParsePost_()
    查看是否满足POST方法条件，开始分析。本程序POST只有登陆
    和注册两种，所以要满足header_["Content-Type"]=="application/x-www-form-urlencoded"。即“表单数据”。
    从中查找出TAG判断是登陆还是注册，调用UserVerify
    函数进行认证，并得到对应的html：欢迎or错误

    # ParseFromUrlencoded_()
    URL编码分析。使用双指针i，j分析请求体，“表单数据”的内容格式一般为
    “key1=value1&key2=value2”，由此逐步分析。其中‘%’为汉字按照
    UTF-8转换为字节流的形式，使用ConverHex函数转换16进制为10进制。
    ‘+‘无影响

    # UserVerify(const string& name,const string& pwd,bool isLogin)
    用户认证。创建sql句柄，使用SqlConnRAII类将连接池实例队列中的
    空闲连接取出。flag表示是否能够认证成功，order为mysql命令。
    先尝试select根据username在数据库中查找出username和password，
    若出错则返回0。否则将结果存在res中使用mysql_fetch_row捕获
    结果中的行将查询出的password存储到password字符串中。查看是否
    在登陆？是则比较pwd和password决定flag。不在登陆(注册)则表明
    用户名已存在。

    # state_        PARSE_STATE
    表明此时分析的状态

    # method_       string
    请求方法，本程序支持了GET和POST方法

    # path_         string
    请求头中的第二部分，经一系列解析后会变成rss文件夹
    中的文件路径内容

    # version_      string
    请求中的HTTP版本

    # body_         string
    请求体内容

    # header_       unordered_map<string,string>
    请求头中关于冒号':'两边的key&value对应关系，只有在POST
    方法时用到了，用于查看是否为“表单数据”

    # post_         unordered_map<string,string>
    若为POST请求，存储请求体内容中的key&value，用于更新数据库

    # DEFAULT_HTML  static const unordered_set<string>
    给浏览器准备的HTML页面名

    # DEFUALT_HTML_TAG  unordered_map<string,int>
    HTML文件对应的标签，主要用于判断为登陆还是注册

    # ConverHex(char ch)
    将16进制中的字母部分转换为十进制数字


## 类内部公有函数

    # HttpRequest()
    调用Init()函数

    # ~HttpRequest() = default
    默认析构函数

    # Init()
    将method_,path_,version_,body_,
    header_,post_置空，state_转为第一个状态

    # parse(Buffer& buff)
    逐步进行分析buff中的内容。根据Http报文结尾格式读取行。
    然后根据state_逐步调用对应函数进行分析行、头、体。读完
    buff将buff清空，退出循环，返回成功

    # path() / method() / version 
    返回对应的私有成员值

    # GetPost(string& key) / (const char* key)
    返回post_中key对应的的value

    # IsKeepAlive() const
    返回header_中是否有{"Connection","keep-alive"}
    (表明正在保持连接)，以及版本是否为1.1



### HTTP回应类实现

    //Http回应报文实例如下: 
行    HTTP/1.1 200 OK
头    Content-Type: text/html; charset=UTF-8
    Content-Length: 1234
    Server: Apache/2.4.1

体    <!DOCTYPE html>
    <html>
    <head>
        <title>示例页面</title>
    </head>
    <body>请求成功</body>
    </html>

## 类内部私有成员和函数

    # AddStateLine_(Buffer& buff)
    往buff中添加响应行。如果在COUDE_STATUS中找到code_，
    设置好对应的状态，否则code_=400，表示Bad Request，
    将状态行格式内容放入buff中

    # AddHeader_(Buffer& buff)
    添加响应头。本程序只要最简单的响应头，先添加Connection,
    再添加Content-type，还有一个Content-length在下个函数
    中添加

    # AddContent_(Buffer& buff)
    添加响应体。打开对应的文件描述符(只读)，打开失败调用
    ErrorContent函数；成功则将文件放到到内存映射区，利用
    共享内存提高进程间通信效率。若映射失败，调用ErrorContent
    生成错误Html文件作为响应体内容。设置mmFile_为函数返回值，
    即映射区首地址。关闭文件描述符，buff中添加Content-length
    内容

    # ErrorHtml_()
    根据code_状态码，得到对应的html文件，使用stat函数
    将文件信息放入mmFileStat_结构中

    # GetFileType_()
    获取文件类型，根据文件后缀，在SUFFIX_TYPE中寻找对应
    文件的Content-type，找不到默认为文本文件

    # code_         int
    响应状态码

    # isKeepAlive_  bool
    是否保持连接状态

    # path_         string
    文件路径名称

    # srcDir_       string
    源文件夹路径

    # mmFile        char*
    文件在映射区的首地址

    # mmFileStat    stat
    文件结构，保存了文件的各类信息

    # SUFFIX_TYPE   unordered_map<string,string>
    后缀类型在响应头中的Content type

    # CODE_STATUS   unordered_map<int,string>
    响应状态码对应的意义

    # CODE_PATH     unordered_map<int,string>
    各响应状态码的文件名称


## 类内部公有函数

    # HttpResponse()
    初始化状态码-1,字符串都为空，isKeepAlive_为0
    表示尚未连接，mmFile_和mmFileStat_分别为空指针
    和0

    # ~HttpResponse()
    调用UnmapFile()函数

    # Init(const string& srcDir,string& path,bool isKeepAlive,int code)
    初始化函数，把code_,isKeepAlive_,path_,srcDir_按照
    函数参数初始化，mmFile_和mmFileStat_仍为空

    # MakeResponse(Buffer& buff)
    制作响应报文到buff中。先使用stat函数将文件信息存入
    mmFileStat_中，若失败或者非文件：404,若不可读取：
    403Forbidden,若code=-1，表示刚初始化，200OK。调用
    ErrorHtml_函数根据code_选择文件，最后依次添加行，
    头，体

    # UnmapFile()
    取消内存映射，将mmFile_置空

    # File() 
    返回mmFile_的值，即文件映射开头地址

    # FileLen()
    返回文件长度

    # ErrorContent(Buffer& buff,string message)
    生成响应体内容，并添加到buff中

    # Code() const 
    返回code_值



### 使用上面两类实现HTTP连接类


## 类内部私有成员和函数

    # fd_           int
    连接描述符

    # addr_         sockaddr_in
    与客户端绑定的addr地址

    # isClose_      bool
    是否关闭标志

    # iovCnt_       int
    iov数组的数目

    # iov_[2]       iovec
    iovec数组，iov_[0]存放writeBuff_内容，
    iov_[1]存放文件内容，一起往fd_中写

    # readBuff_     Buffer
    读缓冲区，读取请求报文request_

    # writeBuff_    Buffer
    写缓冲区,存放响应报文的行、头内容，被iov_[0]捕获

    # request_      HttpRequest
    请求类对象

    # response_     HttpResponse
    回应类对象


## 类内部公有函数

    # HttpConn()
    初始化fd_为-1,addr_为0,isClose_为1

    # ~HttpConn()
    调用Close()函数

    # Init(int sockFd,const sockaddr_in& addr)
    初始化函数。userCnt++客户数目加一，根据函数各个参数
    初始化类内部成员，读写缓冲区全部清空，isClose_为0表示
    开启

    # read(int* saveErrno)
    从fd_中读取到readBuff_,若为ET边缘触发模式则保持循环

    # write(int* saveErrno)
    从iov_向fd_中写入数据。当写完或者发生错误时停止。若总长度大于
    单个iov_[0]的长度，说明iov_[0]中内容已经写完，还写了部分iov_[1]
    中的内容，iov_[1]偏移，因为已经写完iov_[0]内容，所以可以清空
    writeBuff_；若总长度小于iov_[0]长度，iov_[0]偏移，并且移动
    writeBuff_中readPos_.满足循环条件为isET或iov_中数据量大于10KB
    返回len长度，即最后一次写的长度

    # Close()
    调用response_.UnmapFile()函数取消内存映射，isCLose_
    标志状态置1,减少UserCnt，关闭fd_

    # GetFd() const
    返回fd_

    # GerPort() const
    返回addr_.sin_port

    # GetIP() const
    返回inet_ntoa(addr_.sin_addr)
    (网络端转主机端)

    # GetAddr() const
    返回addr_

    # process()
    整体运作流程函数。先初始化request_，查看readBuff_
    尝试解析readBUff_中的Http请求报文，若解析成功则初始化
    response_为200OK,否则为400Bad request.调用MakeResponse
    函数向writeBuff_中写响应报文。使用iov_[0]存放写入的内容，
    iov_[1]存放内存映射的文件内容。返回流程运作成功

    # ToWriteBytes()
    返回iov_[0]和iov_[1]的长度和，即将要写入的响应报文长度

    # IsKeepAlive()
    返回request_.IsKeepAlive()

    # isET      static 
    是否边缘触发标志

    # srcDir    static const char*
    本地源文件夹路径

    # userCnt   atomic<int>
    客户数量，原子性