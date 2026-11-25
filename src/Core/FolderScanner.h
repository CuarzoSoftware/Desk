#ifndef FOLDERSCANNER_H
#define FOLDERSCANNER_H

#include <Core/Types.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace CZ { class CZEventSource; }
struct DeskEntry;

/**
 * @brief Reads folders in the background (see Desk::ScanFolder()), so big or slow folders never
 *        block the app.
 *
 * One worker thread, with its own XDGKit (MIME types, launchers), reads the requested folders in
 * order. Each result is handed to its callback on the main thread (from the event loop).
 */
class FolderScanner final
{
public:
    /// Requires the CZCore (the results are received through its event loop).
    FolderScanner() noexcept;
    ~FolderScanner() noexcept;

    using Callback = std::function<void(std::vector<DeskEntry> &&entries)>;

    /**
     * @brief Reads @p dir in the background. @p done gets its files on the main thread.
     *
     * @return The request's id (see cancel()).
     */
    UInt64 scan(const std::filesystem::path &dir, bool hidden, Callback done) noexcept;

    /// Drops a request: its callback is never called.
    void cancel(UInt64 id) noexcept;

private:
    struct Request
    {
        UInt64 id;
        std::filesystem::path dir;
        bool hidden;
        Callback done;
        std::vector<DeskEntry> entries; // Result
    };

    void run() noexcept;
    void deliver() noexcept;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Request>> m_pending, m_finished;
    std::vector<UInt64> m_cancelled;
    UInt64 m_nextId { 1 };
    bool m_quit { false };
    int m_eventFd { -1 };
    std::shared_ptr<CZEventSource> m_source;
    std::thread m_thread;
};

#endif // FOLDERSCANNER_H
