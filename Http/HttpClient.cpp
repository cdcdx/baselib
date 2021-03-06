#include "stdafx.h"
#include "HttpClient.h"
#include <algorithm>
namespace curl {

	static int dbg_trace(CURL *handle, curl_infotype type,char *data, size_t size,void *userp)
	{
		std::stringstream buf;
		switch (type) {
		case CURLINFO_TEXT:
			buf << data;
		default: /* in case a new one is introduced to shock us */
			return 0;
		case CURLINFO_HEADER_OUT:
			buf <<std::endl<<"=> Send header";
			break;
		case CURLINFO_DATA_OUT:
			buf <<std::endl<< "=> Send data";
			break;
		case CURLINFO_SSL_DATA_OUT:
			buf <<std::endl<< "=> Send SSL data";
			break;
		case CURLINFO_HEADER_IN:
			buf <<std::endl<< "<= Recv header";
			break;
		case CURLINFO_DATA_IN:
			buf <<std::endl<< "<= Recv data";
			break;
		case CURLINFO_SSL_DATA_IN:
			buf <<std::endl<< "<= Recv SSL data";
			break;
		}
		buf.write((char *)data, size);
		buf.flush();
		buf << "\r\n";
		::OutputDebugStringA(buf.str().c_str());
		CDebug *dbg = static_cast<CDebug*>(userp);	
		if(dbg)
			dbg->OnCurlDbgTrace(buf);

		return 0;
	}
	int StreamSeek(void *userp, curl_off_t offset, int origin) 
	{
		//if( offset==1 && origin== SEEK_SET)
		//	return CURL_SEEKFUNC_CANTSEEK;
		//std::stringstream *buf = static_cast<std::stringstream*>(userp);
		//buf->seekg(offset);
		return CURL_SEEKFUNC_CANTSEEK;
	}
	size_t StreamHeader(char *ptr, size_t size, size_t nmemb, void *userdata) 
	{
		std::stringstream *buf = static_cast<std::stringstream*>(userdata);
		if (size)
		{
			buf->write(ptr, size*nmemb);
			buf->flush();
		}
		if (buf->tellp() < 0)
			return 0;
		return (size*nmemb);
	}
	size_t StreamSave(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		std::stringstream *buf = static_cast<std::stringstream*>(userdata);
		if( size )
		{
			buf->write(ptr,size*nmemb);
			buf->flush();
		}
		if( buf->tellp() < 0 ) 
			return 0;
		return (size*nmemb);
	}
	size_t StreamSaveFile(char *ptr, size_t size, size_t nmemb, void *userdata)
	{
		FILE *buf = static_cast<FILE*>(userdata);
		size_t written = 0;
		if (size) {
			written = fwrite(ptr, size, nmemb, buf);
		}
		return (size*nmemb);
	}
	size_t StreamUpdate(void *ptr, size_t size, size_t nmemb, void *userdata)
	{
		std::stringstream *buf = static_cast<std::stringstream*>(userdata);
		std::string test = buf->str();
		buf->read((char*)ptr,size*nmemb);
		return buf->gcount();
	}
	void CHttpClient::GlobalSetup() 
	{
		curl_global_init(CURL_GLOBAL_ALL);
	}
	void CHttpClient::GlobalClean()
	{
		curl_global_cleanup();
	}
	long CHttpClient::PerformUrl(CURL* url)
	{
		CURLcode ret = curl_easy_perform(url);
		if (ret != CURLE_OK) {
			return ret;
		}
		long retcode = 0;
		ret = curl_easy_getinfo(url, CURLINFO_RESPONSE_CODE,&retcode);
		if (ret != CURLE_OK) {
			return ret;
		}
		return retcode;
	}
	CHttpClient::CHttpClient()
		:m_url(NULL)
		,m_headerlist(NULL)
		,m_postBoundary(NULL)
		,m_lastBoundary(NULL)
		,m_bWriteHeader(true)
		,m_Save2File(NULL)
		,m_bDecodeBody(false)
		,m_bEncodeUrl(true)
		,m_dbg(NULL)
	{
		bHttps = false;
		m_tmOut = 10000;
		m_tgProxy.nType = Proxy::NONE;
		m_url   = curl_easy_init();
	}
	void CHttpClient::SetDebug(CDebug *dbg)
	{
		if(m_dbg)
			return ;
		m_dbg = dbg;
	}
	void CHttpClient::SetContent(const std::string& data) 
	{
		m_rbuf << data;
		m_rbuf.flush();
	}
	const std::stringstream& CHttpClient::getContent() const {
		return m_rbuf;
	}
	CHttpClient::~CHttpClient()
	{
		ClearAll();
		if(m_dbg)
		{
			m_dbg->OnCurlDbgRelease();
			m_dbg = NULL;
		}
	}
	void	CHttpClient::ClearAll()
	{
		if( m_url )
		{
			curl_easy_cleanup(m_url);
			m_url = NULL;
		}
		if( m_headerlist )
		{
			curl_slist_free_all(m_headerlist);
			m_headerlist=NULL;
		}
		ClearBoundary();
		m_header.clear();
		m_params.clear();
	}
	void	CHttpClient::SetTimeout(long out)
	{
		m_tmOut = out;
	}
	void	CHttpClient::SetAgent(const CAtlString &val)
	{
		m_agent = val;
	}
	
	CURL*	CHttpClient::GetCURL()
	{
		return m_url;
	}
	void	CHttpClient::SetProxy(const Proxy &tgProxy)
	{
		m_tgProxy = tgProxy;
	}
	void	CHttpClient::ClearBoundary()
	{
		if( m_postBoundary)
		{
			curl_formfree(m_postBoundary);
			m_postBoundary = NULL;
		}
	}
	void	CHttpClient::EnableWriteHeader(bool b) 
	{
		m_bWriteHeader = b;
	}
	void	CHttpClient::AddBoundary(const std::string& szName, const std::string& szValue, ParamAttr dwParamAttr)
	{
		CURLFORMcode code = CURL_FORMADD_OK;
		if (dwParamAttr == ParamNormal)
		{
			code = curl_formadd(&m_postBoundary, &m_lastBoundary,
				CURLFORM_COPYNAME, szName.c_str(),
				CURLFORM_COPYCONTENTS, szValue.c_str(),
				CURLFORM_END);
		}
		else if (dwParamAttr == ParamFile)
		{
			code = curl_formadd(&m_postBoundary, &m_lastBoundary,
				CURLFORM_COPYNAME, szName.c_str(),
				CURLFORM_FILE, szValue.c_str(),
				CURLFORM_FILENAME, szName.c_str(),
				CURLFORM_END);
		}
		else if (dwParamAttr == ParamFileData) {
			code = curl_formadd(&m_postBoundary, &m_lastBoundary,
				CURLFORM_COPYNAME, "file",
				CURLFORM_FILENAME, szName.c_str(),
				CURLFORM_COPYCONTENTS, szValue.c_str(),
				CURLFORM_CONTENTSLENGTH,szValue.size(),
				CURLFORM_END);
		}
	}
	void	CHttpClient::AddBoundary(const CAtlString &szName,const CAtlString &szValue,ParamAttr dwParamAttr)
	{
		if( dwParamAttr==ParamNormal )
		{
			curl_formadd(&m_postBoundary,&m_lastBoundary,
						 CURLFORM_COPYNAME,(char*)CT2CA(szName),
						 CURLFORM_COPYCONTENTS,(char*)CT2CA(szValue),
						 CURLFORM_END);
		}
		else if( dwParamAttr==ParamFile )
		{
			std::string file;
			std::string name = (char*)CT2CA(szName, CP_UTF8);
			file = (char*)CT2CA(szValue, CP_UTF8);
			curl_formadd(&m_postBoundary, &m_lastBoundary,
				CURLFORM_COPYNAME, name.c_str(),
				CURLFORM_FILE, file.c_str(),
				CURLFORM_FILENAME, name.c_str(),
				CURLFORM_END);
		}
		else if (dwParamAttr == ParamFileData) {
			std::string val = (char*)CT2CA(szValue, CP_UTF8);
			std::string name = (char*)CT2CA(szName, CP_UTF8);
			curl_formadd(&m_postBoundary, &m_lastBoundary,
				CURLFORM_COPYNAME, "file",
				CURLFORM_FILENAME, name.c_str(),
				CURLFORM_COPYCONTENTS, val.c_str(),
				CURLFORM_END);
		}
	}
	void	CHttpClient::AddHeader(const CAtlString &szName, const CAtlString &szValue) {
		AddHeader((const char*)CT2CA(szName), (const char*)CT2CA(szValue));
	}
	void	CHttpClient::AddHeader(const std::string &szName, const std::string &szValue) {
		m_header.insert(std::make_pair(szName, szValue));
	}

	void	CHttpClient::AddParam(const CAtlString &szName, const CAtlString &szValue)
	{
		AddParam((const char*)CT2CA(szName),(const char*)CT2CA(szValue));
	}
	void	CHttpClient::AddParam(const std::string &szName, const std::string &szValue)
	{
		m_params.insert(std::make_pair(szName, szValue));
	}
	void	CHttpClient::SetEncodeUrl(bool e)
	{
		m_bEncodeUrl = e;
	}
	void	CHttpClient::BodySaveFile(FILE *f) 
	{
		m_Save2File = f;
	}
	void	CHttpClient::PerformParam(const std::string& url)
	{
		curl_easy_reset(m_url);
		m_wbuf.clear();	//清除操作标记，如 输入输出流指针
		m_wbuf.str("");
		m_wbuf.flush();

		m_rbuf.clear();	//清除操作标记，如 输入输出流指针

		m_headbuf.clear();
		m_headbuf.str("");
		{	//处理协议头
			if (m_headerlist)
			{
				curl_slist_free_all(m_headerlist);
				m_headerlist = NULL;
			}
			mapStrings::iterator itPos = m_header.begin();
			for (itPos; itPos != m_header.end(); itPos++)
			{
				std::string strVal;
				if (!itPos->first.empty() && !itPos->second.empty()) {
					strVal = itPos->first + ":" + itPos->second;
					m_headerlist = curl_slist_append(m_headerlist,strVal.c_str());
				}				
			}
		}
		{
			//char *ret = curl_easy_escape(m_url,strTmp,strTmp.GetLength());
			//strTmp = ret;
			//curl_free(ret);
			curl_easy_setopt(m_url, CURLOPT_URL, url.c_str());
		}
		if( std::string::npos!=url.find("https") )
		{
			bHttps = true;
			curl_easy_setopt(m_url,CURLOPT_SSL_VERIFYPEER,false);
			curl_easy_setopt(m_url,CURLOPT_SSL_VERIFYHOST,false);
		}
		else
		{
			bHttps = false;
		}
		curl_easy_setopt(m_url, CURLOPT_CONNECTTIMEOUT, 10);		//链接超时
		if (m_headerlist)
			curl_easy_setopt(m_url, CURLOPT_HTTPHEADER, m_headerlist);
		if(!m_agent.IsEmpty())
			curl_easy_setopt(m_url, CURLOPT_USERAGENT, (char*)CT2CA(m_agent, CP_UTF8));
		
		curl_easy_setopt(m_url, CURLOPT_TIMEOUT, m_tmOut / 100);	//超时单位秒
		curl_easy_setopt(m_url, CURLOPT_FOLLOWLOCATION, 1L);
		 
		curl_easy_setopt(m_url, CURLOPT_AUTOREFERER, 1L);
		curl_easy_setopt(m_url, CURLOPT_FORBID_REUSE, 1L);

		if (!m_cookie.empty()) {
			curl_easy_setopt(m_url, CURLOPT_COOKIE,m_cookie.c_str());
		}
		if (m_bDecodeBody) {
			curl_easy_setopt(m_url, CURLOPT_HTTP_CONTENT_DECODING,1L);
			curl_easy_setopt(m_url, CURLOPT_HTTP_TRANSFER_DECODING,1L);
		}
		else {
			curl_easy_setopt(m_url, CURLOPT_HTTP_CONTENT_DECODING, 0L);
			curl_easy_setopt(m_url, CURLOPT_HTTP_TRANSFER_DECODING,0L);
		}
		//curl_easy_setopt(m_url, CURLOPT_COOKIESESSION, 1L);
		//curl_easy_setopt(m_url, CURLOPT_COOKIEFILE, "curl.cookie");
		//curl_easy_setopt(m_url, CURLOPT_ACCEPT_ENCODING, "zh-CN,zh;q=0.8");
		//keep alive
		//curl_easy_setopt(m_url, CURLOPT_TCP_KEEPALIVE, 1L);
		//curl_easy_setopt(m_url, CURLOPT_TCP_KEEPIDLE, 120L);
		//curl_easy_setopt(m_url, CURLOPT_TCP_KEEPINTVL, 60L);

		if (m_Save2File)
		{
			curl_easy_setopt(m_url, CURLOPT_WRITEFUNCTION, StreamSaveFile);
			curl_easy_setopt(m_url, CURLOPT_WRITEDATA, (void*)m_Save2File);
		}
		else
		{
			curl_easy_setopt(m_url, CURLOPT_WRITEFUNCTION, StreamSave);
			curl_easy_setopt(m_url, CURLOPT_WRITEDATA, (void*)&m_wbuf);
		}
		if (m_rbuf.tellp()) {
			curl_off_t nLen = (curl_off_t)m_rbuf.tellp();
			curl_easy_setopt(m_url, CURLOPT_READFUNCTION, StreamUpdate);
			curl_easy_setopt(m_url, CURLOPT_READDATA, (void*)&m_rbuf);
		}
		if (m_tgProxy.nType != Proxy::NONE)
		{
			curl_easy_setopt(m_url, CURLOPT_PROXYTYPE, m_tgProxy.nType);
			curl_easy_setopt(m_url, CURLOPT_PROXYPORT, m_tgProxy.nPort);
			//curl_easy_setopt(m_url, CURLOPT_PROXY_SERVICE_NAME, (char*)CT2CA(m_tgProxy.strServer));
			curl_easy_setopt(m_url, CURLOPT_PROXYUSERNAME, (char*)CT2CA(m_tgProxy.strName));
			curl_easy_setopt(m_url, CURLOPT_PROXYUSERPWD, (char*)CT2CA(m_tgProxy.strPass));
		}
		if (m_bWriteHeader) {
			curl_easy_setopt(m_url, CURLOPT_HEADERDATA, (void*)&m_headbuf);
			curl_easy_setopt(m_url, CURLOPT_HEADERFUNCTION, StreamHeader);
		}
		//curl_easy_setopt(m_url, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
		if(m_dbg)
		{
			curl_easy_setopt(m_url, CURLOPT_VERBOSE, 1L);
			curl_easy_setopt(m_url, CURLOPT_DEBUGFUNCTION, dbg_trace);
			if(bHttps==false)
				curl_easy_setopt(m_url, CURLOPT_DEBUGDATA, (void*)m_dbg);
		}
#if defined(_DEBUG)
		curl_easy_setopt(m_url, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(m_url, CURLOPT_DEBUGFUNCTION, dbg_trace);
#endif
	}
	void	CHttpClient::EnableDecode(bool bDecode) 
	{
		m_bDecodeBody = bDecode;
	}
	void	CHttpClient::PerformParam(const CAtlString& url)
	{
		PerformParam((char*)CT2CA(url));		
	}
	std::string CHttpClient::RequestGet(const CAtlString& url,bool cHeader,bool cParam,bool perform)
	{
		return RequestGet((char*)CT2CA(url),perform);
	}
	std::string CHttpClient::RequestGet(const std::string& url,bool cHeader,bool cParam,bool perform)
	{
		pfmCode = CURL_LAST;
		if (m_url)
		{
			std::string rqFull(url);
			std::string rqParams = encodeParam();
			if(!rqParams.empty())
			{
				rqFull += "?";
				rqFull += rqParams;
			}
			PerformParam(rqFull);
			curl_easy_setopt(m_url, CURLOPT_POST, 0L);			
			curl_easy_setopt(m_url, CURLOPT_HTTPGET, 1L);
			curl_easy_setopt(m_url, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
			if(cParam)
				m_params.clear();
			if(cHeader)
				m_header.clear();
			if(perform)
				pfmCode = curl_easy_perform(m_url);
		}
		return GetStream();
	}

	std::string	CHttpClient::RequestPost(const CAtlString& url,bool cHeader,bool cParam,bool perform)
	{
		return RequestPost((char*)CT2CA(url),perform,cHeader,cParam);
	}
	std::string CHttpClient::RequestPost(const std::string& url,bool cHeader,bool cParam,bool perform) 
	{
		pfmCode = CURL_LAST;
		if (m_url)
		{
			std::stringstream dbgSS;

			PerformParam(url);
			curl_easy_setopt(m_url, CURLOPT_POST, 1L);
			curl_easy_setopt(m_url, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
			if(bHttps && m_dbg)
				dbgSS<<"url "<< url <<std::endl;
			if (!m_params.empty())
			{
				std::string encode = encodeParam();
				curl_easy_setopt(m_url, CURLOPT_POSTFIELDSIZE, encode.length());
				curl_easy_setopt(m_url, CURLOPT_COPYPOSTFIELDS,encode.c_str());
				if(bHttps && m_dbg)
					dbgSS<<"param "<< encode <<std::endl;
			}
			else if (m_rbuf.tellp())
			{
				curl_easy_setopt(m_url, CURLOPT_POSTFIELDSIZE, m_rbuf.tellp());
			}
			if (m_postBoundary)
			{
				curl_easy_setopt(m_url, CURLOPT_HTTPPOST, m_postBoundary);
			}
			if(cParam)
				m_params.clear();
			if(cHeader)
				m_header.clear();
			if(bHttps && m_dbg)
				m_dbg->OnCurlDbgTrace(dbgSS);
			if(perform)
				pfmCode = curl_easy_perform(m_url);
		}
		return GetStream();
	}
	long		CHttpClient::ReqeustCode()
	{
		if(m_url==NULL)
			return -1;
		long code = 0;
		if( CURLE_OK==curl_easy_getinfo(m_url, CURLINFO_RESPONSE_CODE, &code) )
		{
			if(code==0&&pfmCode)
				return pfmCode;
			return code;
		}
		return -1;
	}
	bool CHttpClient::IsResponseChunk()
	{
		bool chunked = false;
		char line[512];
		m_headbuf.seekp(0);
		m_headbuf.seekg(0);
		while(m_headbuf.getline(line,sizeof(line)))
		{
			if(0==strcmp(strlwr(line),"transfer-encoding: chunked\r"))
			{
				chunked = true;
				break;
			}
			else if(0==strcmp(strlwr(line),"headertransfer-encoding: chunked\r"))
			{
				chunked = true;
				break;
			}
		}
		return chunked;
	}
	//有些服务chuck长度和实际数据不一致
	void readChuckFromStream(std::stringstream &ss,std::string &out,long nlen)
	{
		char line[128];
		long npos = 0;	//块数
		long nys = 0;	//余数
		long buflen = 0;
		if(nlen<sizeof(line))
		{
			npos = 1;
			nys =0;
			buflen = nlen;
		}
		else
		{
			buflen = sizeof(line);
			npos = nlen/sizeof(line);
			nys = nlen-npos*sizeof(line);
		}
		out.clear();
		for(long n=0;n<npos;n++)
		{
			memset(line,0,sizeof(line));
			if(ss.read(line,buflen))
				out.append(line,buflen);
		}
		memset(line,0,sizeof(line));
		if(nys)
		{
			if(ss.read(line,nys))
				out.append(line,nys);
		}
	}
	//有些服务chuck长度和实际数据不一致
	void readChuckFromStream(std::stringstream &ss,std::string &out)
	{
		char crlf[2]={0,0};
		char chTmp=0;
		std::string chuck;
		while(ss.read(&chTmp,1))
		{
			if(chTmp=='\r' && ss.peek()=='\n')
			{
				//跳过末尾
				ss.seekg(1,std::ios::cur);
				break ;
			}
			chuck += chTmp;
		}
		out = chuck;
		return ;
	}
	std::vector<std::string> CHttpClient::GetChunks()
	{
		std::vector<std::string>  ret;
		if(!IsResponseChunk())
		{
			ret.push_back(GetStream());			
			return ret;
		}
		bool error = false;
		long nChuck = -1;
		char line[512];
		m_wbuf.seekp(0);
		m_wbuf.seekg(0);
		while(!m_wbuf.eof())
		{
			memset(line,0,sizeof(line));
			if( !m_wbuf.getline(line,sizeof(line)) )
			{
				error = true;
				break;
			}
			sscanf(line, "%x", &nChuck);
			if(nChuck==0)
				break;
			if(nChuck<0)
			{
				error = true;
				break;
			}
			std::string elem;
			readChuckFromStream(m_wbuf,elem);
			memset(line,0,sizeof(line));
			ret.push_back(elem);
			//读取两个字符判断当前chuck末尾
			line[0] = m_wbuf.peek();
			if(line[0]=='\r')
				m_wbuf.seekg(2,std::ios::cur);		
		}
		if(error)
		{
			ret.push_back(GetStream());			
			return ret;
		}
		return ret;
	}
	std::string CHttpClient::MakeChunks()
	{
		std::vector<std::string> vtSome;
		vtSome = GetChunks();
		if(vtSome.size()==1)
			return vtSome[0];
		std::string ret("");
		for(size_t n=0;n<vtSome.size();n++)
		{
			ret += vtSome[n];
		}
		return ret;
	}
	std::string	CHttpClient::GetStream()
	{
		std::string strRet("");
		if(bHttps && m_dbg)
		{
			std::stringstream trace;
			trace<<"code "<<ReqeustCode()<<" data "<< m_wbuf.str();
			trace.flush();
			m_dbg->OnCurlDbgTrace(trace);
		}
		if(200!=ReqeustCode())
			return strRet;	//非200返回空
		HandleCookie();
		if( m_wbuf.tellp() < 0 ) 
			return strRet;
		return m_wbuf.str();
	}
	std::string	CHttpClient::GetHeader()
	{
		std::string strRet("");
		if (m_headbuf.tellp() < 0)
			return strRet;
		return m_headbuf.str();
	}
	std::string CHttpClient::DecodeUrl(const std::string &v)
	{
		std::string ret;
		char *result = curl_easy_unescape(m_url, v.c_str(), v.length(),0);
		if (result) {
			ret = result;
			curl_free(result);
		}
		return ret;
	}
	std::string CHttpClient::EncodeUrl(const std::string &v) 
	{
		std::string ret;
		char *result = curl_easy_escape(m_url, v.c_str(), v.length());
		if (result) {
			ret = result;
			curl_free(result);
		}
		return ret;
	}
	void	CHttpClient::SetCookie(const std::string &val) {
		m_cookie = val;
	}
	const std::string& CHttpClient::GetCookie(){
		return m_cookie;
	}
	void CHttpClient::HandleCookie()
	{
		std::string setCookie("set-cookie");
		std::string cookie("cookie");
		std::string line;
		std::string tmp;
		m_headbuf.seekp(0);
		m_headbuf.seekg(0);
		while(std::getline(m_headbuf,line))
		{
			std::string t1 = line.substr(0,setCookie.length());
			std::string t2 = line.substr(0,cookie.length());
			if(t1.size())
				std::transform(t1.begin(), t1.end(), t1.begin(), ::tolower);
			if(t2.size())
				std::transform(t2.begin(), t2.end(), t2.begin(), ::tolower); 
			size_t startPos=0,endPos = 0;
			if(t1==setCookie){
				//跳过:号
				startPos = t1.length()+1;
				endPos = line.find(';');
			}
			else if(t2==cookie){
				startPos = t2.length()+1;
				endPos = line.find(';');
			}
			if(startPos && endPos){
				if(endPos!=-1)
					tmp = line.substr(startPos,endPos-startPos);
				else
					tmp = line.substr(startPos);
				m_cookie = tmp;
				break;
			}
			//transform(str.begin(), str.end(), str.begin(), ::toupper); //将小写的都转换成大写
		}		
	}
	std::string CHttpClient::encodeParam() 
	{
		std::string ret;
		mapStrings::iterator itPos = m_params.begin();
		for (itPos; itPos != m_params.end(); itPos++)
		{
			std::string strVal;
			if (!itPos->first.empty()) {
				char *key = NULL;
				char *val = NULL;
				if(m_bEncodeUrl)
				{
					key = curl_easy_escape(m_url,itPos->first.c_str(), itPos->first.length());
					if(!itPos->second.empty())
						val = curl_easy_escape(m_url,itPos->second.c_str(),itPos->second.length());
				}
				if (key) {
					ret.append(key);
					curl_free(key);
				}
				else
					ret.append(itPos->first);
				ret.append("=");
				if (val) {
					ret.append(val);
					curl_free(val);
				}
				else
					ret.append(itPos->second);
				ret += "&";
			}
		}
		if (ret.empty()|| m_url==NULL)
			return ret;
		ret.erase( ret.begin()+(ret.length()-1) ); //去掉最后的&
		return ret;
	}
}