#pragma once

#include <vector>
#include <sys/types.h>
#include <unistd.h>

namespace ur::thread
{
    inline pid_t get_current_tid()
    {
        return gettid();
    }

    bool suspend_thread(pid_t tid);
    bool resume_thread(pid_t tid);

    void suspend_all_other_threads();
    void resume_all_other_threads();

    std::vector<pid_t> get_all_threads();
}
