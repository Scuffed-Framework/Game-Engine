#pragma once
#include <filesystem>
#include <string>

#include <Platform/PlatformIncludes.hpp>

namespace SF::Engine
{

    struct ProcessID
    {
        Platform::NativePID Value = Platform::InvalidPID;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != Platform::InvalidPID; }

        constexpr explicit operator bool() const noexcept { return IsValid(); }

        friend constexpr bool operator==(ProcessID, ProcessID) noexcept = default;
    };

    enum class ProcessState
    {
        Invalid,
        Running,
        Suspended,
        Exited
    };

    class Process
    {
    private:
        Platform::NativeProcessHandle Handle = Platform::InvalidProcessHandle;

    public:
        Process() = default;
        explicit Process(ProcessID pid);
        explicit Process(const std::filesystem::path &program, const std::filesystem::path &directory) :
            Process(program, {}, directory)
        {
        }
        explicit Process(const std::filesystem::path &program, const std::vector<std::string> &args,
                         const std::filesystem::path &directory)
        {
            *this = Launch(program, args, directory);
        }

        ~Process();

        Process(const Process &)            = delete;
        Process &operator=(const Process &) = delete;

        Process(Process &&other) noexcept;
        Process &operator=(Process &&other) noexcept;

        [[nodiscard]] ProcessID GetPID() const noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsRunning() const;
        [[nodiscard]] ProcessState GetState() const;
        [[nodiscard]] int GetExitCode() const;
        [[nodiscard]] std::string GetName() const;

        bool Wait();
        bool Wait(uint32_t timeoutMilliseconds);

        [[nodiscard]] bool Terminate(int exitCode = 0) const;

        static Process Current();

        static Process Launch(const std::filesystem::path &executable, const std::vector<std::string> &arguments = {},
                              const std::filesystem::path &workingDirectory = {});

    private:
        ProcessID m_PID{};
        int m_ExitStatus = 0;

    public:
        static std::optional<Process> GetProcessById(ProcessID pid);
        static std::vector<Process> GetProcessesByName(std::string_view name);
        static std::vector<Process> GetProcesses();
    };
} // namespace SF::Engine
