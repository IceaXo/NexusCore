#pragma once
#include <iostream>
#include <fstream>
#include <string>

// Tee streambuf: writes to both original stdout/stderr and a shared log file
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* orig, std::ofstream& file)
        : original_(orig), file_(file) {}

protected:
    int overflow(int c) override {
        if (c != EOF) {
            if (file_.is_open()) file_.put((char)c);
            if (original_) original_->sputc((char)c);
        }
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        if (file_.is_open()) file_.write(s, n);
        if (original_) original_->sputn(s, n);
        return n;
    }

    int sync() override {
        if (file_.is_open()) file_.flush();
        if (original_) original_->pubsync();
        return 0;
    }

private:
    std::streambuf* original_;
    std::ofstream& file_;
};
