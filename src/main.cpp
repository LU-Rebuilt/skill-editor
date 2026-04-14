// LU Skill Editor
// Graphical tool for viewing and editing behavior trees used by LEGO Universe skills.
//
// Data sources:
//   - CDClient SQLite (.sqlite) from DarkflameServer or community CDClient SQLite
//   - CDClient FDB (.fdb) from the original game client (via lu_assets parser)
//
// Usage:
//   skill_editor [cdclient.sqlite]
//
// If a path is given, loads it immediately. Otherwise use File > Open.

#include "main_window.h"

#include <QApplication>
#include <QFile>
#include <QSettings>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("LU-Rebuilt");
    app.setApplicationName("SkillEditor");

    skill_editor::MainWindow window;
    window.show();

    if (argc > 1) {
        window.load_database(argv[1]);
    } else {
        // Auto-load last used database
        QSettings settings;
        QString last = settings.value("last_cdclient").toString();
        if (!last.isEmpty() && QFile::exists(last)) {
            window.load_database(last.toStdString());
        }
    }

    return app.exec();
}
