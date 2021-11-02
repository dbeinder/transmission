/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

class Session;

class SystemTrayIcon
{
public:
    SystemTrayIcon(Glib::RefPtr<Session> const& core);
    ~SystemTrayIcon();

    void refresh();

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};