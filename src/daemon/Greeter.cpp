/***************************************************************************
* Copyright (c) 2013 Abdurrahman AVCI <abdurrahmanavci@gmail.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
***************************************************************************/

#include "Greeter.h"

#include "Configuration.h"
#include "Constants.h"
#include "DaemonApp.h"
#include "Session.h"
#include "Display.h"

#include <QDebug>
#include <QProcess>

#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

namespace SDDM {
    Greeter::Greeter(QObject *parent) : QObject(parent) {
    }

    Greeter::~Greeter() {
        stop();
    }

    void Greeter::setDisplay(const QString &display) {
        m_display = display;
    }

    void Greeter::setAuthPath(const QString &authPath) {
        m_authPath = authPath;
    }

    void Greeter::setSocket(const QString &socket) {
        m_socket = socket;
    }

    void Greeter::setTheme(const QString &theme) {
        m_theme = theme;
    }

    bool Greeter::start() {
        // check flag
        if (m_started)
            return false;

        struct passwd *pw = nullptr;
        if (!daemonApp->configuration()->testing)
        {
            pw = getpwnam(qPrintable("sddm"));
            if (!pw) {
                qWarning() << " DAEMON: Failed to switch to switch greeter to user sddm";
                //continue anyway?? Otherwise we'll block out everyone self compiling
                //from logging in
            }
        }
        
        // create process
        m_process = new Session("greeter", this);
        // set process environment
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

        if (pw) {
            m_process->setUser(pw->pw_name);
            m_process->setDir(pw->pw_dir);
            m_process->setUid(pw->pw_uid);
            m_process->setGid(pw->pw_gid);

            //take ownership of the socket so we can read/write to it
            chown(qPrintable(m_socket), pw->pw_uid, pw->pw_uid);
            env.insert("HOME", pw->pw_dir);
            env.insert("USER", "sddm");

            //unset any random stuff we inherit from parent relating to home
            //Neon5 specific patch
            env.insert("XDG_CONFIG_HOME", QString());
            env.insert("XDG_CACHE_HOME", QString());
            env.insert("XDG_DATA_HOME", QString());
            env.insert("KDEHOME", QString());
        }

        // delete process on finish
        connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(finished()));

        connect(m_process, SIGNAL(readyReadStandardOutput()), SLOT(onReadyReadStandardOutput()));
        connect(m_process, SIGNAL(readyReadStandardError()), SLOT(onReadyReadStandardError()));

        // log message
        qDebug() << " DAEMON: Greeter starting...";

        env.insert("DISPLAY", m_display);
        env.insert("XAUTHORITY", m_authPath);
        env.insert("XCURSOR_THEME", daemonApp->configuration()->cursorTheme());
        m_process->setProcessEnvironment(env);

        // start greeter
        m_process->start(QString("%1/sddm-greeter").arg(BIN_INSTALL_DIR), { "--socket", m_socket, "--theme", m_theme });

        //if we fail to start bail immediately, and don't block in waitForStarted
        if (m_process->state() == QProcess::NotRunning) {
            qCritical() << "DAEMON: Greeter failed to launch.";
            return false;
        }
        // wait for greeter to start
        if (!m_process->waitForStarted()) {
            // log message
            qCritical() << " DAEMON: Failed to start greeter.";

            // return fail
            return false;
        }

        // log message
        qDebug() << " DAEMON: Greeter started.";

        // set flag
        m_started = true;

        // return success
        return true;
    }

    void Greeter::stop() {
        // check flag
        if (!m_started)
            return;

        // log message
        qDebug() << " DAEMON: Greeter stopping...";

        // terminate process
        m_process->terminate();

        // wait for finished
        if (!m_process->waitForFinished(5000))
            m_process->kill();
    }

    void Greeter::finished() {
        // check flag
        if (!m_started)
            return;

        // reset flag
        m_started = false;

        // log message
        qDebug() << " DAEMON: Greeter stopped.";

        // clean up
        m_process->deleteLater();
        m_process = nullptr;
    }

    void Greeter::onReadyReadStandardError()
    {
        if (m_process) {
            qDebug() << "Greeter StdErr: " << m_process->readAllStandardError();
        }
    }

    void Greeter::onReadyReadStandardOutput()
    {
        if (m_process) {
            qDebug() << "Greeter StdOut: " << m_process->readAllStandardOutput();
        }
    }
}
