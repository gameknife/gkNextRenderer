#pragma once

namespace NextRenderer
{
    inline void PlatformInit()
    {
        
    }

    inline void HideConsole()
    {

    }

    inline void OSCommand(const char* command)
    {

    }

    inline void OSProcess(const char* exe)
    {

    }
}
#if ANDROID
#include <imgui_impl_android.h>
#include <android/log.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

static void MakeExternalDirectory( android_app* app, std::string srcPath )
{
    if( std::filesystem::exists(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath) )
    {
        return;
    }
    
    std::filesystem::create_directories(std::filesystem::path(std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath));
    
    AAssetDir* assetDir = AAssetManager_openDir(
                        app->activity->assetManager, srcPath.c_str());
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != NULL){
        AAsset* file = AAssetManager_open(app->activity->assetManager, (srcPath + "/" + filename).c_str(),
                                       AASSET_MODE_BUFFER);
        size_t fileLen = AAsset_getLength(file);
        std::vector<char> fileData;
        fileData.resize(fileLen);

        AAsset_read(file, static_cast<void*>(fileData.data()), fileLen);
        AAsset_close(file);

        std::string targetPath = std::string("/sdcard/Android/data/com.gknextrenderer/files/") + srcPath + "/" + filename;
    
        FILE* targetFile = fopen(targetPath.c_str(), "wb");
        fwrite(fileData.data(), 1, fileLen, targetFile);
        fclose(targetFile);
    }
    AAssetDir_close(assetDir);
}

class androidbuf : public std::streambuf {
public:
    enum { bufsize = 512 }; // ... or some other suitable buffer size
    androidbuf() { this->setp(buffer, buffer + bufsize - 1); }

private:
    int overflow(int c)
    {
        if (c == traits_type::eof()) {
            *this->pptr() = traits_type::to_char_type(c);
            this->sbumpc();
        }
        return this->sync()? traits_type::eof(): traits_type::not_eof(c);
    }

    int sync()
    {
        int rc = 0;
        if (this->pbase() != this->pptr()) {
            char writebuf[bufsize+1];
            memcpy(writebuf, this->pbase(), this->pptr() - this->pbase());
            writebuf[this->pptr() - this->pbase()] = '\0';

            rc = __android_log_print(ANDROID_LOG_INFO, "vkdemo", "%s", writebuf) > 0;
            this->setp(buffer, buffer + bufsize - 1);
        }
        return rc;
    }

    char buffer[bufsize];
};

#include <android/log.h>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>

class AndroidLogOutputStream {
public:
    AndroidLogOutputStream() {
        backup_fd = dup(fileno(stdout));
        pipe(pipe_fd);
        dup2(pipe_fd[1], fileno(stdout));
        close(pipe_fd[1]);

        // 创建新线程来读取管道的内容并将其写入 Android 日志
        pthread_create(&thread, nullptr, &AndroidLogOutputStream::logThreadFunction, this);
    }

    ~AndroidLogOutputStream() {
        dup2(backup_fd, fileno(stdout));
        close(backup_fd);
    }

private:
    static void* logThreadFunction(void* arg) {
        auto* instance = static_cast<AndroidLogOutputStream*>(arg);
        char buffer[512];

        while (true) {
            ssize_t count = read(instance->pipe_fd[0], buffer, sizeof(buffer) - 1);
            if (count <= 0) {
                break;
            }
            buffer[count] = '\0';
            __android_log_print(ANDROID_LOG_INFO, "gknext", "%s", buffer);
        }

        return nullptr;
    }

    int backup_fd;
    int pipe_fd[2];
    pthread_t thread;
};

#endif
