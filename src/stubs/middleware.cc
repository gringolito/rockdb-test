// Copyright (c) 2019 Filipe Utzig <filipeutzig@gmail.com>
//
// ----------------------------------------------------------------------------
// "THE BEER-WARE LICENSE" (Revision 42):
// <filipeutzig@gmail.com> wrote this file. As long as you retain this notice
// you can do whatever you want with this stuff. If we meet some day, and you
// think this stuff is worth it, you can buy me a beer in return. Filipe Utzig.
// ----------------------------------------------------------------------------
#include "stubs/middleware.h"

#include <poll.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>

#include <algorithm>
#include <vector>

#include <sys/types.h>

#include "libfswatch/c++/monitor_factory.hpp"

#include "utils/file.h"

namespace mpsync {
namespace stubs {

static const std::string kPidPath{ "/tmp/stubmw/pids/" };

Middleware::Middleware()
    : myself_({}),
      listening_server_({}),
      on_server_found_cb_(nullptr),
      on_server_lost_cb_(nullptr),
      server_pid_({}),
      monitor_(nullptr),
      server_is_found_(false),
      server_published_(false),
      first_poll_call_(true)
{
    printf("%s()\n", __func__);
}

Middleware::~Middleware()
{
    printf("%s()\n", __func__);

    if (server_published_) {
        UnpublishServer();
    }

    if (monitor_) {
        delete monitor_;
    }
}

void Middleware::PublishServer(const ProcessSignature &server_signature)
{
    printf("%s()\n", __func__);

    myself_ = server_signature;

    server_pid_file_.open(kPidPath + myself_._name, std::ofstream::binary | std::ofstream::trunc);
    server_pid_file_ << getpid() << std::endl;
    server_published_ = true;
}

void Middleware::UnpublishServer()
{
    printf("%s()\n", __func__);

    if (!server_published_) {
        return;
    }

    server_pid_file_ << 0 << std::endl;
    server_pid_file_.close();
    server_published_ = false;
}

void Middleware::RegisterToServer(const ProcessSignature &server_signature,
                                  OnServerFoundCb on_server_found_event,
                                  OnServerLostCb on_server_lost_event)
{
    printf("%s()\n", __func__);

    listening_server_ = server_signature;
    on_server_found_cb_ = on_server_found_event;
    on_server_lost_cb_ = on_server_lost_event;
    WatchServerPid();
}

void Middleware::SubscribeToFdEvents(int fd, OnFdEventCb on_fd_event)
{
    printf("%s()\n", __func__);

    fd_callbacks_[fd] = [fd, on_fd_event]() { on_fd_event(fd); };
    poll_fds_.insert(fd);
}

void Middleware::UnsubscribeFromFdEvents(int fd)
{
    printf("%s()\n", __func__);

    auto fd_search = fd_callbacks_.find(fd);
    if (fd_search != fd_callbacks_.end()) {
        poll_fds_.erase(fd);
        fd_callbacks_.erase(fd_search);
    }
}

bool Middleware::LoopWhile(bool *keeprunning)
{
    printf("%s()\n", __func__);

    while (*keeprunning) {
        if (!Poll()) {
            return false;
        }
    }

    return true;
}

void Middleware::FswatchCb(const std::vector<fsw::event> &events, void *cookie)
{
    auto self = static_cast<Middleware *>(cookie);
    self->ProcessMonitorEvents(events);
}

void Middleware::WatchServerPid()
{
    printf("%s()\n", __func__);

    std::string pid_file(kPidPath + listening_server_._name);
    if (!utils::File::Exists(pid_file)) {
        assert(utils::File::Create(kPidPath, listening_server_._name));
    }

    // std::function<void(const std::vector<fsw::event> &, void *)> callback =
    // [this](const std::vector<fsw::event> &e, void *) { ProcessMonitorEvents(e); };
    // monitor_ = fsw::monitor_factory::create_monitor(
    // fsw_monitor_type::system_default_monitor_type, { pid_file },
    // callback.target<void(const std::vector<fsw::event> &, void *)>());
    monitor_ = fsw::monitor_factory::create_monitor(fsw_monitor_type::system_default_monitor_type,
                                                    { pid_file }, &Middleware::FswatchCb);
    assert(monitor_);
    monitor_->start();
}

void Middleware::ProcessMonitorEvents(const std::vector<fsw::event> &events)
{
    printf("%s()\n", __func__);

    for (const auto &event : events) {
        auto flags = event.get_flags();
        for (const auto &flag : flags) {
            if (flag & (Created | Updated)) {
                ProcessPidFileEvent();
            }
            else if (flag & (Removed /* | Link */)) {
                ServerLost();
            }
            else {
                printf("%s() flag=%#x\n", __func__, flag);
            }
        }
    }
}

void Middleware::ProcessPidFileEvent()
{
    auto pid = ReadPid();
    if (pid._pid != 0) {
        if (server_pid_._pid == pid._pid) {
            return;
        }

        /* A new server was found, so notify that the latest was lost */
        if (server_pid_._pid != 0) {
            ServerLost();
        }
        ServerFound(std::move(pid));
    }
    else {
        ServerLost();
    }
}

Pid Middleware::ReadPid()
{
    printf("%s()\n", __func__);

    std::ifstream pid_file(kPidPath + listening_server_._name, std::ofstream::binary);
    Pid pid;
    pid_file >> pid._pid;
    return pid;
}

void Middleware::ServerLost()
{
    printf("%s()\n", __func__);

    if (!server_is_found_) {
        printf("not server_is_found_\n");
        return;
    }

    if (on_server_lost_cb_) {
        on_server_lost_cb_(server_pid_);
    }

    server_pid_ = {};
    server_is_found_ = false;
}

void Middleware::ServerFound(Pid &&pid)
{
    printf("%s()\n", __func__);

    server_is_found_ = true;
    server_pid_ = std::move(pid);

    if (on_server_found_cb_) {
        on_server_found_cb_(server_pid_);
    }
}

bool Middleware::Poll()
{
    printf("%s()\n", __func__);

    if (first_poll_call_) {
        first_poll_call_ = false;
        if (!listening_server_._name.empty()) {
            ProcessPidFileEvent();
        }
    }

    std::vector<pollfd> fds;
    fds.resize(poll_fds_.size());

    std::transform(poll_fds_.begin(), poll_fds_.end(), fds.begin(), [](int fd) -> pollfd {
        return pollfd{ .fd = fd, .events = POLLIN | POLLPRI, .revents = 0 };
    });

    int ret = poll(fds.data(), fds.size(), -1 /* no timeout */);
    if (ret < 0) {
        return false;
    }

    for (const auto &fd : fds) {
        if (!ProcessPollEvent(&fd)) {
            return false;
        }
    }

    return true;
}

bool Middleware::ProcessPollEvent(const pollfd *fd)
{
    printf("%s()\n", __func__);

    /** Process input events in fd */
    if (fd->revents & (POLLIN | POLLPRI)) {
        fd_callbacks_.at(fd->fd)();
    }

    /** Process errors in fd */
    if (fd->revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return false;
    }

    return true;
}

}  // namespace stubs
}  // namespace mpsync