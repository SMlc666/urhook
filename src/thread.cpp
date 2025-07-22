#include "ur/thread.h"

#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <string>
#include <charconv>
#include <string_view>

namespace ur::thread
{
    namespace
    {
        bool send_signal_to_thread(pid_t tid, int signal)
        {
            return tgkill(getpid(), tid, signal) == 0;
        }
    }

    bool suspend_thread(pid_t tid)
    {
        return send_signal_to_thread(tid, SIGSTOP);
    }

    bool resume_thread(pid_t tid)
    {
        return send_signal_to_thread(tid, SIGCONT);
    }

    void suspend_all_other_threads()
    {
        auto current_tid = gettid();
        for (auto tid : get_all_threads()) {
            if (tid != current_tid) {
                suspend_thread(tid);
            }
        }
    }

    void resume_all_other_threads()
    {
        auto current_tid = gettid();
        for (auto tid : get_all_threads()) {
            if (tid != current_tid) {
                resume_thread(tid);
            }
        }
    }

    std::vector<pid_t> get_all_threads()
    {
        std::vector<pid_t> tids;
        if (auto dir = opendir("/proc/self/task")) {
            while (auto entry = readdir(dir)) {
                if (entry->d_type == DT_DIR) {
                    pid_t tid;
                    std::string_view name(entry->d_name);
                    auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), tid);
                    if (ec == std::errc()) {
                        tids.push_back(tid);
                    }
                }
            }
            closedir(dir);
        }
        return tids;
    }
}
