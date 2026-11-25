#include <Core/FolderScanner.h>
#include <Core/Desk.h>
#include <Core/Log.h>
#include <CZ/Core/CZEventSource.h>
#include <CZ/XDG/XDGKit.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>

FolderScanner::FolderScanner() noexcept
{
    m_eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (m_eventFd >= 0)
        m_source = CZEventSource::Make(m_eventFd, EPOLLIN, CZOwn::Borrow, [this](int fd, UInt32, CZEventSource*){
            eventfd_t value;
            eventfd_read(fd, &value);
            deliver();
        });
    else
        Log(CZError, CZLN, "eventfd failed: folders are read in the foreground");

    m_thread = std::thread([this]{ run(); });
}

FolderScanner::~FolderScanner() noexcept
{
    {
        std::lock_guard lock { m_mutex };
        m_quit = true;
    }

    m_cv.notify_all();

    if (m_thread.joinable())
        m_thread.join();

    m_source.reset();

    if (m_eventFd >= 0)
        close(m_eventFd);
}

UInt64 FolderScanner::scan(const std::filesystem::path &dir, bool hidden, Callback done) noexcept
{
    auto request { std::make_shared<Request>() };
    request->dir = dir;
    request->hidden = hidden;
    request->done = std::move(done);

    // Without the event loop's help: right away
    if (m_eventFd < 0)
    {
        request->id = m_nextId++;
        auto entries { Desk::ScanFolder(*GetDesk()->xdgkit, dir, hidden) };
        request->done(std::move(entries));
        return request->id;
    }

    {
        std::lock_guard lock { m_mutex };
        request->id = m_nextId++;
        m_pending.push_back(request);
    }

    m_cv.notify_one();
    return request->id;
}

void FolderScanner::cancel(UInt64 id) noexcept
{
    std::lock_guard lock { m_mutex };

    // Not read yet: forgotten
    if (std::erase_if(m_pending, [id](const auto &r){ return r->id == id; }) > 0)
        return;

    m_cancelled.push_back(id);
}

void FolderScanner::run() noexcept
{
    // Its own: XDGKit isn't shared between threads
    std::shared_ptr<XDGKit> kit;

    while (true)
    {
        std::shared_ptr<Request> request;

        {
            std::unique_lock lock { m_mutex };
            m_cv.wait(lock, [this]{ return m_quit || !m_pending.empty(); });

            if (m_quit)
                return;

            request = m_pending.front();
            m_pending.pop_front();
        }

        if (!kit)
            kit = XDGKit::Make();

        if (kit)
            request->entries = Desk::ScanFolder(*kit, request->dir, request->hidden);

        {
            std::lock_guard lock { m_mutex };
            m_finished.push_back(request);
        }

        eventfd_write(m_eventFd, 1);
    }
}

void FolderScanner::deliver() noexcept
{
    std::deque<std::shared_ptr<Request>> finished;
    std::vector<UInt64> cancelled;

    {
        std::lock_guard lock { m_mutex };
        finished.swap(m_finished);
        cancelled.swap(m_cancelled);
    }

    for (auto &request : finished)
        if (std::find(cancelled.begin(), cancelled.end(), request->id) == cancelled.end())
            request->done(std::move(request->entries));
}
