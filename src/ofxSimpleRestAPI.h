/*-----------------------------------------------------------/
ofxSimpleRestAPI.h

github.com/azuremous
Created by Jung un Kim a.k.a azuremous on 2/12/20.
/----------------------------------------------------------*/

#pragma once

#include <curl/curl.h>
#include "hmac.h"
#include "evp.h"

namespace{
    size_t saveToFile_cb(void *buffer, size_t size, size_t nmemb, void *userdata){
        auto saveTo = (ofFile*)userdata;
        saveTo->write((const char*)buffer, size * nmemb);
        return size * nmemb;
    }

    size_t saveToMemory_cb(void *buffer, size_t size, size_t nmemb, void *userdata){
        auto response = (ofHttpResponse*)userdata;
        response->data.append((const char*)buffer, size * nmemb);
        return size * nmemb;
    }

    size_t readBody_cb(void *ptr, size_t size, size_t nmemb, void *userdata){
        auto body = (std::string*)userdata;

        if(size*nmemb < 1){
            return 0;
        }

        if(!body->empty()) {
            auto sent = std::min(size * nmemb, body->size());
            memcpy(ptr, body->c_str(), sent);
            *body = body->substr(sent);
            return sent;
        }

        return 0;                          /* no more data left to deliver */
    }
}

static bool curlInited = false;

class ofxSimpleRestAPI {
private:
    std::unique_ptr<CURL, void(*)(CURL*)> curl;
    ofHttpRequest requestMachine;
    ofBuffer data;
protected:
    //https://forum.openframeworks.cc/t/sending-put-instead-of-post-request/29860/2
    ofHttpResponse handleRequest(const ofHttpRequest & request) {
        curl_slist *headers = nullptr;
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0);
        curl_easy_setopt(curl.get(), CURLOPT_URL, request.url.c_str());

        // always follow redirections
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

        // Set content type and any other header
        if(request.contentType!=""){
            headers = curl_slist_append(headers, ("Content-Type: " + request.contentType).c_str());
        }
        for(map<string,string>::const_iterator it = request.headers.cbegin(); it!=request.headers.cend(); it++){
            headers = curl_slist_append(headers, (it->first + ": " +it->second).c_str());
        }

        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

        std::string body = request.body;
        // set body if there's any
        if(request.body!=""){
    //        curl_easy_setopt(curl.get(), CURLOPT_UPLOAD, 1L); // Tis does PUT instead of POST
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, request.body.size());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, nullptr);
            //curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, readBody_cb);
            curl_easy_setopt(curl.get(), CURLOPT_READDATA, &body);
        }else{
    //        curl_easy_setopt(curl.get(), CURLOPT_UPLOAD, 0L);
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0);
            //curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, nullptr);
            curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, nullptr);
            curl_easy_setopt(curl.get(), CURLOPT_READDATA, nullptr);
        }
        if(request.method == ofHttpRequest::GET){
            curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1);
            curl_easy_setopt(curl.get(), CURLOPT_POST, 0);
        }else{
            curl_easy_setopt(curl.get(), CURLOPT_POST, 1);
            curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 0);
        }

        if(request.timeoutSeconds>0){
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, request.timeoutSeconds);
        }

        // start request and receive response
        ofHttpResponse response(request, 0, "");
        CURLcode err = CURLE_OK;
        if(request.saveTo){
            ofFile saveTo(request.name, ofFile::WriteOnly, true);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &saveTo);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, saveToFile_cb);
            err = curl_easy_perform(curl.get());
        }else{
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, saveToMemory_cb);
            err = curl_easy_perform(curl.get());
        }
        if(err==CURLE_OK){
            long http_code = 0;
            curl_easy_getinfo (curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
            response.status = http_code;
        }else{
            response.error = curl_easy_strerror(err);
            response.status = -1;
        }

        if(headers){
            curl_slist_free_all(headers);
        }

        return response;
    }
    
public:
    ofxSimpleRestAPI():curl(nullptr, nullptr){
        if(!curlInited){
             curl_global_init(CURL_GLOBAL_ALL);
        }
        curl = std::unique_ptr<CURL, void(*)(CURL*)>(curl_easy_init(), curl_easy_cleanup);
    }
    
    string createHMAC(string key, string data){
        
        unsigned int diglen;

        unsigned char result[EVP_MAX_MD_SIZE];

        unsigned char* digest = HMAC(EVP_sha256(),
                      reinterpret_cast<const unsigned char*>(key.c_str()), key.length(),
                      reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
                      result, &diglen);
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < diglen; i++){
            ss << std::hex << std::setw(2)  << (unsigned int)digest[i];
        }

        return (ss.str());
    }
    
    int setRequest(string req, ofHttpRequest::Method method){
        ofHttpRequest request(req, "");
        request.method = method;
        requestMachine = request;
        return request.getId();
    }
    
    void setRequestBody(string title, string body){
        requestMachine.body = title+"="+body;
    }
    
    int getResponseStatus(){
        ofHttpResponse response(handleRequest(requestMachine));
        int status = response.status;
        if(status == 200){ data = response.data; }
        return response.status;
    }
    
    string getResult(string word){
        auto result = ofJson::parse(data.getText());
        return result[word];
    }
    
};