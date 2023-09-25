/*
 *  curl_http.cpp
 *  design
 *
 *  Created by bsupnik on 5/21/13.
 *  Copyright 2013 Laminar Research. All rights reserved.
 *
 */

#include "XDefs.h"
#include "curl_http.h"

#if HAS_GATEWAY

#include "curl/curl.h"
#include "AssertUtils.h"
#if !IBM
#include <pthread.h>
#endif
#include <errno.h>

int atomic_load(volatile int * a) { return *a; }
void atomic_store(volatile int * a, int v) { *a = v; }




const time_t TIMEOUT_SEC = (30);

void	UTL_http_encode_url(std::string& io_url)
{
	std::string::size_type p;
	while((p=io_url.find(' ')) != io_url.npos)
		io_url.replace(p,1,"%20");
}


curl_http_get_file::curl_http_get_file(
							const std::string&			inURL,
							const std::string&			outDestFile) :
	m_progress(-1),
	m_status(in_progress),
	m_halt(0),
	m_dest_path(outDestFile),
	m_url(inURL),
	m_dest_buffer(NULL),
	m_errcode(0),
	m_last_dl_amount(0.0)
{
	UTL_http_encode_url(m_url);

	DebugAssert(inURL.size() > 7);
	DebugAssert(
		strncmp(inURL.c_str(),"http://",7) == 0 ||
		strncmp(inURL.c_str(),"https://",8) == 0);
	
	#if IBM
	m_thread = CreateThread(NULL,0,thread_proc,this,0,NULL);
	#else
	pthread_create(&m_thread, NULL, thread_proc, this);
	#endif

}

curl_http_get_file::curl_http_get_file(
							const std::string&			inURL,
							std::vector<char>*		outDestBuffer) :
	m_progress(-1),
	m_status(in_progress),
	m_halt(0),
	m_url(inURL),
	m_dest_buffer(outDestBuffer),
	m_errcode(0),
	m_last_dl_amount(0.0)
{
	UTL_http_encode_url(m_url);

	DebugAssert(inURL.size() > 7);
	DebugAssert(
		strncmp(inURL.c_str(),"http://",7) == 0 ||
		strncmp(inURL.c_str(),"https://",8) == 0);
	
	#if IBM
	m_thread = CreateThread(NULL,0,thread_proc,this,0,NULL);
	#else
	pthread_create(&m_thread, NULL, thread_proc, this);
	#endif

}
curl_http_get_file::curl_http_get_file(
							const std::string&			inURL,
							const std::string *			post_data,
							const std::string *			put_data,
							std::vector<char>*			outBuffer) :
	m_progress(-1),
	m_status(in_progress),
	m_halt(0),
	m_url(inURL),
	m_post(post_data ? *post_data : std::string()),
	m_put(put_data ? *put_data : std::string()),
	m_dest_buffer(outBuffer)
{
	UTL_http_encode_url(m_url);

	DebugAssert(inURL.size() > 7);
	DebugAssert(
		strncmp(inURL.c_str(),"http://",7) == 0 ||
		strncmp(inURL.c_str(),"https://",8) == 0);
	 
	#if IBM
	m_thread = CreateThread(NULL,0,thread_proc,this,0,NULL);
	#else
	pthread_create(&m_thread, NULL, thread_proc, this);
	#endif
}

				
curl_http_get_file::~curl_http_get_file()
{
	atomic_store(&m_halt,1);

	#if IBM
	WaitForSingleObject(m_thread,INFINITE);
	CloseHandle(m_thread);
	#else
	void * ret;
	pthread_join(m_thread, &ret);
	#endif
}
	
float		curl_http_get_file::get_progress(void)
{
	return m_progress;
}

bool	curl_http_get_file::is_done(void)
{
	return atomic_load(&m_status) != in_progress;
}

bool	curl_http_get_file::is_ok(void)
{
	DebugAssert(m_status != in_progress);
	return atomic_load(&m_status) == done_OK;
}

bool	curl_http_get_file::is_net_fail(void)
{
	DebugAssert(m_status == done_error);
	return UTL_http_is_error_bad_net(m_errcode);
}

int	curl_http_get_file::get_error(void)
{
	DebugAssert(m_status == done_error);
	return m_errcode;
}

void	curl_http_get_file::get_error_data(std::vector<char>& out_data)
{
	DebugAssert(m_status == done_error);
	swap(out_data, m_dl_buffer);
}

const std::string&	curl_http_get_file::get_url() const
{
	return m_url;
}

size_t		curl_http_get_file::read_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	curl_http_get_file * me = (curl_http_get_file *) userp;
	size_t want = size * nmemb;
	size_t ret = min(want,me->m_put.size());
	
	memcpy(contents, &me->m_put[0], ret);
	
	me->m_put.erase(me->m_put.begin(), me->m_put.begin()+ret);
	
	return ret / size;
}
		
size_t		curl_http_get_file::write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	curl_http_get_file * me = (curl_http_get_file *) userp;

	size_t bytes = nmemb * size;
	char * char_data = (char *) contents;

	// This is a relatively inefficeint way to realloc our std::vector - the STL std::vector will typically
	// use log growht (e.g. double the std::vector each time).  But our progress function will also 
	// reserve capacity.  Typically this gets hit once because the start of the data comes in the same
	// burst as the end of headers, so CURL asks us to save this first, THEN issues a prog report.
	me->m_dl_buffer.insert(me->m_dl_buffer.end(), char_data, char_data + bytes);
	me->m_last_data_time = time(NULL);
	
	return bytes;
}

int			curl_http_get_file::progress_cb(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded)
{
	curl_http_get_file * me = (curl_http_get_file *) ptr;

	if(atomic_load(&me->m_halt))
		return 1;

	time_t now = time(NULL);

	if(NowDownloaded > me->m_last_dl_amount)
	{
		me->m_last_dl_amount = NowDownloaded;
		me->m_last_data_time = now;
	}
	else if(now - me->m_last_data_time > TIMEOUT_SEC)
	{
		return 1;
	}
	if(TotalToDownload > 0.0)
	{
		// If we have a total, reserve AT LEAST that much to avoid thrashing std::vector needlessly.
		size_t bytes = TotalToDownload;
		if(me->m_dl_buffer.capacity() < bytes)
			me->m_dl_buffer.reserve(bytes);
	
		double prog = NowDownloaded * 100.0 / TotalToDownload;
		int intp = prog;
		me->m_progress = intp;
	}	
	else   // gzipped transfers dont know the total transfer size ahead of time, so we report in kB, as a *negative* number
		me->m_progress = (int) -NowDownloaded / 1024.0;

	return 0;
}

#if IBM
DWORD WINAPI
#else
void *
#endif
curl_http_get_file::thread_proc(void * param)
{
	curl_http_get_file * me = (curl_http_get_file *) param;

	struct  curl_slist * chunk = NULL;	

	me->m_last_data_time = time(NULL);
	
	CURL *	curl = curl_easy_init();

	curl_easy_setopt(curl, CURLOPT_URL, me->m_url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);	// Required because we do a redirect to protect against URL/Server changes breaking URLs
	curl_easy_setopt(curl, CURLOPT_REFERER, "https://developer.x-plane.com/tools/worldeditor/");

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, param);
		
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_cb);	
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, param);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // empty std::string is expanded into all methods supported by this version of curl.
#if WED
	LOG_MSG("I/CURL setting up download\n");
	fflush(gLogFile);
	curl_easy_setopt(curl, CURLOPT_STDERR, gLogFile);
#include "WED_Version.h"
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "WorldEditor/" WED_VERSION_STRING_SHORT );  // OSM tile server requires a referer std::string
#endif
#if 1 // DEV	
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif	
//	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 60.0);

	if(!me->m_post.empty())
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, me->m_post.c_str());
	
	if(!me->m_put.empty())
	{
		chunk = curl_slist_append(chunk, "Content-Type: application/json");
		
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);	
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_cb);
		curl_easy_setopt(curl, CURLOPT_READDATA, param);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE, me->m_put.size());
	}
	
	CURLcode res = curl_easy_perform(curl);

#if WED
	LOG_MSG("I/CURL perform() done, CURLcode %d\n", res);
	fflush(gLogFile);
#endif
	
	// A note on thread safety: we need to ensure that writes to memory of our error code or data go out BEFORE
	// we flip the bit to say we are done.  So we use
	// A bit of a hack: if the callback kills us, just call it a time-out - it either means the host thread lost 
	// patience or we timed out.
	if(res == CURLE_ABORTED_BY_CALLBACK)
		res = CURLE_OPERATION_TIMEDOUT;

	if(res != CURLE_OK)
	{
		me->m_errcode = res;
		atomic_store(&me->m_status, done_error);
	}
	else
	{
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		
		if(http_code != 200)
		{
			DebugAssert(http_code != 0);
			
			me->m_errcode = http_code;
			atomic_store(&me->m_status, done_error);
		}
		else
		{
			if(me->m_dest_buffer)
			{
				me->m_dest_buffer->swap(me->m_dl_buffer);
				atomic_store(&me->m_status, done_OK);
			}
			else
			{
				FILE * fi = fopen(me->m_dest_path.c_str(),"wb");
				if(fi == NULL)
				{
					me->m_errcode = errno;
				} 
				else
				{
					size_t ws = fwrite(&me->m_dl_buffer[0], 0, me->m_dl_buffer.size(), fi);
					if(ws != me->m_dl_buffer.size())
					{
						me->m_errcode = ferror(fi);
					}
					fclose(fi);
				}
				atomic_store(&me->m_status, me->m_errcode == 0 ? done_OK : done_error);
			
			}
		}
	}

	/* always cleanup */ 
	if(chunk)
		curl_slist_free_all(chunk);
	curl_easy_cleanup(curl);

	return NULL;
}

bool	UTL_http_is_error_bad_net(int err)
{
	// This is a short list of things that might indicate a net connectivity problem.
	if(err == CURLE_OPERATION_TIMEDOUT)		return true;
	if(err == CURLE_PARTIAL_FILE)			return true;	// We get these if we don't get a complete transfer - maybe someone hung up
	if(err == CURLE_GOT_NOTHING)			return true;	// due to flakey internet?
	if(err == CURLE_COULDNT_RESOLVE_PROXY)	return true;	// Maybe we can't even connect due to this
	if(err == CURLE_COULDNT_RESOLVE_HOST)	return true;	// range of problems...maybe no net route to server.
	if(err == CURLE_COULDNT_CONNECT)		return true;
											return false;
}

#endif /* HAS_GATEWAY */
