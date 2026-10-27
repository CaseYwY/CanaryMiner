// Incremental terminal-key decoding. No blocking read while an escape sequence is incomplete.
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace om {

constexpr int kKeyUp = 0x10001, kKeyDown = 0x10002, kKeyPageUp = 0x10003, kKeyPageDown = 0x10004;
constexpr int kKeyLeft = 0x10005, kKeyRight = 0x10006, kKeyBackTab = 0x10007, kKeyF2 = 0x10008;

class TerminalKeyDecoder {
   public:
    void feed(unsigned char byte, uint64_t nowMs) {
        expire(nowMs);
        if (pending_.empty()) {
            if (byte == 27) { pending_ = "\x1b"; since_ = nowMs; }
            else ready_.push_back(byte);
            return;
        }
        pending_ += static_cast<char>(byte);
        if (pending_.size() == 2 && byte != '[' && byte != 'O') {
            // Alt+key is not a plain key (in particular, do not turn Alt+Q into Quit).
            pending_.clear();
            return;
        }
        if (pending_.size() < 3) return;
        if (byte >= 0x40 && byte <= 0x7e) {
            int key = 0;
            if (pending_ == "\x1b[A" || pending_ == "\x1bOA") key = kKeyUp;
            else if (pending_ == "\x1b[B" || pending_ == "\x1bOB") key = kKeyDown;
            else if (pending_ == "\x1b[C" || pending_ == "\x1bOC") key = kKeyRight;
            else if (pending_ == "\x1b[D" || pending_ == "\x1bOD") key = kKeyLeft;
            else if (pending_ == "\x1b[5~") key = kKeyPageUp;
            else if (pending_ == "\x1b[6~") key = kKeyPageDown;
            else if (pending_ == "\x1b[Z") key = kKeyBackTab;
            else if (pending_ == "\x1bOQ" || pending_ == "\x1b[12~") key = kKeyF2;
            if (key) ready_.push_back(key);
            pending_.clear();
        } else if (pending_.size() >= 24) {
            pending_.clear();
        }
    }

    int poll(uint64_t nowMs) {
        expire(nowMs);
        if (ready_.empty()) return 0;
        const int key = ready_.front();
        ready_.pop_front();
        return key;
    }

   private:
    std::string pending_;
    std::deque<int> ready_;
    uint64_t since_ = 0;
    void expire(uint64_t nowMs) {
        if (!pending_.empty() && nowMs >= since_ && nowMs - since_ >= 100) {
            if (pending_.size() == 1) ready_.push_back(27);
            pending_.clear();  // discard malformed/truncated sequences, never dispatch their suffix as a shortcut
        }
    }
};

}  // namespace om
