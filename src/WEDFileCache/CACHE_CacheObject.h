/* 
 * Copyright (c) 2016, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */

#ifndef CACHE_CACHEOBJECT_H
#define CACHE_CACHEOBJECT_H

#include "WED_FileCache.h"
#include "curl_http.h"
#include "RAII_Classes.h"
#include <time.h>

class CACHE_CacheObject
{
public:
	CACHE_CacheObject();
	~CACHE_CacheObject();

	const time_t get_cool_down_time() const;
	void         reset_cool_down_time();

	CACHE_domain get_domain() const;
	
	const std::string& get_disk_location() const;
	void   set_disk_location(const std::string& location);

	//Gets the last url associated with this CO, regardless of network/cache errors or RAII Handle status
	const std::string& get_last_url() const;

	CACHE_error_type get_last_error_type() const;
	void             set_last_error_type(CACHE_error_type error_type);

	time_t           get_last_time_modified() const;
	void             set_last_time_modified(time_t mtime);

	void             create_RAII_curl_hndl(const std::string& url, int buf_reserve_size=0);

	//Returns the current RAII_CurlHandle object or NULL if there is none
	RAII_CurlHandle* const get_RAII_curl_hndl();

	//Forces RAII_CurlHandle to close and be destroyed early
	void             close_RAII_curl_hndl();

	//We pass in the status because CACHE_CacheObject doesn't have one
	WED_file_cache_response get_response_from_object_state(CACHE_status status) const;

	//Trigger the cool down clock to reset and start counting down
	void trigger_cool_down();

	//Is the file cooling down
	int cool_down_seconds_left(const CACHE_domain_policy& policy) const;

	//Has the cache object grown stale over time?
	bool needs_refresh(const CACHE_domain_policy& policy) const;

private:
	friend class WED_FileCache;

	//When the cool down was triggered. Reset when object is destroyed and recreated.
	time_t m_cool_down_timestamp;

	CACHE_domain m_domain;

	//Real FQPN on disk, "" if non-existant. Given to WED_file_cache_response
	std::string m_disk_location;

	//The last error associated with this CACHE object. NOT given to WED_file_cache_response
	CACHE_error_type m_last_error_type;

	//The last HTTP url associated with this CACHE object, independent of a successful download. Given by WED_file_cache_request
	std::string m_last_url;
	
	//The last time the file was modified on disk
	time_t m_last_time_modified;

	//The curl_http_get_file that is associated with this cache_object
	//Deleted on curl_http_get_file being done (error or not) and WED_file_cache_shutdown
	RAII_CurlHandle* m_RAII_curl_hndl;

	CACHE_CacheObject(const CACHE_CacheObject& copy);
	CACHE_CacheObject& operator= (const CACHE_CacheObject& rhs);
	
	friend class CACHE_FileCacheInitializer;
	//Note on state lifespan: m_cool_down_timestamp, m_last_url, and m_last_error_type are only "reset" when object is deleted and recreated
};
#endif
