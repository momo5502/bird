#include "thread.hpp"

#ifdef _WIN32
#include "string.hpp"
#include "finally.hpp"

#include <TlHelp32.h>
#endif

namespace utils::thread
{
#ifdef _WIN32
    bool set_name(const HANDLE t, const std::string& name)
    {
        const nt::library kernel32("kernel32.dll");
        if (!kernel32)
        {
            return false;
        }

        const auto set_description = kernel32.get_proc<HRESULT(WINAPI*)(HANDLE, PCWSTR)>("SetThreadDescription");
        if (!set_description)
        {
            return false;
        }

        return SUCCEEDED(set_description(t, string::convert(name).data()));
    }

    bool set_name(const DWORD id, const std::string& name)
    {
        auto* const t = OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, id);
        if (!t)
            return false;

        const auto _ = utils::finally([t]() { CloseHandle(t); });

        return set_name(t, name);
    }
#endif

    bool set_name(const std::string& name)
    {
        (void)name;
#ifdef _WIN32
        return set_name(GetCurrentThread(), name);
#else
        return false;
#endif
    }

    bool set_priority(const priority p)
    {
        (void)p;
#ifdef _WIN32
        auto priority_class = NORMAL_PRIORITY_CLASS;
        switch (p)
        {
        case priority::low:
            priority_class = BELOW_NORMAL_PRIORITY_CLASS;
            break;
        case priority::high:
            priority_class = HIGH_PRIORITY_CLASS;
            break;
        case priority::normal:
            priority_class = NORMAL_PRIORITY_CLASS;
            break;
        }

        return SetThreadPriority(GetCurrentThread(), priority_class);
#else
        return false;
#endif
    }

#ifdef _WIN32
    std::vector<DWORD> get_thread_ids()
    {
        const nt::handle<nt::InvalidHandleValueFunc> h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, GetCurrentProcessId());
        if (!h)
        {
            return {};
        }

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (!Thread32First(h, &entry))
        {
            return {};
        }

        std::vector<DWORD> ids{};

        do
        {
            const auto check_size = entry.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID);
            entry.dwSize = sizeof(entry);

            if (check_size && entry.th32OwnerProcessID == GetCurrentProcessId())
            {
                ids.emplace_back(entry.th32ThreadID);
            }
        } while (Thread32Next(h, &entry));

        return ids;
    }

    void for_each_thread(const std::function<void(HANDLE)>& callback, const DWORD access)
    {
        const auto ids = get_thread_ids();

        for (const auto& id : ids)
        {
            handle thread(id, access);
            if (thread)
            {
                callback(thread);
            }
        }
    }

    void suspend_other_threads()
    {
        for_each_thread([](const HANDLE thread) {
            if (GetThreadId(thread) != GetCurrentThreadId())
            {
                SuspendThread(thread);
            }
        });
    }

    void resume_other_threads()
    {
        for_each_thread([](const HANDLE thread) {
            if (GetThreadId(thread) != GetCurrentThreadId())
            {
                ResumeThread(thread);
            }
        });
    }
#endif
}
