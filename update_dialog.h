#ifndef UPDATE_DIALOG_H
#define UPDATE_DIALOG_H

#include <QDialog>
#include <QDateTime>

namespace Ui {
class update_dialog;
}

class update_dialog : public QDialog
{
    Q_OBJECT

public:
    explicit update_dialog(QWidget *parent = 0);
    ~update_dialog();

    void runUpdater();

    struct gitVersion
    {
        int versionMajor;
        int versionMinor;
        int versionRev;
        int versionDev;
        int versionGit;
    };


private:
    Ui::update_dialog *ui;

    int getLatestVersionData();
    int parseGitVersion(QString versionString, gitVersion &version);
    void parseUpdateData();
    void setDialogText();

    QString currentVersion;
    QString latestVersion;
    QDateTime latestVersionDate;
    QString latestVersionURL;
    bool networkError;
    bool hasNewVersion;
};

#endif // UPDATE_DIALOG_H
