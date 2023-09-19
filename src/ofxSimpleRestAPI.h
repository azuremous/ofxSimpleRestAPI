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
    string errorString;
    string certificatePath;
    string keyPath;
    string password;
    bool useCertificate;
    bool useSSL;
    bool showVerbose;
protected:
    //https://forum.openframeworks.cc/t/sending-put-instead-of-post-request/29860/2
    ofHttpResponse handleRequest(const ofHttpRequest & request) {
        curl_slist *headers = nullptr;
        if(showVerbose){
            curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);
        }

        if(useCertificate){
            curl_easy_setopt(curl.get(), CURLOPT_SSLCERT, certificatePath.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_SSLKEY, keyPath.c_str());
            if(password != ""){
                curl_easy_setopt(curl.get(), CURLOPT_KEYPASSWD, password.c_str());
            }
        }else{
            if(!useSSL){
                curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0);
            }
        }
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
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, request.body.size());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, nullptr);
            curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, readBody_cb);
            curl_easy_setopt(curl.get(), CURLOPT_READDATA, &body);
        }else{
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, 0);
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
    ofxSimpleRestAPI():curl(nullptr, nullptr),useCertificate(false),useSSL(false),showVerbose(false){
        if(!curlInited){
             curl_global_init(CURL_GLOBAL_ALL);
        }
        curl = std::unique_ptr<CURL, void(*)(CURL*)>(curl_easy_init(), curl_easy_cleanup);
    }
    
    void setRequestBody(const string &title, const string &body){
        requestMachine.body = title +"="+body;
    }
    
    void setRequestBody(const string &body){
        requestMachine.body = body;
    }
    
    int setRequest(const string &req, const ofHttpRequest::Method &method, const int &time = 0, const string &contentType = "", const string &header = ""){
        int point = req.find("https:");
        if(point == 0){
            useSSL = true;
        }
        ofHttpRequest request(req, header);
        request.method = method;
        request.contentType = contentType;
        if(time != 0) {
            request.timeoutSeconds = time;
        }
        requestMachine = request;
        return request.getId();
    }
    
    int getResponseStatus(){
        ofHttpResponse response(handleRequest(requestMachine));
        int status = response.status;
        data = response.data;
        errorString = response.error;
        return response.status;
    }
    
    void setToUseSSL(bool use = true){
        useSSL = use;
    }
    
    void showDetailLog(){
        showVerbose = true;
    }
    
    void saveToFile(string name){
        requestMachine.name = name;
        requestMachine.saveTo = true;
    }
    
    void setCertification(string certificate, string key, string pass = ""){
        certificatePath = certificate;
        keyPath = key;
        password = pass;
        useCertificate = true;
    }
    
    string createHMAC(string key, string data, const EVP_MD *type = EVP_sha512()){
        unsigned int diglen;
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned char* digest = HMAC(type,
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
    
    string getData(){
        return data.getText();
    }
    
    string encodeString(const string &s){
        string result = s;
            if(curl) {
                char *output = curl_easy_escape(curl.get(), result.c_str(), result.size());
              if(output) {
                  result = output;
                  curl_free(output);
              }
            }
            return result;
    }
    
    string getError() const { return errorString; }
    
    template <typename T> T getData(string word, string owner = ""){
        if(owner == "") { owner = data.getText(); }
        auto _data = ofJson::parse(owner);
        T result = static_cast<T>(_data[word]);
        return result;
    }
    
};
